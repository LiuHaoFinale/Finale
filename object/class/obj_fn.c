/*
 * @Author: LiuHao
 * @Date: 2024-03-15 22:53:27
 * @Description: 
 */
#include "obj_fn.h"
#include "meta_obj.h"
#include "vm.h"
#include "class.h"

/**
 * @brief 创建upvalue对象
*/
ObjUpvalue* NewObjUpvalue(VM *vm, Value *localVarPtr)
{
    ObjUpvalue *objUpvalue = ALLOCATE(vm, ObjUpvalue);
    InitObjHeader(vm, &objUpvalue->objHeader, OT_UPVALUE, NULL);
    objUpvalue->localVarPtr = localVarPtr;
    objUpvalue->closedUpvalue = VT_TO_VALUE(VT_NULL);
    objUpvalue->next = NULL;
    return objUpvalue;
}

/**
 * @brief 以函数fn创建一个闭包
*/
ObjClosure* NewObjClosure(VM *vm, ObjFn *objFn)
{
    ObjClosure *objClosure = ALLOCATE_EXTRA(vm, ObjClosure, sizeof(ObjUpvalue *) * objFn->upvalueNum);

    InitObjHeader(vm, &objClosure->objHeader, OT_CLOSURE, vm->fnClass);
    objClosure->fn = objFn;

    // 清除upvalue 避免在填充upvalue数组之前处罚GC
    uint32_t idx = 0;
    while (idx < objFn->upvalueNum) {
        objClosure->upvalues[idx] = NULL;
        idx ++;
    }
    return objClosure;
}

/**
 * @brief 创建一个空函数
*/
ObjFn*NewObjFn(VM *vm, ObjModule *objModule, uint32_t maxStackSlotUsedNum)
{
    ObjFn *objFn = ALLOCATE(vm, ObjFn);
    if (objFn == NULL)
    {
        MEM_ERROR("Allocate ObjFn Failed!");
    }
    InitObjHeader(vm, &objFn->objHeader, OT_FUNCTION, vm->fnClass);
    ByteBufferInit(&objFn->instructStream);
    ValueBufferInit(&objFn->constants);
    objFn->module = objModule;
    objFn->maxStackSlotUsedNum = maxStackSlotUsedNum;
    objFn->upvalueNum = objFn->argNum = 0;
#ifdef DEBUG    
    objFn->debug = ALLOCATE(vm, FnDebug);
    objFn->debug->fnName = NULL;
    IntBufferInit(&objFn->debug->lineNo);
#endif
    return objFn;
}