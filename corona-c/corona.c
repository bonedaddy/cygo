
#include <unistd.h>
#include <stdio.h>
#include <assert.h>

// #include <private/pthread_support.h>
#include <coro.h>
#include <collectc/hashtable.h>
#include <collectc/array.h>

#include <corona.h>
#include <coronapriv.h>

const int dftstksz = 128*1024;
const int dftstkusz = dftstksz/8; // unit size by sizeof(void*)

typedef struct yieldinfo {
    bool seted;
    bool ismulti;
    int ytype;
    long fd;
    int nfds;
    long fds[9];
    int ytypes[9];
    // fiber* curgr;
} yieldinfo;
typedef struct machine {
    int id; // mcid
    crnqueue* ngrs; // fiber*  新任务，未分配栈
    crnmap* grs;  // # grid => fiber*
    fiber* curgr;   // 当前在执行的, 这好像得用栈结构吗？(应该不需要，fibers之间是并列关系)
    pmutex_t pkmu; // pack lock
    pcond_t pkcd;
    bool parking;
    int wantgclock;
    yieldinfo yinfo;
    struct GC_stack_base stksb;
    void* gchandle;
    void* savefrm;
    pthread_t thr;
    coro_context coctx0;
} machine;

typedef struct corona {
    int gridno; // fiber id generate
    crnmap* inuseids; // in use fiber ids, grid => nilptr
    crnmap* mths; // thno => pthread_t*
    crnmap* mchs; // thno => machine*
    bool inited;
    pmutex_t initmu;
    pcond_t initcd;
    bool stopworld;
    coro_context coctx0;
    coro_context maincoctx;

    netpoller* np;
    rtsettings* rtsets;
} corona;


///
extern void corowp_create(coro_context *ctx, coro_func coro, void *arg, void *sptr,  size_t ssze);
extern void corowp_transfer(coro_context *prev, coro_context *next);
extern void corowp_destroy (coro_context *ctx);
extern int corowp_stack_alloc (coro_stack *stack, unsigned int size);
extern void corowp_stack_free(coro_stack* stack);
extern int gettid();

static corona* gnr__ = 0;
static void(*crn_thread_createcb)(void* arg) = 0;

// 前置声明一些函数
machine* crn_machine_get(int id);
fiber* crn_machine_grget(machine* mc, int id);
void crn_machine_grfree(machine* mc, int id);
void crn_machine_signal(machine* mc);

static int crn_nxtid(corona* nr) {
    while(true) {
        int id = atomic_addint(&nr->gridno, 1);
        if (id <= 0) {
            lwarn("gridno overflow %d\n", id);
            atomic_casint(&nr->gridno, id, 0);
            continue;
        }
        if (crnmap_contains_key(nr->inuseids, id)) {
            continue;
        }
        // int rv = crnmap_add(nr->inuseids,(uintptr_t)id,(void*)(uintptr_t)1);
        // assert(rv == CC_OK);
        return id;
    }
    assert(1==2);
}

// fiber internal API
static void fiber_finalizer(void* gr) {
    fiber* f = (fiber*)gr;
    linfo("fiber dtor %p %d\n", gr, f->id);
    assert(1==2);
}
fiber* crn_fiber_new(int id, coro_func fn, void* arg) {
    fiber* gr = (fiber*)crn_gc_malloc(sizeof(fiber));
    crn_set_finalizer(gr, fiber_finalizer);
    gr->id = id;
    gr->fnproc = fn;
    gr->arg = arg;
    // gr->hcelem = invlidptr;
    extern HashTable* crnhashtable_new_uintptr();
    gr->specifics = crnhashtable_new_uintptr();
    return gr;
}
static void crn_fiber_setstate(fiber* gr, grstate state) {
    assert(sizeof(grstate) == sizeof(int));
    atomic_setint((int*)(&gr->state), (int)state);
}
static grstate crn_fiber_getstate(fiber* gr) {
    assert(sizeof(grstate) == sizeof(int));
    return atomic_getint((int*)(&gr->state));
}
// alloc stack and context
void crn_fiber_new2(fiber*gr, size_t stksz) {
    // corowp_stack_alloc(&gr->stack, stksz);
    gr->stack.sptr = crn_gc_malloc_uncollectable(stksz);
    // gr->stack.sptr = calloc(1, stksz);
    gr->stack.ssze = stksz;
    gr->mystksb.mem_base = (void*)((uintptr_t)gr->stack.sptr + stksz);
    crn_fiber_setstate(gr, runnable);
    // GC_add_roots(gr->stack.sptr, gr->stack.sptr+(gr->stack.ssze));
    // 这一句会让fnproc直接执行，但是可能需要的是创建与执行分开。原来是针对-DCORO_PTHREAD
    // corowp_create(&gr->coctx, gr->fnproc, gr->arg, gr->stack.sptr, dftstksz);
}
void crn_fiber_destroy(fiber* gr) {
    crn_set_finalizer(gr,nilptr);
    int state = crn_fiber_getstate(gr);
    assert(state != executing );
    int grid = gr->id;
    int mcid = gr->mcid;
    size_t ssze = gr->stack.ssze; // save temp value

    crn_fiber_setstate(gr,nostack);
    Array* specs = nilptr;
    int rv = hashtable_get_values(gr->specifics, &specs);
    if (rv != CC_OK && rv != 2) linfo("rv=%d sz=%d\n", rv, hashtable_size(gr->specifics));
    assert(rv == CC_OK || rv == 2);
    if (specs != nilptr) {
        for (int i = 0; i < array_size(specs); i ++) {
            void* v = nilptr;
            int rv = array_get_at(specs, i, &v);
            assert(rv == CC_OK);
            if (v != nilptr) crn_gc_free(v);
        }
        array_destroy(specs);
    }
    hashtable_destroy(gr->specifics);
    ssze += sizeof(fiber);
    // linfo("gr %d on %d, freed %d, %d\n", grid, mcid, ssze, sizeof(fiber));
    corowp_destroy(&gr->coctx);
    if (gr->stack.sptr != 0) {
        crn_gc_free2(gr->stack.sptr);
        // free(gr->stack.sptr);
    }
    void* optr = gr;
    crn_gc_free(gr); // malloc/calloc分配的不能用GC_FREE()释放
    ldebug("fiber freed %d-%d %p\n", grid, mcid, optr);
}

