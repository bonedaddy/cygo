
#include <assert.h>
#include <stdarg.h>

#include "cxrtbase.h"

// corona
typedef struct corona corona;

extern corona* crn_init_and_wait_done();
extern void crn_post(void(*fn)(void*arg), void*arg);
extern void crn_sched();
extern void crn_set_finalizer(void*ptr, void(*fn)(void*));
typedef struct hchan hchan;
extern hchan* hchan_new(int cap);
extern int hchan_cap(hchan* hc);
extern int hchan_len(hchan* hc);
extern int hchan_send(hchan* hc, void* data);
extern int hchan_recv(hchan* hc, void** pdata);


extern void GC_allow_register_threads();
static void cxrt_init_gc_env() {
    // GC_set_free_space_divisor(50); // default 3
    // GC_INIT();
    // GC_allow_register_threads();
}

void cxrt_init_routine_env() {
    // assert(1==2);
}

void cxrt_init_env() {
    cxrt_init_gc_env();
    crn_init_and_wait_done();
    //
}

void cxrt_fiber_post(void (*fn)(void*), void*arg) {
    crn_post(fn, arg);
}
void cxrt_set_finalizer(void* ptr,void (*fn) (void*)) {
    crn_set_finalizer(ptr, fn);
}
void* cxrt_chan_new(int sz) {
    void* ch = hchan_new(sz);
    assert(ch != nilptr);
    printf("cxrt_chan_new, %p\n", ch);
    return ch;
}
void cxrt_chan_send(void*ch, void*arg) {
    assert(ch != nilptr);
    hchan_send(ch, arg);
}
void* cxrt_chan_recv(void*ch) {
    assert(ch != nilptr);
    void* data = nilptr;
    hchan_recv(ch, &data);
    return data;
}

/////
void println(const char* fmt, ...) {
    va_list arg;
    int done;

    va_start (arg, fmt);
    done = vprintf (fmt, arg);
    va_end (arg);

    printf("\n");
}
void println2(const char* filename, int lineno, const char* funcname, const char* fmt, ...) {
    const char* fbname = strrchr(filename, '/');
    if (fbname != nilptr) { fbname = fbname + 1; }
    else { fbname = filename; }

    printf("%s:%d:%s ", fbname, lineno, funcname);

    va_list arg;
    int done;

    va_start (arg, fmt);
    done = vprintf (fmt, arg);
    va_end (arg);

    printf("\n");
}
void panic(cxstring*s) {
    if (s != nilptr) {
        printf("%.*s", s->len, s->ptr);
    }else{
        printf("<%p>", s);
    }
    memcpy((void*)0x1, "abc", 3);
}
void panicln(cxstring*s) {
    cxstring* lr = cxstring_new_cstr("\n");
    if (s != nilptr) {
        s = cxstring_add(s, lr);
    } else{
        s = lr;
    }
    panic(s);
}

#include <unistd.h>
#include <sys/syscall.h>

pid_t cxgettid() {
#ifdef SYS_gettid
    pid_t tid = syscall(SYS_gettid);
    return tid;
#else
#error "SYS_gettid unavailable on this system"
    return 0;
#endif
}

