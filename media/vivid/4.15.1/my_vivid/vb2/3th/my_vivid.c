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


#include <media/videobuf-core.h>


#define DBG_1	printk("111\n");
#define DBG_2	printk("222\n");
#define DBG_3	printk("333\n");
#define DBG_4	printk("444\n");
#define DBG_5	printk("555\n");
#define DBG_6	printk("555\n");

#define MODULE_NAME "myvivid"


struct myvivid_buffer {
	/* common v4l buffer stuff -- must be first */
	struct vb2_v4l2_buffer	vb;
	struct list_head	    list;
};

struct myvivid_dev {
    struct v4l2_device		v4l2_dev;
    struct video_device	    *vdev;
    struct vb2_queue        vb_vid_cap_q;
    spinlock_t              slock;
    
    struct mutex		    lock;
    
    struct list_head	    active;
    struct wait_queue_head  waitq;
    
    
    struct v4l2_format	    format;
};

static struct myvivid_dev *myvivid_dev;

static struct timer_list myvivid_timer;

#define vid_limit 16






static void myvivid_timer_function(struct timer_list *t)
{
	printk("%s \n", __func__);
	struct myvivid_buffer *vid_cap_buf = NULL;
    unsigned long flags = 0;
    
    __u32 size = (__u32)myvivid_dev->format.fmt.pix.sizeimage;
	unsigned char *p = NULL;

	//spin_lock_irqsave(&myvivid_dev->slock, flags);

	if (list_empty(&myvivid_dev->active)) {
		printk(KERN_ALERT "No active queue to serve\n");
		goto OUT;
	}
 
	/* get the queued_list's first entry */
    vid_cap_buf = list_entry(myvivid_dev->active.next, struct myvivid_buffer, list);
    list_del(&vid_cap_buf->list);    
    //spin_unlock_irqrestore(&myvivid_dev->slock, flags);
    
	void *vbuf = vb2_plane_vaddr(&vid_cap_buf->vb.vb2_buf, 0);
	p = vbuf;
	memset(vbuf, 0xff, size);

    vb2_buffer_done(&vid_cap_buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
	
OUT:
	/* 3. reset timer */
	mod_timer(&myvivid_timer, jiffies + (HZ/30));//30fps	
}

static int queue_setup(struct vb2_queue *vq,
		       unsigned *nbuffers, unsigned *nplanes,
		       unsigned sizes[], struct device *alloc_devs[])

{
    printk("%s\n", __func__);
	__u32 size = (__u32)myvivid_dev->format.fmt.pix.sizeimage;
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
	printk("%s, count=%d, size=%d\n", __func__, *nbuffers, size);
	return 0;
}

static int buffer_prepare(struct vb2_buffer *vb)
{
    printk("%s\n", __func__);
	__u32 size = (__u32)myvivid_dev->format.fmt.pix.sizeimage;
	
	if (vb2_plane_size(vb, 0) < size) {
		printk("%s data will not fit into plane (%lu < %u)\n",
				__func__, vb2_plane_size(vb, 0), size);
		return -EINVAL;
	}
	
	vb2_set_plane_payload(vb, 0, size);

	//precalculate_bars(dev);
	//precalculate_line(dev);
	return 0;
}

static void buffer_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct myvivid_buffer *buf = container_of(vbuf, struct myvivid_buffer, vb);
    unsigned long flags = 0;
    
	printk("%s\n", __func__);

	//spin_lock_irqsave(&myvivid_dev->slock, flags);
	list_add_tail(&buf->list, &myvivid_dev->active);
	//spin_unlock_irqrestore(&myvivid_dev->slock, flags);
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
	return;
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
	return;
}




static const struct vb2_ops myvivid_qops = {
	.queue_setup		= queue_setup,
	.buf_prepare		= buffer_prepare,
	.buf_queue		    = buffer_queue,
	.start_streaming	= start_streaming,
	.stop_streaming		= stop_streaming,
	.wait_prepare		= vb2_ops_wait_prepare,
	.wait_finish		= vb2_ops_wait_finish,
};


static int myvivid_open(struct file *file)
{
    printk("%s \n", __func__);

	myvivid_timer.expires = jiffies + 1;
	add_timer(&myvivid_timer);
	//return v4l2_fh_open(file);
	return 0;
}

static int myvivid_release(struct file *file)
{
    printk("%s \n", __func__);
	//videobuf_stop(&myvivid_vb_vidqueue);
	//videobuf_mmap_free(&myvivid_vb_vidqueue);

	del_timer(&myvivid_timer);
	
	//if (myvivid_dev->vdev->queue)
	//	return vb2_fop_release(file);
	//return v4l2_fh_release(file);
	return 0;
}




static const struct v4l2_file_operations myvivid_fops = {
	.owner = THIS_MODULE,
	.open = myvivid_open,
	.release = myvivid_release,
	.unlocked_ioctl = video_ioctl2,
	.read = vb2_fop_read,
	.mmap = vb2_fop_mmap,
};


