#include <linux/atomic.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/vmalloc.h>
#include <linux/wait.h>
#include <linux/version.h>
#include <asm/unaligned.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/videodev2.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-event.h>
#include <media/v4l2-device.h>
#include <media/videobuf-core.h>
#include <media/videobuf-vmalloc.h>
#include <media/v4l2-common.h>

#include "uvcvideo.h"

#define MODULE_NAME "myuvc"

/* Values for bmHeaderInfo (Video and Still Image Payload Headers, 2.4.3.3) */
#define UVC_STREAM_EOH	(1 << 7)
#define UVC_STREAM_ERR	(1 << 6)
#define UVC_STREAM_STI	(1 << 5)
#define UVC_STREAM_RES	(1 << 4)
#define UVC_STREAM_SCR	(1 << 3)
#define UVC_STREAM_PTS	(1 << 2)
#define UVC_STREAM_EOF	(1 << 1)
#define UVC_STREAM_FID	(1 << 0)

/* 参考uvc_video_queue定义一些结构体 */


struct myuvc_buffer {

	/* Touched by interrupt handler. */
	struct v4l2_buffer buf;
	int state;

	unsigned long vma_use_count; /* 表示是否已经被mmap */
	wait_queue_head_t wait;      /* APP要读某个缓冲区,如果无数据,在此休眠 */
	
	struct list_head stream;
	struct list_head irq;
};

struct myuvc_queue {
    void *mem;                 //整块内存的起始地址
    int count;                 //队列分配了几个buf
    int buf_size;              //每个buf大小
	struct myuvc_buffer buffer[32];
	
    struct urb *urb[32];       //32个urb
    char *urb_buffer[32];      //分配的urb buffer
    dma_addr_t urb_dma[32];    //urb buffer的dma
    unsigned int urb_size;     //urb大小
    
    struct list_head mainqueue; //供APP读取数据用
    struct list_head irqqueue;  //供底层驱动产生数据用
};


//用于保存从USB获取的摄像头参数
struct myuvc_streaming_control
{
    __u16 bmHint;
    __u8  bFormatIndex;
    __u8  bFrameIndex;
    __u32 dwFrameInterval;
    __u16 wKeyFrameRate;
    __u16 wPFrameRate;
    __u16 wCompQuality;
    __u16 wCompWindowSize;
    __u16 wDelay;
    __u32 dwMaxVideoFrameSize;
    __u32 dwMaxPayloadTransferSize;
    __u32 dwClockFrequency;
    __u8  bmFramingInfo;
    __u8  bPreferedVersion;
    __u8  bMinVersion;
    __u8  bMaxVersion;
};

struct frame_desc{
	int width;
	int height;
};

static struct myuvc_streaming_control my_uvc_params;

#define MYUVC_URBS_NUM  2  //限制最多2个urb,一个也行


static struct myuvc_queue myuvc_queue;

static struct video_device *myuvc_vdev;
static struct usb_device *myuvc_udev;
static struct v4l2_device v4l2_dev;
static int myuvc_bEndpointAddress = 0x81;
static int myuvc_streaming_intf;
static int myuvc_control_intf;
static int myuvc_streaming_bAlternateSetting = 8;
static struct v4l2_format myuvc_format;
static struct myuvc_streaming_control myuvc_params;

static struct frame_desc frames[] = {{640, 480}, {352, 288}, {320, 240}, {176, 144}, {160, 120}};
static int frame_idx = 1;
static int bBitsPerPixel = 16; /* lsusb -v -d 0x1e4e:  "bBitsPerPixel" */
static int uvc_version = 0x0100; /* lsusb -v -d 0x1e4e: bcdUVC */

static int wMaxPacketSize = 1024;





/* A1 */
static int myuvc_open(struct file *filp)
{
    printk("%s \n", __func__);

	return 0;
}


/* A9 把缓存映射到APP的空间,以后APP就可以直接操作这块缓存 */
static int myuvc_mmap(struct file *filp, struct vm_area_struct *vm_area)
{
    printk("%s \n", __func__);

	return 0;
}


/* A12 APP调用POLL/select来确定缓存是否就绪(有数据) */
static unsigned int myuvc_poll(struct file *filp, struct poll_table_struct *wait)
{
    printk("%s \n", __func__);
	return 0;
}

