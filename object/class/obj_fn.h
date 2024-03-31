/*
 * @Author: LiuHao
 * @Date: 2024-03-15 22:37:51
 * @Description: 
 */
#ifndef _OBJECT_OBJ_FN_H
#define _OBJECT_OBJ_FN_H

#include "meta_obj.h"
#include "utils.h"

typedef struct {
    char *fnName;// 函数名
    IntegerBuffer lineNo; // 行号
} FnDebug; // 在函数中的调试结构

typedef struct {
    ObjHeader objHeader; 
    ByteBuffer instructStream; // 编译后的指令流
    ValueBuffer constants; // 常量表 函数一般不用
    ObjModule *module; // 所属模块
    uint32_t maxStackSlotUsedNum; // 本函数最多使用的栈空间
    uint32_t upvalueNum; // 本函数所涵盖的upvalue数量
    uint8_t argNum; // 函数期望的参数个数
#ifdef DEBUG
    FnDebug *debug;
#endif
} ObjFn; // 指令流单元（一定程度上是函数对象）

typedef struct upvalue {
    ObjHeader objHeader;
    Value *localVarPtr; // 栈是个Value类型的数组，local_var_ptr指向upvalue所关联的局部变量
    Value closedUpvalue; // 已被关闭的upvalue
    struct upvalue *next; // 用以链接openupvalue链表
} ObjUpvalue; // upvalue对象

/**
 * @brief 闭包指的是函数依赖的"环境"，环境即upvalues
*/
typedef struct {
    ObjHeader objHeader;
    ObjFn *fn; // 闭包所要引用的函数
    ObjUpvalue *upvalues[0]; // 用以存储此函数的closed upvalue
} ObjClosure; // 闭包对象

/**
 * @brief 函数的调用栈帧，是函数的一套运行环境
*/
typedef struct {
    uint8_t *ip; // 程序计数器，指向下一个将被指向的指令
    ObjClosure *closure; // 在本frame中执行的闭包函数
    // frame 是共享thread.stack
    // 指向本frame所在thread运行时栈的起始地址
    Value *stackStart;
} Frame; // 调用框架

#define INITIAL_FRAME_NUM 4 // 线程中Frame的个数

ObjUpvalue* NewObjUpvalue(VM *vm, Value *localVarPtr);
ObjClosure* NewObjClosure(VM *vm, ObjFn *objFn);
ObjFn* NewObjFn(VM *vm, ObjModule *objModule, uint32_t maxStackSlotUsedNum);

#endif