// frame related for some frame based integeration
static void* crn_get_frame_default() { return nilptr; }
static void crn_set_frame_default(void*f) {}
static void*(*crn_get_frame)() = crn_get_frame_default;
static void(*crn_set_frame)(void*f) = crn_set_frame_default;
void crn_set_frame_funcs(void*(*getter)(), void(*setter)(void*)) {
    crn_get_frame = getter;
    crn_set_frame = setter;
}

// 恢复到线程原始的栈
void* crn_gc_setbottom0(void*arg) {
    fiber* gr = (fiber*)arg;
    GC_set_stackbottom(gr->gchandle, gr->stksb);
    // GC_stackbottom = sb2.bottom;
    return 0;
}
// coroutine动态栈
void* crn_gc_setbottom1(void*arg) {
    fiber* gr = (fiber*)arg;

    GC_set_stackbottom(gr->gchandle, &gr->mystksb);
    // GC_stackbottom = sb1.bottom;
    return 0;
}

void crn_fiber_forward(void* arg) {
    fiber* gr = (fiber*)arg;
    // crn_call_with_alloc_lock(crn_gc_setbottom1, gr);

    gr->fnproc(gr->arg);
    crn_fiber_setstate(gr,finished);
    // linfo("coro end??? %d\n", 1);
    // TODO coro 结束，回收coro栈

    // 好像应该在外层处理
    crn_call_with_alloc_lock(crn_gc_setbottom0, gr);

    // 这个跳回ctx应该是不对的，有可能要跳到其他的gr而不是默认gr？
    corowp_transfer(&gr->coctx, gr->coctx0); // 这句要写在函数fnproc退出之前？
}

// TODO 有时候它不一定是从ctx0跳转，或者是跳转到ctx0。这几个函数都是 crn_fiber_run/resume,suspend
// 一定是从ctx0跳过来的，因为所有的fibers是由调度器发起 run/resume/suspend，而不是其中某一个fiber发起
void crn_fiber_run_first(fiber* gr) {
    // first run
    assert(atomic_getbool(&gr->isresume) == false);
    atomic_setbool(&gr->isresume, true);
    // 对-DCORO_PTHREAD来说，这句是真正开始执行
    corowp_create(&gr->coctx, crn_fiber_forward, gr, gr->stack.sptr, gr->stack.ssze);

    crn_fiber_setstate(gr,executing);
    machine* mc = crn_machine_get(gr->mcid);
    fiber* curgr = mc->curgr;
    mc->curgr = gr;
    coro_context* curcoctx = curgr == 0? gr->coctx0 : &curgr->coctx; // 暂时无用

    crn_call_with_alloc_lock(crn_gc_setbottom1, gr);
    // 对-DCORO_UCONTEXT/-DCORO_ASM等来说，这句是真正开始执行
    corowp_transfer(gr->coctx0, &gr->coctx);
    // corowp_transfer(&gr->coctx, gr->coctx0); // 这句要写在函数fnproc退出之前？
}

// 由于需要考虑线程的问题，不能直接在netpoller线程调用
void crn_fiber_resume(fiber* gr) {
    assert(gr->isresume == true);
    grstate oldst = crn_fiber_getstate(gr);
    assert(oldst != executing);
    assert(oldst != finished);
    crn_fiber_setstate(gr,executing);

    if (gr->myfrm != nilptr) crn_set_frame(gr->myfrm); // 恢复fiber的frame
    crn_call_with_alloc_lock(crn_gc_setbottom1, gr);
    // 对-DCORO_UCONTEXT/-DCORO_ASM等来说，这句是真正开始执行
    corowp_transfer(gr->coctx0, &gr->coctx);
}

void crn_fiber_run(fiber* gr) {
    // linfo("rungr %d %d\n", gr->id, gr->isresume);
    if (atomic_getbool(&gr->isresume) == true) {
        crn_fiber_resume(gr);
    } else {
        crn_fiber_run_first(gr);
    }
}

// 只改状态，不切换
void crn_fiber_resume_same_thread(fiber* gr) {
    assert(gr->isresume == true);
    assert(gr->state != executing);
    assert(gr->state != finished);

    crn_fiber_setstate(gr,runnable);
}
void crn_fiber_resume_xthread(fiber* gr) {
    if (gr->id <= 0) {
        linfo("some error occurs??? %d\n", gr->id);
        return;
        // maybe fiber already finished and deleted
        // TODO assert(gr != nilptr && gr->id > 0); // needed ???
    }
    if (crn_fiber_getstate(gr) == runnable) {
        lverb("resume but runnable %d\n", gr->id);
        crn_machine_signal(crn_machine_get(gr->mcid));
        return;
    }
    if (crn_fiber_getstate(gr) == executing) {
        ldebug("resume but executing grid=%d, mcid=%d\n", gr->id, gr->mcid);
        return;
    }
    if (crn_fiber_getstate(gr) == finished) {
        linfo("resume but finished grid=%d, mcid=%d\n", gr->id, gr->mcid);
        return;
    }

    // atomic_casint(&gr->state, waiting, runnable);
    crn_fiber_setstate(gr,runnable);
    if (gr->mcid > 100) { // TODO improve this hotfix
        linfo("mcid error %d\n", gr->mcid);
        return;
    }
    machine* mc = crn_machine_get(gr->mcid);
    crn_machine_signal(mc);
}
void crn_fiber_suspend(fiber* gr) {
    gr->myfrm = crn_get_frame();
    crn_set_frame(gr->savefrm);
    crn_fiber_setstate(gr,waiting);
    crn_call_with_alloc_lock(crn_gc_setbottom0, gr);
    corowp_transfer(&gr->coctx, gr->coctx0);
}

