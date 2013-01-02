typedef struct _reading tlx_reading;

struct _reading {
    unsigned int id;
    int raw;
    time_t ctime;
    time_t mtime;
    tlx_reading * next;
};

extern tlx_reading * tlx_root_reading;
extern time_t tlx_ctime, tlx_mtime;
extern int tlx_running;

void * tlx_thread(void *);