/* A18 关闭 */
static int myuvc_close(struct file *filp)
{
    printk("%s \n", __func__);

	return 0;
}

static const struct v4l2_file_operations myuvc_fops = {
	.owner			= THIS_MODULE,
	.unlocked_ioctl = video_ioctl2,

	.open			= myuvc_open,
	.mmap 			= myuvc_mmap,
	.poll  			= myuvc_poll,
	
	.release		= myuvc_close,
};

int myuvc_querycap(struct file *file, void *fh,
			   struct v4l2_capability *cap)
{
    printk("%s \n", __func__);

	strscpy(cap->driver, MODULE_NAME, sizeof(cap->driver));
	strscpy(cap->card, MODULE_NAME, sizeof(cap->card));
	
	//usb_make_path(stream->dev->udev, cap->bus_info, sizeof(cap->bus_info));
	snprintf(cap->bus_info, sizeof(cap->bus_info),
					"platform:%s", MODULE_NAME);

	cap->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;
	
	return 0;
}

int myuvc_enum_fmt_vid_cap(struct file *file, void *fh,
					  struct v4l2_fmtdesc *f)
{
	printk("%s \n", __func__);

	if (f->index >= 1)
		return -EINVAL;
	
	/* 支持什么格式呢?
     * 查看VideoStreaming Interface的描述符,
     * 得到GUID为"59 55 59 32 00 00 10 00 80 00 00 aa 00 38 9b 71"
     */
	
	//strcpy(f->description, "MJPEG");
	//f->pixelformat = V4L2_PIX_FMT_MJPEG;
	strcpy(f->description, "4:2:2, packed, YUYV");
	f->pixelformat = V4L2_PIX_FMT_YUYV;
    f->type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    
	return 0;
}

int myuvc_g_fmt_vid_cap(struct file *file, void *fh,
			  struct v4l2_format *f)
{
	printk("%s \n", __func__);
	memcpy(f, &myuvc_format, sizeof(*f));
	return 0;
}

int myuvc_try_fmt_vid_cap(struct file *file, void *fh,
				struct v4l2_format *f)
{
	printk("%s \n", __func__);
	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	
	if (f->fmt.pix.pixelformat != V4L2_PIX_FMT_YUYV)
        return -EINVAL;

    /* 调整format的width, height, 
     * 计算bytesperline, sizeimage
     */

    /* 人工查看描述符, 确定支持哪几种分辨率 */
    f->fmt.pix.width  = frames[frame_idx].width;
    f->fmt.pix.height = frames[frame_idx].height;
    
    f->fmt.pix.field      = V4L2_FIELD_NONE;//???
    
	f->fmt.pix.bytesperline =
		(f->fmt.pix.width * bBitsPerPixel) >> 3;
	f->fmt.pix.sizeimage =
		f->fmt.pix.height * f->fmt.pix.bytesperline;
	
	return 0;
}

int myuvc_s_fmt_vid_cap(struct file *file, void *fh,
			  struct v4l2_format *f)
{
	printk("%s \n", __func__);
	
	int ret = myuvc_try_fmt_vid_cap(file, NULL, f);
	if (ret < 0)
		return ret;

    memcpy(&myuvc_format, f, sizeof(myuvc_format));
	return 0;
}


static int myuvc_free_buffers(void)
{
    vfree(myuvc_queue.mem);
    memset(&myuvc_queue, 0, sizeof(myuvc_queue));
    return 0;
}

/* A7 APP调用该ioctl让驱动程序分配若干个缓存, APP将从这些缓存中读到视频数据 
 * 参考: uvc_alloc_buffers
 */
int myuvc_reqbufs(struct file *file, void *fh,
			struct v4l2_requestbuffers *rb)
{
	printk("%s \n", __func__);
	int nbuffers  = rb->count;
	unsigned int bufsize = PAGE_ALIGN(myuvc_format.fmt.pix.sizeimage);
	unsigned int i;
	void *mem = NULL;
	int ret=0;
    
	if ((ret = myuvc_free_buffers()) < 0)
		goto done;

	/* Bail out if no buffers should be allocated. */
	if (nbuffers == 0)
		goto done;

	/* Decrement the number of buffers until allocation succeeds. */
	for (; nbuffers > 0; --nbuffers) {
		mem = vmalloc_32(nbuffers * bufsize);
		if (mem != NULL)
			break;
	}

