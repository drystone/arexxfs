typedef struct _reading tlx_reading;

struct _reading {
    unsigned int id;
    int raw;
    time_t ctime;
    time_t mtime;
    tlx_reading * next;
};

extern time_t tlx_ctime, tlx_mtime;

void tlx_init();
tlx_reading * tlx_get_root();
tlx_reading * tlx_get_reading(const char * id);
