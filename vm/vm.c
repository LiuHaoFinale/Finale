/*
 * @Author: LiuHao
 * @Date: 2024-03-30 22:46:56
 * @Description: 
 */
#include "vm.h"
#include <stdlib.h>
#include "utils.h"
#include "obj_thread.h"
#include "header_obj.h"

void InitVM(VM *vm)
{
    vm->allocatedBytes = 0;
    vm->curParser = NULL;
    vm->allObjects = NULL;
}

VM* NewVM(void)
{
    VM *vm = (VM *)malloc(sizeof(VM));
    if (vm == NULL) {
        MEM_ERROR("Allocate vm Fail!");
    }
    InitVM(vm);
    // BuildCore(vm); // 在读取源码之前先编译核心模块
    return vm;
}

/**
 * @brief 确保stack有效
*/
void EnsureStack(VM *vm, ObjThread *objThread, const uint32_t needSlots)
{
    if (objThread->stackCapacity >= needSlots) {
        return ;
    } else {
        // else
    }
    uint32_t newStackCapacity = CeilToPowerOf2(needSlots);
    ASSERT(newStackCapacity > objThread->stackCapacity, "newStackCapacity error!");

    // 记录原栈底以用于下面判断扩容后的栈是否原地扩容
    Value *oldStackBottom = objThread->stack;
    uint32_t slotSize = sizeof(Value);
    objThread->stack = (Value *)MemManager(vm, objThread->stack, objThread->stackCapacity * slotSize, newStackCapacity);
    objThread->stackCapacity = newStackCapacity;

    // 判断是否原地扩容
    long offset = objThread->stack - oldStackBottom;

    // 说明os无法在原地满足内存需求，充分分配了起始地址
    if (offset != 0) {
        uint32_t idx = 0;
        while (idx < objThread->usedFrameNum) { // 更新各堆栈框架中的起始地址stackStart
            objThread->frames[idx ++].stackStart += offset;
        }
        // 更新upvalue中localVarPtr指向的内存块中的内存地址（栈中slot地址）
        ObjUpvalue *objUpvalue = objThread->openUpvalues;
        while (objUpvalue) {
            objUpvalue->localVarPtr += offset;
            objUpvalue = objUpvalue->next;
        }
        // 更新栈顶
        objThread->esp += offset;
    }
}

/**
 * @brief 为objClosure在ObjThread中创建运行时栈
*/
inline static void CreateFrame(VM *vm, ObjThread *objThread, ObjClosure *objClosure, const int argNum)
{
    if (objThread->usedFrameNum + 1 > objThread->frameCapacity) { // 扩容
        uint32_t newCapacity = objThread->frameCapacity * 2U;
        uint32_t frameSize = sizeof(Frame);
        objThread->frames = (Frame *)MemManager(vm, objThread->frames, 
                frameSize * (objThread->frameCapacity), frameSize * newCapacity);
        objThread->frameCapacity = newCapacity;
    }

    // 栈大小等于栈顶 - 栈底
    uint32_t stackSlots = (uint32_t)(objThread->esp - objThread->stack);
    // 总共需要的栈大小
    uint32_t needSlots = stackSlots + objClosure->fn->maxStackSlotUsedNum;

    EnsureStack(vm, objThread, needSlots);

    // 准备上CPU
    // objThread->esp - argNum是被调用函数的堆栈框架在站中的起始地址
    // 减去了argNum，目的是被调用的闭包函数objClosure可以访问到栈中自己的参数
    // argNum占用的框架在编译过程中已经算在了maxStackSlotUsedNum中了
    PrepareFrame(objThread, objClosure, objThread->esp - argNum); 
}

/**
 * @brief upvalue的关闭
 * 本函数要关闭的马上要出作用域的局部变量，因此该作用域及其之内嵌套更深作用域的局部变量都应该回收
 * 地址位于栈顶lastSlot后面的肯定作用域更深，栈顶是向高地址发展的
*/
static void ClosedUpvalue(ObjThread *objThread, Value *lastSlot)
{
    ObjUpvalue *objUpvalue = objThread->openUpvalues; // openUpvalues是在本线程中已经打开过的upvalue的链表首节点
    // objUpvalue->localVarPtr >= lastSlot是需要被关闭的局部变量的条件
    while ((objUpvalue != NULL) && (objUpvalue->localVarPtr >= lastSlot)) {
        objUpvalue->closedUpvalue = *(objUpvalue->localVarPtr); // 被销毁的局部变量会放到closedUpvalue
        objUpvalue->localVarPtr = &(objUpvalue->closedUpvalue); // localVarPtr指向运行时栈中的局部变量改为本结构中的closedUpvalue
        objUpvalue = objUpvalue->next;
    }
    objThread->openUpvalues = objUpvalue;
}

/**
 * @brief 创建线程已打开的upvalue链表，并将localVarPtr所属的upvalue以降序插入到该链表
*/
static ObjUpvalue* CreateOpenUpvalue(VM *vm, ObjThread *objThread, Value *localVarPtr)
{

}