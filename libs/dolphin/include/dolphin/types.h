#ifndef _DOLPHIN_TYPES_H_
#define _DOLPHIN_TYPES_H_

typedef signed   char          s8;
typedef unsigned char          u8;
typedef signed   short int     s16;
typedef unsigned short int     u16;
#ifdef TARGET_PC
#include <stdint.h>
typedef int32_t            s32;
typedef uint32_t           u32;
#else
typedef signed   long          s32;
typedef unsigned long          u32;
#endif
typedef signed   long long int s64;
typedef unsigned long long int u64;

typedef volatile u8  vu8;
typedef volatile u16 vu16;
#ifdef TARGET_PC
typedef volatile uint32_t  vu32;
#else
typedef volatile u32 vu32;
#endif
typedef volatile u64 vu64;
typedef volatile s8  vs8;
typedef volatile s16 vs16;
#ifdef TARGET_PC
typedef volatile int32_t   vs32;
#else
typedef volatile s32 vs32;
#endif
typedef volatile s64 vs64;

typedef float  f32;
typedef double f64;

typedef volatile f32 vf32;
typedef volatile f64 vf64;

typedef char *Ptr;

typedef int BOOL;

typedef unsigned int uint;

#define FALSE 0
#define TRUE 1

#if defined(__MWERKS__)
#define AT_ADDRESS(addr) : (addr)
#elif defined(__GNUC__)
//#define AT_ADDRESS(addr) __attribute__((address((addr))))
#define AT_ADDRESS(addr)  // was removed in GCC. define in linker script instead.
#else
#define AT_ADDRESS(addr)
#endif

#ifndef ATTRIBUTE_ALIGN
#if defined(__MWERKS__) || defined(__GNUC__)
#define ATTRIBUTE_ALIGN(num) __attribute__((aligned(num)))
#elif defined(_MSC_VER)
#define ATTRIBUTE_ALIGN(num)
#else
#error unknown compiler
#endif
#endif

#ifndef DECL_WEAK
#if defined(__MWERKS__)
#define DECL_WEAK __declspec(weak)
#elif defined(__GNUC__)
#define DECL_WEAK __attribute__((weak))
#elif defined(_MSC_VER)
#define DECL_WEAK
#else
#error unknown compiler
#endif
#endif

#ifndef NULL
#ifdef __cplusplus
#if __cplusplus >= 201103L
#define NULL nullptr
#else
#define NULL 0
#endif
#else
#define NULL ((void*)0)
#endif
#endif

#ifdef __MWERKS__
#define __REGISTER register
#else
#define __REGISTER
#endif

#include <cstddef>

#ifdef TARGET_PC
#ifdef __cplusplus
static_assert(sizeof(s32) == 4, "s32 must be 4 bytes");
static_assert(sizeof(u32) == 4, "u32 must be 4 bytes");
static_assert(sizeof(s16) == 2, "s16 must be 2 bytes");
static_assert(sizeof(u16) == 2, "u16 must be 2 bytes");
#endif
#endif

#endif
