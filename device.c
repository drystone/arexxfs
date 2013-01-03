/*
This file is part of arexxfs, a fuse interface to the arexx data loggers

Copyright © 2013 John Hedges <john@drystone.co.uk>

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

#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <libusb.h>
#include <string.h>
#include <stdlib.h>
#include <syslog.h>

#include <pthread.h>

#include "device.h"

#define VENDOR  0x0451
#define PRODUCT 0x3211
#define ENDPOINT_DOWN 0x01
#define ENDPOINT_UP   0x81
#define IOLEN   64

time_t tlx_mtime, tlx_ctime;

static tlx_reading * _root_reading = NULL;
static int tlx_running = 0;

static void _start_tlx_thread();

void tlx_init() {
    _start_tlx_thread();
}

tlx_reading * tlx_get_root() {
    _start_tlx_thread();
    return _root_reading;
}

tlx_reading * tlx_get_reading(const char * sid)
{
    // make sure tlx device is still communicating
    _start_tlx_thread();

    char * e;
    long int id = strtol(sid, &e, 10);
    if (*e != '\0')
        return NULL;    // strtol didn't convert whole sid

    tlx_reading * p = _root_reading;
    while (p) {
        if ((long int)p->id == id)
            return p;
        p = p->next;
    }

    return NULL;
}

static int _record_reading(unsigned int id, unsigned int raw)
{
    tlx_reading * p = _root_reading;

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
    p->next = _root_reading;
    _root_reading = p;
    return 0;
}

static void _tlx_loop(libusb_device_handle * handle)
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
        } else sleep(1);
        iobuf[0] = 3; // read
    }
}

void * _tlx_thread(void * arg) {
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
    _tlx_loop(handle);
    libusb_close(handle);

exit:
    libusb_free_device_list(devs, 1);
exit1:
    libusb_exit(NULL);
exit2:
    tlx_running = 0;
    return NULL;
}

static void _start_tlx_thread()
{
    static pthread_mutex_t _start_tlx_mutex = PTHREAD_MUTEX_INITIALIZER;

    if (!tlx_running) {
        pthread_mutex_lock(&_start_tlx_mutex);
        if (!tlx_running) {
            tlx_running = 1;
            pthread_t th;
            pthread_create(&th, NULL, _tlx_thread, NULL);
        }
        pthread_mutex_unlock(&_start_tlx_mutex);
    }
}

