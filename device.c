#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <libusb.h>
#include <string.h>
#include <stdlib.h>
#include <syslog.h>

#include "device.h"

#define VENDOR  0x0451
#define PRODUCT 0x3211
#define ENDPOINT_DOWN 0x01
#define ENDPOINT_UP   0x81
#define IOLEN   64

tlx_reading * tlx_root_reading = NULL;
time_t tlx_mtime, tlx_ctime;
int tlx_running = 0;

static int _record_reading(unsigned int id, unsigned int raw)
{
    tlx_reading * p = tlx_root_reading;

    while (p) {
        if (p->id == id) {
            p->raw = raw;
            p->mtime = time(NULL);
            return 0;
        }
        p = p->next;
    }

    p = (tlx_reading *)malloc(sizeof(tlx_reading));
    if (!p) {
        syslog(LOG_DAEMON | LOG_ERR, "malloc failed (%m)");
        return -1;
    }
    p->id = id;
    p->raw = raw;
    p->ctime = p->mtime = tlx_mtime = time(NULL);
    p->next = tlx_root_reading;
    tlx_root_reading = p;
    return 0;
}

static void do_loop(libusb_device_handle * handle)
{
    int transferred;
    unsigned char iobuf[IOLEN];

    iobuf[0] = 4; // initialise
    while (1) {
        if (libusb_bulk_transfer(handle, ENDPOINT_DOWN, iobuf, IOLEN, &transferred, 0) < 0) {
            syslog(LOG_DAEMON | LOG_CRIT, "libusb_bulk_transfer down failed (%m)");
            break;
        }
        if (libusb_bulk_transfer(handle, ENDPOINT_UP, iobuf, IOLEN, &transferred, 0) < 0) {
            syslog(LOG_DAEMON | LOG_CRIT, "libusb_bulk_transfer up failed (%m)");
            break;
        }
        if (transferred == IOLEN && (iobuf[0] || iobuf[1])) {
            if (_record_reading((iobuf[3]<<8) + iobuf[2], (iobuf[4]<<8) + iobuf[5]) < 0)
                break;
        }
        iobuf[0] = 3; // read
    }
}

void * tlx_thread(void * arg) {
    libusb_device **devs, **pdev;
    libusb_device_handle *handle;
    (void) arg;

    // set mtime and ctime for directory
    tlx_ctime = tlx_mtime = time(NULL);
    
    if (libusb_init(NULL) < 0) {
        syslog(LOG_DAEMON | LOG_CRIT, "libusb_init failed (%m)");
        goto exit2;
    }

    if (libusb_get_device_list(NULL, &devs) < 0) {
        syslog(LOG_DAEMON | LOG_CRIT, "libusb_get_device_list failed (%m)");
        goto exit1;
    }

    for(pdev = devs; *pdev != NULL; ++pdev) {
        struct libusb_device_descriptor udd;
        memset(&udd, 0, sizeof(udd));
        libusb_get_device_descriptor(*pdev, &udd);
        if (udd.idVendor == VENDOR && udd.idProduct == PRODUCT)
            break;
    }

    if (!*pdev) {
        syslog(LOG_DAEMON | LOG_CRIT, "arexx data logging device not found");
        goto exit;
    }

    if (libusb_open(*pdev, &handle) < 0) {
        syslog(LOG_DAEMON | LOG_CRIT, "libusb_open failed (%m)");
        goto exit;
    }

    syslog(LOG_DAEMON | LOG_INFO, "arexx data logging device active");

    if (libusb_kernel_driver_active(handle, 0) == 1)
        libusb_detach_kernel_driver(handle, 0);
    libusb_claim_interface(handle, 0);
    do_loop(handle);
    libusb_close(handle);

exit:
    libusb_free_device_list(devs, 1);
exit1:
    libusb_exit(NULL);
exit2:
    tlx_running = 0;
    return NULL;
}

