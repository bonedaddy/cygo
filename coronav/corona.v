module coronav

import time

// need use crn_gc_malloc/crn_gc_realloc in v build, not just GC_malloc/GC_realloc
// this can help avoid gc hang problem
// and cxrt_init_env need run more eary

#flag -I@VMOD/cxrt/corona-c -I@VMOD/cxrt/src -I@VMOD/cxrt/3rdparty/cltc/src
#flag -L@VMOD/cxrt/bysrc
#flag -L@VMOD/cxrt/cltc/lib
#flag -lcxrt -lcrn
// #flag -levent -levent_pthreads
#flag -lgc -lpthread

#include "corona_util.h"
#include "crnpub.h"
#include "hookcb.h"
#include "cxrtbase.h"

// struct C.corona{}

pub struct Corona {
mut:
    h *C.corona
	useit int
}

fn C.crn_init_and_wait_done() *C.corona
fn C.crn_goid() int
fn C.crn_post(f voidptr, arg voidptr)
fn C.crn_lock_osthread()

fn C.hookcb_oncreate()
fn C.gettid() int
// fn C.sleep(s int) int
fn C.crn_get_stats()


pub fn lock_osthread() { C.crn_lock_osthread() }
pub fn add_custom_fd(fd int) { C.hookcb_oncreate(fd, 4, true, 0, 0, 0) }

pub fn new() &Corona{
	crn := &Corona{0,0}
	h := crn_init_and_wait_done()
	println('h=$h')
	return crn
}

pub fn (crn mut Corona) post(f fn (voidptr), arg voidptr) {
	crn.useit++
	C.crn_post(f, arg)
}

pub fn (crn mut Corona) goid() int {
	crn.useit++
	return C.crn_goid()
}

pub fn goid() int { return C.crn_goid() }

pub fn gettid() int { return C.gettid() }
pub fn sleep(s int) { C.sleep(s) }

pub fn post(f fn(voidptr), arg voidptr) {
	C.crn_post(f, arg)
}

pub struct InnerStats {
mut:
    mch_totcnt   int
	mch_actcnt int
	fiber_totcnt int
	fiber_actcnt int
	fiber_totmem int
	maxstksz int
}

pub fn get_stats() &InnerStats {
	st := &InnerStats{}
	C.crn_get_stats(st)
	return st
}

// redefine in vlip/builtin/bdwgc.v
// fn C.cxrt_init_env()

pub fn init_env() {
	//
	C.cxrt_init_env()
}

pub fn forever() {
	for { time.sleep(500) }
}
pub fn fortimer(timeoutms int) {
	assert 1==2
}

