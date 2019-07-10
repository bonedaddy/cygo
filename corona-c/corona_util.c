#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

#include <unistd.h>
#include <sys/syscall.h>
// #include <threads.h>

#include <yieldtypes.h>
#include <corona_util.h>

#include "futex.h"

pid_t gettid() {
#ifdef SYS_gettid
    pid_t tid = syscall(SYS_gettid);
    return tid;
#else
#error "SYS_gettid unavailable on this system"
    return 0;
#endif
}

int (array_randcmp) (const void*a, const void*b) {
    int n = rand() % 3;
    return n-1;
}

// used before inited
#define lograw(fmt, ...)                                                \
    if (SHOWLOG) {                                                      \
        const char* filename = __FILE__; char* fbname = strrchr(filename, '/'); \
        if (fbname != NULL) fbname ++;                                  \
        crn_loglock();                                                  \
        fprintf(stderr, "%s:%d %s: ", fbname, __LINE__, __func__); \
        fprintf(stderr, fmt, __VA_ARGS__);                              \
        fflush(stderr); crn_logunlock();                                \
    }

static rtsettings rtsetsobj = {.loglevel = LOGLVL_INFO,};
rtsettings* rtsets = &rtsetsobj;
static int loglvl = LOGLVL_INFO;
// or CRNDEBUG="loglvl=3,leakdt=1,gcpercent=30,gctrace=1,..."
static void crn_loglvl_forenv_CRNDEBUG() {
    char sep = ',';
    char* CRNDEBUG = getenv("CRNDEBUG");
    if (CRNDEBUG == 0 || strlen(CRNDEBUG) == 0) return;

    int keybpos = 0; // begin pos
    int keyepos = -1; // end pos
    int valbpos = -1;
    int valepos = -1;

    char* ptr = strdup(CRNDEBUG);
    int pos2 = 0;
    for (int pos = 0; pos <= strlen(CRNDEBUG) ; pos++) {
        char ch = CRNDEBUG[pos];
        if (pos2 == 0 && (ch == ' ' || ch == ',')) continue;
        if (ch == ' ') continue;
        ptr[pos2++] = ch;
    }
    for (int pos = 0; pos <= strlen(ptr) ; pos++) {
        if (ptr[pos] == '=') {
            keyepos = pos;
            valbpos = pos+1;
        }
        if (ptr[pos] == ',' || ptr[pos] == '\0') {
            valepos = pos;
            if (keybpos == -1 || keyepos == -1 || valbpos == -1 || valepos == -1) {
                break;
            }
            char* key = strndup(&ptr[keybpos], keyepos-keybpos);
            char* val = strndup(&ptr[valbpos], valepos-valbpos);
            if (strcmp(key, "loglvl") == 0) {
                int lvl = atoi(val);
                if (lvl >= LOGLVL_FATAL && lvl <= LOGLVL_TRACE) {
                    loglvl = lvl;
                }else{
                    lograw("Invalid setting log level %s\n", val);
                }
            }else if (strcmp(key, "gctrace") == 0) {
            }else if (strcmp(key, "gcrate") == 0) {
            }else{
                lograw("Invalid setting key %s\n", key);
            }
            free(key); free(val);

            if (ptr[pos] == '\0') {
                break;
            }
            keybpos = pos+1;
            keyepos = valbpos = valepos = -1;
        }
    }
    free(ptr);
}
void crn_loglvl_forenv() {
    crn_loglvl_forenv_CRNDEBUG();
    rtsets->loglevel = loglvl;
    rtsets->maxprocs = 3; // TODO CPU thread count + 1
    rtsets->gcpercent = 100;
}

static pmutex_t crn_loglk;
void __attribute__((no_instrument_function))
crn_loglock() {
    // pmutex_lock(&loglk);
}
void __attribute__((no_instrument_function))
crn_logunlock() {
    // pmutex_unlock(&loglk);
    // pmutex_trylock(&loglk);
}

