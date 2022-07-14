#include <linux/module.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/font.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/videodev2.h>
#include <linux/v4l2-dv-timings.h>
#include <media/videobuf2-vmalloc.h>
#include <media/videobuf2-dma-contig.h>
#include <media/v4l2-dv-timings.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-event.h>

#include <linux/fb.h>
#include <linux/workqueue.h>
#include <media/cec.h>
#include <media/videobuf2-v4l2.h>
#include <media/v4l2-device.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-ctrls.h>
//#include <media/tpg/v4l2-tpg.h>
#include <media/videobuf-core.h>
#include <media/videobuf-vmalloc.h>


#define DBG_1	printk("111\n");
#define DBG_2	printk("222\n");
#define DBG_3	printk("333\n");
#define DBG_4	printk("444\n");
#define DBG_5	printk("555\n");


#define VIVID_MODULE_NAME "myVivid"
//#define DEBUG 1


static struct video_device *myVivid_dev;
static struct v4l2_device v4l2_dev;
static struct v4l2_format myVivid_format;

static struct videobuf_queue myVivid_vb_vidqueue;
static spinlock_t myVivid_queue_slock;

static struct list_head  myVivid_vb_local_queue;
static struct timer_list myVivid_timer;


/* 参考documentations/video4linux/v4l2-framework.txt:
 *     drivers\media\video\videobuf-core.c 
 ops->buf_setup   - calculates the size of the video buffers and avoid they
            to waste more than some maximum limit of RAM;
 ops->buf_prepare - fills the video buffer structs and calls
            videobuf_iolock() to alloc and prepare mmaped memory;
 ops->buf_queue   - advices the driver that another buffer were
            requested (by read() or by QBUF);
 ops->buf_release - frees any buffer that were allocated.
 
 *
 */


/* ------------------------------------------------------------------
	Videobuf operations
   ------------------------------------------------------------------*/
/* APP调用ioctl VIDIOC_REQBUFS时会导致此函数被调用,
 * videobuf_reqbufs(){ ... q->ops->buf_setup(q, &count, &size); ...}
 * 它重新调整count和size
 */
int myVivid_buffer_setup(struct videobuf_queue *q, unsigned int *count, unsigned int *size)
{
	printk("%s \n", __func__);

	*size = myVivid_format.fmt.pix.sizeimage;
	if(0 == *count)
		*count = 32;
	
	return 0;
}


/* APP调用ioctlVIDIOC_QBUF时导致此函数被调用,
 * 它会填充video_buffer结构体并调用videobuf_iolock来分配内存
 * 
 */
int myVivid_buffer_prepare(struct videobuf_queue *q, struct videobuf_buffer *vb, enum v4l2_field field)
{
	printk("%s \n", __func__);
	/* 1. 做準備工作 */
	if (VIDEOBUF_NEEDS_INIT == vb->state) {
		struct v4l2_pix_format *pix;

		pix = &myVivid_format.fmt.pix;
		vb->size			= pix->sizeimage; /* real frame size */
		vb->bytesperline 	= pix->bytesperline;
		vb->width			= pix->width;
		vb->height			= pix->height;
		vb->field  			= field;

	/* 2. videobuf_iolock分配內存(userptr才會分配), 我們用mmap */
#if 0
		rc = videobuf_iolock(q, vb, NULL);
		if (rc < 0)
			return rc;
#endif
	}
	/* 3. 設置狀態 */
	//vb->field = field;
	vb->state = VIDEOBUF_PREPARED;
	
	return 0;
}

/* APP调用ioctlVIDIOC_QBUF时:
 * 1. 先调用buf_prepare进行一些准备工作
 * 2. 把buf放入队列
 * 3. 调用buf_queue(起通知作用)
 */
void myVivid_buffer_queue(struct videobuf_queue *q, struct videobuf_buffer *vb)
{
	printk("%s \n", __func__);
	vb->state = VIDEOBUF_QUEUED;
	
    /* 把videobuf放入本地一个队列尾部
     * 定时器处理函数就可以从本地队列取出videobuf
     */
	list_add_tail(&vb->queue, &myVivid_vb_local_queue);
}

