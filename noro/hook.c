#include "hook.h"
#include <stdlib.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <poll.h>
#if defined(LIBGO_SYS_Linux)
# include <sys/epoll.h>
#elif defined(LIBGO_SYS_FreeBSD)
# include <sys/event.h>
# include <sys/time.h>
#endif

#if defined(LIBGO_SYS_Linux)
# define ATTRIBUTE_WEAK __attribute__((weak))
#elif defined(LIBGO_SYS_FreeBSD)
# define ATTRIBUTE_WEAK __attribute__((weak_import))
#endif

pipe_t pipe_f = NULL;
socket_t socket_f = NULL;
socketpair_t socketpair_f = NULL;
connect_t connect_f = NULL;
read_t read_f = NULL;
readv_t readv_f = NULL;
recv_t recv_f = NULL;
recvfrom_t recvfrom_f = NULL;
recvmsg_t recvmsg_f = NULL;
write_t write_f = NULL;
writev_t writev_f = NULL;
send_t send_f = NULL;
sendto_t sendto_f = NULL;
sendmsg_t sendmsg_f = NULL;
poll_t poll_f = NULL;
select_t select_f = NULL;
accept_t accept_f = NULL;
sleep_t sleep_f = NULL;
usleep_t usleep_f = NULL;
nanosleep_t nanosleep_f = NULL;
close_t close_f = NULL;
fcntl_t fcntl_f = NULL;
ioctl_t ioctl_f = NULL;
getsockopt_t getsockopt_f = NULL;
setsockopt_t setsockopt_f = NULL;
dup_t dup_f = NULL;
dup2_t dup2_f = NULL;
dup3_t dup3_f = NULL;
fclose_t fclose_f = NULL;
fopen_t fopen_f = NULL;
#if defined(LIBGO_SYS_Linux)
pipe2_t pipe2_f = NULL;
gethostbyname_r_t gethostbyname_r_f = NULL;
gethostbyname2_r_t gethostbyname2_r_f = NULL;
gethostbyaddr_r_t gethostbyaddr_r_f = NULL;
epoll_wait_t epoll_wait_f = NULL;
#elif defined(LIBGO_SYS_FreeBSD)
#endif


// #include "hookcb.h"
#include "noropriv.h"

int pipe(int pipefd[2])
{
    if (!socket_f) initHook();
    linfo("%d\n", pipefd[0]);

    int rv = pipe_f(pipefd);
    if (rv == 0) {
        if (noro_in_processor()) {
            hookcb_oncreate(pipefd[0], FDISPIPE, false, 0,0,0);
            hookcb_oncreate(pipefd[1], FDISPIPE, false, 0,0,0);
        }
    }
    return rv;
}
#if defined(LIBGO_SYS_Linux)
int pipe2(int pipefd[2], int flags)
{
    // if (noro_in_processor())
    if (!socket_f) initHook();
    linfo("%d\n", flags);

    int rv = pipe2_f(pipefd, flags);
    if (rv == 0) {
        hookcb_oncreate(pipefd[0], FDISPIPE, !!(flags&O_NONBLOCK), 0,0,0);
        hookcb_oncreate(pipefd[1], FDISPIPE, !!(flags&O_NONBLOCK), 0,0,0);
    }
    return rv;
}
#endif
int socket(int domain, int type, int protocol)
{
    // if (noro_in_processor())
    if (!socket_f) initHook();
    // linfo("socket_f=%p\n", socket_f);

    int sock = socket_f(domain, type, protocol);
    if (sock >= 0) {
        hookcb_oncreate(sock, FDISSOCKET, false, domain, type, protocol);
        // linfo("task(%s) hook socket, returns %d.\n", "", sock);
        // linfo("domain=%d type=%s(%d) socket=%d\n", domain, type==SOCK_STREAM ? "tcp" : "what", type, sock);
    }

    return sock;
}

int socketpair(int domain, int type, int protocol, int sv[2])
{
    // if (noro_in_processor())
    if (!socketpair_f) initHook();
    linfo("%d\n", type);

    int rv = socketpair_f(domain, type, protocol, sv);
    if (rv == 0) {
        hookcb_oncreate(sv[0], FDISSOCKET, false, domain, type, protocol);
        hookcb_oncreate(sv[1], FDISSOCKET, false, domain, type, protocol);
    }
    return rv;
}

int connect(int fd, const struct sockaddr *addr, socklen_t addrlen)
{
    if (!connect_f) initHook();
    // linfo("%d\n", fd);
    time_t btime = time(0);
    for (int i = 0;; i++) {
        int rv = connect_f(fd, addr, addrlen);
        int eno = rv < 0 ? errno : 0;
        if (rv >= 0) {
            // linfo("connect ok %d %d, %d, %d\n", fd, errno, time(0)-btime, i);
            return rv;
        }
        if (eno != EINPROGRESS && eno != EALREADY) {
            linfo("Unknown %d %d %d %d %s\n", fd, rv, eno, i, strerror(eno));
            return rv;
        }
        noro_processor_yield(fd, YIELD_TYPE_CONNECT);
    }
    assert(1==2); // unreachable
}