// machine internal API
static void machine_finalizer(void* vdmc) {
    machine* mc = (machine*)vdmc;
    linfo("machine dtor %p %d\n", mc, mc->id);
    assert(1==2); // long live object
}
static void queue_finalizer(void* mc) {
    linfo("machine dtor %p\n", mc);
}
machine* crn_machine_new(int id) {
    machine* mc = (machine*)crn_gc_malloc(sizeof(machine));
    crn_set_finalizer(mc,machine_finalizer);
    mc->id = id;
    mc->grs = crnmap_new_uintptr();
    mc->ngrs = crnqueue_new();
    crn_set_finalizer(mc->ngrs, queue_finalizer);
    corowp_create(&mc->coctx0, 0, 0, 0, 0);
    return mc;
}
void crn_machine_init_crctx(machine* mc) {
    corowp_create(&mc->coctx0, 0, 0, 0, 0);
}
static void dumphtkeys(HashTable* ht) {
    Array* arr = nilptr;
    hashtable_get_keys(ht, &arr);
    linfo("%p keysz %d keys=%p\n", ht, array_size(arr), arr);
    if (arr != nilptr && array_size(arr) > 0)
        for (int i = 0; i < array_size(arr); i++) {
            void* key = nilptr;
            array_get_at(arr, i, &key);
            linfo("i=%d key=%d\n", i, key);
        }
    if (arr != nilptr) array_destroy(arr);
}
static bool checkhtkeys(crnmap* ht) {
    Array* arr = nilptr;
    int rv = crnmap_get_keys(ht, &arr);
    int htsz = crnmap_size(ht);
    if (htsz > 0) {
        assert(arr != nilptr);
        int arsz = array_size(arr);
        if (arsz != htsz) {
            lwarn("arsz=%d, htsz=%d\n", arsz, htsz);
            assert(arsz == htsz);
        }
    }
    if (arr != nilptr) array_destroy(arr);
    return false;
}

machine* __attribute__((no_instrument_function))
crn_machine_get(int id) {
    if (id <= 0) {
        linfo("Invalid mcid %d\n", id);
        return nilptr;
        assert(id > 0);
    }
    machine* mc = 0;
    int rv = crnmap_get(gnr__->mchs, (uintptr_t)id, (void**)&mc);
    assert(rv == CC_OK || rv == CC_ERR_KEY_NOT_FOUND);
    // linfo("get mc %d=%p\n", id, mc);
    if (mc == 0 && id != 1) {
        linfo("cannot get mc %d %d\n", id, crnmap_size(gnr__->mchs));
        // dumphtkeys(gnr__->mchs);
        checkhtkeys(gnr__->mchs);
        // assert(mc != 0);
    }
    if (mc != 0) {
        // FIXME
        if (mc->id != id) {
            linfo("get mc %d=%p, found=%d, size=%d\n", id, mc, mc->id, crnmap_size(gnr__->mchs));

            machine* mc2 = 0;
            int rv = crnmap_get(gnr__->mchs, (uintptr_t)id, (void**)&mc2);
            linfo("get mc %d=%p found=%d\n", id, mc2, mc2->id);
            assert(rv == CC_OK);
        }
        assert(mc->id == id);
    }
    return mc;
}
machine* __attribute__((no_instrument_function))
    crn_machine_get_nolk(int id) {
    if (id <= 0) {
        linfo("Invalid mcid %d\n", id);
        return nilptr;
        assert(id > 0);
    }
    machine* mc = 0;
    int rv = crnmap_get_nolk(gnr__->mchs, (uintptr_t)id, (void**)&mc);
    assert(rv == CC_OK || rv == CC_ERR_KEY_NOT_FOUND);
    return mc;
}

void crn_machine_gradd(machine* mc, fiber* gr) {
    int rv = crnmap_add(mc->grs, (uintptr_t)(gr->id), gr);
    assert(rv == CC_OK);
}
fiber* __attribute__((no_instrument_function))
crn_machine_grget(machine* mc, int id) {
    fiber* gr = 0;
    int rv = crnmap_get(mc->grs, (uintptr_t)id, (void**)&gr);
    if (rv != CC_OK && rv != CC_ERR_KEY_NOT_FOUND) linfo("rv=%d\n", rv);
    assert(rv == CC_OK || rv == CC_ERR_KEY_NOT_FOUND);
    return gr;
}
fiber* crn_machine_grdel(machine* mc, int id) {
    fiber* gr = 0;
    int rv = crnmap_remove(mc->grs, (uintptr_t)id, (void**)&gr);
    assert(rv == CC_OK);
    // assert(gr != 0);
    return gr;
}
void crn_machine_grfree(machine* mc, int id) {
    fiber* gr = crn_machine_grdel(mc, id);
    assert(gr != nilptr);
    assert(gr->id == id);
    crn_fiber_destroy(gr);
}
// for mc->id == 1
fiber* crn_machine_grtake(machine* mc) {
    assert(mc->id == 1);
    fiber* gr = crnmap_takeone(mc->grs);
    return gr;
}
void crn_machine_signal(machine* mc) {
    if (mc == nilptr) {
        lwarn("Invalid mc %p\n", mc);
        return;
        assert(mc != nilptr);
    }
    pcond_signal(&mc->pkcd);
}

