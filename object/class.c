/*
 * @Author: LiuHao
 * @Date: 2024-03-15 23:06:56
 * @Description: 
 */
#include "class.h"
#include "common.h"
#include "obj_string.h"
#include "obj_range.h"
#include "core.h"
#include "vm.h"
#include "utils.h"
#include "compile.h"
#include <string.h>

DEFINE_BUFFER_METHOD(Method)

/**
 * @brief 判断a和b是否相等
*/
boolean ValueIsEqual(Value a, Value b)
{
    if (a.valueType != b.valueType) {
        return false;
    }
    if (a.valueType == VT_NUM) {
        return a.num == b.num;
    }
    if (a.objHeader == b.objHeader) {
        return true;
    }
    if (a.objHeader->type != b.objHeader->type) {
        return false;
    }
    if (a.objHeader->type == OT_STRING) {
        ObjString *strA = VALUE_TO_OBJSTR(a);
        ObjString *strB = VALUE_TO_OBJSTR(b);
        return (strA->value.length == strB->value.length && memcmp(strA->value.start, strB->value.start, strA->value.length) == 0);
    }
    if (a.objHeader->type == OT_RANGE) {
        ObjRange *rgA = VALUE_TO_OBJRANGE(a);
        ObjRange *rgB = VALUE_TO_OBJRANGE(b);
        return (rgA->from == rgB->from && rgA->to == rgB->to);
    }
    return false;
}

/**
 * @brief 新建裸类
 *          一般的类都有归属类，即圆心锡类meta-class，裸类是没有归属的类
*/
Class* NewRawClass(VM *vm, const char *name, uint32_t fieldNum)
{
    Class *class = ALLOCATE(vm, Class);
    InitObjHeader(vm, &class->objHeader, OT_CLASS, NULL);
    class->name = NewObjString(vm, name, strlen(name));
    class->fieldNum = fieldNum;
    class->superClass = NULL; // 默认无父类
    MethodBufferInit(&class->methods);
    return class;
}

/**
 * @brief 获得对象obj所属的类
 *          数字等Value也被视为对象，因此参数为Value，获得对象obj所属的类
*/
inline Class* GetClassOfObj(VM *vm, Value object)
{
    switch (object.valueType) {
        case VT_NULL:
            return vm->nullClass;
        case VT_FALSE:
        case VT_TRUE:
            return vm->boolClass;
        case VT_NUM:
            return vm->numClass;
        case VT_OBJ:
            return VALUE_TO_OBJ(object)->class;
        default:
            NOT_REACHED()
    }
    return NULL;
}

/**
 * @brief 创建一个类
*/
Class* NewClass(VM *vm, ObjString *className, uint32_t fieldNum, Class *superClass)
{
    #define MAX_METACLASS_LEN MAX_ID_LEN + 10
    char newClassName[MAX_METACLASS_LEN] = {'\0'};
    #undef MAX_METACLASS_LEN
    memcpy(newClassName, className->value.start, className->value.length);
    memcpy(newClassName + className->value.length, "metaclass", 10U);
    Class *metaClass = NewRawClass(vm, newClassName, 0);
    metaClass->objHeader.class = vm->classOfClass;
    BindSuperClass(vm, metaClass, vm->classOfClass);
    memcpy(newClassName, className->value.start, className->value.length);
    newClassName[className->value.length] = '\0';
    Class *class = NewRawClass(vm, newClassName, fieldNum);
    class->objHeader.class = metaClass;
    BindSuperClass(vm, class, superClass);
    return class;
}

