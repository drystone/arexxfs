#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

#include <fuse.h>

#include "device.h"

static int _getattr(const char *path, struct stat *stbuf)
{
    memset(stbuf, 0, sizeof(struct stat));

    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0555;
        stbuf->st_nlink = 2;
        stbuf->st_ctime = tlx_ctime;
        stbuf->st_mtime = tlx_mtime;
        return 0;
    }

    tlx_reading * p = tlx_get_reading(path + 1);
    if (p) {
        stbuf->st_mode = S_IFREG | 0444;
        stbuf->st_nlink = 1;
        stbuf->st_size = 5;
        stbuf->st_ctime = p->ctime;
        stbuf->st_mtime = p->mtime;
        return 0;
    }

    return -ENOENT;
}

static int _readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                         off_t offset, struct fuse_file_info *fi)
{
    if(strcmp(path, "/") != 0)
        return -ENOENT;

    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    
    tlx_reading * p = tlx_get_root();
    while (p) {
        char id[6]; // max 5-digit id
        sprintf(id, "%u", p->id);
        filler(buf, id, NULL, 0);
        p = p->next;
    }

    return 0;
}

static int _open(const char *path, struct fuse_file_info *fi)
{
    if ((fi->flags & 3) != O_RDONLY)
        return -EACCES;

    if (tlx_get_reading(path + 1) == NULL)
        return -ENOENT;

    return 0;
}

static int _read(const char *path, char *buf, size_t size, off_t offset,
                      struct fuse_file_info *fi)
{
    tlx_reading * p = tlx_get_reading(path + 1);
    if (p == NULL)
        return -ENOENT;

    if (offset < 5) {
        char * hex = "0123456789ABCDEF";
        char raw[5];
        raw[0] = hex[p->raw >> 12];
        raw[1] = hex[(p->raw >> 8) & 0xf];
        raw[2] = hex[(p->raw >> 4) & 0xf];
        raw[3] = hex[p->raw & 0xf];
        raw[4] = '\n';
        if (offset + size > 5)
            size = 5 - offset;
        memcpy(buf, raw + offset, size);
    } else
        size = 0;

    return size;
}

static void * _init(struct fuse_conn_info * conn)
{
    tlx_init();
    return NULL;
}

static struct fuse_operations _oper = {
    .getattr    = _getattr,
    .readdir    = _readdir,
    .open   = _open,
    .read   = _read,
    .init   = _init,
};

int main(int argc, char *argv[])
{
    return fuse_main(argc, argv, &_oper, NULL);
}