void __attribute__((no_instrument_function))
crn_simlog(int level, const char *filename, int line, const char* funcname, const char *fmt, ...) {
    if (level > loglvl) return;
    static __thread char obuf[612] = {0};
    char* fbname = strrchr(filename, '/');
    if (fbname != NULL) fbname ++;
    struct timeval ltv = {0};
    gettimeofday(&ltv, 0);
    crn_loglock();
    int len = snprintf(obuf, sizeof(obuf)-1, "%ld.%ld %s:%d %s: ",
                       ltv.tv_sec, ltv.tv_usec, fbname, line, funcname);

    va_list args;
    va_start(args, fmt);
    len += vsnprintf(obuf+len, sizeof(obuf)-len-1, fmt, args);
    va_end(args);
    obuf[len] = '\0';
    // fprintf(stderr, "%s", buf);
    // fflush(stderr);
    write(STDERR_FILENO, obuf, len);
    crn_logunlock();
}

// nolock version, used when stopped the world
void crn_simlog2(int level, const char *filename, int line, const char* funcname, const char *fmt, ...) {
    static __thread char obuf[612] = {0};
    char* fbname = strrchr(filename, '/');
    if (fbname != NULL) fbname ++;
    struct timeval ltv = {0};
    gettimeofday(&ltv, 0);
    // loglock();
    // fprintf(stderr, "%ld.%ld %s:%d %s: ", ltv.tv_sec, ltv.tv_usec, fbname, line, funcname);
    int len = snprintf(obuf, sizeof(obuf), "%ld.%ld %s:%d %s: ",
                       ltv.tv_sec, ltv.tv_usec, fbname, line, funcname);

    va_list args;
    va_start(args, fmt);
    // vfprintf(stderr, fmt, args);
    len += vsnprintf(obuf+len,sizeof(obuf)-len,fmt,args);
    va_end(args);
    write(STDERR_FILENO, obuf, len);
    // fflush(stderr);
    // logunlock();
}


const char* yield_type_name(int ytype) {
    switch (ytype) {
    case YIELD_TYPE_NONE:
        return "none";
    case YIELD_TYPE_CHAN_SEND:
        return "chansend";
    case YIELD_TYPE_CHAN_RECV:
        return "chanrecv";
    case YIELD_TYPE_CHAN_SELECT:
        return "chanselect";
    case YIELD_TYPE_CHAN_SELECT_NOCASE:
        return "chanselectnocase";
    case YIELD_TYPE_CONNECT:
        return "connect";
    case YIELD_TYPE_READ:
        return "read";
    case YIELD_TYPE_READV:
        return "readv";
    case YIELD_TYPE_RECV:
        return "recv";
    case YIELD_TYPE_RECVFROM:
        return "recvfrom";
    case YIELD_TYPE_RECVMSG:
        return "recvmsg";
    case YIELD_TYPE_RECVMSG_TIMEOUT:
        return "recvmsgtimeo";
    case YIELD_TYPE_WRITE:
        return "write";
    case YIELD_TYPE_WRITEV:
        return "writev";
    case YIELD_TYPE_SEND:
        return "send";
    case YIELD_TYPE_SENDTO:
        return "sendto";
    case YIELD_TYPE_SENDMSG:
        return "sendmsg";

    case YIELD_TYPE_POLL:
        return "poll";
    case YIELD_TYPE_UUPOLL:
        return "uupoll";
    case YIELD_TYPE_SELECT:
        return "select";
    case YIELD_TYPE_ACCEPT:
        return "accept";

    case YIELD_TYPE_SLEEP:
        return "sleep";
    case YIELD_TYPE_MSLEEP:
        return "msleep";
    case YIELD_TYPE_USLEEP:
        return "usleep";
    case YIELD_TYPE_NANOSLEEP:
        return "nanosleep";

    case YIELD_TYPE_GETHOSTBYNAMER:
        return "gethostbynamer";
    case YIELD_TYPE_GETHOSTBYNAME2R:
        return "gethostbyname2r";
    case YIELD_TYPE_GETHOSTBYADDR:
        return "gethostbyaddr";
    case YIELD_TYPE_MAX:
        return "max";
    default:
        return "unknown";
    }
}

typedef enum grstate {nostack=0, runnable, executing, waiting, finished, } grstate;
const char* grstate2str(grstate s) {
    switch (s) {
    case nostack: return "nostack";
    case runnable: return "runnable";
    case executing: return "executing";
    case waiting: return "waiting";
    case finished: return "finished";
    default:
        assert(s >= nostack && s <= finished);
    }
}
