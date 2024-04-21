/*
 * @Author: LiuHao
 * @Date: 2024-03-15 23:36:16
 * @Description: 
 */
#include "vm.h"
#include "obj_thread.h"

/**
 * @brief 为运行函数准备栈帧
*/
void PrepareFrame(ObjThread *objThread, ObjClosure *objClosure, Value *stackStart)
{
    ASSERT(objThread->frameCapacity > objThread->usedFrameNum, "frame not enough!!");

    Frame *frame = &(objThread->frames[objThread->usedFrameNum ++]);
    // thread中的各个frame是共享thread的stack
    // frame用frame->stack_start指向各自frame在thread_stack中的起始地址
    frame->stackStart = stackStart;
    frame->closure = objClosure;
    frame->ip = objClosure->fn->instructStream.datas;
}

/**
 * @brief 新建线程
*/
ObjThread* NewObjThread(VM *vm, ObjClosure *objClosure)
{
    ASSERT(objClosure != NULL, "ObjClosure is Null!");

    Frame *frames = ALLOCATE_ARRAY(vm, Frame, INITIAL_FRAME_NUM);

    uint32_t stackCapacity = CeilToPowerOf2(objClosure->fn->maxStackSlotUsedNum + 1);
    Value *newStack = ALLOCATE_ARRAY(vm, Value, stackCapacity);

    ObjThread *objThread = ALLOCATE(vm, ObjThread);
    InitObjHeader(vm, &objThread->objHeader, OT_THREAD, vm->threadClass);

    objThread->frames = frames;
    objThread->frameCapacity = INITIAL_FRAME_NUM;
    objThread->stack = newStack;
    objThread->stackCapacity = stackCapacity;

    ResetThread(objThread, objClosure);
    return objThread;
}

/**
 * @brief 充值thread
*/
void ResetThread(ObjThread *objThread, ObjClosure *objClosure)
{
    objThread->esp = objThread->stack;
    objThread->openUpvalues = NULL;
    objThread->caller = NULL;
    objThread->errorObj = VT_TO_VALUE(VT_NULL);
    objThread->usedFrameNum = 0;

    ASSERT(objClosure != NULL, "ObjClosure is NULL in function resetThread");
    PrepareFrame(objThread, objClosure, objThread->stack);
}