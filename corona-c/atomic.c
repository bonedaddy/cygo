#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>

#include "atomic.h"

int atomic_addint(int* v, int delta) {
    _Atomic(int)* vp = v;
    return atomic_fetch_add(vp, delta);
}
uint32_t atomic_addu32(uint32_t* v, uint32_t delta) {
    _Atomic(uint32_t)* vp = v;
    return atomic_fetch_add(vp, delta);
}
int32_t atomic_addi32(int32_t* v, int32_t delta) {
    _Atomic(int32_t)* vp = v;
    return atomic_fetch_add(vp, delta);
}
uint64_t atomic_addu64(uint64_t* v, uint64_t delta) {
    // fix: error: address argument to atomic operation must be a pointer to _Atomic type
    _Atomic(uint64_t)* vp = v;
    return atomic_fetch_add(vp, delta);
}
int64_t atomic_addi64(int64_t* v, int64_t delta) {
    _Atomic(int64_t)* vp = v;
    return atomic_fetch_add(vp, delta);
}

bool atomic_notbool(bool* v) {
    _Atomic(bool)* vp = v;
    return atomic_compare_exchange_strong(vp, vp, !*vp);
}

bool atomic_casbool(bool* v, bool oldval, bool newval) {
    _Atomic(bool)* vp = v;
    return atomic_compare_exchange_strong(vp, &oldval, newval);
}
bool atomic_casint(int* v, int oldval, int newval) {
    _Atomic(int)* vp = v;
    return atomic_compare_exchange_strong(vp, &oldval, newval);
}
bool atomic_casu32(uint32_t* v, uint32_t oldval, uint32_t newval) {
    _Atomic(uint32_t)* vp = v;
    return atomic_compare_exchange_strong(vp, &oldval, newval);
}
bool atomic_casu64(uint64_t* v, uint64_t oldval, uint64_t newval) {
    _Atomic(uint64_t)* vp = v;
    return atomic_compare_exchange_strong(vp, &oldval, newval);
}
bool atomic_casuptr(uintptr_t* v, uintptr_t oldval, uintptr_t newval) {
    _Atomic(uintptr_t)* vp = v;
    return atomic_compare_exchange_strong(vp, &oldval, newval);
}
bool atomic_casptr(void** v, void* oldval, void* newval) {
    _Atomic(void*)* vp = v;
    return atomic_compare_exchange_strong(vp, &oldval, newval);
}
bool atomic_casi32(int32_t* v, int32_t oldval, int32_t newval) {
    _Atomic(int32_t)* vp = v;
    return atomic_compare_exchange_strong(vp, &oldval, newval);
}
bool atomic_casi64(int64_t* v, int64_t oldval, int64_t newval) {
    _Atomic(int64_t)* vp = v;
    return atomic_compare_exchange_strong(vp, &oldval, newval);
}

int atomic_swapint(int* v0, int newval) {
    _Atomic(int)* vp = v0;
    return atomic_exchange(vp, newval);
}
uint32_t atomic_swapu32(uint32_t* v0, uint32_t newval) {
    _Atomic(uint32_t)* vp = v0;
    return atomic_exchange(vp, newval);
}
uint64_t atomic_swapu64(uint64_t* v0, uint64_t newval) {
    _Atomic(uint64_t)* vp = v0;
    return atomic_exchange(vp, newval);
}
uintptr_t atomic_swapuptr(uintptr_t* v0, uintptr_t newval) {
    _Atomic(uintptr_t)* vp = v0;
    return atomic_exchange(vp, newval);
}
void* atomic_swapptr(void** v0, void* newval) {
    _Atomic(void*)* vp = v0;
    return atomic_exchange(vp, newval);
}
int32_t atomic_swapi32(int32_t* v0, int32_t newval) {
    _Atomic(int32_t)* vp = v0;
    return atomic_exchange(vp, newval);
}
int64_t atomic_swapi64(int64_t* v0, int64_t newval) {
    _Atomic(int64_t)* vp = v0;
    return atomic_exchange(vp, newval);
}

void atomic_setbool(bool* v, bool val) {
    _Atomic(bool)* vp = v;
    atomic_store(vp, val);
}
void atomic_setint(int* v, int val) {
    _Atomic(int)* vp = v;
    atomic_store(vp, val);
}
void atomic_setu32(uint32_t* v, uint32_t val) {
    _Atomic(uint32_t)* vp = v;
    atomic_store(vp, val);
}
void atomic_setu64(uint64_t* v, uint64_t val) {
    _Atomic(uint64_t)* vp = v;
    atomic_store(vp, val);
}
void atomic_setuptr(uintptr_t* v, uintptr_t val) {
    _Atomic(uintptr_t)* vp = v;
    atomic_store(vp, val);
}
void atomic_setptr(void** v, void* val) {
    _Atomic(void*)* vp = v;
    atomic_store(vp, val);
}
void atomic_seti32(int32_t* v, int32_t val) {
    _Atomic(int32_t)* vp = v;
    atomic_store(vp, val);
}
void atomic_seti64(int64_t* v, int64_t val) {
    _Atomic(int64_t)* vp = v;
    atomic_store(vp, val);
}

bool atomic_getbool(bool* v) {
    _Atomic(bool)* vp = v;
    return atomic_load(vp);
}
int atomic_getint(int* v) {
    _Atomic(int)* vp = v;
    return atomic_load(vp);
}
uint32_t atomic_getu32(uint32_t* v) {
    _Atomic(uint32_t)* vp = v;
    return atomic_load(vp);
}
uint64_t atomic_getu64(uint64_t* v) {
    _Atomic(uint64_t)* vp = v;
    return atomic_load(vp);
}
uintptr_t atomic_getuptr(uintptr_t* v) {
    _Atomic(uintptr_t)* vp = v;
    return atomic_load(vp);
}
void* atomic_getptr(void** v) {
    _Atomic(void*)* vp = v;
    return atomic_load(vp);
}
int32_t atomic_geti32(int32_t* v) {
    _Atomic(int32_t)* vp = v;
    return atomic_load(vp);
}
int64_t atomic_geti64(int64_t* v) {
    _Atomic(int64_t)* vp = v;
    return atomic_load(vp);
}

