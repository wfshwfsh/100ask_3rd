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

#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf-core.h>



#define MODULE_NAME "myuvc"



struct v4l2_format myuvc_format;

struct frame_desc{
	int width;
	int height;
};

static struct frame_desc frames[] = {{640, 480}, {352, 288}, {320, 240}, {176, 144}, {160, 120}};
static int frame_idx=0;

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

static struct myuvc_queue myuvc_queue;


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

	strscpy(cap->driver, "myuvc", sizeof(cap->driver));
	strscpy(cap->card, "myuvc", sizeof(cap->card));
	
	//usb_make_path(stream->dev->udev, cap->bus_info, sizeof(cap->bus_info));
	snprintf(cap->bus_info, sizeof(cap->bus_info),
					"platform:%s", MODULE_NAME);

	cap->capabilities = V4L2_CAP_DEVICE_CAPS | V4L2_CAP_STREAMING;
	cap->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;

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
    kfree(myuvc_queue.mem);
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
int myuvc_qbuf(struct file *file, void *fh,
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

/* A11 启动传输 
 * 参考: uvc_video_enable(video, 1)
 */
int myuvc_streamon(struct file *file, void *fh,
			 enum v4l2_buf_type i)
{
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

#if 1
	/* 缓冲区操作: 申请/查询/放入队列/取出队列 */
	.vidioc_reqbufs 	  = myuvc_reqbufs,
	.vidioc_querybuf	  = myuvc_querybuf,
	.vidioc_qbuf		  = myuvc_qbuf,
	.vidioc_dqbuf		  = myuvc_dqbuf,

	// 启动/停止
	.vidioc_streamon	  = myuvc_streamon,
	.vidioc_streamoff	  = myuvc_streamoff,
#endif
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

		//if not set, will cause return EINVAL(-22)
		//myuvc_vdev->device_caps    = V4L2_CAP_STREAMING;
		
		
		/*3. 註冊 */
		ret = video_register_device(myuvc_vdev, VFL_TYPE_GRABBER, -1/*auto*/);
		if(ret){
			printk("video_register_device failed ??? ret=%d", ret);
			return ret;
		}
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
