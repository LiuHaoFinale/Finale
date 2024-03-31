/*
 * @Author: LiuHao
 * @Date: 2024-03-15 22:23:04
 * @Description: 
 */
#ifndef _OBJECT_HEADER_OBJ_H
#define _OBJECT_HEADER_OBJ_H

#include "utils.h"
#include "common.h"

typedef enum {
    OT_CLASS,  
    OT_LIST,
    OT_MAP,
    OT_MODULE,
    OT_RANGE,
    OT_STRING,
    OT_UPVALUE,
    OT_FUNCTION,
    OT_CLOSURE,
    OT_INSTANCE,
    OT_THREAD
} ObjType; // 对象类型

typedef struct ObjHeader {
    ObjType type;
    boolean isdark; // 对象是否可达
    Class *class; // 对象所属于的类 元类
    struct ObjHeader *next; // 用以链接所有已分配对象
} ObjHeader;  // 对象头，用于记录元信息和垃圾回收

typedef enum { 
    VT_UNDEFINED, VT_NULL, VT_FALSE, VT_TRUE, VT_NUM, VT_OBJ 
} ValueType;

typedef struct {
    ValueType valueType;
    union {
        double num;
        ObjHeader *objHeader;
    };
} Value; // 通用值结构

DECLARE_BUFFER_TYPE(Value)

void InitObjHeader(VM *vm, ObjHeader *objHeader, ObjType objType, Class *class);

#endif