int myvivid_querycap(struct file *file, void *fh,
			   struct v4l2_capability *cap)
{
    printk("%s \n", __func__);

	strscpy(cap->driver, MODULE_NAME, sizeof(cap->driver));
	strscpy(cap->card, MODULE_NAME, sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info),
					"platform:%s", MODULE_NAME);
	
	cap->version = 0x0001;
	cap->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;

	return 0;
}

int myvivid_enum_fmt_vid_cap(struct file *file, void *fh,
					  struct v4l2_fmtdesc *f)
{
	printk("%s \n", __func__);
	if (f->index >= 1)
		return -EINVAL;

	strcpy(f->description, "4:2:2, packed, YUYV");
	f->pixelformat = V4L2_PIX_FMT_YUYV;

	return 0;
}

int myvivid_g_fmt_vid_cap(struct file *file, void *fh,
			  struct v4l2_format *f)
{
	printk("%s \n", __func__);

	memcpy(f, &myvivid_dev->format, sizeof(myvivid_dev->format.fmt));
	return 0;
}

int myvivid_try_fmt_vid_cap(struct file *file, void *fh,
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
    
    printk("height = %d\n", f->fmt.pix.height);
    printk("bytesperline = %d\n", f->fmt.pix.bytesperline);
    printk("sizeimage = %d\n", f->fmt.pix.sizeimage);
	return 0;
}

int myvivid_s_fmt_vid_cap(struct file *file, void *fh,
			  struct v4l2_format *f)
{
	printk("%s \n", __func__);
	int ret = myvivid_try_fmt_vid_cap(file, NULL, f);
	if(ret<0)
		return ret;
	
	memcpy(&myvivid_dev->format, f, sizeof(myvivid_dev->format.fmt));
	return 0;
}

int myvivid_reqbufs(struct file *file, void *fh,
			struct v4l2_requestbuffers *b)
{
	printk("%s \n", __func__);
    return vb2_ioctl_reqbufs(file, fh, b);
}

int myvivid_querybuf(struct file *file, void *fh,
			 struct v4l2_buffer *b)
{
	printk("%s \n", __func__);
	return vb2_querybuf(&myvivid_dev->vb_vid_cap_q, b);
}


int myvivid_qbuf(struct file *file, void *fh,
		 struct v4l2_buffer *b)
{
	printk("%s \n", __func__);
	return vb2_qbuf(&myvivid_dev->vb_vid_cap_q, b);
}

int myvivid_dqbuf(struct file *file, void *fh,
		  struct v4l2_buffer *b)
{
	printk("%s \n", __func__);
	return vb2_dqbuf(&myvivid_dev->vb_vid_cap_q, b, file->f_flags & O_NONBLOCK);
}


int myvivid_streamon(struct file *file, void *fh,
			 enum v4l2_buf_type i)
{
	return vb2_streamon(&myvivid_dev->vb_vid_cap_q, i);
}

int myvivid_streamoff(struct file *file, void *fh,
		  enum v4l2_buf_type i)
{
	return vb2_streamoff(&myvivid_dev->vb_vid_cap_q, i);
}



struct v4l2_ioctl_ops myvivid_ioctl_ops = {

	// 表示它是一个摄像头设备
	.vidioc_querycap	  = myvivid_querycap,

	/* 用于列举、获得、测试、设置摄像头的数据的格式 */
	.vidioc_enum_fmt_vid_cap  = myvivid_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap	  = myvivid_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap   = myvivid_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap	  = myvivid_s_fmt_vid_cap,

	///* 缓冲区操作: 申请/查询/放入队列/取出队列 */
	//.vidioc_reqbufs 	  = myvivid_reqbufs,
	//.vidioc_querybuf	  = myvivid_querybuf,
	//.vidioc_qbuf		  = myvivid_qbuf,
	//.vidioc_dqbuf		  = myvivid_dqbuf,
	//
	//// 启动/停止
	//.vidioc_streamon	  = myvivid_streamon,
	//.vidioc_streamoff	  = myvivid_streamoff,
    
	.vidioc_reqbufs		= vb2_ioctl_reqbufs,
	.vidioc_create_bufs	= vb2_ioctl_create_bufs,
	.vidioc_querybuf	= vb2_ioctl_querybuf,
	.vidioc_qbuf		= vb2_ioctl_qbuf,
	.vidioc_dqbuf		= vb2_ioctl_dqbuf,
	.vidioc_expbuf		= vb2_ioctl_expbuf,
    
	.vidioc_streamon	= vb2_ioctl_streamon,
	.vidioc_streamoff	= vb2_ioctl_streamoff,
};

static void myvivid_dev_release(struct video_device *vdev)
{
}

