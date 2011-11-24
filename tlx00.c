/*
tlx00 - kernel module for Arexx tlx00 series data logger

Copyright (C) 2011 John Hedges

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/usb.h>
#include <linux/workqueue.h>
#include <linux/kobject.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,0,0)
#include <linux/slab.h>
#endif

#undef PDEBUG
#define PDEBUG(fmt, args...)
#ifdef DEBUG
#  undef PDEBUG
#  define PDEBUG(fmt, args...) printk( KERN_DEBUG "tlx00: " fmt, ## args)
#endif

#define DRIVER_AUTHOR "John Hedges, john@drystone.co.uk"
#define DRIVER_DESC "Arexx temperature logger driver for linux (c) 2011"

#define TL500_VENDOR_ID 0x0451
#define TL500_PRODUCT_ID 0x3211

static void sndcomplete(struct urb* urb);
static void rcvcomplete(struct urb* urb);

struct tlx00_dev {
    int bulk_in_endpointAddr;
    int bulk_out_endpointAddr;
    int iobufsize;
    unsigned char *iobuf;
    struct urb* urb;
    struct usb_device* usbdev;
    struct workqueue_struct* workqueue;
    struct kset *kset;
};

struct sensor {
    struct kobject kobj;
    int raw;
    long timestamp;
};

static struct tlx00_dev* dev = NULL;

static void poll(struct work_struct *unused)
{
    PDEBUG("poll\n");
    usb_fill_bulk_urb(dev->urb, dev->usbdev,
                     usb_sndbulkpipe(dev->usbdev, dev->bulk_out_endpointAddr),
                     dev->iobuf, dev->iobufsize, sndcomplete, NULL);
    
    dev->iobuf[0] = 0x03;
    usb_submit_urb(dev->urb, GFP_KERNEL);
}

DECLARE_DELAYED_WORK(pollwork, poll);

static ssize_t sensor_show(struct kobject *kobj, struct attribute *attr, char *buf)
{
    struct sensor *sens;

    sens = container_of(kobj, struct sensor, kobj);
    sprintf(buf, "%d\n", sens->raw);
    return strlen(buf);
}

static void sensor_release(struct kobject *kobj)
{
    kfree(container_of(kobj, struct sensor, kobj));
}

static struct attribute sensor_attrib = {
    .name = "raw",
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,0,0)
    .owner = NULL,
#endif
    .mode = S_IRUGO
};

static struct attribute *sensor_attribs[] = {
    &sensor_attrib,
    NULL
};

static struct sysfs_ops sensor_sysfs_ops = {
    .show = sensor_show,
    .store = NULL
};

static struct kobj_type sensor_kobj_type = {
    .release = sensor_release,
    .sysfs_ops = &sensor_sysfs_ops,
    .default_attrs = sensor_attribs
};
    
static struct kobject *find_kobj(const char *name) {
    struct kobject *kobj;

    list_for_each_entry(kobj, &dev->kset->list, entry) {
        if (!strcmp(kobj->name, name))
            return kobj;
    }
    return NULL;
}

static void rcvcomplete(struct urb* urb)
{
    char buf[6];
    struct kobject *kobj;
    struct sensor *sens;

    PDEBUG("rcvcomplete %d\n", urb->actual_length);
    PDEBUG("%2.2hhx %2.2hhx %2.2hhx %2.2hhx %2.2hhx %2.2hhx %2.2hhx %2.2hhx\n",
        dev->iobuf[0], dev->iobuf[1], dev->iobuf[2], dev->iobuf[3],
        dev->iobuf[4], dev->iobuf[5], dev->iobuf[6], dev->iobuf[7]);
    switch (dev->iobuf[1]) {
    case 0x09:
    case 0x0a:
        sprintf(buf, "%d", (dev->iobuf[3]<<8)+dev->iobuf[2]);
        kobj = find_kobj(buf);
        if (kobj)
            sens = container_of(kobj, struct sensor, kobj);
        else {
            sens = kmalloc(sizeof(struct sensor), GFP_KERNEL);
            memset(sens, 0, sizeof(struct sensor));
            kobj = &sens->kobj;
            kobj->kset = dev->kset;
            if (kobject_init_and_add(kobj, &sensor_kobj_type, NULL, buf))
                printk(KERN_ERR "Failed to add kobject");
        }
        sens->raw = (dev->iobuf[4]<<8)+dev->iobuf[5];
        queue_delayed_work(dev->workqueue, &pollwork, 0);
        break;
    default:
        queue_delayed_work(dev->workqueue, &pollwork, HZ);
    };
}

static void sndcomplete(struct urb* urb)
{
    PDEBUG("sndcomplete %d\n", urb->actual_length);

    usb_fill_bulk_urb(dev->urb, dev->usbdev,
                     usb_rcvbulkpipe(dev->usbdev, dev->bulk_in_endpointAddr),
                     dev->iobuf, dev->iobufsize, rcvcomplete, NULL);
    
    usb_submit_urb(dev->urb, GFP_KERNEL);
}

static void cleanup(void)
{
    struct kobject *kobj, *kobj1;

    if (dev) {
        if (dev->workqueue) {
            cancel_delayed_work(&pollwork);
            flush_workqueue(dev->workqueue);
            destroy_workqueue(dev->workqueue);
        }
        if (dev->urb)
            usb_free_urb(dev->urb);
        if (dev->iobuf)
            kfree(dev->iobuf);
        if (dev->kset) {
            list_for_each_entry_safe(kobj, kobj1, &dev->kset->list, entry) {
                kobject_put(kobj);
            }
            kset_unregister(dev->kset);
        }
        kfree(dev);
        dev = NULL;
    }
}

static int tlx00_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
    struct usb_host_interface * iface_desc;
    struct usb_endpoint_descriptor * endpoint;
    int i, buffer_size;
    int errno;

    PDEBUG("probed\n");
 
    dev = kmalloc(sizeof(struct tlx00_dev), GFP_KERNEL);
    if (!dev) {
        errno = -ENOMEM;
        goto error;
    }
    memset(dev, 0, sizeof(struct tlx00_dev));

    dev->kset = kset_create_and_add("tlx00", NULL, NULL);
    if (!dev->kset) {
        errno = -ENOMEM;
        goto error;
    }

    /* set up the endpoint information */
    /* use only the first bulk-in and bulk-out endpoints */
    iface_desc = intf->cur_altsetting;
    for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
        endpoint = &iface_desc->endpoint[i].desc;

        if (!dev->bulk_in_endpointAddr &&
            (endpoint->bEndpointAddress & USB_DIR_IN) &&
            ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
                    == USB_ENDPOINT_XFER_BULK)) {
            /* we found a bulk in endpoint */
            buffer_size = endpoint->wMaxPacketSize;
            dev->iobufsize = buffer_size;
            dev->bulk_in_endpointAddr = endpoint->bEndpointAddress;
            dev->iobuf = kmalloc(buffer_size, GFP_KERNEL);
            if (!dev->iobuf) {
                err("Could not allocate iobuf");
                errno = -ENOMEM;
                goto error;
            }
        }

        if (!dev->bulk_out_endpointAddr &&
            !(endpoint->bEndpointAddress & USB_DIR_IN) &&
            ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
                    == USB_ENDPOINT_XFER_BULK)) {
            /* we found a bulk out endpoint */
            dev->bulk_out_endpointAddr = endpoint->bEndpointAddress;
        }
    }
    if (!(dev->bulk_in_endpointAddr && dev->bulk_out_endpointAddr)) {
        err("Could not find both bulk-in and bulk-out endpoints");
        errno = -EINVAL;
        goto error;
    }

    dev->usbdev = interface_to_usbdev(intf);

    dev->urb = usb_alloc_urb(0, GFP_KERNEL);
    if (!dev->urb)
    {
        errno = -ENOMEM;
        goto error;
    }

    dev->iobuf[0] = 0x04;
    usb_bulk_msg(dev->usbdev,
                 usb_sndbulkpipe(dev->usbdev, dev->bulk_out_endpointAddr),
                 dev->iobuf, dev->iobufsize, NULL, 0);

    dev->workqueue = create_singlethread_workqueue("tlx00");
    queue_delayed_work(dev->workqueue, &pollwork, 0);

    return 0;

error:
    cleanup();
    return errno;
}

static void tlx00_disconnect(struct usb_interface *intf)
{
    PDEBUG("disconnect\n");
    cleanup();
}

static struct usb_device_id tlx00_table [  ] = {
    { USB_DEVICE(TL500_VENDOR_ID, TL500_PRODUCT_ID) },
    { }
};

MODULE_DEVICE_TABLE (usb, tlx00_table);

static struct usb_driver tlx00_driver = {
    .name = "tlx00",
    .id_table = tlx00_table,
    .probe = tlx00_probe,
    .disconnect = tlx00_disconnect,
};

static int __init tlx00_init(void)
{
    int result;

    result = usb_register(&tlx00_driver);
    if (result)
        err("usb_register failed. Error number %d", result);

    return result;
}

static void __exit tlx00_exit(void)
{
    usb_deregister(&tlx00_driver);
}

MODULE_AUTHOR( DRIVER_AUTHOR );
MODULE_DESCRIPTION( DRIVER_DESC );
MODULE_LICENSE("GPL v2");
module_init(tlx00_init);
module_exit(tlx00_exit);

