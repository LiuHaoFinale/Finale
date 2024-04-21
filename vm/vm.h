/*
 * @Author: LiuHao
 * @Date: 2024-03-14 22:17:26
 * @Description: 
 */
#ifndef _VM_VIRTMEM_H
#define _VM_VIRTMEM_H

#include "common.h"
#include "header_obj.h"
#include "obj_map.h"
#include "obj_thread.h"
#include <stdint.h>

#define MAX_TEMP_ROOTS_NUM 8 // 最多临时根对象数量

typedef uint8_t Opcode;

#define OPCODE_SLOTS(opcode, effect) OPCODE_##opcode,
typedef enum {
    #include "opcode.inc"
} OpCode;
#undef OPCODE_SLOTS

typedef enum vmResult {
    VM_RESULT_SUCCESS, VM_RESULT_ERROR
} VMResult; // 虚拟机执行结果

typedef struct gray {
    ObjHeader **grayObjects;
    uint32_t capacity;
    uint32_t count;
} Gray; // 灰色对象信息结构

typedef struct configuration {
    int heapGrowthFactor; // 堆生长因子
    uint32_t initialHeapSize; // 初始堆大小
    uint32_t minHeapSize; // 最小堆大小
    uint32_t nextGC; // 第一次出发GC堆的大小，默认为initialHeapSize
} Configuration;

struct vm {
    uint32_t allocatedBytes; // 累计已分配的内存量
    Parser *curParser; // 当前词法分析器
    ObjHeader *allObjects; // 所有已分配对象链表
    SymbolTable allMethodNames; // 所有类的方法名
    ObjMap *allModules;
    ObjThread *curThread; // 当前正在执行的线程

    Class *classOfClass;
    Class *objectClass;
    Class *mapClass;
    Class *rangeClass;
    Class *listClass;
    Class *fnClass;
    Class *stringClass;
    Class *nullClass;
    Class *boolClass;
    Class *numClass;
    Class *threadClass;

    // 临时的根对象集合，存储临时需要被GC保留的对象，避免回收
    ObjHeader *tmpRoots[MAX_TEMP_ROOTS_NUM];
    uint32_t tmpRootNum;

    // 用于存储存活对象
    Gray grays;
    Configuration config;
};

void InitVM(VM *vm);
VM* NewVM(void);
void FreeVM(VM *vm);
VMResult ExecuteInstruction(VM *vm, register ObjThread *curThread);
void EnsureStack(VM *vm, ObjThread *objThread, const uint32_t needSlots);
void PushTmpRoot(VM *vm, ObjHeader *obj);
void PopTmpRoot(VM *vm);
#endif