int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    if (!accept_f) initHook();
    linfo("%d fdnb=%d\n", sockfd, fd_is_nonblocking(sockfd));
    while(1){
        int rv = accept_f(sockfd, addr, addrlen);
        int eno = rv < 0 ? errno : 0;
        if (rv >= 0) {
            hookcb_oncreate(rv, FDISSOCKET, false, AF_INET, SOCK_STREAM, 0);
            return rv;
        }
        if (eno != EINPROGRESS) {
            linfo("fd=%d err=%d eno=%d err=%s\n", sockfd, rv, errno, strerror(errno));
            return rv;
        }
        noro_processor_yield(sockfd, YIELD_TYPE_ACCEPT);
    }
    assert(1==2); // unreachable
}

ssize_t read(int fd, void *buf, size_t count)
{
    if (!noro_in_processor()) return read_f(fd, buf, count);
    if (!read_f) initHook();
    // linfo("%d fdnb=%d bufsz=%d\n", fd, fd_is_nonblocking(fd), count);
    while (1){
        ssize_t rv = read_f(fd, buf, count);
        int eno = rv < 0 ? errno : 0;
        if (rv >= 0) {
            return rv;
        }
        if (eno != EINPROGRESS && eno != EAGAIN) {
            linfo("fd=%d err=%d eno=%d err=%s\n", fd, rv, errno, strerror(errno));
            return rv;
        }
        noro_processor_yield(fd, YIELD_TYPE_READ);
    }
    assert(1==2); // unreachable
}

ssize_t readv(int fd, const struct iovec *iov, int iovcnt)
{
    if (!readv_f) initHook();
    linfo("%d\n", fd);
    assert(1==2);
}

int fd_is_valid(int fd)
{
    return fcntl_f(fd, F_GETFD) != -1 || errno != EBADF;
}
ssize_t recv(int sockfd, void *buf, size_t len, int flags)
{
    if (!recv_f) initHook();
    // linfo("%d %d %d\n", sockfd, len, flags);
    while (1) {
        ssize_t rv = recv_f(sockfd, buf, len, flags);
        int eno = rv < 0 ? errno : 0;
        if (rv == 0) {
            hookcb_onclose(sockfd);
        }
        if (rv >= 0) {
            return rv;
        }
        if (eno != EINPROGRESS && eno != EAGAIN) {
            linfo("fd=%d err=%d eno=%d err=%s\n", sockfd, rv, errno, strerror(errno));
            return rv;
        }
        int fdvalid = fd_is_valid(sockfd);
        if (fdvalid != 1) {
            linfo("invalid fd=%d val=%d\n", sockfd, fdvalid);
            assert(fd_is_valid(sockfd) == 1);
        }
        noro_processor_yield(sockfd, YIELD_TYPE_RECV);
    }
    assert(1==2); // unreachable
}

ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
        struct sockaddr *src_addr, socklen_t *addrlen)
{
    if (!recvfrom_f) initHook();
    struct timeval btv = {0};
    struct timeval etv = {0};
    gettimeofday(&btv, 0);
    // linfo("%d %ld.%ld\n", sockfd, btv.tv_sec, btv.tv_usec);
    while (1){
        ssize_t rv = recvfrom_f(sockfd, buf, len, flags, src_addr, addrlen);
        int eno = rv < 0 ? errno : 0;
        gettimeofday(&etv, 0);
        // linfo("%d %ld.%ld\n", sockfd, etv.tv_sec, etv.tv_usec);
        if (rv >= 0) {
            return rv;
        }
        if (eno != EINPROGRESS && eno != EAGAIN) {
            linfo("fd=%d rv=%d eno=%d err=%s\n", sockfd, rv, eno, strerror(eno));
            return rv;
        }
        noro_processor_yield(sockfd, YIELD_TYPE_RECVFROM);
    }
    assert(1==2); // unreachable
}

ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags)
{
    if (!noro_in_processor()) return recvmsg_f(sockfd, msg, flags);
    if (!recvmsg_f) initHook();
    // linfo("%d fdnb=%d\n", sockfd, fd_is_nonblocking(sockfd));
    time_t btime = time(0);
    for (int i = 0; ; i ++){
        ssize_t rv = recvmsg_f(sockfd, msg, flags);
        int eno = rv < 0 ? errno : 0;
        if (rv >= 0) {
            return rv;
        }

        fdcontext* fdctx = hookcb_get_fdcontext(sockfd);
        bool isudp = fdcontext_is_socket(fdctx) && !fdcontext_is_tcpsocket(fdctx);
        // linfo("fd=%d isudp=%d\n", sockfd, isudp);

        if (eno != EINPROGRESS && eno != EAGAIN && eno != EWOULDBLOCK) {
            linfo("fd=%d fdnb=%d rv=%d eno=%d err=%s\n", sockfd, fd_is_nonblocking(sockfd), rv, eno, strerror(eno));
            return rv;
        }
        if (isudp) {
            time_t dtime = time(0) - btime;
            if (i > 0 && dtime >= 5) {
                linfo("timedout udpfd=%d, %ld\n", sockfd, dtime);
                return 0; // timedout
            }

            int optval = 0;
            int optlen = 0;
            int rv2 = getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &optval, &optlen);
            // linfo("opt rv2=%d, len=%d val=%d\n", rv2, optlen, optval);
            int rv3 = getsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &optval, &optlen);
            // linfo("opt rv3=%d, len=%d val=%d\n", rv3, optlen, optval);

            long timeoms = optval > 0 ? optval : 5678;
            // default udp timeout 5s
            long tfds[2] = {};
            int ytypes[2] = {};
            tfds[0] = sockfd;
            ytypes[0] = YIELD_TYPE_RECVMSG;
            tfds[1] = timeoms;
            ytypes[1] = YIELD_TYPE_MSLEEP;
            noro_processor_yield_multi(YIELD_TYPE_RECVMSG_TIMEOUT, 2, tfds, ytypes);
        }else{
            noro_processor_yield(sockfd, YIELD_TYPE_RECVMSG);
        }
    }
    assert(1==2); // unreachable
}