static __thread int gcurmcid__ = 0; // thread local
static __thread int gcurgrid__ = 0; // thread local
static __thread machine* gcurmcobj = 0; // thread local
int __attribute__((no_instrument_function))
crn_goid() { return gcurgrid__; }
fiber* __attribute__((no_instrument_function))
crn_fiber_getcur() {
    int grid = gcurgrid__;
    int mcid = gcurmcid__;
    if (mcid == 0) {
        // linfo("Not fiber, main/poller thread %d?\n", mcid);
        return 0;
    }
    machine* mcx = crn_machine_get(mcid);
    assert(mcx != nilptr);
    fiber* gr = 0;
    gr = crn_machine_grget(mcx, grid);
    if (gr == nilptr) {
        linfo("wtf why gr nil, curmc %d, curgr %d\n", mcid, grid);
    }
    // assert(gr != nilptr);
    return gr;
}
void* crn_fiber_getspec(void* spec) {
    fiber* gr = crn_fiber_getcur();
    if (gr == 0) {
        linfo("Not fiber, main/poller thread %d?\n", gettid());
        return 0;
    }
    void* v = nilptr;
    int rv = hashtable_get(gr->specifics, spec, &v);
    assert(rv == CC_OK || rv == CC_ERR_KEY_NOT_FOUND);
    return v;
}
void crn_fiber_setspec(void* spec, void* val) {
    fiber* gr = crn_fiber_getcur();
    if (gr == 0) {
        linfo("Not fiber, main/poller thread %d?\n", gettid());
        return;
    }
    void* oldv = nilptr;
    int rv = hashtable_remove(gr->specifics, spec, &oldv);
    assert(rv == CC_OK || rv == CC_ERR_KEY_NOT_FOUND);
    rv = hashtable_add(gr->specifics, spec, val);
    assert(rv == CC_OK);
    if (oldv != nilptr) {
        lwarn("Override key %p=%p\n", spec, oldv);
        crn_gc_free(oldv);
    }
}
// int crn_num_fibers() { return atomic_getint(gnr__); }
// procer internal API
static
int crn_post_sized(coro_func fn, void*arg, size_t stksz) {
    linfo("fn=%p, arg=%p %d\n", fn, arg, gnr__->gridno+1);
    machine* mc = crn_machine_get(1);
    // linfo("mc=%p, %d %p, %d\n", mc, mc->id, mc->ngrs, queue_size(mc->ngrs));
    if (mc != 0 && mc->id != 1) {
        // FIXME
        linfo("nothing mc=%p, %d %p, %d\n", mc, mc->id, mc->ngrs, crnqueue_size(mc->ngrs));
        return -1;
    }

    int id = crn_nxtid(gnr__);
    fiber* gr = crn_fiber_new(id, fn, arg);
    crn_fiber_new2(gr, stksz);
    assert(mc->ngrs != nilptr);
    int rv = crnqueue_enqueue(mc->ngrs, gr);
    assert(rv == CC_OK);
    int qsz = crnqueue_size(mc->ngrs);
    // dumphtkeys(gnr__->mchs);
    checkhtkeys(gnr__->mchs);
    if (qsz > 128) {
        linfo("wow so many ngrs %d\n", qsz);
    }
    pcond_signal(&mc->pkcd);
    return id;
}
int crn_post(coro_func fn, void*arg) {
    return crn_post_sized(fn, arg, dftstksz);
}

static
void crn_procer_setname(int id) {
    char buf[32] = {0};
    snprintf(buf, sizeof(buf), "crn_procer_%d", id);
    pthread_setname_np(pthread_self(), buf);
}
static
void* crn_procer_netpoller(void*arg) {
    machine* mc = (machine*)arg;
    gcurmcobj = mc;
    struct GC_stack_base stksb = {};
    GC_get_stack_base(&stksb);
    // GC_register_my_thread(&stksb);
    mc->gchandle = GC_get_my_stackbottom(&mc->stksb);
    ltrace("%d, %d\n", mc->id, gettid());
    crn_procer_setname(mc->id);

    netpoller_loop();

    assert(1==2);
    // cannot reachable
    for (;;) {
        sleep(600);
    }
    return nilptr;
}

static void* crn_procer1(void*arg) {
    machine* mc = (machine*)arg;
    gcurmcobj = mc;
    struct GC_stack_base stksb = {};
    GC_get_stack_base(&stksb);
    // GC_register_my_thread(&stksb);
    mc->gchandle = GC_get_my_stackbottom(&mc->stksb);
    // linfo("%d %d\n", mc->id, gettid());
    crn_procer_setname(mc->id);
    gnr__->inited = true;
    pcond_signal(&gnr__->initcd);

    for (;;) {
        int newgn = crnqueue_size(mc->ngrs);
        if (newgn == 0) {
            mc->parking = true;
            pcond_wait(&mc->pkcd, &mc->pkmu);
            mc->parking = false;
        }

        // linfo("newgr %d\n", newgn);
        for (newgn = 0;;) {
            fiber* newgr = nilptr;
            int rv = crnqueue_poll(mc->ngrs, (void**)&newgr);
            assert(rv == CC_OK || rv == CC_ERR_OUT_OF_RANGE);
            if (newgr == nilptr) {
                break;
            }
            // dumphtkeys(gnr__->mchs);
            crn_machine_gradd(mc, newgr);
            newgn ++;
            // dumphtkeys(gnr__->mchs);
            checkhtkeys(gnr__->mchs);
        }
        if (newgn == 0) continue;

        // TODO 应该放到schedule中
        // find free machine and runnable fiber
        Array* arr2 = nilptr;
        int rv = crnmap_get_keys(gnr__->mchs, &arr2);
        assert(rv == CC_OK);
        int arr2sz = arr2 == nilptr ? 0 : array_size(arr2);
        for (;arr2 != nilptr;) {
            fiber* gr = crn_machine_grtake(mc);
            if (gr == nilptr) {
                linfo("why nil %d %d\n", crnmap_size(mc->grs), arr2sz);
                break;
            }

            machine* mct = 0;

            for (int j = 0; j < arr2sz; j++) {
                int rdidx = abs(rand()) % arr2sz;
                void* key = nilptr;
                int rv = array_get_at(arr2, rdidx, &key);
                if (rv != CC_OK) linfo("rv=%d %d\n", rv, arr2sz);
                assert(rv == CC_OK);
                if ((uintptr_t)key <= 2) continue;

                // linfo("checking machine %d/%d %d\n", j, array_size(arr2), key);
                mct = crn_machine_get((int)(uintptr_t)key);
                if (mct->parking) {
                    // linfo("got a packing machine %d <- gr %d\n", mct->id, gr->id);
                    break;
                }
                if (gr->id == 1) { break; }
                mct = nilptr;
            }
            if (mct == nilptr) {
                ldebug("no enough mc? %d\n", gr->id);
                // try select random one?
                // 暂时先放回全局队列中吧
                crn_machine_gradd(mc, gr);
                break;
            }
            if (mct != nilptr) {
                lverb("move %d to %d\n", gr->id, mct->id);
                crn_machine_gradd(mct, gr);
                crn_machine_signal(mct);
                break;
            }
        }
        if (arr2 != nilptr) { array_destroy(arr2); }
    }
}