	if (mem == NULL) {
		ret = -ENOMEM;
		goto done;
	}

	memset(&myuvc_queue, 0, sizeof(myuvc_queue));

    //初始化两个队列,my_uvc_vidioc_qbuf
    INIT_LIST_HEAD(&myuvc_queue.mainqueue);
    INIT_LIST_HEAD(&myuvc_queue.irqqueue);

	for (i = 0; i < nbuffers; ++i) {
		memset(&myuvc_queue.buffer[i], 0, sizeof myuvc_queue.buffer[i]);
		myuvc_queue.buffer[i].buf.index = i;
		myuvc_queue.buffer[i].buf.m.offset = i * bufsize;
		myuvc_queue.buffer[i].buf.length = myuvc_format.fmt.pix.sizeimage;
		myuvc_queue.buffer[i].buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		myuvc_queue.buffer[i].buf.sequence = 0;
		myuvc_queue.buffer[i].buf.field = V4L2_FIELD_NONE;
		myuvc_queue.buffer[i].buf.memory = V4L2_MEMORY_MMAP;
		myuvc_queue.buffer[i].buf.flags = 0;
		myuvc_queue.buffer[i].state     = VIDEOBUF_IDLE;
		init_waitqueue_head(&myuvc_queue.buffer[i].wait);
	}

	myuvc_queue.mem = mem;
	myuvc_queue.count = nbuffers;
	myuvc_queue.buf_size = bufsize;
	ret = nbuffers;
	
done:
	return ret;
}

/* A8 查询缓存状态, 比如地址信息(APP可以用mmap进行映射) 
 * 参考 uvc_query_buffer
 */
int myuvc_querybuf(struct file *file, void *fh,
			 struct v4l2_buffer *v4l2_buf)
{
	printk("%s \n", __func__);
	struct myuvc_buffer *buf = &myuvc_queue.buffer[v4l2_buf->index];
	
	if (v4l2_buf->index >= myuvc_queue.count) {
		return -EINVAL; 
	}
	
	memcpy(v4l2_buf, &buf->buf, sizeof *v4l2_buf);

    //更新flags???
	if (buf->vma_use_count)
		v4l2_buf->flags |= V4L2_BUF_FLAG_MAPPED;
    
#if 0 //與原本uvc拆分=>不更新buf->flags
	switch (buf->state) {
	case VIDEOBUF_ERROR:
	case VIDEOBUF_DONE:
		v4l2_buf->flags |= V4L2_BUF_FLAG_DONE;
		break;
	case VIDEOBUF_QUEUED:
	case VIDEOBUF_ACTIVE:
		v4l2_buf->flags |= V4L2_BUF_FLAG_QUEUED;
		break;
	case VIDEOBUF_IDLE:
	default:
		break;
	}
#endif
	return 0;
}

/* A10 把缓冲区放入队列, 底层的硬件操作函数将会把数据放入这个队列的缓存 
 * 参考: uvc_queue_buffer
 */
int myuvc_qbuf(struct file *file, void *fh,
		 struct v4l2_buffer *v4l2_buf)
{
	printk("%s \n", __func__);
	
	struct myuvc_buffer *buf = &myuvc_queue.buffer[v4l2_buf->index];
	
    /* 0. APP传入的v4l2_buf可能有问题, 要做判断 */
    if (v4l2_buf->type != V4L2_BUF_TYPE_VIDEO_CAPTURE || v4l2_buf->memory != V4L2_MEMORY_MMAP)
        return -EINVAL;

    if (v4l2_buf->index >= myuvc_queue.count)
        return -EINVAL;

    if (buf->state != VIDEOBUF_IDLE)
        return -EINVAL;

    /* 1. 修改状态 */
	buf->state = VIDEOBUF_QUEUED;
	buf->buf.bytesused = 0;
	
    /* 2. 放入2个队列 */
    /* 队列1: 供APP使用 
     * 当缓冲区没有数据时,放入mainqueue队列
     * 当缓冲区有数据时, APP从mainqueue队列中取出
     */
	list_add_tail(&buf->stream, &myuvc_queue.mainqueue);
	
