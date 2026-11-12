#ifndef WPROC_UTIL_TYPES_H
#define WPROC_UTIL_TYPES_H

// Freestanding fixed-width types (no <stdint.h> / <stddef.h> needed).
// Assumes LP64 (x86_64 Linux).
typedef unsigned long      u64;
typedef long               i64;
typedef unsigned int       u32;
typedef int                i32;
typedef unsigned short     u16;
typedef short              i16;
typedef unsigned char      u8;
typedef signed char        i8;
typedef unsigned long      usize;
typedef long               isize;

typedef _Bool bool;
#define true  1
#define false 0

#define NULL ((void *)0)

#define U64_MAX   ((u64)-1)
#define U32_MAX   ((u32)-1)
#define I64_MAX   ((i64)(U64_MAX >> 1))

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))
#define MIN(a, b)    ((a) < (b) ? (a) : (b))
#define MAX(a, b)    ((a) > (b) ? (a) : (b))

_Static_assert(sizeof(u64) == 8, "wproc assumes LP64");
_Static_assert(sizeof(void *) == 8, "wproc assumes 64-bit pointers");

#endif // WPROC_UTIL_TYPES_H
