typedef struct _reading tlx_reading;

struct _reading {
    unsigned int id;
    int raw;
    tlx_reading * next;
};

extern tlx_reading * tlx_root_reading;

void * tlx_thread(void *);