static int myvivid_probe(struct platform_device *pdev)
{
    printk("%s \n", __func__);
	int ret = 0;
    
    
	myvivid_dev = (struct myvivid_dev *)devm_kzalloc(&pdev->dev,
			sizeof(struct myvivid_dev), GFP_KERNEL);
    
	if (!myvivid_dev) {
		printk(KERN_ERR "allocate memory for vivi failed\n");
		return -ENOMEM;
	}

    /* Initialize the top-level structure */
	ret = v4l2_device_register(&pdev->dev, &myvivid_dev->v4l2_dev);
	if (ret < 0) {
		printk(KERN_ERR "v4l2_device regsiter fail, ret(%d)\n", ret);
		goto free_mem;
	}
    
    /* initialize locks */
    spin_lock_init(&myvivid_dev->slock);
    
	/* initialize the vb2 queue */
	mutex_init(&myvivid_dev->lock);
	myvivid_dev->vb_vid_cap_q.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    myvivid_dev->vb_vid_cap_q.io_modes = VB2_MMAP;
    myvivid_dev->vb_vid_cap_q.buf_struct_size = sizeof(struct myvivid_buffer);
    myvivid_dev->vb_vid_cap_q.ops = &myvivid_qops;
    myvivid_dev->vb_vid_cap_q.mem_ops = &vb2_vmalloc_memops;
    myvivid_dev->vb_vid_cap_q.timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
    myvivid_dev->vb_vid_cap_q.min_buffers_needed = 1;
    
	myvivid_dev->vb_vid_cap_q.drv_priv = myvivid_dev;
	myvivid_dev->vb_vid_cap_q.lock = &myvivid_dev->lock;
	myvivid_dev->vb_vid_cap_q.dev = &pdev->dev;
 
	ret = vb2_queue_init(&myvivid_dev->vb_vid_cap_q);
	if (ret)
		goto free_mem;
    
	/* init video active queues */
	INIT_LIST_HEAD(&myvivid_dev->active);
	init_waitqueue_head(&myvivid_dev->waitq);
    
    /* initialize the video_device structure */
    myvivid_dev->vdev = video_device_alloc();
	if(NULL == myvivid_dev->vdev){
		printk("failed to alloc video device ");
		goto remove_v4l2;
	}
    
	strscpy(myvivid_dev->vdev->name, MODULE_NAME, sizeof(myvivid_dev->vdev->name));
	myvivid_dev->vdev->release   		= myvivid_dev_release;
	myvivid_dev->vdev->fops 			= &myvivid_fops;
    myvivid_dev->vdev->ioctl_ops 		= &myvivid_ioctl_ops;
    myvivid_dev->vdev->device_caps      = V4L2_CAP_VIDEO_CAPTURE | \
		       V4L2_CAP_READWRITE | V4L2_CAP_STREAMING;
    
	myvivid_dev->vdev->lock = &myvivid_dev->lock;
	myvivid_dev->vdev->queue = &myvivid_dev->vb_vid_cap_q;
	myvivid_dev->vdev->v4l2_dev = &myvivid_dev->v4l2_dev;
	video_set_drvdata(myvivid_dev->vdev, myvivid_dev);
    
    
	/* register video device */
	ret = video_register_device(myvivid_dev->vdev, VFL_TYPE_GRABBER, -1);
	if (ret < 0) {
		printk(KERN_ERR "video_device register failed, ret(%d)\n", ret);
		goto remove_vdev;
	}
    
    
    
	/* 用定时器产生数据并唤醒进程 */
	timer_setup(&myvivid_timer, myvivid_timer_function, 0);
	
    return 0;


remove_vdev:
	video_device_release(myvivid_dev->vdev);

remove_v4l2:
	v4l2_device_unregister(&myvivid_dev->v4l2_dev);

free_mem:    
    kfree(myvivid_dev);
    
	return ret;
}

static int myvivid_remove(struct platform_device *pdev)
{
    printk("remove \n");

    v4l2_device_unregister(&myvivid_dev->v4l2_dev);
	
	if(myvivid_dev->vdev != NULL){
		video_unregister_device(myvivid_dev->vdev);
	    video_device_release(myvivid_dev->vdev);
	}
    return 0;
}

static void myvivid_pdev_release(struct device *dev)
{
    printk("platform release \n");
}

static struct platform_device myvivid_pdev = {
	.name		    = "myvivid",
	.dev.release	= myvivid_pdev_release,
};

static struct platform_driver myvivid_pdrv = {
	.probe		= myvivid_probe,
	.remove		= myvivid_remove,
	.driver		= {
		.name	= "myvivid",
	},
};


static int __init myvivid_init(void)
{
    int ret;
    ret = platform_device_register(&myvivid_pdev);
    if(ret){
        printk("platform_device_register FAIL ???");    return ret;
    }
    
    ret = platform_driver_register(&myvivid_pdrv);
    if(ret){
        printk("platform_driver_register FAIL ???");
        platform_device_unregister(&myvivid_pdev);
        return ret;
    }
    
    return ret;
}

static void __exit myvivid_exit(void)
{
    platform_driver_unregister(&myvivid_pdrv);
    platform_device_unregister(&myvivid_pdev);
}

module_init(myvivid_init);
module_exit(myvivid_exit);


MODULE_DESCRIPTION("Virtual Video Test Driver");
MODULE_AUTHOR("Will Chen");
MODULE_LICENSE("GPL");
