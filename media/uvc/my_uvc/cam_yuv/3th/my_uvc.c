#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/videodev2.h>
#include <linux/vmalloc.h>
#include <linux/wait.h>
#include <linux/mm.h>
#include <asm/atomic.h>
#include <asm/unaligned.h>

#include <media/v4l2-device.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf-core.h>

#include "uvcvideo.h"


#define MODULE_NAME "myuvc"
#define UVC_MAX_PACKETS 32

//對應幾種___
#define MYUVC_URBS 5


struct myuvc_streaming_control {
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


struct v4l2_format myuvc_format;

struct frame_desc{
	int width;
	int height;
};

static struct frame_desc frames[] = {{640, 480}, {352, 288}, {320, 240}, {176, 144}, {160, 120}};
static int frame_idx=1;

static int myuvc_bEndpointAddress = 0x81;
static int wMaxPacketSize = 1024;
static int bBitsPerPixel = 16; /* lsusb -v -d xxxx:  "bBitsPerPixel" for YUYV only */



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
    void *mem;
    int count;
    int buf_size;
	struct myuvc_buffer buffer[32];
	

	struct list_head mainqueue;
	struct list_head irqqueue;
};

static struct video_device *myuvc_vdev;
static struct usb_device *myuvc_udev;
static struct v4l2_device v4l2_dev;
static struct myuvc_queue myuvc_queue;

static int uvc_version = 0x0100; /* lsusb -v -d 0x1e4e: bcdUVC */

static int myuvc_ctrl_intf;
static int myuvc_streaming_intf;
static int myuvc_streaming_bAlternateSetting = 8;
static struct myuvc_streaming_control myuvc_params;



/* A1 */
static int myuvc_open(struct file *filp)
{
    printk("%s \n", __func__);

	return 0;
}


/*
 * VMA operations.
 */
static void myuvc_vm_open(struct vm_area_struct *vma)
{
	struct uvc_buffer *buffer = vma->vm_private_data;
	buffer->vma_use_count++;
}

static void myuvc_vm_close(struct vm_area_struct *vma)
{
	struct uvc_buffer *buffer = vma->vm_private_data;
	buffer->vma_use_count--;
}

static struct vm_operations_struct uvc_vm_ops = {
	.open		= myuvc_vm_open,
	.close		= myuvc_vm_close,
};


/* A9 把缓存映射到APP的空间,以后APP就可以直接操作这块缓存 */
static int myuvc_mmap(struct file *file, struct vm_area_struct *vma)
{
    printk("%s \n", __func__);
	struct myuvc_buffer *buffer;
	struct page *page;
	unsigned long addr, start, size;
	unsigned int i;
	int ret = 0;

	start = vma->vm_start;
	size = vma->vm_end - vma->vm_start;

	//mutex_lock(&video->queue.mutex);

	/* 应用程序调用mmap函数时, 会传入offset参数
	* 根据这个offset找出指定的缓冲区
	*/
	for (i = 0; i < myuvc_queue.count; ++i) {
		buffer = &myuvc_queue.buffer[i];
		if ((buffer->buf.m.offset >> PAGE_SHIFT) == vma->vm_pgoff)
			break;
	}

	if (i == myuvc_queue.count || size != myuvc_queue.buf_size) {
		ret = -EINVAL;
		goto done;
	}

	/*
	 * VM_IO marks the area as being an mmaped region for I/O to a
	 * device. It also prevents the region from being core dumped.
	 */
	vma->vm_flags |= VM_IO;

	/* 根据虚拟地址找到缓冲区对应的page构体 */
	addr = (unsigned long)myuvc_queue.mem + buffer->buf.m.offset;
	while (size > 0) {
		page = vmalloc_to_page((void *)addr);
		
		/* 把page和APP传入的虚拟地址挂构 */
		if ((ret = vm_insert_page(vma, start, page)) < 0)
			goto done;

		start += PAGE_SIZE;
		addr += PAGE_SIZE;
		size -= PAGE_SIZE;
	}

	vma->vm_ops = &uvc_vm_ops;
	vma->vm_private_data = buffer;
	myuvc_vm_open(vma);

done:
	//mutex_unlock(&video->queue.mutex);
	return ret;
}


/* A12 APP调用POLL/select来确定缓存是否就绪(有数据) */
static unsigned int myuvc_poll(struct file *file, struct poll_table_struct *wait)
{
    printk("%s \n", __func__);
	struct myuvc_buffer *buf;
	unsigned int mask = 0;

	//mutex_lock(&queue->mutex);
	if (list_empty(&myuvc_queue.mainqueue)) {
		mask |= POLLERR;
		goto done;
	}
	buf = list_first_entry(&myuvc_queue.mainqueue, struct myuvc_buffer, stream);

	poll_wait(file, &buf->wait, wait);
	if (buf->state == UVC_BUF_STATE_DONE ||
	    buf->state == UVC_BUF_STATE_ERROR)
		mask |= POLLIN | POLLRDNORM;

done:
	//mutex_unlock(&queue->mutex);
	return mask;
}

/* A18 关闭 */
static int myuvc_close(struct file *file)
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

static int myuvc_querycap(struct file *file, void *fh,
			   struct v4l2_capability *cap)
{
    printk("%s \n", __func__);

	strscpy(cap->driver, "myuvc", sizeof(cap->driver));
	strscpy(cap->card, "myuvc", sizeof(cap->card));
	
	//usb_make_path(stream->dev->udev, cap->bus_info, sizeof(cap->bus_info));
	snprintf(cap->bus_info, sizeof(cap->bus_info),
					"platform:%s", MODULE_NAME);

	cap->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;
	
	return 0;
}

static int myuvc_enum_fmt_vid_cap(struct file *file, void *fh,
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
	return 0;
}

