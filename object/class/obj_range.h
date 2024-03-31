/*
 * @Author: LiuHao
 * @Date: 2024-03-15 23:14:28
 * @Description: 
 */
#ifndef _OBJECT_RANGE_H
#define _OBJECT_RANGE_H

#include "class.h"

typedef struct {
    ObjHeader objHeader;
    int from;
    int to;
} ObjRange;

ObjRange* NewObjRange(VM *vm, int from, int to);

#endif