/* APP不再使用队列时, 用它来释放内存 */
void myVivid_buffer_release(struct videobuf_queue *q, struct videobuf_buffer *vb)
{
	printk("%s \n", __func__);
	videobuf_vmalloc_free(vb);
	vb->state = VIDEOBUF_NEEDS_INIT;
}

static struct videobuf_queue_ops myVivid_video_qops =
{
    .buf_setup      = myVivid_buffer_setup, /* 计算大小以免浪费 */
    .buf_prepare    = myVivid_buffer_prepare,
    .buf_queue      = myVivid_buffer_queue,
    .buf_release    = myVivid_buffer_release,
};





static void myVivid_timer_function(struct timer_list *t)
{
	//printk("%s \n", __func__);
	struct videobuf_buffer *vb;
	void *vbuf = NULL;
	struct timeval ts;
	
	/* 1. fill video data */
	/* 1.1 從本地對列取出第一個buffer */
	//spin_lock_irqsave(&dev->slock, flags);
	if (list_empty(&myVivid_vb_local_queue)) {
		printk("No active video buffer\n");
		//spin_unlock_irqrestore(&dev->slock, flags);
		goto OUT;
	}

	vb = list_entry(myVivid_vb_local_queue.next, struct videobuf_buffer, queue);
	//spin_unlock_irqrestore(&dev->slock, flags);

    /* Nobody is waiting on this buffer, return */
    if (!waitqueue_active(&vb->done))
        goto OUT;

	/* 1.2 Fill buffer */
	vbuf = videobuf_to_vmalloc(vb);
	do_gettimeofday(&ts);
	
	memset(vbuf, 0, vb->size);
	//vivi_fillbuff(dev, buf);

	vb->field_count++;
	vb->ts = ts;
    vb->state = VIDEOBUF_DONE;

	/* 1.3 把videobuf从本地队列中删除 */
	list_del(&vb->queue);

	/* 2. wakeup */
	wake_up(&vb->done);
	
OUT:
	/* 3. reset timer */
	mod_timer(&myVivid_timer, jiffies + (HZ/30));//30fps	
}

static int myVivid_open(struct file *file)
{
    printk("%s \n", __func__);
	videobuf_queue_vmalloc_init(&myVivid_vb_vidqueue, &myVivid_video_qops,
                                NULL, &myVivid_queue_slock, V4L2_BUF_TYPE_VIDEO_CAPTURE, V4L2_FIELD_INTERLACED,
                                sizeof(struct videobuf_buffer), NULL, NULL); /* 倒数第3个参数是buffer的头部大小 */

	myVivid_timer.expires = jiffies + 1;
	add_timer(&myVivid_timer);
	return 0;
}

static int myVivid_release(struct file *file)
{
    printk("%s \n", __func__);
	videobuf_stop(&myVivid_vb_vidqueue);
	videobuf_mmap_free(&myVivid_vb_vidqueue);

	del_timer(&myVivid_timer);
	return 0;
}

static int myVivid_mmap(struct file *file, struct vm_area_struct *vm_area)
{
    printk("%s \n", __func__);
	videobuf_mmap_mapper(&myVivid_vb_vidqueue, vm_area);
	return 0;
}

static unsigned int myVivid_poll(struct file *file, struct poll_table_struct *poll_table)
{
    printk("%s \n", __func__);
	return videobuf_poll_stream(file, &myVivid_vb_vidqueue, poll_table);
}


static const struct v4l2_file_operations myVivid_fops = {
	.owner			= THIS_MODULE,
	.unlocked_ioctl = video_ioctl2,
	.open			= myVivid_open,
	.release		= myVivid_release,
	.mmap 			= myVivid_mmap,
	
	.poll  			= myVivid_poll,
};

