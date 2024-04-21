/*
 * @Author: LiuHao
 * @Date: 2024-03-15 23:09:34
 * @Description: 
 */
#include "obj_list.h"
#include "utils.h"
#include "header_obj.h"

static void ShrinkList(VM *vm, ObjList *objList, uint32_t newCapacity);

/**
 * @brief 新建list对象，元素个数为element_num
*/
ObjList* NewObjList(VM *vm, uint32_t elementNum)
{
    Value *elementArray = NULL;
    // 先分配内存，后调用initobjHeader，避免gc无谓的遍历
    if (elementNum > 0) {
        elementArray = ALLOCATE_ARRAY(vm, Value, elementNum);
    }
    ObjList *objList = ALLOCATE(vm, ObjList);

    objList->elements.datas = elementArray;
    objList->elements.capacity = objList->elements.count = elementNum;
    InitObjHeader(vm, &objList->objHeader, OT_LIST, vm->listClass);
    return objList;
}

/**
 * @brief 删除list中索引为index处的元素
*/
Value RemoveElement(VM *vm, ObjList *objList, uint32_t index)
{
    Value valueRemoved = objList->elements.datas[index];

    // 使index后面的元素前移一位，覆盖index处的元素
    uint32_t idx = index;
    while (idx < objList->elements.count) {
        objList->elements.datas[idx] = objList->elements.datas[idx + 1];
        idx ++;
    }

    // 若容量利用率过低就减小容量
    uint32_t _capacity = objList->elements.capacity / CAPACITY_GROW_FACTOR;
    if (_capacity > objList->elements.count) {
        ShrinkList(vm, objList, _capacity);
    }

    objList->elements.count --;
    return valueRemoved;
}

/**
 * @brief 在objlist中索引为index处插入value，类似于list[index]=value
*/
void InsertElement(VM *vm, ObjList *objList, uint32_t index, Value value)
{
    if (index > objList->elements.count - 1) {
        RUNTIME_ERROR("Index Out Bounded!");
    }
    // 准备一个value的框架以容纳新元素产生的空间波动
    // 即最后一个元素要后移1个空间
    ValueBufferAdd(vm, &objList->elements, VT_TO_VALUE(VT_NULL));

    // 下面使index后面的元素整体后移一位
    uint32_t idx = objList->elements.count - 1;
    while (idx > index) {
        objList->elements.datas[idx] = objList->elements.datas[idx - 1];
        idx --;
    }

    // 在index插入数值
    objList->elements.datas[index] = value;
}

/**
 * @brief 调整list容量
*/
static void ShrinkList(VM *vm, ObjList *objList, uint32_t newCapacity)
{
    uint32_t oldSize = objList->elements.capacity * sizeof(Value);
    uint32_t newSize = newCapacity * sizeof(Value);

    MemManager(vm, objList->elements.datas, oldSize, newSize);
    objList->elements.capacity = newCapacity;
}