ssize_t write(int fd, const void *buf, size_t count)
{
    if (!write_f) initHook();
    // linfo("%d %d\n", fd, count);

    while(1){
        ssize_t rv = write_f(fd, buf, count);
        int eno = rv < 0 ? errno : 0;
        if (rv >= 0) {
            return rv;
        }
        if (eno != EINPROGRESS && eno != EAGAIN) {
            linfo("fd=%d rv=%d eno=%d err=%s\n", fd, rv, eno, strerror(eno));
            return rv;
        }
        noro_processor_yield(fd, YIELD_TYPE_WRITE);
    }
    assert(1==2); // unreachable
}

ssize_t writev(int fd, const struct iovec *iov, int iovcnt)
{
    if (!writev_f) initHook();
    linfo("%d\n", fd);
    assert(1==2);
}

ssize_t send(int sockfd, const void *buf, size_t len, int flags)
{
    if (!send_f) initHook();
    // linfo("%d %d %d fdnb=%d\n", sockfd, len, flags, fd_is_nonblocking(sockfd));
    while (true) {
        ssize_t rv = send_f(sockfd, buf, len, flags);
        int eno = rv < 0 ? errno : 0;
        if (rv >= 0) {
            assert(rv == len);
            return rv;
        }

        if (eno != EINPROGRESS && eno != EAGAIN) {
            linfo("fd=%d rv=%d eno=%d err=%s\n", sockfd, rv, eno, strerror(eno));
            return rv;
        }
        noro_processor_yield(sockfd, YIELD_TYPE_SEND);
    }
    assert(1==2); // unreachable
}

ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
        const struct sockaddr *dest_addr, socklen_t addrlen)
{
    if (!sendto_f) initHook();
    linfo("%d\n", sockfd);
    assert(1==2);
    return -1;
}

ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags)
{
    if (!noro_in_processor()) return sendmsg_f(sockfd, msg, flags);
    if (!sendmsg_f) initHook();
    // linfo("%d fdnb=%d\n", sockfd, fd_is_nonblocking(sockfd));
    while (1){
        ssize_t rv = sendmsg_f(sockfd, msg, flags);
        int eno = rv < 0 ? errno : 0;
        if (rv >= 0) {
            return rv;
        }
        if (eno != EINPROGRESS && eno != EAGAIN) {
            linfo("fd=%d rv=%d eno=%d err=%s\n", sockfd, rv, eno, strerror(eno));
            return rv;
        }
        noro_processor_yield(sockfd, YIELD_TYPE_SENDMSG);
    }
    assert(1==2); // unreachable
}