// schedue functions
static
fiber* crn_sched_get_glob_one(machine*mc) {
    // linfo("try get glob %d\n", mc->id);
    machine* mc1 = crn_machine_get(1);
    if (mc1 == 0) return 0;

    fiber* gr = crn_machine_grtake(mc1);
    if (gr != 0) {
        ldebug("got %d glob task on %d\n", gr->id, mc->id);
    }
    return gr;
}
// prepare new task
static bool crn_fiber_runnable_filter(void* tmp) {
    return crn_fiber_getstate((fiber*)tmp) == runnable;
}
static
fiber* crn_sched_get_ready_one(machine*mc) {
    fiber* rungr = (fiber*)crnmap_findone(mc->grs, crn_fiber_runnable_filter);
    return rungr;
}
static
void crn_sched_run_one(machine* mc, fiber* rungr) {
    mc->curgr = rungr;
    gcurgrid__ = rungr->id;
    rungr->coctx0 = &mc->coctx0;
    rungr->stksb = &mc->stksb;
    rungr->gchandle = mc->gchandle;
    rungr->mcid = mc->id;
    rungr->savefrm = mc->savefrm;
    crn_fiber_run(rungr);
    // crn_call_with_alloc_lock(crn_gc_setbottom0, rungr);
    gcurgrid__ = 0;
    mc->curgr = nilptr;

    int curst = crn_fiber_getstate(rungr);
    if (curst == waiting) {
        // 在这才解锁，用于确保rungr状态完全切换完成
        if (rungr->hclock != nilptr) {
            pmutex_t* l = rungr->hclock;
            rungr->hclock = nilptr;
            pmutex_unlock(l);
            // linfo("unlocked chan lock %p on %d\n", l, rungr->id);
        }
    } else if (curst == finished) {
        // linfo("finished gr %d\n", rungr->id);
        crn_machine_grfree(mc, rungr->id);
    }else{
        // is break from fiber when not waiting or finished an error? not
        linfo("break from gr %d, state=%d pkreason=%d(%s)\n",
              rungr->id, curst, rungr->pkreason, yield_type_name(rungr->pkreason));
    }
}

// make sure fiber suspended then push to netpoller
static
void crn_procer_yield_commit(machine* mc, fiber* gr) {
    yieldinfo* yinfo = &mc->yinfo;
    if (yinfo->seted == false) {
        return;
    }

    if (yinfo->ismulti == true) {
        for (int i = 0; i < yinfo->nfds; i++) {
            netpoller_yieldfd(yinfo->fds[i], yinfo->ytypes[i], gr);
        }
    }else{
        netpoller_yieldfd(yinfo->fd, yinfo->ytype, gr);
    }
    memset(yinfo, 0, sizeof(yieldinfo));
}
static void* crn_procerx(void*arg) {
    machine* mc = (machine*)arg;
    gcurmcobj = mc;
    GC_get_stack_base(&mc->stksb);
    // GC_register_my_thread(&mc->stksb);
    mc->gchandle = GC_get_my_stackbottom(&mc->stksb);
    if (crn_thread_createcb != 0) {
        crn_thread_createcb((void*)(uintptr_t)mc->id);
    }

    // linfo("%d %d\n", mc->id, gettid());
    crn_procer_setname(mc->id);
    gcurmcid__ = mc->id;

    mc->savefrm = crn_get_frame();
    for (;;) {
        // check global queue
        bool stopworld = atomic_getbool(&gnr__->stopworld);
        if (!stopworld) {
            fiber* rungr = nilptr;
            rungr = crn_sched_get_ready_one(mc);
            if (rungr != 0) {
                crn_sched_run_one(mc, rungr);
                crn_procer_yield_commit(mc, rungr);
                continue;
            }
            if (rand() % 3 == 2) {
                rungr = crn_sched_get_glob_one(mc);
                if (rungr != 0) {
                    crn_machine_gradd(mc, rungr);
                    continue;
                }
            }
        }
        {
            if (stopworld) {
                linfo("no task, parking... %d by %d\n", mc->id, stopworld);
            }
            // linfo("no task, parking... %d by %d\n", mc->id, stopworld);
            int rv = atomic_casbool(&mc->parking, false, true);
            assert(rv == true);
            pcond_wait(&mc->pkcd, &mc->pkmu);
            rv = atomic_casbool(&mc->parking, true, false);
            assert(rv == true);
        }
        // sleep(3);
    }
}

bool __attribute__((no_instrument_function))
crn_in_procer() { return gcurmcid__ != 0; }

