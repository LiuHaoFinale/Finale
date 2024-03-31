/*
 * @Author: LiuHao
 * @Date: 2024-03-15 22:34:23
 * @Description: 
 */
#include "header_obj.h"
#include "vm.h"
#include "class.h"

DEFINE_BUFFER_METHOD(Value)

/**
 * @brief 初始化对象头
*/
void InitObjHeader(VM *vm, ObjHeader *objHeader, ObjType objType, Class *class)
{
    objHeader->type = objType;
    // 与GC相关
    objHeader->isdark = false;
    objHeader->class = class;  // 设置meta类
    objHeader->next = vm->allObjects;  // 设新对象头放在链表头
    vm->allObjects = objHeader; // 重新指向
}