// ---------------------------------------------------------------------------
// ------ for dns syscall
int __poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
    if (!noro_in_processor()) return poll_f(fds, nfds, timeout);
    if (!poll_f) initHook();
    // linfo("%d fd0=%d timeo=%d\n", nfds, fds[0].fd, timeout);
    if (timeout == 0) {  // non-block
        int rv = poll_f(fds, nfds, timeout);
        return rv;
    }
    int nevts = 0;
    for (int i = 0; i < nfds; i ++) {
        if (fds[i].events & POLLIN) { nevts += 1; }
        if (fds[i].events & POLLOUT) { nevts += 1; }
        if (fds[i].events & POLLERR) {  }
        if (POLLIN | POLLOUT == fds[i].events || POLLIN == fds[i].events || POLLOUT == fds[i].events) {
        }else{
            linfo("not supported poll event set %d %d\n", POLLIN | POLLOUT, fds[i].events);
        }
        if (fd_is_nonblocking(fds[i].fd) == 0) {
            linfo("blocking socket found %d %d\n", i, fds[i].fd);
        }
    }
    long tfds[nevts+1];
    int tytypes[nevts+1];
    for (int i = 0, j = 0; i < nfds; i ++) {
        if (fds[i].events & POLLIN) {
            tfds[j] = fds[i].fd;
            tytypes[j] = YIELD_TYPE_READ;
            j++;
        }
        if (fds[i].events & POLLOUT) {
            tfds[j] = fds[i].fd;
            tytypes[j] = YIELD_TYPE_WRITE;
            j++;
        }
    }
    int ynfds = nevts;
    if (timeout > 0) {
        tfds[ynfds] = timeout;
        tytypes[ynfds] = YIELD_TYPE_MSLEEP;
        ynfds += 1;
        // linfo("timeout set %d nfds=%d nevts=%d ynfds=%d\n", timeout, nfds, nevts, ynfds);
    }

    for (int i = 0; ; i++) {
        int rv = poll_f(fds, nfds, 0);
        int eno = rv < 0 ? errno : 0;
        // linfo("i=%d %d fd0=%d timeo=%d rv=%d\n", i, nfds, fds[0].fd, timeout, rv);
        if (rv > 0) {
            return rv;
        }
        if (timeout > 0 && i > 0) {
            return 0;
        }
        if (rv < 0) {
            if (eno != EINPROGRESS && eno != EAGAIN) {
                linfo("rv=%d eno=%d err=%s\n", rv, eno, strerror(eno));
                return rv;
            }
        }
        noro_processor_yield_multi(YIELD_TYPE_UUPOLL, ynfds, tfds, tytypes);
    }
    assert(1==2);
}
int poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
    return __poll(fds, nfds, timeout);
}

#if defined(LIBGO_SYS_Linux)
struct hostent* gethostbyname(const char* name)
{
    if (!gethostbyname_r_f) initHook();
    // linfo("%s\n", name);
    if (!noro_in_processor()) {
        static __thread struct hostent host_ = {0};
        static __thread char buf[4096] = {0};
        struct hostent* host = &host_;
        struct hostent* result = 0;
        int herror = 0;
        int rv = gethostbyname_r(name, host, buf, sizeof(buf), &result, &herror);
        if (rv == 0 && result == host) {
            return host;
        }
        return 0;
    }

    // below should be fiber vars, not thread vars
    static __thread struct hostent ghn_host = {0}; // thread local
    static __thread int ghn_errno = 0;  // thread local
    static __thread int ghn_bufsz = 0;  // thread local
    static __thread char* ghn_buf = 0;  // thread local

    memset(&ghn_host, 0, sizeof(struct hostent));
    ghn_errno = 0;
    if (ghn_bufsz == 0) {
        ghn_bufsz = 128;
        ghn_buf = calloc(1, ghn_bufsz);
    }
    struct hostent* host = &ghn_host;
    struct hostent* result = 0;

    int rv = -1;
    while (1) {
        rv = gethostbyname_r(name, host, &ghn_buf[0], ghn_bufsz, &result, &ghn_errno);
        int eno = rv;
        if (rv == ERANGE && ghn_errno == NETDB_INTERNAL) {
            ghn_bufsz *= 2;
            ghn_buf = realloc(ghn_buf, ghn_bufsz);
            continue;
        }
        if (eno != EINPROGRESS && eno != EAGAIN) {
            linfo("rv=%d eno=%d err=%s\n", rv, eno, strerror(eno));
        }
        if (rv == 0) {
            break;
        }
    }

    if (rv == 0 && host == result) {
        return host;
    }
    linfo("rv=%d eno=%d err=%s\n", rv, ghn_errno, strerror(ghn_errno));
    linfo("rv=%d eno=%d err=%s\n", rv, errno, strerror(errno));
    return 0;
}
int gethostbyname_r(const char *__restrict name,
			    struct hostent *__restrict __result_buf,
			    char *__restrict __buf, size_t __buflen,
			    struct hostent **__restrict __result,
			    int *__restrict __h_errnop)
{
    if (!gethostbyname_r_f) initHook();
    linfo("%d\n", __buflen);
    int rv = gethostbyname_r_f(name, __result_buf, __buf, __buflen, __result, __h_errnop);
    int eno = rv == 0 ? 0 : errno;
    linfo("%s rv=%d eno=%d, err=%d\n", name, rv, eno, strerror(eno));
    return rv;
}