int crn_procer_yield(long fd, int ytype) {
    // check是否是procer线程
    if (gcurmcid__ == 0) {
        linfo("maybe not procer thread %ld %d\n", fd, ytype)
            // 应该不是 procer线程
            return -1;
    }
    // linfo("yield fd=%ld, ytype=%s(%d), mcid=%d, grid=%d\n", fd, yield_type_name(ytype), ytype, gcurmcid__, gcurgrid__);
    machine* mc = crn_machine_get(gcurmcid__);
    assert(mc != nilptr);
    fiber* gr = crn_fiber_getcur();
    assert(gr != nilptr);
    gr->pkreason = ytype;
    if (ytype == YIELD_TYPE_CHAN_RECV || ytype == YIELD_TYPE_CHAN_SEND ||
        ytype == YIELD_TYPE_CHAN_SELECT || ytype == YIELD_TYPE_CHAN_SELECT_NOCASE) {
    } else {
        mc->yinfo.seted = true;
        mc->yinfo.ismulti = false;
        mc->yinfo.ytype = ytype;
        mc->yinfo.fd = fd;
        // netpoller_yieldfd(fd, ytype, gr);
    }
    crn_fiber_suspend(gr);
    return 0;
}
int crn_procer_yield_multi(int ytype, int nfds, long fds[], int ytypes[]) {
    // check是否是procer线程
    if (gcurmcid__ == 0) {
        linfo("maybe not procer thread %d %d\n", nfds, ytype)
            // 应该不是 procer线程
            return -1;
    }
    // linfo("yield %d ytype=%s(%d), mcid=%d, grid=%d\n", nfds, yield_type_name(ytype), ytype, gcurmcid__, gcurgrid__);
    machine* mc = crn_machine_get(gcurmcid__);
    assert(mc != nilptr);
    fiber* gr = crn_fiber_getcur();
    assert(gr != nilptr);
    gr->pkreason = ytype;

    mc->yinfo.seted = true;
    mc->yinfo.ismulti = true;
    mc->yinfo.ytype = ytype;
    mc->yinfo.nfds = nfds;

    for (int i = 0; i < nfds; i ++) {
        long fd = fds[i];
        int ytype = ytypes[i];
        if (ytype == YIELD_TYPE_CHAN_RECV || ytype == YIELD_TYPE_CHAN_SEND ||
            ytype == YIELD_TYPE_CHAN_SELECT || ytype == YIELD_TYPE_CHAN_SELECT_NOCASE) {
            assert(1==2);
        } else {
            mc->yinfo.fds[i] = fds[i];
            mc->yinfo.ytypes[i] = ytypes[i];
            // netpoller_yieldfd(fd, ytype, gr);
        }
    }
    crn_fiber_suspend(gr);
    return 0;
}
void crn_procer_resume_one(void* gr_, int ytype, int grid, int mcid) {
    fiber* gr = (fiber*)gr_;
    fiber* curgr = crn_fiber_getcur();
    ytype = (ytype == 0 ? gr->pkreason : ytype);
    machine* mc = crn_machine_get(mcid);
    assert(mc != nilptr);
    fiber* gr2 = crn_machine_grget(mc, grid);
    if (gr2 != gr) {
        ldebug("Invalid gr %p=%p curid=%d %d\n", gr, gr2, grid);
        return;
    }
    // linfo("netpoller notify, ytype=%d %p, id=%d\n", ytype, gr, gr->id);
    if (grid != gr->id || mcid != gr->mcid) {
        // sometimes resume from netpoller is too late, gr already gone
        ldebug("Invalid gr %p curid=%d %d\n", gr, gr->id, grid);
        return;
    }
    if (curgr != nilptr && gr->mcid == curgr->mcid) {
        crn_fiber_resume_same_thread(gr);
        // 相同machine线程的情况，要主动出让执行权。
        // 另外考虑是否只针对chan send/recv。
        crn_procer_yield(1001, YIELD_TYPE_NANOSLEEP);
    }else {
        crn_fiber_resume_xthread(gr);
    }
}
void crn_sched() {
    crn_procer_yield(1000, YIELD_TYPE_NANOSLEEP);
}

static
int __attribute__((no_instrument_function))
hashtable_cmp_int(const void *key1, const void *key2) {
    if (key1 == key2) return 0;
    else if((uintptr_t)(key1) > (uintptr_t)(key2)) return 1;
    else return -1;
}

corona* crn_get() { return gnr__;}

// FIXME 导致过早回收？？？
// this callback function run on stoped world
// dont alloc memory on heap in this function, or maybe hang for malloc related deadlock
static
void crn_gc_push_other_roots1() {
    corona* nr = crn_get();
    if (nr == nilptr) return;
    if (nr != nilptr && nr->inited == false) return;
    // if (gcurmcid__ != 0) return;

    // linfo2("tid=%d mcid=%d\n", gettid(), gcurmcid__);
    int grcnt = 0;
    int executing_cnt = 0;
    for (int i = 3; i <= 5; i++ ) {
        machine* mc = crn_machine_get(i);
        // linfo2("%d %p\n", i, mc);
        HashTable* htobj = (HashTable*)mc->grs;
        HashTableIter htiter;
        hashtable_iter_init(&htiter, htobj);
        TableEntry *entry = nilptr;
        while (hashtable_iter_next(&htiter, &entry) != CC_ITER_END) {
            fiber* gr = entry->value;
            executing_cnt += gr->state == executing ? 1 : 0;

            void* stktop = gr->stack.sptr;
            void* stkbtm = ((void*)((uintptr_t)gr->stack.sptr) + gr->stack.ssze);
            ssize_t stksz = (ssize_t)stkbtm - (ssize_t)stktop;

            // linfo2("i/j=%d/%d id=%d state=%d(%s) pkrs=%d(%s) gr=%p\n",
            //       i, j, gr->id, (int)gr->state, grstate2str(gr->state),
            //       gr->pkreason, yield_type_name(gr->pkreason), gr);
            // linfo2("stkinfo top=%p btm=%p szo=%ld szr=%ld\n", stktop, stkbtm, gr->stack.ssze, stksz);
            GC_remove_roots(gr->stack.sptr, gr->stack.sptr + 1);
            // GC_add_roots(gr->stack.sptr, ((void*)((uintptr_t)gr->stack.sptr) + 130000));
            if (gr->state != executing) {
                GC_push_all_eager(stktop, stkbtm);
            }else
            if (gr->state == executing) {
                // GC_remove_roots(stktop, (void*)((uintptr_t)stkbtm + 1)); // assert crash
            }
        }
    }
    // linfo2("tid=%d mchs=%d grs=%d runcnt=%d\n", gettid(), 3, grcnt, executing_cnt);
}
static void crn_gc_push_other_roots2() {
    corona* nr = crn_get();
    if (nr == nilptr || (nr != nilptr && nr->inited == false)) return;
    // if (gcurmcid__ != 0) return;

    linfo2("tid=%d mcid=%d\n", gettid(), gcurmcid__);
}

