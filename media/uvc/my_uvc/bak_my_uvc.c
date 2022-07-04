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



static struct video_device *myUvc_vdev;




#define MODULE_NAME "myUvc"



static unsigned int myUvc_poll(struct file *filp, struct poll_table_struct *poll_table)
{
    printk("%s \n", __func__);
	return 0;
}


static long myUvc_ioctl(struct file *filp, unsigned int cmd, unsigned long int arg)
{
    printk("%s \n", __func__);

	return 0;
}

static int myUvc_mmap(struct file *filp, struct vm_area_struct *vm_area)
{
    printk("%s \n", __func__);

	return 0;
}

static int myUvc_open(struct file *filp)
{
    printk("%s \n", __func__);

	return 0;
}

static int myUvc_release(struct file *filp)
{
    printk("%s \n", __func__);

	return 0;
}

static const struct v4l2_file_operations myUvc_fops = {
	.owner			= THIS_MODULE,
#if 0
	.poll  			= myUvc_poll,
	.unlocked_ioctl = myUvc_ioctl,

	.mmap 			= myUvc_mmap,
	.open			= myUvc_open,
	.release		= myUvc_release,
#endif
};

int myUvc_querycap(struct file *file, void *fh,
			   struct v4l2_capability *cap)
{
    printk("%s \n", __func__);

	return 0;
}

int myUvc_enum_fmt_vid_cap(struct file *file, void *fh,
					  struct v4l2_fmtdesc *f)
{
	printk("%s \n", __func__);

	return 0;
}

int myUvc_g_fmt_vid_cap(struct file *file, void *fh,
			  struct v4l2_format *f)
{
	printk("%s \n", __func__);

	return 0;
}

int myUvc_try_fmt_vid_cap(struct file *file, void *fh,
				struct v4l2_format *f)
{
	printk("%s \n", __func__);

	return 0;
}

int myUvc_s_fmt_vid_cap(struct file *file, void *fh,
			  struct v4l2_format *f)
{
	printk("%s \n", __func__);

	return 0;
}

int myUvc_reqbufs(struct file *file, void *fh,
			struct v4l2_requestbuffers *b)
{
	printk("%s \n", __func__);
	return 0;
}

int myUvc_querybuf(struct file *file, void *fh,
			 struct v4l2_buffer *b)
{
	printk("%s \n", __func__);

	return 0;
}


int myUvc_qbuf(struct file *file, void *fh,
		 struct v4l2_buffer *b)
{
	printk("%s \n", __func__);

	return 0;
}

int myUvc_dqbuf(struct file *file, void *fh,
		  struct v4l2_buffer *b)
{
	return 0;
}


int myUvc_streamon(struct file *file, void *fh,
			 enum v4l2_buf_type i)
{
	return 0;
}

int myUvc_streamoff(struct file *file, void *fh,
		  enum v4l2_buf_type i)
{
	return 0;
}



struct v4l2_ioctl_ops myUvc_ioctl_ops = {

	// 表示它是一个摄像头设备
	.vidioc_querycap	  = myUvc_querycap,
#if 0
	/* 用于列举、获得、测试、设置摄像头的数据的格式 */
	.vidioc_enum_fmt_vid_cap  = myUvc_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap	  = myUvc_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap   = myUvc_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap	  = myUvc_s_fmt_vid_cap,

	/* 缓冲区操作: 申请/查询/放入队列/取出队列 */
	.vidioc_reqbufs 	  = myUvc_reqbufs,
	.vidioc_querybuf	  = myUvc_querybuf,
	.vidioc_qbuf		  = myUvc_qbuf,
	.vidioc_dqbuf		  = myUvc_dqbuf,

	// 启动/停止
	.vidioc_streamon	  = myUvc_streamon,
	.vidioc_streamoff	  = myUvc_streamoff,
#endif
};






static void myUvc_dev_release(struct video_device *vdev)
{
    printk("%s \n", __func__);

}

int myUvc_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
    static int cnt = 0;
    struct usb_device *dev = interface_to_usbdev(intf);
    int ret = 0;
    printk("%s cnt=%d\n", __func__, cnt++);
    
    //if(cnt == 1){
    //    
    //}else if(cnt == 2){
    //    
    //}
	///* 1. alloc video device */
	//myUvc_vdev = video_device_alloc();
    //
	///* 2. fill video device structure */
	//strscpy(myUvc_vdev->name, MODULE_NAME, sizeof(myUvc_vdev->name));
	//myUvc_vdev->release   		= myUvc_dev_release;
	//myUvc_vdev->fops 			= &myUvc_fops;
    //myUvc_vdev->ioctl_ops 	    = &myUvc_ioctl_ops;
    //
	////if not set, will cause return EINVAL(-22)
	////myUvc_vdev->device_caps    = V4L2_CAP_STREAMING | V4L2_CAP_VIDEO_M2M_MPLANE;
    //
	//
	///* 3. register video device */
    //ret = video_register_device(myUvc_vdev, VFL_TYPE_GRABBER, -1/*auto*/);
    //if(ret){
	//	printk("video_register_device failed ??? ret=%d", ret);
	//	goto ERR_RELEASE_DEV;
	//}
    
    /* ... */

    return 0;

ERR_RELEASE_DEV:
	video_device_release(myUvc_vdev);
	return -ENODEV;
}

void myUvc_disconnect(struct usb_interface *intf)
{
    static int cnt = 0;
    printk("%s cnt=%d\n", __func__, cnt++);
	
	//if(myUvc_vdev->v4l2_dev != NULL)
	//    v4l2_device_unregister(myUvc_vdev->v4l2_dev);
	if(myUvc_vdev != NULL){
        video_unregister_device(myUvc_vdev);
	    video_device_release(myUvc_vdev);
	}
	return 0;
}



static const struct usb_device_id uvc_ids[] = {
    /* Generic USB Video Class */
	{ USB_INTERFACE_INFO(USB_CLASS_VIDEO, 1, 0) },  /* video Ctrl Intf */
	{ USB_INTERFACE_INFO(USB_CLASS_VIDEO, 2, 0) },  /* video Stream Intf */
	{}
};

static struct usb_driver myUvc_drv = {

    .name		= MODULE_NAME,
    .probe		= myUvc_probe,
    .disconnect	= myUvc_disconnect,
    .id_table	= uvc_ids,

};

static int myUvc_init(void)
{
    usb_register(&myUvc_drv);
    return 0;
}

static void myUvc_exit(void)
{
    usb_deregister(&myUvc_drv);
}


module_init(myUvc_init);
module_exit(myUvc_exit);


MODULE_DESCRIPTION("Virtual Video Test Driver");
MODULE_AUTHOR("Will Chen");
MODULE_LICENSE("GPL");