struct hostent* gethostbyname2(const char* name, int af)
{
    linfo("%d\n", af);
    assert(1==2);
    return NULL;
}
// why this call cannot hooked?
int gethostbyname2_r(const char *name, int af,
        struct hostent *ret, char *buf, size_t buflen,
        struct hostent **result, int *h_errnop)
{
    if (!gethostbyname2_r_f) initHook();
    linfo("%s %d\n", name, af);
    assert(1==2);
}

struct hostent *gethostbyaddr(const void *addr, socklen_t len, int type)
{
    linfo("%d\n", type);
    assert(1==2);
    return NULL;

}
int gethostbyaddr_r(const void *addr, socklen_t len, int type,
        struct hostent *ret, char *buf, size_t buflen,
        struct hostent **result, int *h_errnop)
{
    if (!gethostbyaddr_r_f) initHook();
    linfo("%d\n", type);
    assert(1==2);
}
#endif

// ---------------------------------------------------------------------------

int select(int nfds, fd_set *readfds, fd_set *writefds,
           fd_set *exceptfds, struct timeval *timeout)
{
    if (!select_f) initHook();
    linfo("%d\n", nfds);
    assert(1==2);
}

unsigned int sleep(unsigned int seconds)
{
    if (!noro_in_processor()) return sleep_f(seconds);
    if (!sleep_f) initHook();
    // linfo("%d\n", seconds);

    {
        int rv = noro_processor_yield(seconds, YIELD_TYPE_SLEEP);
        return 0;
    }
}

int usleep(useconds_t usec)
{
    if (!noro_in_processor()) return usleep_f(usec);
    if (!usleep_f) initHook();
    // linfo("%d\n", usec);

    time_t btime = time(0);
    {
        int rv = noro_processor_yield(usec, YIELD_TYPE_USLEEP);
        return 0;
    }
}

int nanosleep(const struct timespec *req, struct timespec *rem)
{
    if (!noro_in_processor()) return nanosleep_f(req, rem);
    if (!nanosleep_f) initHook();
    // linfo("%d, %d\n", req->tv_sec, req->tv_nsec);
    {
        long ns = req->tv_sec * 1000000000 + req->tv_nsec;
        int rv = noro_processor_yield(ns, YIELD_TYPE_NANOSLEEP);
        return 0;
    }
}

int close(int fd)
{
    if (!close_f) initHook();
    // linfo("%d\n", fd);

    hookcb_onclose(fd);
    {
        return close_f(fd);
    }
    return 0;
}

int __close(int fd)
{
    if (!close_f) initHook();
    linfo("%d\n", fd);

    hookcb_onclose(fd);
    {
        return close_f(fd);
    }
    return 0;
}

int fcntl(int __fd, int __cmd, ...)
{
    if (!fcntl_f) initHook();
    // linfo("%d\n", __fd);

    va_list va;
    va_start(va, __cmd);

    switch (__cmd) {
        // int
        case F_DUPFD:
        case F_DUPFD_CLOEXEC:
            {
                // TODO: support FD_CLOEXEC
                int fd = va_arg(va, int);
                va_end(va);
                int newfd = fcntl_f(__fd, __cmd, fd);
                if (newfd < 0) return newfd;

                linfo("what can i do %d\n", __fd);
                return newfd;
            }

        // int
        case F_SETFD:
        case F_SETOWN:

#if defined(LIBGO_SYS_Linux)
        case F_SETSIG:
        case F_SETLEASE:
        case F_NOTIFY:
#endif

#if defined(F_SETPIPE_SZ)
        case F_SETPIPE_SZ:
#endif
            {
                int arg = va_arg(va, int);
                va_end(va);
                return fcntl_f(__fd, __cmd, arg);
            }

        // int
        case F_SETFL:
            {
                int flags = va_arg(va, int);
                va_end(va);

                linfo("what can i do %d\n", __fd);
                return fcntl_f(__fd, __cmd, flags);
            }

        // struct flock*
        case F_GETLK:
        case F_SETLK:
        case F_SETLKW:
            {
                struct flock* arg = va_arg(va, struct flock*);
                va_end(va);
                return fcntl_f(__fd, __cmd, arg);
            }

        // struct f_owner_ex*
#if defined(LIBGO_SYS_Linux)
        case F_GETOWN_EX:
        case F_SETOWN_EX:
            {
                struct f_owner_exlock* arg = va_arg(va, struct f_owner_exlock*);
                va_end(va);
                return fcntl_f(__fd, __cmd, arg);
            }
#endif

        // void
        case F_GETFL:
            {
                va_end(va);
                return fcntl_f(__fd, __cmd);
            }

        // void
        case F_GETFD:
        case F_GETOWN:

#if defined(LIBGO_SYS_Linux)
        case F_GETSIG:
        case F_GETLEASE:
#endif

#if defined(F_GETPIPE_SZ)
        case F_GETPIPE_SZ:
#endif
        default:
            {
                va_end(va);
                return fcntl_f(__fd, __cmd);
            }
    }
    assert(1==2);
}

