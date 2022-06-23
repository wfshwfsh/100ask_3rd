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
#include <media/tpg/v4l2-tpg.h>







#define VIVID_MODULE_NAME "myVivid"
//#define DEBUG 1


//vivid_create_instance






static __poll_t myVivid_poll(struct file *filp, struct poll_table_struct *poll_table)
{
    printk("%s \n", __func__);
	return 0;
}


static long myVivid_ioctl(struct file *filp, unsigned int cmd, unsigned long int arg)
{
    printk("%s \n", __func__);

	return 0;
}

static int myVivid_mmap(struct file *filp, struct vm_area_struct *vm_area)
{
    printk("%s \n", __func__);

	return 0;
}

static int myVivid_open(struct file *filp)
{
    printk("%s \n", __func__);

	return 0;
}

static int myVivid_release(struct file *filp)
{
    printk("%s \n", __func__);

	return 0;
}

static const struct v4l2_file_operations myVivid_fops = {
	.owner			= THIS_MODULE,
#if 0
	.poll  			= myVivid_poll,
	.unlocked_ioctl = myVivid_ioctl,

	.mmap 			= myVivid_mmap,
	.open			= myVivid_open,
	.release		= myVivid_release,
#endif
};

int myVivid_querycap(struct file *file, void *fh,
			   struct v4l2_capability *cap)
{
    printk("%s \n", __func__);

	return 0;
}

int myVivid_enum_fmt_vid_cap(struct file *file, void *fh,
					  struct v4l2_fmtdesc *f)
{
	printk("%s \n", __func__);

	return 0;
}

int myVivid_g_fmt_vid_cap(struct file *file, void *fh,
			  struct v4l2_format *f)
{
	printk("%s \n", __func__);

	return 0;
}

int myVivid_try_fmt_vid_cap(struct file *file, void *fh,
				struct v4l2_format *f)
{
	printk("%s \n", __func__);

	return 0;
}

int myVivid_s_fmt_vid_cap(struct file *file, void *fh,
			  struct v4l2_format *f)
{
	printk("%s \n", __func__);

	return 0;
}

int myVivid_reqbufs(struct file *file, void *fh,
			struct v4l2_requestbuffers *b)
{
	printk("%s \n", __func__);
	return 0;
}

int myVivid_querybuf(struct file *file, void *fh,
			 struct v4l2_buffer *b)
{
	printk("%s \n", __func__);

	return 0;
}


int myVivid_qbuf(struct file *file, void *fh,
		 struct v4l2_buffer *b)
{
	printk("%s \n", __func__);

	return 0;
}

int myVivid_dqbuf(struct file *file, void *fh,
		  struct v4l2_buffer *b)
{
	return 0;
}


int myVivid_streamon(struct file *file, void *fh,
			 enum v4l2_buf_type i)
{
	return 0;
}

int myVivid_streamoff(struct file *file, void *fh,
		  enum v4l2_buf_type i)
{
	return 0;
}



struct v4l2_ioctl_ops myVivid_ioctl_ops = {

	// 表示它是一个摄像头设备
	.vidioc_querycap	  = myVivid_querycap,
#if 0
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
#endif
};


static struct video_device *myVivid_dev;
static struct v4l2_device v4l2_dev;

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
		printk("failed to alloc video device ret=%d", ret);
		return -ENOMEM;
	}
	
	//2. 設置
	strscpy(myVivid_dev->name, VIVID_MODULE_NAME, sizeof(myVivid_dev->name));
	myVivid_dev->release   		= myVivid_dev_release;
	myVivid_dev->fops 			= &myVivid_fops;
    myVivid_dev->ioctl_ops 	= &myVivid_ioctl_ops;
	myVivid_dev->v4l2_dev  		= &v4l2_dev;

	//if not set, will cause return EINVAL(-22)
	myVivid_dev->device_caps    = V4L2_CAP_STREAMING | V4L2_CAP_VIDEO_M2M_MPLANE;
	
	//3. 分配video device
    ret = video_register_device(myVivid_dev, VFL_TYPE_GRABBER, -1/*auto*/);
    if(ret){
		printk("video_register_device failed ??? ret=%d", ret);
		goto ERR_RELEASE_DEV;
	}

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
