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
#include <media/videobuf2-core.h>
#include <media/videobuf2-vmalloc.h>
#include <media/videobuf2-dma-contig.h>
#include <media/v4l2-dv-timings.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-event.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>



#define DBG_1	printk("111\n");
#define DBG_2	printk("222\n");
#define DBG_3	printk("333\n");
#define DBG_4	printk("444\n");
#define DBG_5	printk("555\n");


#define VIVID_MODULE_NAME "myVivid"
//#define DEBUG 1


/* buffer for one video frame */
struct myvivid_buffer {
	/* common v4l buffer stuff -- must be first */
	struct vb2_buffer	vb;
	struct list_head	list;
	struct vivi_fmt        *fmt;
};




static struct video_device *myVivid_dev;
static struct v4l2_device v4l2_dev;
static struct v4l2_format myVivid_format;

//static struct videobuf_queue myVivid_vb_vidqueue;
struct vb2_queue myVivid_vb_vidq;

static struct mutex myVivid_vb_mutex;

static spinlock_t myVivid_queue_slock;

//static struct list_head  myVivid_vb_local_queue;
//static struct timer_list myVivid_timer;

//#include "fill_buf.c"



#define vid_limit 16



static int queue_setup(struct vb2_queue *vq,
		       unsigned *nbuffers, unsigned *nplanes,
		       unsigned sizes[], struct device *alloc_devs[])

{
    printk("%s\n", __func__);
	unsigned long size = myVivid_format.fmt.pix.sizeimage;
	if (0 == *nbuffers)
		*nbuffers = 32;

	while (size * *nbuffers > vid_limit * 1024 * 1024)
		(*nbuffers)--;

	*nplanes = 1;
	sizes[0] = size;

	/*
	 * videobuf2-vmalloc allocator is context-less so no need to set
	 * alloc_ctxs array.
	 */
	printk("%s, count=%d, size=%ld\n", __func__, *nbuffers, size);
	return 0;
}

static int buffer_prepare(struct vb2_buffer *vb)
{
    printk("%s\n", __func__);
	//unsigned long size = myVivid_format.fmt.pix.sizeimage;
	//
	//if (vb2_plane_size(vb, 0) < size) {
	//	printk("%s data will not fit into plane (%lu < %lu)\n",
	//			__func__, vb2_plane_size(vb, 0), size);
	//	return -EINVAL;
	//}
	//
	//vb2_set_plane_payload(vb, 0, size);


	//precalculate_bars(dev);
	//precalculate_line(dev);

	return 0;
}

static void buffer_finish(struct vb2_buffer *vb)
{
	printk("%s\n", __func__);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct v4l2_timecode *tc = &vbuf->timecode;
	unsigned fps = 30;
	unsigned seq = vbuf->sequence;

	/*
	 * Set the timecode. Rarely used, so it is interesting to
	 * test this.
	 */
	vbuf->flags |= V4L2_BUF_FLAG_TIMECODE;
	
	tc->type = V4L2_TC_TYPE_30FPS;
	tc->flags = 0;
	tc->frames = seq % fps;
	tc->seconds = (seq / fps) % 60;
	tc->minutes = (seq / (60 * fps)) % 60;
	tc->hours = (seq / (60 * 60 * fps)) % 24;
	return 0;
}

static void buffer_cleanup(struct vb2_buffer *vb)
{
	printk("%s\n", __func__);
}

static void buffer_queue(struct vb2_buffer *vb)
{
	unsigned long flags = 0;

	printk("%s\n", __func__);

	//spin_lock_irqsave(&myVivid_queue_slock, flags);
    //orig: list_add_tail(&buf->list, &dev->vid_cap_active);
	//list_add_tail(&vb->vb2_queue, &myVivid_vb_vidq);
	//spin_unlock_irqrestore(&myVivid_queue_slock, flags);
}

static int start_streaming(struct vb2_queue *q, unsigned int count)
{
	printk("%s\n", __func__);
	//return vivi_start_generating(dev); => trigger timer to start fill frames
	return 0;
}

/* abort streaming and wait for last buffer */
static void stop_streaming(struct vb2_queue *q)
{
	printk("%s\n", __func__);
	//vivi_stop_generating(dev); => trigger timer to stop fill frames
	return 0;
}

static void vivi_lock(struct vb2_queue *q)
{
    printk("%s\n", __func__);
	//mutex_lock(&myVivid_vb_mutex);
}

static void vivi_unlock(struct vb2_queue *q)
{
    printk("%s\n", __func__);
	//mutex_unlock(&myVivid_vb_mutex);
}


static struct vb2_ops myVivid_video_qops = {
	.queue_setup		= queue_setup,
	.buf_prepare		= buffer_prepare,
	.buf_finish			= buffer_finish,
	//.buf_cleanup		= buffer_cleanup,
	.buf_queue			= buffer_queue,
	.start_streaming	= start_streaming,
	.stop_streaming		= stop_streaming,
	.wait_prepare		= vivi_unlock,
	.wait_finish		= vivi_lock,
};