int ioctl(int fd, unsigned long int request, ...)
{
    if (!ioctl_f) initHook();
    // linfo("%d\n", fd);

    va_list va;
    va_start(va, request);
    void* arg = va_arg(va, void*);
    va_end(va);

    if (FIONBIO == request) {
        linfo("what can i do %d\n", fd);
    }

    return ioctl_f(fd, request, arg);
}

int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen)
{
    if (!getsockopt_f) initHook();
    // linfo("%d %d %d\n", sockfd, level, optname);
    {
        int rv = getsockopt_f(sockfd, level, optname, optval, optlen);
        // linfo("%d %d %d ret=%d optlen=%d\n", sockfd, level, optname, rv, *optlen);
        return rv;
    }
}
int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen)
{
    if (!setsockopt_f) initHook();
    // linfo("%d %d %d\n", sockfd, level, optname);
    {
        int rv = setsockopt_f(sockfd, level, optname, optval, optlen);
        if (rv == 0 && level == SOL_SOCKET) {
            if (optname == SO_RCVTIMEO || optname == SO_SNDTIMEO) {
                linfo("what can i do %d\n", sockfd);
            }
        }
        return rv;
    }
    assert(1==2);
}

int dup(int oldfd)
{
    if (!dup_f) initHook();
    linfo("%d\n", oldfd);
    assert(1==2);
}
// TODO: support FD_CLOEXEC
int dup2(int oldfd, int newfd)
{
    if (!dup2_f) initHook();
    linfo("%d\n", newfd);
    assert(1==2);
}
// TODO: support FD_CLOEXEC
int dup3(int oldfd, int newfd, int flags)
{
    if (!dup3_f) initHook();
    linfo("%d\n", flags);
    assert(1==2);
}

int fclose(FILE* fp)
{
    if (!fclose_f) initHook();
    int fd = fileno(fp);
    // linfo("%p, %d\n", fp, fd);
    return fclose_f(fp);
}
FILE* fopen(const char *pathname, const char *mode)
{
    if (!fopen_f) initHook();
    // linfo("%s %s\n", pathname, mode);

    FILE* fp = fopen_f(pathname, mode);
    int fd = fileno(fp);
    hookcb_oncreate(fd, FDISFILE, 0, 0,0,0);
    // linfo("%s %s %d fdnb=%d\n", pathname, mode, fd, fd_is_nonblocking(fd));
    return fp;
}

#if defined(LIBGO_SYS_Linux)
// TODO conflict with libevent epoll_wait
int epoll_wait_wip(int epfd, struct epoll_event *events, int maxevents, int timeout)
{
    if (!epoll_wait_f) initHook();
    linfo("epfd=%d maxevents=%d timeout=%d\n", epfd, maxevents, timeout);
    {
        int rv = epoll_wait_f(epfd, events, maxevents, timeout);
        return rv;
    }
    // return libgo_epoll_wait(epfd, events, maxevents, timeout);
}

#elif defined(LIBGO_SYS_FreeBSD)
#endif

#if defined(LIBGO_SYS_Linux)
ATTRIBUTE_WEAK extern int __pipe(int pipefd[2]);
ATTRIBUTE_WEAK extern int __pipe2(int pipefd[2], int flags);
ATTRIBUTE_WEAK extern int __socket(int domain, int type, int protocol);
ATTRIBUTE_WEAK extern int __socketpair(int domain, int type, int protocol, int sv[2]);
ATTRIBUTE_WEAK extern int __connect(int fd, const struct sockaddr *addr, socklen_t addrlen);
ATTRIBUTE_WEAK extern ssize_t __read(int fd, void *buf, size_t count);
ATTRIBUTE_WEAK extern ssize_t __readv(int fd, const struct iovec *iov, int iovcnt);
ATTRIBUTE_WEAK extern ssize_t __recv(int sockfd, void *buf, size_t len, int flags);
ATTRIBUTE_WEAK extern ssize_t __recvfrom(int sockfd, void *buf, size_t len, int flags,
        struct sockaddr *src_addr, socklen_t *addrlen);