static bool crn_machine_all_parking(int nochkid) {
    bool allpark = true;
    for (int i = 3; i <= 5; i++ ) {
        if (i == nochkid) { continue; }
        machine* mc = crn_machine_get_nolk(i);
        // linfo2("mcid=%d mc=%p pk=%d\n", i, mc, mc->parking);
        if (atomic_getbool(&mc->parking) == true) {
            continue;
        }
        int lkcnt = atomic_getint(&mc->wantgclock);
        if (lkcnt > 0) {
            linfo2("wantgclock, as safepoint %d %d\n", i, lkcnt);
        }else{
            allpark = false;
            break;
        }
    }
    return allpark;
}
static void crn_gc_start_proc() {
    // linfo2("gc start %d\n", gettid());
    corona* nr = crn_get();
    if (nr == nilptr || (nr != nilptr && nr->inited == false)) return;
    int rv = atomic_casbool(&nr->stopworld,false,true);
    if (rv == false) {
    }
    assert(rv == true);
    int nochkid = gcurmcid__;
    if (nochkid != 0) {
        // linfo2("wow machine thread gc %d\n", nochkid);
    }

    // unlock here maybe cause another collect enter
    // GC_alloc_unlock();
    for (int i = 3; i <= 5; i++ ) {
        if (i == nochkid) { continue; }
        machine* mc = crn_machine_get_nolk(i);
        if (mc == 0) {
            linfo2("mchssz = %d\n", crnmap_size(nr->mchs));
            assert(mc != nilptr);
            break;
        }
    }

    time_t btime = time(0);
    for(int i = 0;; i++) {
        bool allpark = crn_machine_all_parking(nochkid);
        if (allpark) {
            // linfo2("allpark good %d\n", allpark);
        }else{
            if (i == 0) {
                linfo2("not allpark bad %d\n", allpark);
            }
        }
        time_t nowt = time(0);
        if (!allpark) {
            /* if (i > 9) { */
            /*     linfo2("go on gc anyway %d\n", i); */
            /*     break; */
            /* } */
            if (nowt-btime >= 3) {
                // maybe in calling GC_malloc, which has mutex lock
                linfo2("wait too long for all parking %d %d %d\n", nochkid, nowt-btime, i);
                assert(1==2);
                break;
            }
            extern int (*usleep_f)(useconds_t usec);
            usleep_f(1000*10); // 10ms
            continue;
        }

        if (allpark) {
            if (i > 0) {
                linfo2("finally waited parking %d %d\n", i, nowt-btime);
            }
            break;
        }
        // break;
    }

    for (int i = 3; i <= 5; i++ ) {
    }
    // GC_alloc_lock();
}
static void crn_gc_stop_proc() {
    // linfo2("gc finished %d\n", gettid());
    corona* nr = crn_get();
    if (nr == nilptr || (nr != nilptr && nr->inited == false)) return;
    int nochkid = gcurmcid__;

    // GC_alloc_unlock();
    for (int i = 3; i <= 5; i++ ) {
        if (i == nochkid) { continue; }
        machine* mc = crn_machine_get_nolk(i);
        if (mc == 0) {
            linfo2("mchssz = %d\n", crnmap_size(gnr__->mchs));
            assert(mc != nilptr);
            break;
        }
    }
    // GC_alloc_lock();

    int rv = atomic_casbool(&nr->stopworld,true,false);
    assert(rv == true);
}
static
void crn_gc_on_collection_event2(GC_EventType evty) {
    // linfo2("%d=%s mcid=%d\n", evty, crn_gc_event_name(evty), gcurmcid__);
    corona* nr = crn_get();
    if (nr == nilptr || (nr != nilptr && nr->inited == false)) return;

    switch (evty) {
    case GC_EVENT_END:
        crn_gc_stop_proc();
        break;
    }
}

static
void crn_gc_on_collection_event(GC_EventType evty) {
    // linfo2("%d=%s mcid=%d\n", evty, crn_gc_event_name(evty), gcurmcid__);
    corona* nr = crn_get();
    if (nr == nilptr || (nr != nilptr && nr->inited == false)) return;

    // GC_alloc_unlock();
    if (evty == GC_EVENT_PRE_STOP_WORLD) {
        linfo2("%d %d\n", evty, gettid());
        for (int i = 3; i <= 5; i++ ) {
            machine* mc = crn_machine_get(i);
            if (mc == 0) {
                linfo2("mchssz = %d\n", crnmap_size(nr->mchs));
                break;
            }
        }

        bool allpark = true;

        /* for (;;) { */
        /*     for (int i = 3; i <= 5; i++ ) { */
        /*         machine* mc = crn_machine_get(i); */
        /*         linfo2("%d %p %d\n", i, mc, mc->parking); */
        /*         if (mc->parking == false) { */
        /*             allpark = false; */
        /*             break; */
        /*         } */
        /*     } */
        /*     if (!allpark) { */
        /*         usleep(100*1000); */
        /*     }else{ */
        /*         break; */
        /*     } */
        /* } */
    } else if (evty == GC_EVENT_POST_STOP_WORLD) {
        // here call is equal to GC_set_push_other_roots() callback call
        // seems not equal: when call push other roots1 here, seems gc collectc fine
        // but if use GC_set_push_other_roots, seems gc collected shouldn't collect memory
        // crn_gc_push_other_roots1();

        for (int i = 3; i <= 5; i++ ) {
            machine* mc = crn_machine_get(i);
            if (mc == 0) {
                linfo2("mchssz = %d\n", crnmap_size(gnr__->mchs));
                break;
            }
            if (mc->parking == true) {
                continue;
            }
            linfo2("needed reset stkbtm? %d %p\n", i, mc->curgr);
            if (mc->curgr == nilptr) {
                continue;
            }
            linfo2("needed reset stkbtm? %d %p\n", i, mc->curgr);
        }
    } else if (evty == GC_EVENT_PRE_START_WORLD) {
        for (int i = 3; i <= 5; i++ ) {
            machine* mc = crn_machine_get(i);
            if (mc == 0) {
                linfo2("mchssz = %d\n", crnmap_size(gnr__->mchs));
                break;
            }
        }
    } else if (evty == GC_EVENT_END) {
        linfo2("gc finished %d\n", 0);
    }
    // GC_alloc_lock();
}
static
void crn_gc_on_thread_event(GC_EventType evty, void* thid) {
    linfo2("%d=%s %p\n", evty, crn_gc_event_name(evty), thid);
    corona* nr = crn_get();
    if (nr == nilptr || (nr != nilptr && nr->inited == false)) return;
}