static int myVivid_open(struct file *file)
{
    printk("%s \n", __func__);
	
	//mutex_init(&myVivid_vb_mutex);
	
	return v4l2_fh_open(file);
}

static int myVivid_close(struct file *file)
{
    printk("%s \n", __func__);
	//videobuf_stop(&myVivid_vb_vidqueue);
	//videobuf_mmap_free(&myVivid_vb_vidqueue);

	return v4l2_fh_release(file);
}


int myVivid_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct video_device *vdev = video_devdata(file);

	return vb2_mmap(&myVivid_vb_vidq, vma);
}

static const struct v4l2_file_operations myVivid_fops = {
	.owner			= THIS_MODULE,

	.open			= myVivid_open,
	.release		= myVivid_close,
    
	.unlocked_ioctl = video_ioctl2,
	.mmap           = myVivid_mmap,
    
    //.poll   		= vb2_fop_poll,
};

int myVivid_querycap(struct file *file, void *priv,
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

static int myVivid_enum_fmt_vid_cap(struct file *file, void  *priv,
					struct v4l2_fmtdesc *f)
{
	if (f->index >= 1)
		return -EINVAL;

	strcpy(f->description, "4:2:2, packed, YUYV");
	f->pixelformat = V4L2_PIX_FMT_YUYV;
	return 0;
}

static int myVivid_g_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct vivi_dev *dev = video_drvdata(file);

	memcpy(f, &myVivid_format, sizeof(myVivid_format));
	return 0;
}

static int myVivid_try_fmt_vid_cap(struct file *file, void *priv,
			struct v4l2_format *f)
{
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

static int myVivid_s_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	int ret = myVivid_try_fmt_vid_cap(file, NULL, f);
	if(ret < 0)
		return ret;
	
	memcpy(&myVivid_format, f, sizeof(myVivid_format));
	return 0;
}

static int myVivid_reqbufs(struct file *file, void *priv,
			  struct v4l2_requestbuffers *p)
{
    printk("%s\n", __func__);
	return vb2_reqbufs(&myVivid_vb_vidq, p);
}

static int myVivid_querybuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
    printk("%s\n", __func__);
	return vb2_querybuf(&myVivid_vb_vidq, p);
}

static int myVivid_qbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
    printk("%s\n", __func__);
	return vb2_qbuf(&myVivid_vb_vidq, p);
}

static int myVivid_dqbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
    printk("%s\n", __func__);
	return vb2_dqbuf(&myVivid_vb_vidq, p, file->f_flags & O_NONBLOCK);
}

static int myVivid_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
    printk("%s\n", __func__);
    return vb2_streamon(&myVivid_vb_vidq, i);
}

static int myVivid_streamoff(struct file *file, void *priv, enum v4l2_buf_type i)
{
    printk("%s\n", __func__);
    return vb2_streamoff(&myVivid_vb_vidq, i);
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

#if 0
	// 启动/停止
	.vidioc_streamon	  = myVivid_streamon,
	.vidioc_streamoff	  = myVivid_streamoff,
#endif
};



static void myVivid_dev_release(struct video_device *vdev)
{
    printk("%s \n", __func__);

}

static int myVivid_probe(struct platform_device *pdev)
{
    printk("%s \n", __func__);
	int ret = 0;

    /* initialize vid_cap queue */
	memset(&myVivid_vb_vidq, 0, sizeof(myVivid_vb_vidq));
	myVivid_vb_vidq.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	myVivid_vb_vidq.io_modes = VB2_MMAP | VB2_USERPTR | VB2_READ;
	//myVivid_vb_vidq.drv_priv = dev;
	myVivid_vb_vidq.buf_struct_size = sizeof(struct myvivid_buffer);
	myVivid_vb_vidq.ops = &myVivid_video_qops;
	myVivid_vb_vidq.mem_ops = &vb2_vmalloc_memops;
    myVivid_vb_vidq.timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
    myVivid_vb_vidq.min_buffers_needed = 2;
    //myVivid_vb_vidq.lock = &dev->mutex;
    myVivid_vb_vidq.dev = v4l2_dev.dev;

	ret = vb2_queue_init(&myVivid_vb_vidq);
    printk("vb2_queue_init ret = %d\n", ret);
    
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
	
	//spin_lock_init(&myVivid_queue_slock);
	
	//3. 分配video device
    ret = video_register_device(myVivid_dev, VFL_TYPE_GRABBER, -1/*auto*/);
    if(ret){
		printk("video_register_device failed ??? ret=%d", ret);
		goto ERR_RELEASE_DEV;
	}

	/* 用定时器产生数据并唤醒进程 */
	//timer_setup(&myVivid_timer, myVivid_timer_function, 0);
	//INIT_LIST_HEAD(&myVivid_vb_local_queue);
	
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
	
	if(myVivid_dev != NULL){
		video_unregister_device(myVivid_dev);
	    video_device_release(myVivid_dev);
	}
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