int myVivid_querycap(struct file *file, void *fh,
			   struct v4l2_capability *cap)
{
    printk("%s \n", __func__);

	strscpy(cap->driver, "myVivid", sizeof(cap->driver));
	strscpy(cap->card, "myVivid", sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info),
					"platform:%s", VIVID_MODULE_NAME);
	
	cap->version = 0x0001;
	cap->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;

	return 0;
}

int myVivid_enum_fmt_vid_cap(struct file *file, void *fh,
					  struct v4l2_fmtdesc *f)
{
	printk("%s \n", __func__);
	if (f->index >= 1)
		return -EINVAL;

	strcpy(f->description, "4:2:2, packed, YUYV");
	f->pixelformat = V4L2_PIX_FMT_YUYV;

	return 0;
}

int myVivid_g_fmt_vid_cap(struct file *file, void *fh,
			  struct v4l2_format *f)
{
	printk("%s \n", __func__);

	memcpy(f, &myVivid_format, sizeof(myVivid_format));
	return 0;
}

int myVivid_try_fmt_vid_cap(struct file *file, void *fh,
				struct v4l2_format *f)
{
	printk("%s \n", __func__);
	unsigned int maxw, maxh;
    enum v4l2_field field;

    if (f->fmt.pix.pixelformat != V4L2_PIX_FMT_YUYV)
        return -EINVAL;

	field = f->fmt.pix.field;

	if (field == V4L2_FIELD_ANY) {
		field = V4L2_FIELD_INTERLACED;
	} else if (V4L2_FIELD_INTERLACED != field) {
		return -EINVAL;
	}

	maxw  = 1024;
	maxh  = 768;

    /* 调整format的width, height, 
     * 计算bytesperline, sizeimage
     */
	v4l_bound_align_image(&f->fmt.pix.width, 48, maxw, 2,
			      &f->fmt.pix.height, 32, maxh, 0, 0);
	f->fmt.pix.bytesperline =
		(f->fmt.pix.width * 16) >> 3;
	f->fmt.pix.sizeimage =
		f->fmt.pix.height * f->fmt.pix.bytesperline;

	return 0;
}

int myVivid_s_fmt_vid_cap(struct file *file, void *fh,
			  struct v4l2_format *f)
{
	printk("%s \n", __func__);
	int ret = myVivid_try_fmt_vid_cap(file, NULL, f);
	if(ret<0)
		return ret;
	
	memcpy(&myVivid_format, f, sizeof(myVivid_format));
	return 0;
}

int myVivid_reqbufs(struct file *file, void *fh,
			struct v4l2_requestbuffers *b)
{
	printk("%s \n", __func__);
	return videobuf_reqbufs(&myVivid_vb_vidqueue, b);
}

int myVivid_querybuf(struct file *file, void *fh,
			 struct v4l2_buffer *b)
{
	printk("%s \n", __func__);
	return videobuf_querybuf(&myVivid_vb_vidqueue, b);
}


int myVivid_qbuf(struct file *file, void *fh,
		 struct v4l2_buffer *b)
{
	printk("%s \n", __func__);
	return videobuf_qbuf(&myVivid_vb_vidqueue, b);
}

int myVivid_dqbuf(struct file *file, void *fh,
		  struct v4l2_buffer *b)
{
	printk("%s \n", __func__);
	return videobuf_dqbuf(&myVivid_vb_vidqueue, b, file->f_flags & O_NONBLOCK);
}


int myVivid_streamon(struct file *file, void *fh,
			 enum v4l2_buf_type i)
{
	return videobuf_streamon(&myVivid_vb_vidqueue);
}

int myVivid_streamoff(struct file *file, void *fh,
		  enum v4l2_buf_type i)
{
	return videobuf_streamoff(&myVivid_vb_vidqueue);
}



struct v4l2_ioctl_ops myVivid_ioctl_ops = {

	// 表示它是一个摄像头设备
	.vidioc_querycap	  = myVivid_querycap,