ATTRIBUTE_WEAK extern ssize_t __recvmsg(int sockfd, struct msghdr *msg, int flags);
ATTRIBUTE_WEAK extern ssize_t __write(int fd, const void *buf, size_t count);
ATTRIBUTE_WEAK extern ssize_t __writev(int fd, const struct iovec *iov, int iovcnt);
ATTRIBUTE_WEAK extern ssize_t __send(int sockfd, const void *buf, size_t len, int flags);
ATTRIBUTE_WEAK extern ssize_t __sendto(int sockfd, const void *buf, size_t len, int flags,
        const struct sockaddr *dest_addr, socklen_t addrlen);
ATTRIBUTE_WEAK extern ssize_t __sendmsg(int sockfd, const struct msghdr *msg, int flags);
ATTRIBUTE_WEAK extern int __libc_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
ATTRIBUTE_WEAK extern int __libc_poll(struct pollfd *fds, nfds_t nfds, int timeout);
ATTRIBUTE_WEAK extern int __select(int nfds, fd_set *readfds, fd_set *writefds,
                          fd_set *exceptfds, struct timeval *timeout);
ATTRIBUTE_WEAK extern unsigned int __sleep(unsigned int seconds);
ATTRIBUTE_WEAK extern int __nanosleep(const struct timespec *req, struct timespec *rem);
ATTRIBUTE_WEAK extern int __libc_close(int);
ATTRIBUTE_WEAK extern int __fcntl(int __fd, int __cmd, ...);
ATTRIBUTE_WEAK extern int __ioctl(int fd, unsigned long int request, ...);
ATTRIBUTE_WEAK extern int __getsockopt(int sockfd, int level, int optname,
        void *optval, socklen_t *optlen);
ATTRIBUTE_WEAK extern int __setsockopt(int sockfd, int level, int optname,
        const void *optval, socklen_t optlen);
ATTRIBUTE_WEAK extern int __dup(int);
ATTRIBUTE_WEAK extern int __dup2(int, int);
ATTRIBUTE_WEAK extern int __dup3(int, int, int);
ATTRIBUTE_WEAK extern int __usleep(useconds_t usec);
ATTRIBUTE_WEAK extern int __new_fclose(FILE *fp);
#if defined(LIBGO_SYS_Linux)
ATTRIBUTE_WEAK extern int __gethostbyname_r(const char *__restrict __name,
			    struct hostent *__restrict __result_buf,
			    char *__restrict __buf, size_t __buflen,
			    struct hostent **__restrict __result,
			    int *__restrict __h_errnop);
ATTRIBUTE_WEAK extern int __gethostbyname2_r(const char *name, int af,
        struct hostent *ret, char *buf, size_t buflen,
        struct hostent **result, int *h_errnop);
ATTRIBUTE_WEAK extern int __gethostbyaddr_r(const void *addr, socklen_t len, int type,
        struct hostent *ret, char *buf, size_t buflen,
        struct hostent **result, int *h_errnop);
ATTRIBUTE_WEAK extern int __epoll_wait_nocancel(int epfd, struct epoll_event *events,
        int maxevents, int timeout);
#elif defined(LIBGO_SYS_FreeBSD)
#endif

// 某些版本libc.a中没有__usleep.
ATTRIBUTE_WEAK int __usleep(useconds_t usec)
{
    struct timespec req = {usec / 1000000, usec * 1000};
    return __nanosleep(&req, NULL);
}
#endif


