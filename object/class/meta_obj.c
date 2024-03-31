/*
 * @Author: LiuHao
 * @Date: 2024-03-31 14:45:55
 * @Description: 
 */
#include "meta_obj.h"
#include "class.h"
#include "vm.h"
#include "common.h"

/**
 * @brief 新建模块
*/
ObjModule* NewObjModule(VM *vm, const char *modName)
{
    ObjModule *objModule = ALLOCATE(vm, ObjModule);
    if (objModule == NULL) {
        MEM_ERROR("Allocate ObjModule Failed!");
    }

    // ObjModule是元信息对象，不属于任何一个类
    InitObjheader(vm, &objModule->objHeader, OT_MODULE, NULL);
    
    StringBufferInit(&objModule->moduleVarName);
    StringBufferInit(&objModule->moduleVarValue);

    objModule->name = NULL; // 核心模块名为NULL
    if (modName != NULL) {
        objModule->name = NewObjString(vm, modName, strlen(modName));
    }

    return objModule;
}

/**
 * @brief 创建类class的实例
*/
ObjInstance* NewObjInstance(VM *vm, Class *myClass)
{
    ObjInstance *objInstance = ALLOCATE_EXTRA(vm, ObjInstance, sizeof(Value) * myClass->fieldNum);

    InitOobjheader(vm, &objInstance->objHeader, OT_INSTANCE, myClass);
    // 初始化field为NULL
    uint32_t idx = 0;
    while (idx < myClass->fieldNum) {
        objInstance->fields[idx ++] = VT_TO_VALUE(VT_NULL);
    }
    return objInstance;
}