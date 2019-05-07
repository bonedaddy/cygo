#ifndef _CXRT_BASE_H_
#define _CXRT_BASE_H_

#include <stdlib.h>
#include <stdint.h>

typedef uint8_t bool;
typedef uint8_t byte;
typedef uint8_t uint8;
typedef int8_t int8;
typedef uint16_t uint16;
typedef int16_t int16;
typedef uint32_t uint32;
typedef int32_t int32;
typedef uint64_t uint64;
typedef int64_t int64;
typedef float float32;
typedef double float64;

typedef struct {
    char* data;
    int len;
} string;

void println(const char* fmt, ...);

#endif