static int doInitHook()
{
    if (connect_f) return 0;
    connect_f = (connect_t)dlsym(RTLD_NEXT, "connect");
    linfo("%s:%d, doInitHook %p\n", __FILE__, __LINE__, connect_f);

    if (connect_f) {
        pipe_f = (pipe_t)dlsym(RTLD_NEXT, "pipe");
        socket_f = (socket_t)dlsym(RTLD_NEXT, "socket");
        socketpair_f = (socketpair_t)dlsym(RTLD_NEXT, "socketpair");
        connect_f = (connect_t)dlsym(RTLD_NEXT, "connect");
        read_f = (read_t)dlsym(RTLD_NEXT, "read");
        readv_f = (readv_t)dlsym(RTLD_NEXT, "readv");
        recv_f = (recv_t)dlsym(RTLD_NEXT, "recv");
        recvfrom_f = (recvfrom_t)dlsym(RTLD_NEXT, "recvfrom");
        recvmsg_f = (recvmsg_t)dlsym(RTLD_NEXT, "recvmsg");
        write_f = (write_t)dlsym(RTLD_NEXT, "write");
        writev_f = (writev_t)dlsym(RTLD_NEXT, "writev");
        send_f = (send_t)dlsym(RTLD_NEXT, "send");
        sendto_f = (sendto_t)dlsym(RTLD_NEXT, "sendto");
        sendmsg_f = (sendmsg_t)dlsym(RTLD_NEXT, "sendmsg");
        accept_f = (accept_t)dlsym(RTLD_NEXT, "accept");
        poll_f = (poll_t)dlsym(RTLD_NEXT, "poll");
        select_f = (select_t)dlsym(RTLD_NEXT, "select");
        sleep_f = (sleep_t)dlsym(RTLD_NEXT, "sleep");
        usleep_f = (usleep_t)dlsym(RTLD_NEXT, "usleep");
        nanosleep_f = (nanosleep_t)dlsym(RTLD_NEXT, "nanosleep");
        close_f = (close_t)dlsym(RTLD_NEXT, "close");
        fcntl_f = (fcntl_t)dlsym(RTLD_NEXT, "fcntl");
        ioctl_f = (ioctl_t)dlsym(RTLD_NEXT, "ioctl");
        getsockopt_f = (getsockopt_t)dlsym(RTLD_NEXT, "getsockopt");
        setsockopt_f = (setsockopt_t)dlsym(RTLD_NEXT, "setsockopt");
        dup_f = (dup_t)dlsym(RTLD_NEXT, "dup");
        dup2_f = (dup2_t)dlsym(RTLD_NEXT, "dup2");
        dup3_f = (dup3_t)dlsym(RTLD_NEXT, "dup3");
        fclose_f = (fclose_t)dlsym(RTLD_NEXT, "fclose");
        fopen_f = (fopen_t)dlsym(RTLD_NEXT, "fopen");
#if defined(LIBGO_SYS_Linux)
        pipe2_f = (pipe2_t)dlsym(RTLD_NEXT, "pipe2");
        gethostbyname_r_f = (gethostbyname_r_t)dlsym(RTLD_NEXT, "gethostbyname_r");
        gethostbyname2_r_f = (gethostbyname2_r_t)dlsym(RTLD_NEXT, "gethostbyname2_r");
        gethostbyaddr_r_f = (gethostbyaddr_r_t)dlsym(RTLD_NEXT, "gethostbyaddr_r");
        epoll_wait_f = (epoll_wait_t)dlsym(RTLD_NEXT, "epoll_wait");
#elif defined(LIBGO_SYS_FreeBSD)
#endif
    } else {
#if defined(LIBGO_SYS_Linux)
        pipe_f = &__pipe;
//        printf("use static hook. pipe_f=%p\n", (void*)pipe_f);
        socket_f = &__socket;
        socketpair_f = &__socketpair;
        connect_f = &__connect;
        read_f = &__read;
        readv_f = &__readv;
        recv_f = &__recv;
        recvfrom_f = &__recvfrom;
        recvmsg_f = &__recvmsg;
        write_f = &__write;
        writev_f = &__writev;
        send_f = &__send;
        sendto_f = &__sendto;
        sendmsg_f = &__sendmsg;
        accept_f = &__libc_accept;
        poll_f = &__libc_poll;
        select_f = &__select;
        sleep_f = &__sleep;
        usleep_f = &__usleep;
        nanosleep_f = &__nanosleep;
        close_f = &__libc_close;
        fcntl_f = &__fcntl;
        ioctl_f = &__ioctl;
        getsockopt_f = &__getsockopt;
        setsockopt_f = &__setsockopt;
        dup_f = &__dup;
        dup2_f = &__dup2;
        dup3_f = &__dup3;
        fclose_f = &__new_fclose;
#if defined(LIBGO_SYS_Linux)
        pipe2_f = &__pipe2;
        gethostbyname_r_f = &__gethostbyname_r;
        gethostbyname2_r_f = &__gethostbyname2_r;
        gethostbyaddr_r_f = &__gethostbyaddr_r;
        epoll_wait_f = &__epoll_wait_nocancel;
#elif defined(LIBGO_SYS_FreeBSD)
#endif
#endif
    }

    if (!pipe_f || !socket_f || !socketpair_f ||
            !connect_f || !read_f || !write_f || !readv_f || !writev_f || !send_f
            || !sendto_f || !sendmsg_f || !accept_f || !poll_f || !select_f
            || !sleep_f|| !usleep_f || !nanosleep_f || !close_f || !fcntl_f || !setsockopt_f
            || !getsockopt_f || !dup_f || !dup2_f || !fclose_f
#if defined(LIBGO_SYS_Linux)
            || !pipe2_f
            || !gethostbyname_r_f
            || !gethostbyname2_r_f
            || !gethostbyaddr_r_f
            || !epoll_wait_f
#elif defined(LIBGO_SYS_FreeBSD)
#endif
            // 老版本linux中没有dup3, 无需校验
            // || !dup3_f
            )
    {
        fprintf(stderr, "Hook syscall failed. Please don't remove libc.a when static-link.\n");
        exit(1);
    }
    return 0;
}

static int isInit = 0;
void initHook()
{
    isInit = doInitHook();
    (void)isInit;
}

#ifdef STANDALONE_HOOK
void main() {
    int a = socket(1, 1,1);
    printf("a=%d\n", a);
}
#endif

