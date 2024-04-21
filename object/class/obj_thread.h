/*
 * @Author: LiuHao
 * @Date: 2024-03-15 23:34:59
 * @Description: 
 */
#ifndef _OBJECT_THREAD_H
#define _OBJECT_THREAD_H

#include "obj_fn.h"

typedef struct frame Frame;
typedef struct upvalue ObjUpvalue;
typedef struct objClosure ObjClosure;

typedef struct ObjThread {
    ObjHeader objHeader;
    Value *stack; // 运行时栈的栈底
    Value *esp; // 运行时的栈顶
    uint32_t stackCapacity; // 栈容量

    Frame *frames; // 调用空框架
    uint32_t usedFrameNum; // 已使用的frame数量
    uint32_t frameCapacity; // frame容量

    ObjUpvalue *openUpvalues; // upvalue的链表首节点
    struct ObjThread *caller; // 当前thread的调用者

    Value errorObj; // 
} ObjThread; // 线程对象

void PrepareFrame(ObjThread *objThread, ObjClosure *objClosure, Value *stackStart);
ObjThread* NewObjThread(VM *vm, ObjClosure *objClosure);
void ResetThread(ObjThread *objThread, ObjClosure *objClosure);

#endif