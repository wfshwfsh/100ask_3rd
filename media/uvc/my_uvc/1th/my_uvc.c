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





int myUvc_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
    static int cnt = 0;
    struct usb_device *dev = interface_to_usbdev(intf);
    int ret = 0;
    printk("%s cnt=%d\n", __func__, cnt++);
    
    return 0;

ERR_RELEASE_DEV:
	video_device_release(myUvc_vdev);
	return -ENODEV;
}

void myUvc_disconnect(struct usb_interface *intf)
{
    static int cnt = 0;
    printk("%s cnt=%d\n", __func__, cnt++);

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