    /* 队列2: 供产生数据的函数使用
     * 当采集到数据时,从irqqueue队列中取出第1个缓冲区,存入数据
     */
	list_add_tail(&buf->irq, &myuvc_queue.irqqueue);
	
	return 0;
}

/* A13 APP通过poll/select确定有数据后, 把缓存从队列中取出来
 * 参考: uvc_dequeue_buffer
 */
int myuvc_dqbuf(struct file *file, void *fh,
		  struct v4l2_buffer *v4l2_buf)
{
	struct myuvc_buffer *buf;
	
	if (list_empty(&myuvc_queue.mainqueue)) {
		return -EINVAL;
	}
	
	buf = list_first_entry(&myuvc_queue.mainqueue, struct myuvc_buffer, stream);
	
	list_del(&buf->stream);
	
	memcpy(v4l2_buf, &buf->buf, sizeof *v4l2_buf);//???

    //與原生uvc拆分 => 不在使用buf->flags

    //修改状态
	switch (buf->state) {
	case VIDEOBUF_ERROR:
        return -EIO;
	case VIDEOBUF_DONE:
		buf->state |= VIDEOBUF_IDLE;
		break;
	case VIDEOBUF_IDLE:
    case VIDEOBUF_QUEUED:
	case VIDEOBUF_ACTIVE:
	default:
		return -EINVAL;
	}
	
	return 0;
}


static void myuvc_print_streaming_params(struct myuvc_streaming_control *ctrl)
{
    printk("video params:\n");
    printk("bmHint                   = %d\n", ctrl->bmHint);
    printk("bFormatIndex             = %d\n", ctrl->bFormatIndex);
    printk("bFrameIndex              = %d\n", ctrl->bFrameIndex);
    printk("dwFrameInterval          = %d\n", ctrl->dwFrameInterval);
    printk("wKeyFrameRate            = %d\n", ctrl->wKeyFrameRate);
    printk("wPFrameRate              = %d\n", ctrl->wPFrameRate);
    printk("wCompQuality             = %d\n", ctrl->wCompQuality);
    printk("wCompWindowSize          = %d\n", ctrl->wCompWindowSize);
    printk("wDelay                   = %d\n", ctrl->wDelay);
    printk("dwMaxVideoFrameSize      = %d\n", ctrl->dwMaxVideoFrameSize);
    printk("dwMaxPayloadTransferSize = %d\n", ctrl->dwMaxPayloadTransferSize);
    printk("dwClockFrequency         = %d\n", ctrl->dwClockFrequency);
    printk("bmFramingInfo            = %d\n", ctrl->bmFramingInfo);
    printk("bPreferedVersion         = %d\n", ctrl->bPreferedVersion);
    printk("bMinVersion              = %d\n", ctrl->bMinVersion);
    printk("bMinVersion              = %d\n", ctrl->bMinVersion);
}

/* 参考: uvc_v4l2_try_format ∕uvc_probe_video 
 *       uvc_set_video_ctrl(video, probe, 1)
 */
