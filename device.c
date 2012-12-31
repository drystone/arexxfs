#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <libusb.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#include "device.h"

#define VENDOR  0x0451
#define PRODUCT 0x3211
#define ENDPOINT_DOWN 0x01
#define ENDPOINT_UP   0x81
#define IOLEN   64

tlx_reading * tlx_root_reading = NULL;
time_t tlx_mtime, tlx_ctime;

static int _record_reading(unsigned int id, unsigned int raw)
{
    tlx_reading * p = tlx_root_reading;

    while (p)
    {
        if (p->id == id)
        {
            p->raw = raw;
            p->mtime = time(NULL);
            return 0;
        }
        p = p->next;
    }

    p = (tlx_reading *)malloc(sizeof(tlx_reading));
    if (!p) 
    {
        perror("malloc failed");
        return errno;
    }
    p->id = id;
    p->raw = raw;
    p->ctime = p->mtime = tlx_mtime = time(NULL);
    p->next = tlx_root_reading;
    tlx_root_reading = p;
    return 0;
}

static int do_loop(libusb_device_handle * handle)
{
    int transferred;
    unsigned char iobuf[IOLEN];

    iobuf[0] = 4; // initialise
    while (libusb_bulk_transfer(handle, ENDPOINT_DOWN, iobuf, IOLEN, &transferred, 1000) == 0)
    {
        if (libusb_bulk_transfer(handle, ENDPOINT_UP, iobuf, IOLEN, &transferred, 1000) == 0)
        {
            if (transferred == IOLEN && (iobuf[0] || iobuf[1]))
            {
                if (_record_reading((iobuf[3]<<8) + iobuf[2], (iobuf[4]<<8) + iobuf[5]))
                    break;
            }
            else sleep(1);
        }
        else
        {
            perror("libusb_bulk_transfer failed");
            break;
        }
        iobuf[0] = 3; // read
    }
    return errno;
}

void * tlx_thread(void * arg) {
    // set mtime and ctime for directory
    tlx_ctime = tlx_mtime = time(NULL);

    libusb_device **devs = NULL;

    (void) arg;

    if (libusb_init(NULL) < 0)
    {
        perror("libusb_init failed");
        return NULL;
    }

    if (libusb_get_device_list(NULL, &devs) >= 0)
    {
        libusb_device **pdev;
        for(pdev = devs; *pdev != NULL; ++pdev)
        {
            struct libusb_device_descriptor udd;
            if (libusb_get_device_descriptor(*pdev, &udd) == 0)
            {
                if (udd.idVendor == VENDOR && udd.idProduct == PRODUCT)
                {
                    libusb_device_handle *handle;
                    if (libusb_open(*pdev, &handle) == 0)
                    {
                        do_loop(handle);
                        libusb_close(handle);
                    }
                    else perror("libusb_open failed");
                    break;
                }
            }
            else perror("libusb_get_device_descriptor failed");
        }
        libusb_free_device_list(devs, 1);
    }
    else perror("libusb_get_device_list failed");

    libusb_exit(NULL);

    return NULL;
}

