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


static unsigned int myuvc_poll(struct file *filp, struct poll_table_struct *poll_table)
{
    printk("%s \n", __func__);
	return 0;
}



static int myuvc_mmap(struct file *filp, struct vm_area_struct *vm_area)
{
    printk("%s \n", __func__);

	return 0;
}

static int myuvc_open(struct file *filp)
{
    printk("%s \n", __func__);

	return 0;
}

static int myuvc_close(struct file *filp)
{
    printk("%s \n", __func__);

	return 0;
}

static const struct v4l2_file_operations myuvc_fops = {
	.owner			= THIS_MODULE,
	.unlocked_ioctl = video_ioctl2,

#if 1
	.poll  			= myuvc_poll,

	.mmap 			= myuvc_mmap,
	.open			= myuvc_open,
	.release		= myuvc_close,
#endif
};

int myuvc_querycap(struct file *file, void *fh,
			   struct v4l2_capability *cap)
{
    printk("%s \n", __func__);

	strscpy(cap->driver, "myuvc", sizeof(cap->driver));
	strscpy(cap->card, "myuvc", sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info),
					"platform:%s", MODULE_NAME);

	cap->capabilities = V4L2_CAP_DEVICE_CAPS;

	return 0;
}

int myuvc_enum_fmt_vid_cap(struct file *file, void *fh,
					  struct v4l2_fmtdesc *f)
{
	printk("%s \n", __func__);

	return 0;
}

int myuvc_g_fmt_vid_cap(struct file *file, void *fh,
			  struct v4l2_format *f)
{
	printk("%s \n", __func__);

	return 0;
}

int myuvc_try_fmt_vid_cap(struct file *file, void *fh,
				struct v4l2_format *f)
{
	printk("%s \n", __func__);

	return 0;
}

int myuvc_s_fmt_vid_cap(struct file *file, void *fh,
			  struct v4l2_format *f)
{
	printk("%s \n", __func__);

	return 0;
}

int myuvc_reqbufs(struct file *file, void *fh,
			struct v4l2_requestbuffers *b)
{
	printk("%s \n", __func__);
	return 0;
}

int myuvc_querybuf(struct file *file, void *fh,
			 struct v4l2_buffer *b)
{
	printk("%s \n", __func__);

	return 0;
}


int myuvc_qbuf(struct file *file, void *fh,
		 struct v4l2_buffer *b)
{
	printk("%s \n", __func__);

	return 0;
}

int myuvc_dqbuf(struct file *file, void *fh,
		  struct v4l2_buffer *b)
{
	return 0;
}


int myuvc_streamon(struct file *file, void *fh,
			 enum v4l2_buf_type i)
{
	return 0;
}

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
		myuvc_vdev->device_caps    = V4L2_CAP_STREAMING | V4L2_CAP_VIDEO_M2M_MPLANE;
		
		
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

	if(myuvc_vdev != NULL){
		
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