static int myuvc_try_streaming_params(struct myuvc_streaming_control *ctrl)
{
	printk("%s\n", __func__);
	__u8 *data;
	__u16 size;
	int ret;
	
	__u8 type = USB_TYPE_CLASS | USB_RECIP_INTERFACE;
	unsigned int pipe;

	memset(ctrl, 0, sizeof *ctrl);
    
	ctrl->bmHint = 1;	/* dwFrameInterval */
	ctrl->bFormatIndex = 1; //format->index => 目前只支持一種YUV
	ctrl->bFrameIndex  = frame_idx + 1; //分辨率{352, 288}
	ctrl->dwFrameInterval = 333333; //手動設置30fps => 查找dwFrameInterval

	size = uvc_version >= 0x0110 ? 34 : 26;
	data = kzalloc(size, GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	*(__le16 *)&data[0] = cpu_to_le16(ctrl->bmHint);
	data[2] = ctrl->bFormatIndex;
	data[3] = ctrl->bFrameIndex;
	*(__le32 *)&data[4] = cpu_to_le32(ctrl->dwFrameInterval);
	*(__le16 *)&data[8] = cpu_to_le16(ctrl->wKeyFrameRate);
	*(__le16 *)&data[10] = cpu_to_le16(ctrl->wPFrameRate);
	*(__le16 *)&data[12] = cpu_to_le16(ctrl->wCompQuality);
	*(__le16 *)&data[14] = cpu_to_le16(ctrl->wCompWindowSize);
	*(__le16 *)&data[16] = cpu_to_le16(ctrl->wDelay);
	put_unaligned_le32(ctrl->dwMaxVideoFrameSize, &data[18]);
	put_unaligned_le32(ctrl->dwMaxPayloadTransferSize, &data[22]);

	if (size == 34) {
		put_unaligned_le32(ctrl->dwClockFrequency, &data[26]);
		data[30] = ctrl->bmFramingInfo;
		data[31] = ctrl->bPreferedVersion;
		data[32] = ctrl->bMinVersion;
		data[33] = ctrl->bMaxVersion;
	}


	pipe = (SET_CUR & 0x80) ? usb_rcvctrlpipe(myuvc_udev, 0)
			      : usb_sndctrlpipe(myuvc_udev, 0);
	type |= (SET_CUR & 0x80) ? USB_DIR_IN : USB_DIR_OUT;

	ret = usb_control_msg(myuvc_udev, pipe, SET_CUR, type, VS_PROBE_CONTROL << 8,
			0 << 8 | myuvc_streaming_intf, data, size, 5000/*timeout*/);

	if (ret != size) {
		uvc_printk(KERN_ERR, "Failed to set UVC %s control : "
			"%d (exp. %u).\n", "probe",
			ret, size);
		ret = -EIO;
	}
	
	kfree(data);
	return (ret < 0)? ret:0;
}

/* 参考: uvc_v4l2_try_format ∕uvc_probe_video 
 *       uvc_set_video_ctrl(video, probe, 1)
 */
static int myuvc_set_streaming_params(struct myuvc_streaming_control *ctrl)
{
	printk("%s\n", __func__);
	__u8 *data;
	__u16 size;
	int ret;

	size = uvc_version >= 0x0110 ? 34 : 26;
	data = kzalloc(size, GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	*(__le16 *)&data[0] = cpu_to_le16(ctrl->bmHint);
	data[2] = ctrl->bFormatIndex;
	data[3] = ctrl->bFrameIndex;
	*(__le32 *)&data[4] = cpu_to_le32(ctrl->dwFrameInterval);
	*(__le16 *)&data[8] = cpu_to_le16(ctrl->wKeyFrameRate);
	*(__le16 *)&data[10] = cpu_to_le16(ctrl->wPFrameRate);
	*(__le16 *)&data[12] = cpu_to_le16(ctrl->wCompQuality);
	*(__le16 *)&data[14] = cpu_to_le16(ctrl->wCompWindowSize);
	*(__le16 *)&data[16] = cpu_to_le16(ctrl->wDelay);
	put_unaligned_le32(ctrl->dwMaxVideoFrameSize, &data[18]);
	put_unaligned_le32(ctrl->dwMaxPayloadTransferSize, &data[22]);

	if (size == 34) {
		put_unaligned_le32(ctrl->dwClockFrequency, &data[26]);
		data[30] = ctrl->bmFramingInfo;
		data[31] = ctrl->bPreferedVersion;
		data[32] = ctrl->bMinVersion;
		data[33] = ctrl->bMaxVersion;
	}
	
	__u8 type = USB_TYPE_CLASS | USB_RECIP_INTERFACE;
	unsigned int pipe;

	pipe = (SET_CUR & 0x80) ? usb_rcvctrlpipe(myuvc_udev, 0)
			      : usb_sndctrlpipe(myuvc_udev, 0);
	type |= (SET_CUR & 0x80) ? USB_DIR_IN : USB_DIR_OUT;

	ret = usb_control_msg(myuvc_udev, pipe, SET_CUR, type, VS_COMMIT_CONTROL << 8,
			0 << 8 | myuvc_streaming_intf, data, size, 5000/*timeout*/);

	if (ret != size) {
		uvc_printk(KERN_ERR, "Failed to set UVC %s control : "
			"%d (exp. %u).\n", "commit",
			ret, size);
		ret = -EIO;
	}
	
	kfree(data);
	return (ret < 0)? ret:0;
}

/* 参考: uvc_get_video_ctrl 
 (ret = uvc_get_video_ctrl(video, probe, 1, GET_CUR)) 
 static int uvc_get_video_ctrl(struct uvc_video_device *video,
     struct uvc_streaming_control *ctrl, int probe, __u8 query)
 */
static int myuvc_get_streaming_params(struct myuvc_streaming_control *ctrl)
{
	printk("%s\n", __func__);
	__u8 *data;
	__u16 size;
	int ret;

	size = uvc_version >= 0x0110 ? 34 : 26;
	data = kmalloc(size, GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	__u8 type = USB_TYPE_CLASS | USB_RECIP_INTERFACE;
	unsigned int pipe;

	pipe = (GET_CUR & 0x80) ? usb_rcvctrlpipe(myuvc_udev, 0)
			      : usb_sndctrlpipe(myuvc_udev, 0);
	type |= (GET_CUR & 0x80) ? USB_DIR_IN : USB_DIR_OUT;

	ret = usb_control_msg(myuvc_udev, pipe, GET_CUR, type, VS_PROBE_CONTROL << 8,
			0 << 8 | myuvc_streaming_intf, data, size, 5000/*timeout*/);
	
	if (ret != size) {
		goto OUT;
	}
	
	ctrl->bmHint = le16_to_cpup((__le16 *)&data[0]);
	ctrl->bFormatIndex = data[2];
	ctrl->bFrameIndex = data[3];
	ctrl->dwFrameInterval = le32_to_cpup((__le32 *)&data[4]);
	ctrl->wKeyFrameRate = le16_to_cpup((__le16 *)&data[8]);
	ctrl->wPFrameRate = le16_to_cpup((__le16 *)&data[10]);
	ctrl->wCompQuality = le16_to_cpup((__le16 *)&data[12]);
	ctrl->wCompWindowSize = le16_to_cpup((__le16 *)&data[14]);
	ctrl->wDelay = le16_to_cpup((__le16 *)&data[16]);
	ctrl->dwMaxVideoFrameSize = get_unaligned_le32(&data[18]);
	ctrl->dwMaxPayloadTransferSize = get_unaligned_le32(&data[22]);

	if (size == 34) {
		ctrl->dwClockFrequency = get_unaligned_le32(&data[26]);
		ctrl->bmFramingInfo = data[30];
		ctrl->bPreferedVersion = data[31];
		ctrl->bMinVersion = data[32];
		ctrl->bMaxVersion = data[33];
	} else {
		//ctrl->dwClockFrequency = video->dev->clock_frequency;
		ctrl->bmFramingInfo = 0;
		ctrl->bPreferedVersion = 0;
		ctrl->bMinVersion = 0;
		ctrl->bMaxVersion = 0;
	}
	
OUT:
	kfree(data);
	return (ret < 0)? ret:0;
}









/* A11 启动传输 
 * 参考: uvc_video_enable(video, 1)
 */
int myuvc_streamon(struct file *file, void *fh,
			 enum v4l2_buf_type i)
{
	printk("%s\n", __func__);
	int ret;
    /* 1. 向USB摄像头设置参数: 比如使用哪个format, 使用这个format下的哪个frame(分辨率) 
     * 参考: uvc_set_video_ctrl / uvc_get_video_ctrl
     * 1.1 根据一个结构体uvc_streaming_control设置数据包: 可以手工设置,也可以读出后再修改
     * 1.2 调用usb_control_msg发出数据包
     */

    /* a. 测试参数 */
    ret = myuvc_try_streaming_params(&myuvc_params);
    printk("myuvc_try_streaming_params ret = %d\n", ret);

    /* b. 取出参数 */
    ret = myuvc_get_streaming_params(&myuvc_params);
    printk("myuvc_get_streaming_params ret = %d\n", ret);

    /* c. 设置参数 */
    ret = myuvc_set_streaming_params(&myuvc_params);
    printk("myuvc_set_streaming_params ret = %d\n", ret);
	
	myuvc_print_streaming_params(&myuvc_params);
    
    
    
    
	return 0;
}

/*
 * A14 之前已经通过mmap映射了缓存, APP可以直接读数据
 * A15 再次调用myuvc_vidioc_qbuf把缓存放入队列
 * A16 poll...
 */

/* A17 停止 */
int myuvc_streamoff(struct file *file, void *fh,
		  enum v4l2_buf_type i)
{
	return 0;
}



struct v4l2_ioctl_ops myuvc_ioctl_ops = {

	// 表示它是一个摄像头设备
	.vidioc_querycap	  = myuvc_querycap,

	/* 用于列举、获得、测试、设置摄像头的数据的格式 */
	.vidioc_enum_fmt_vid_cap  = myuvc_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap	  = myuvc_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap   = myuvc_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap	  = myuvc_s_fmt_vid_cap,

	/* 缓冲区操作: 申请/查询/放入队列/取出队列 */
	.vidioc_reqbufs 	  = myuvc_reqbufs,
	.vidioc_querybuf	  = myuvc_querybuf,
	.vidioc_qbuf		  = myuvc_qbuf,
	.vidioc_dqbuf		  = myuvc_dqbuf,

	// 启动/停止
	.vidioc_streamon	  = myuvc_streamon,
	.vidioc_streamoff	  = myuvc_streamoff,
};

static struct video_device *myuvc_vdev;

static void myuvc_dev_release(struct video_device *vdev)
{
    printk("%s \n", __func__);

}

int myuvc_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
    int ret;
	static int cnt = 0;
    printk("%s cnt=%d\n", __func__, cnt++);
    struct usb_device *dev = interface_to_usbdev(intf);
    struct usb_device_descriptor *descriptor = &dev->descriptor;
    struct usb_host_config *host_config;
    struct usb_host_interface *interface;
    //struct usb_interface_descriptor	*interface;
    struct usb_host_endpoint *endPoint;
    
    //myuvc_udev = dev;//panic on NULL ptr when set_cur, ...
    myuvc_udev = interface_to_usbdev(intf);
    if (cnt == 1) //获取编号
        myuvc_control_intf = intf->cur_altsetting->desc.bInterfaceNumber;
    else if (cnt == 2)
        myuvc_streaming_intf = intf->cur_altsetting->desc.bInterfaceNumber;

        
	if(cnt == 2)
	{
		/*1. 分配一個video結構體 */
		myuvc_vdev = video_device_alloc();
		if(NULL == myuvc_vdev){
			printk("failed to alloc video device");
			return -ENOMEM;
		}
		
		/*2. 設置 */
		strscpy(myuvc_vdev->name, MODULE_NAME, sizeof(myuvc_vdev->name));
		myuvc_vdev->release   		= myuvc_dev_release;
		myuvc_vdev->fops 			= &myuvc_fops;
		myuvc_vdev->ioctl_ops 		= &myuvc_ioctl_ops;
        
        myuvc_vdev->v4l2_dev 	    = &v4l2_dev;
		myuvc_vdev->device_caps	    = V4L2_CAP_STREAMING;
		
		/*3. 註冊 */
		ret = video_register_device(myuvc_vdev, VFL_TYPE_GRABBER, -1/*auto*/);
		if(ret){
			printk("video_register_device failed ??? ret=%d", ret);
			return ret;
		}
        
        myuvc_streamon(NULL, NULL, 0);
	}

    return 0;
}

void myuvc_disconnect(struct usb_interface *intf)
{
    static int cnt = 0;
    printk("%s cnt=%d\n", __func__, cnt++);

    if (cnt == 2)
    {
        video_unregister_device(myuvc_vdev);
        video_device_release(myuvc_vdev);
    }

	return;
}



static const struct usb_device_id uvc_ids[] = {
    /* Generic USB Video Class */
	{ USB_INTERFACE_INFO(USB_CLASS_VIDEO, 1, 0) },  /* video Ctrl Intf */
	{ USB_INTERFACE_INFO(USB_CLASS_VIDEO, 2, 0) },  /* video Stream Intf */
	{}
};

static struct usb_driver myuvc_drv = {

    .name		= MODULE_NAME,
    .probe		= myuvc_probe,
    .disconnect	= myuvc_disconnect,
    .id_table	= uvc_ids,

};

static int myuvc_init(void)
{
    usb_register(&myuvc_drv);
    return 0;
}

static void myuvc_exit(void)
{
    usb_deregister(&myuvc_drv);
}


module_init(myuvc_init);
module_exit(myuvc_exit);


MODULE_DESCRIPTION("Virtual Video Test Driver");
MODULE_AUTHOR("Will Chen");
MODULE_LICENSE("GPL");