void crn_pre_gclock_proc() {
    // linfo("hohoo %d\n", gcurmcid__);
    if (gcurmcobj == nilptr) return;

    for(int i = 0; ; i++) {
        int v = atomic_getint(&gcurmcobj->wantgclock);
        assert(v >= 0);
        int rv = atomic_casint(&gcurmcobj->wantgclock, v, v+1);
        if (rv == false && i > 9) {
            assert(rv == true);
        }
        if (rv == true) break;
    }

}
void crn_post_gclock_proc() {
    // linfo("hohoo %d\n", gcurmcid__);
    if (gcurmcobj == nilptr) return;

    for(int i = 0; ; i++) {
        int v = atomic_getint(&gcurmcobj->wantgclock);
        assert(v > 0);
        int rv = atomic_casint(&gcurmcobj->wantgclock, v, v-1);
        if (rv == false && i > 9) {
            assert(rv == true);
        }
        if (rv == true) break;
    }

}

bool gcinited = false;
static
void crn_init_intern() {
    srand(time(0));
    crn_loglvl_forenv();
    extern void (*crn_pre_gclock_fn)();
    extern void (*crn_post_gclock_fn)();
    crn_pre_gclock_fn = crn_pre_gclock_proc;
    crn_post_gclock_fn = crn_post_gclock_proc;

    // GC_set_find_leak(1);
    GC_set_finalize_on_demand(0);
    GC_set_free_space_divisor(50); // default 3
    GC_set_dont_precollect(1);
    GC_set_dont_expand(1);
    // GC_enable_incremental();
    // GC_set_rate(5);
    // GC_set_all_interior_pointers(1);
    // TODO
    // GC_set_push_other_roots(crn_gc_push_other_roots1); // run in which threads?
    extern void GC_set_start_callback(void(*fn)());
    GC_set_start_callback(crn_gc_start_proc);
    GC_set_on_collection_event(crn_gc_on_collection_event2);
    // GC_set_on_thread_event(crn_gc_on_thread_event);
    GC_INIT();
    GC_allow_register_threads();
    // linfo("main thread registered: %d\n", GC_thread_is_registered()); // yes
    // linfo("gcfreq=%d\n", GC_get_full_freq()); // 19
    // GC_set_full_freq(5);
    // struct GC_stack_base stksb;
    // GC_get_stack_base(&stksb);
    // GC_register_my_thread(&stksb);
    gcinited = true;

    // log init here
    netpoller_use_threads();
}

corona* crn_new() {
    if (gnr__) {
        linfo("wtf...%d\n",1);
        return gnr__;
    }
    crn_init_intern();

    corona* nr = (corona*)crn_gc_malloc(sizeof(corona));
    nr->mths = crnmap_new_uintptr();
    nr->mchs = crnmap_new_uintptr();

    nr->gridno = 1;
    nr->inuseids = crnmap_new_uintptr();
    nr->np = netpoller_new();

    assert(gnr__ == nilptr);
    gnr__ = nr;
    return nr;
}


// 开启的总线程数除了以下，还有libgc的线程（3个？）
void crn_init(corona* nr) {
    // GC_disable();
    for (int i = 5; i > 0; i --) {
        machine* mc = crn_machine_new(i);
        pthread_t* t = &mc->thr;
        int rv = crnmap_add(nr->mths, (uintptr_t)i, t);
        assert(rv == CC_OK);
        rv = crnmap_add(nr->mchs, (uintptr_t)i, mc);
        assert(rv == CC_OK);
        if (i == 1) {
            pthread_create(t, 0, crn_procer1, (void*)mc);
        } else if (i == 2) {
            pthread_create(t, 0, crn_procer_netpoller, (void*)mc);
        } else {
            pthread_create(t, 0, crn_procerx, (void*)mc);
        }
    }
    // GC_enable();
}
void crn_destroy(corona* lnr) {
    lnr = 0;
    gnr__ = 0;
}
void crn_wait_init_done(corona* nr) {
    ltrace("crninited? %d\n", nr->inited);
    if (nr->inited) { return; }
    pcond_wait(&nr->initcd, &nr->initmu);
}

corona* crn_init_and_wait_done() {
    corona* nr = crn_new();
    assert(nr != nilptr);
    if (!nr->inited) {
        crn_init(nr);
        linfo("wait signal...%d %d\n", nr->inited, gettid());
        crn_wait_init_done(nr);
        linfo("wait signal done %d %d\n", nr->inited, gettid());
        // dumphtkeys(nr->mchs);
        checkhtkeys(nr->mchs);
    }
    return nr;
}

void* crn_set_thread_createcb(void(*fn)(void*arg), void* arg) {
    void(*oldfn)(void*arg) = crn_thread_createcb;
    crn_thread_createcb = fn;
    return (void*)oldfn;
}