	/* 用于列举、获得、测试、设置摄像头的数据的格式 */
	.vidioc_enum_fmt_vid_cap  = myVivid_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap	  = myVivid_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap   = myVivid_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap	  = myVivid_s_fmt_vid_cap,

	/* 缓冲区操作: 申请/查询/放入队列/取出队列 */
	.vidioc_reqbufs 	  = myVivid_reqbufs,
	.vidioc_querybuf	  = myVivid_querybuf,
	.vidioc_qbuf		  = myVivid_qbuf,
	.vidioc_dqbuf		  = myVivid_dqbuf,

	// 启动/停止
	.vidioc_streamon	  = myVivid_streamon,
	.vidioc_streamoff	  = myVivid_streamoff,
};



static void myVivid_dev_release(struct video_device *vdev)
{
    printk("%s \n", __func__);

}

static int myVivid_probe(struct platform_device *pdev)
{
    printk("%s \n", __func__);
	int ret = 0;
	
    //1. 分配video_device結構體
	myVivid_dev = video_device_alloc();
	if(NULL == myVivid_dev){
		printk("failed to alloc video device ");
		return -ENOMEM;
	}
	
	//2. 設置
	strscpy(myVivid_dev->name, VIVID_MODULE_NAME, sizeof(myVivid_dev->name));
	myVivid_dev->release   		= myVivid_dev_release;
	myVivid_dev->fops 			= &myVivid_fops;
    myVivid_dev->ioctl_ops 		= &myVivid_ioctl_ops;
	myVivid_dev->v4l2_dev  		= &v4l2_dev;

	//if not set, will cause return EINVAL(-22)
	myVivid_dev->device_caps    = V4L2_CAP_STREAMING | V4L2_CAP_VIDEO_M2M_MPLANE;
	
	//3. 分配video device
    ret = video_register_device(myVivid_dev, VFL_TYPE_GRABBER, -1/*auto*/);
    if(ret){
		printk("video_register_device failed ??? ret=%d", ret);
		goto ERR_RELEASE_DEV;
	}

	/* 用定时器产生数据并唤醒进程 */
	timer_setup(&myVivid_timer, myVivid_timer_function, 0);
	
	INIT_LIST_HEAD(&myVivid_vb_local_queue);
    return 0;

ERR_RELEASE_DEV:
	video_device_release(myVivid_dev);
	return -ENODEV;
}

static int myVivid_remove(struct platform_device *pdev)
{
    printk("remove \n");

	if(myVivid_dev->v4l2_dev != NULL)
	    v4l2_device_unregister(myVivid_dev->v4l2_dev);
	if(myVivid_dev != NULL)
	    video_device_release(myVivid_dev);

    return 0;
}

static void myVivid_pdev_release(struct device *dev)
{
    printk("platform release \n");
}

static struct platform_device myVivid_pdev = {
	.name		    = "myVivid",
	.dev.release	= myVivid_pdev_release,
};

static struct platform_driver myVivid_pdrv = {
	.probe		= myVivid_probe,
	.remove		= myVivid_remove,
	.driver		= {
		.name	= "myVivid",
	},
};


static int __init myVivid_init(void)
{
    int ret;
    
    ret = platform_device_register(&myVivid_pdev);
    if(ret){
        printk("platform_device_register FAIL ???");    return ret;
    }
    
    ret = platform_driver_register(&myVivid_pdrv);
    if(ret){
        printk("platform_driver_register FAIL ???");
        platform_device_unregister(&myVivid_pdev);
        return ret;
    }
    
    return ret;
}

static void __exit myVivid_exit(void)
{
    platform_driver_unregister(&myVivid_pdrv);
    platform_device_unregister(&myVivid_pdev);
}

module_init(myVivid_init);
module_exit(myVivid_exit);


MODULE_DESCRIPTION("Virtual Video Test Driver");
MODULE_AUTHOR("Will Chen");
MODULE_LICENSE("GPL");
                  
