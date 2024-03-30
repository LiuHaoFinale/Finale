/*
 * @Author: LiuHao
 * @Date: 2024-03-14 21:53:54
 * @Description: 
 */
#ifndef _INCLUDE_COMMON_H
#define _INCLUDE_COMMON_H

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct parser Parser;
typedef struct vm VM;
typedef struct class Class;


#define boolean char
#define true  1
#define false 0
#define UNUSED __attribute__ ((unused))

#ifdef TEST
    #define ASSERT(condition, errMsg) \
        do { \
            if (!(condition)) \
            { \
                fprintf(stderr, "ASSERT failed! %s: %d In function %s(): %s\n", \
                        __FILE__, __LINE__, __func__, errMsg); \
                abort(); \
            } while (0); \
        } while (0);
#else
    #define ASSERT(condition, errMsg) ((void) 0)
#endif
#define NOT_REACHED() \
    do { \
        fprintf(stderr, "NOT_REACHED: %s: %d In function %s()\n", \
                __FILE__, __LINE__, __func__); \
        while (1); \
    } while (0);

#endif // _INCLUDE_COMMON_H