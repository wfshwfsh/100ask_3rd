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


//void dump_uvc_endpt_desc()
//{
//    
//}
//
void dump_uvc_intfAlt_desc(struct usb_interface_descriptor *intf)
{
    static int i;
    printk("    Interface Descriptor altsetting: %d\n", i++);
    printk("      bLength:            %u\n", intf->bLength);
    printk("      bDescriptorType:    %u\n", intf->bDescriptorType);
    printk("      bInterfaceNumber:   %d\n", intf->bInterfaceNumber);
    printk("      bAlternateSetting:  %d\n", intf->bAlternateSetting);
    printk("      bNumEndpoints:      %d\n", intf->bNumEndpoints);
    printk("      bInterfaceClass:    %d\n", intf->bInterfaceClass);
    printk("      bInterfaceSubClass: %d\n", intf->bInterfaceSubClass);
    printk("      bInterfaceProtocol: %d\n", intf->bInterfaceProtocol);
    printk("      iInterface:         %d\n", intf->iInterface);

}

void dump_uvc_intfAsoc_desc(struct usb_interface_assoc_descriptor *assoc_desc)
{
    printk("    Interface Assoc:\n");
    printk("      bLength:            %u\n", assoc_desc->bLength);
    printk("      bDescriptorType:    %u\n", assoc_desc->bDescriptorType);
    printk("      bFirstInterface:    %d\n", assoc_desc->bFirstInterface);
    printk("      bInterfaceCount:    %d\n", assoc_desc->bInterfaceCount);
    printk("      bFunctionClass:     %d\n", assoc_desc->bFunctionClass);
    printk("      bFunctionSubClass:  %d\n", assoc_desc->bFunctionSubClass);
    printk("      bFunctionProtocol:  %d\n", assoc_desc->bFunctionProtocol);
    printk("      iFunction:          %d\n", assoc_desc->iFunction);
}

void dump_uvc_conf_desc(struct usb_host_config *config)
{
    static int i;
    struct usb_config_descriptor	*config_desc = &config->desc;
    
    printk("  Configuration: %d\n", i++);
    printk("    bLength:              %u\n", config_desc->bLength);
    printk("    bDescriptorType:      %u\n", config_desc->bDescriptorType);
    printk("    wTotalLength:         %d\n", config_desc->wTotalLength);
    printk("    bNumInterfaces:       %d\n", config_desc->bNumInterfaces);
    printk("    bConfigurationValue:  %d\n", config_desc->bConfigurationValue);
    printk("    iConfiguration:       %d\n", config_desc->iConfiguration);
    printk("    bmAttributes:         %02xh\n", config_desc->bmAttributes);
    printk("    bMaxPower:            %d\n", config_desc->bMaxPower);
    
    //只關心第一個intf
    struct usb_interface_assoc_descriptor *assoc_desc = config->intf_assoc[0];
    dump_uvc_intfAsoc_desc(assoc_desc);
}

void dump_uvc_desc(struct usb_device_descriptor *descriptor)
{
    /* 打印设备描述符 */
	printk("Device Descriptor:\n"
	       "  bLength             %5u\n"
	       "  bDescriptorType     %5u\n"
	       "  bcdUSB              %2x.%02x\n"
	       "  bDeviceClass        %5u \n"
	       "  bDeviceSubClass     %5u \n"
	       "  bDeviceProtocol     %5u \n"
	       "  bMaxPacketSize0     %5u\n"
	       "  idVendor           0x%04x \n"
	       "  idProduct          0x%04x \n"
	       "  bcdDevice           %2x.%02x\n"
	       "  iManufacturer       %5u\n"
	       "  iProduct            %5u\n"
	       "  iSerial             %5u\n"
	       "  bNumConfigurations  %5u\n",
	       descriptor->bLength, descriptor->bDescriptorType,
	       descriptor->bcdUSB >> 8, descriptor->bcdUSB & 0xff,
	       descriptor->bDeviceClass, 
	       descriptor->bDeviceSubClass,
	       descriptor->bDeviceProtocol, 
	       descriptor->bMaxPacketSize0,
	       descriptor->idVendor,  descriptor->idProduct,
	       descriptor->bcdDevice >> 8, descriptor->bcdDevice & 0xff,
	       descriptor->iManufacturer, 
	       descriptor->iProduct, 
	       descriptor->iSerialNumber, 
	       descriptor->bNumConfigurations);
}

int myUvc_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
    int i,j,k,l;
    static int cnt = 0;
    printk("%s cnt=%d\n", __func__, cnt++);
    struct usb_device *dev = interface_to_usbdev(intf);
    struct usb_device_descriptor *descriptor = &dev->descriptor;
    struct usb_host_config *host_config;
    struct usb_interface_descriptor	*interface;
    
	unsigned char *buffer;
	int buflen;
    int desc_len;
    int desc_cnt;
    
    dump_uvc_desc(descriptor);
	for(i=0;i<descriptor->bNumConfigurations;i++){
        host_config = &dev->config[i];
        dump_uvc_conf_desc(host_config);
        
        //打印接口所有描述符
        printk("num_altsetting %d\n", intf->num_altsetting);
        for(j=0;j<intf->num_altsetting;j++){
            interface = &intf->altsetting[j].desc;
            dump_uvc_intfAlt_desc(interface);
        }
        
        
        buffer = intf->cur_altsetting->extra;
        buflen = intf->cur_altsetting->extralen;
        printk("extra buffer of interface %d:\n", cnt-1);
        k = 0;
        desc_cnt = 0;
        while (k < buflen)
        {
            desc_len = buffer[k];
            printk("extra desc %d: ", desc_cnt);
            for (l = 0; l < desc_len; l++, k++)
            {
                printk("%02x ", buffer[k]);
            }
            desc_cnt++;
            printk("\n");
        }
    }
    
    return 0;

ERR_RELEASE_DEV:
	//video_device_release(myUvc_vdev);
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
