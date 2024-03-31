/*
 * @Author: LiuHao
 * @Date: 2024-03-15 23:15:15
 * @Description: 
 */
#include "obj_range.h"

/**
 * @brief 新建range对象
*/
ObjRange* NewObjRange(VM *vm, int from, int to)
{
    ObjRange *objRange = ALLOCATE(vm, ObjRange);
    InitObjheader(vm, &objRange->objHeader, OT_RANGE, vm->rangeClass);
    objRange->from = from;
    objRange->to = to;
    return objRange;
}