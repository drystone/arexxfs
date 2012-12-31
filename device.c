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
int tlx_quit = 0;

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
        syslog(LOG_DAEMON | LOG_ALERT, "malloc failed (%m)");
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
        if (libusb_bulk_transfer(handle, ENDPOINT_DOWN, iobuf, IOLEN, &transferred, 1000) == 0) {
            if (libusb_bulk_transfer(handle, ENDPOINT_UP, iobuf, IOLEN, &transferred, 1000) == 0)
            {
                if (transferred == IOLEN && (iobuf[0] || iobuf[1]))
                {
                    if (_record_reading((iobuf[3]<<8) + iobuf[2], (iobuf[4]<<8) + iobuf[5]))
                        break;
                }
                else sleep(1);
            } else {
                syslog(LOG_DAEMON | LOG_ALERT, "libusb_bulk_transfer up failed (%m)");
                break;
            }
        } else {
            syslog(LOG_DAEMON | LOG_ALERT, "libusb_bulk_transfer down failed (%m)");
            break;
        }
        iobuf[0] = 3; // read
    }
}

void * tlx_thread(void * arg) {
    (void) arg;

    // set mtime and ctime for directory
    tlx_ctime = tlx_mtime = time(NULL);

    while (!tlx_quit) {
        libusb_device **devs = NULL;

        if (libusb_init(NULL) == 0) {
            if (libusb_get_device_list(NULL, &devs) >= 0) {
                libusb_device **pdev;

                for(pdev = devs; *pdev != NULL; ++pdev) {
                    struct libusb_device_descriptor udd;

                    if (libusb_get_device_descriptor(*pdev, &udd) == 0) {
                        if (udd.idVendor == VENDOR && udd.idProduct == PRODUCT) {
                            libusb_device_handle *handle;

                            if (libusb_open(*pdev, &handle) == 0) {
                                syslog(LOG_DAEMON | LOG_INFO, "arexx data logging device active");
                                if (libusb_kernel_driver_active(handle, 0) == 1)
                                    libusb_detach_kernel_driver(handle, 0);
                                libusb_claim_interface(handle, 0);
                                do_loop(handle);
                                libusb_close(handle);
                            }
                            else syslog(LOG_DAEMON | LOG_ALERT, "libusb_open failed (%m)");
                            break;
                        }
                    }
                    else syslog(LOG_DAEMON | LOG_ALERT, "libusb_get_device_descriptor failed (%m)");
                }
                if (*pdev == NULL)
                    syslog(LOG_DAEMON | LOG_CRIT, "arexx data logging device not found");

                libusb_free_device_list(devs, 1);
            }
            else syslog(LOG_DAEMON | LOG_ALERT, "libusb_get_device_list failed (%m)");

            libusb_exit(NULL);
        }
        else syslog(LOG_DAEMON | LOG_ALERT, "libusb_init failed (%m)");

        if (!tlx_quit)
            sleep(10);
    }
    return NULL;
}

