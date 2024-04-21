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

typedef uint8_t Opcode;

#define OPCODE_SLOTS(opcode, effect) OPCODE_##opcode,
typedef enum {
    #include "opcode.inc"
} OpCode;
#undef OPCODE_SLOTS

typedef enum vmResult {
    VM_RESULT_SUCCESS, VM_RESULT_ERROR
} VMResult; // 虚拟机执行结果

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
};

void InitVM(VM *vm);
VM* NewVM(void);
VMResult ExecuteInstruction(VM *vm, register ObjThread *curThread);
void EnsureStack(VM *vm, ObjThread *objThread, const uint32_t needSlots);

#endif