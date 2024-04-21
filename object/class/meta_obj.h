/*
 * @Author: LiuHao
 * @Date: 2024-03-15 22:46:03
 * @Description: 
 */
#ifndef _OBJECT_META_OBJ_H
#define _OBJECT_META_OBJ_H

/**
 * @brief 元信息 元对象
*/

#include "obj_string.h"
#include "obj_fn.h"
#include "class.h"
#include "vm.h"
#include "common.h"
#include "utils.h"

/**
 * @brief 模块指的是在某独立作用域中定义的代码的集合
 * 在模块对象中只保存其中的模块变量-在模块中定义的全局变量
*/
typedef struct objModule {
    ObjHeader objHeader;
    SymbolTable moduleVarName; // 模块中的模块变量名
    ValueBuffer moduleVarValue; // 模块中的模块变量值
    ObjString *name; // 模块名
} ObjModule;  // 模块对象

/**
 * @brief 对于类的所有对象来说，他们的方法已经放在类中，那么剩下的区别是域的个数不同，数组正好满足此需求
*/
typedef struct {
    ObjHeader objHeader;
    Value fields[0]; // 具体的字段
} ObjInstance; // 对象实例

ObjModule* NewObjModule(VM *vm, const char *modName);
ObjInstance* NewObjInstance(VM *vm, Class *myClass);

#endif