static int myuvc_g_fmt_vid_cap(struct file *file, void *fh,
			  struct v4l2_format *f)
{
	printk("%s \n", __func__);
	memcpy(f, &myuvc_format, sizeof(*f));
	return 0;
}

static int myuvc_try_fmt_vid_cap(struct file *file, void *fh,
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
    
	f->fmt.pix.bytesperline =
		(f->fmt.pix.width * bBitsPerPixel) >> 3;
	f->fmt.pix.sizeimage =
		f->fmt.pix.height * f->fmt.pix.bytesperline;
	
	return 0;
}

static int myuvc_s_fmt_vid_cap(struct file *file, void *fh,
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
    kfree(myuvc_queue.mem);
    memset(&myuvc_queue, 0, sizeof(myuvc_queue));
    return 0;
}

/* A7 APP调用该ioctl让驱动程序分配若干个缓存, APP将从这些缓存中读到视频数据 
 * 参考: uvc_alloc_buffers
 */
static int myuvc_reqbufs(struct file *file, void *fh,
			struct v4l2_requestbuffers *rb)
{
	printk("%s \n", __func__);
	int nbuffers  = rb->count;
	unsigned int bufsize = PAGE_ALIGN(myuvc_format.fmt.pix.sizeimage);
	unsigned int i;
	void *mem = NULL;
	int ret;

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

	

	for (i = 0; i < nbuffers; ++i) {
		memset(&myuvc_queue.buffer[i], 0, sizeof myuvc_queue.buffer[i]);
		myuvc_queue.buffer[i].buf.index = i;
		myuvc_queue.buffer[i].buf.m.offset = i * bufsize; /* 對應complete時的offset */
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
static int myuvc_querybuf(struct file *file, void *fh,
			 struct v4l2_buffer *v4l2_buf)
{
	printk("%s \n", __func__);
	struct myuvc_buffer *buf = &myuvc_queue.buffer[v4l2_buf->index];
	
	if (v4l2_buf->index >= myuvc_queue.count) {
		return -EINVAL; 
	}
	
	memcpy(v4l2_buf, &buf->buf, sizeof *v4l2_buf);

	if (buf->vma_use_count)
		v4l2_buf->flags |= V4L2_BUF_FLAG_MAPPED;

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

	return 0;
}

/* A10 把缓冲区放入队列, 底层的硬件操作函数将会把数据放入这个队列的缓存 
 * 参考: uvc_queue_buffer
 */
static int myuvc_qbuf(struct file *file, void *fh,
		 struct v4l2_buffer *v4l2_buf)
{
	printk("%s \n", __func__);
	
	struct myuvc_buffer *buf = &myuvc_queue.buffer[v4l2_buf->index];
	
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
static int myuvc_dqbuf(struct file *file, void *fh,
		  struct v4l2_buffer *v4l2_buf)
{
	struct myuvc_buffer *buf;
	
	if (list_empty(&myuvc_queue.mainqueue)) {
		return -EINVAL;
	}
	
	buf = list_first_entry(&myuvc_queue.mainqueue, struct myuvc_buffer, stream);
	
	list_del(&buf->stream);
	
	memcpy(v4l2_buf, &buf->buf, sizeof *v4l2_buf);//???

	if (buf->vma_use_count)
		v4l2_buf->flags |= V4L2_BUF_FLAG_MAPPED;

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
static int myuvc_streamon(struct file *file, void *fh,
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
	
    /* d. 设置VideoStreaming Interface所使用的setting
     * d.1 从myuvc_params确定带宽
     * d.2 根据setting的endpoint能传输的 wMaxPacketSize
     *     找到能满足该带宽的setting
     */
    /* 手工确定:
     * bandwidth = myuvc_params.dwMaxPayloadTransferSize = 1024
     * 观察lsusb -v -d 0x1e4e:的结果:
     *                wMaxPacketSize     0x0400  1x 1024 bytes
     * bAlternateSetting       8 => Interface Descriptor altsetting 8
     */
		
	/* 2. 分配设置URB */
	
	/* 3. 提交URB以接收数据 */
	
	return 0;
}

/*
 * A14 之前已经通过mmap映射了缓存, APP可以直接读数据
 * A15 再次调用myuvc_vidioc_qbuf把缓存放入队列
 * A16 poll...
 */

/* A17 停止 */
static int myuvc_streamoff(struct file *file, void *fh,
		  enum v4l2_buf_type t)
{
	struct urb *urb;
	unsigned int i;

    /* 1. kill URB */

    /* 2. free URB */

    /* 3. 设置VideoStreaming Interface为setting 0 */
    
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
    
	myuvc_udev = dev;//panic on NULL ptr when set_cur, ...
	
	if(cnt == 1)
	{
		myuvc_ctrl_intf = intf->cur_altsetting->desc.bInterfaceNumber;
	}
	else if(cnt == 2)
	{
		myuvc_streaming_intf = intf->cur_altsetting->desc.bInterfaceNumber;
		
		/*1. 分配一個video結構體 */
		myuvc_vdev = video_device_alloc();
		if(NULL == myuvc_vdev){
			printk("failed to alloc video device");
			return -ENOMEM;
		}
		
		/*2. 設置 */
		strscpy(myuvc_vdev->name, MODULE_NAME, sizeof(myuvc_vdev->name));
		myuvc_vdev->release  	= myuvc_dev_release;
		myuvc_vdev->fops 		= &myuvc_fops;
		myuvc_vdev->ioctl_ops	= &myuvc_ioctl_ops;
		myuvc_vdev->v4l2_dev 	= &v4l2_dev;
		
		myuvc_vdev->device_caps	= V4L2_CAP_STREAMING | V4L2_CAP_VIDEO_M2M_MPLANE;
		
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
