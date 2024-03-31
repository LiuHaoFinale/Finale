/*
 * @Author: LiuHao
 * @Date: 2024-03-14 22:23:23
 * @Description: 
 */
#include "utils.h"
#include "vm.h"
#include "core.h"
// #include "compile.h"
#include <string.h>
#include <sys/stat.h>

char *rootDir = NULL; // 根目录

// 返回值是Value类型，放在args[0]中
#define RET_VALUE(value) \
    do { \
        args[0] = value;\
        return true; \
    } while (0);

// 将obj转换为Value后作为返回值
#define RET_OBJ(obj_ptr) RET_VALUE(OBJ_TO_VALUE(obj_ptr))
#define RET_BOOL(boolean) RET_VALUE(OBJ_TO_VALUE(boolean))
#define RET_NUM(num) RET_VALUE(OBJ_TO_VALUE(num))
#define RET_NULL RET_VALUE(OBJ_TO_VALUE(VT_NULL))
#define RET_TRUE RET_VALUE(OBJ_TO_VALUE(VT_TRUE))
#define RET_FALSE RET_VALUE(OBJ_TO_VALUE(VT_FALSE))

// 设置线程报错
#define SET_ERROR_FALSE(vmPtr, errMsg) \
    do { \
        vmPtr->curThread->errorObj = OBJ_TO_VALUE(NewObjString(vmPtr, errMsg, strlen(errMsg))); \
        return false; \
    } while (0);

// 绑定方法func到class_ptr的类
#define PRIM_METHOD_BIND(classPtr, methodName, func) \
{ \
    uint32_t length = strlen(methodName); \
    int globalIdx = GetIndexFromSymbolTable(&vm->allMethodNames, methodName, length); \
    if (globalIdx == -1) \
    { \ 
        globalIdx = AddSymbol(vm, &vm->allMethodNames, methodName, length);\
    } \
    Method method; \
    method.type = MT_PRIMITIVE; \
    method.primFn = func; \
    BindMethod(vm, classPtr, (uint32_t)globalIdx, method); \
}

/**
 * @brief 读取源代码文件
*/
char* ReadFile(const char *path)
{
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        IO_ERROR("Could't open file \"%s\".", path);
    }

    struct stat fileStat;
    stat(path, &fileStat);
    size_t fileSize = fileStat.st_size;
    char *fileContent = (char *)malloc(fileSize + 1);
    if (fileContent == NULL) {
        MEM_ERROR("Could't allocate memory for reading file \"%s\".\n", path);
    }

    size_t numRead = fread(fileContent, sizeof(char), fileSize, file);
    if (numRead < fileSize) {
        IO_ERROR("Could't read file \"%s\".\n", path);
    }
    fileContent[fileSize] = '\0';

    fclose(file);
    // printf("%s", file_content);
    return fileContent;
}

/**
 * @brief 编译模块
*/
VMResult ExecuteModule(VM *vm, Value moduleName, const char *moduleCode)
{
    ObjThread *objThread = LoadModule(vm, moduleName, moduleCode);
    return VM_RESULT_ERROR;
}

/**
 * @brief 编译核心模块
 * @details 在读取源码文件之前，先编译核心模块
*/
void BuildCore(VM *vm)
{
    ObjModule *coreModule = NewObjModule(vm, NULL); // NULL为核心模块
    // 创建核心模块，录入virtmem->allModule  {vt_null: coremodule}
    MapSet(vm, vm->allModules, CORE_MODULE, OBJ_TO_VALUE(coreModule));
    // 创建object类并绑定方法 放入coreModule
    vm->objectClass = DefineClass(vm, coreModule, "object");
    PRIM_METHOD_BIND(vm->objectClass, "!", PrimObjectNot);
    PRIM_METHOD_BIND(vm->objectClass, "==(_)", PrimObjectEqual);
    PRIM_METHOD_BIND(vm->objectClass, "!=(_)", PrimObjectNotEqual);
    PRIM_METHOD_BIND(vm->objectClass, "Is(_)", PrimObjectIs);
    PRIM_METHOD_BIND(vm->objectClass, "ToString", PrimClassToString);
    PRIM_METHOD_BIND(vm->objectClass, "Type", PrimObjectType);

    // 定义classOfClass类，它是所有meta类的meta类和基类
    vm->classOfClass = DefineClass(vm, coreModule, "class");

    // object_class是任何类的基类，此处绑定objectClass为classOfClass的基类
    BindSuperClass(vm, vm->classOfClass, vm->objectClass);

    PRIM_METHOD_BIND(vm->classOfClass, "Name", PrimClassName);
    PRIM_METHOD_BIND(vm->classOfClass, "SuperType", PrimClassSuperType);
    PRIM_METHOD_BIND(vm->classOfClass, "ToString", PrimClassToString);

    // object类的元信息类obejctMeta
    Class *objectMetaClass = DefineClass(vm, coreModule, "obejctMeta");
    // class_of_class类是所有meta类的meta类和基类
    BindSuperClass(vm, objectMetaClass, vm->classOfClass);

    PRIM_METHOD_BIND(objectMetaClass, "Same(_,_)", PrimObjectMetaSame);

    // 绑定各自的meta类
    vm->objectClass->objHeader.class = objectMetaClass;
    objectMetaClass->objHeader.class = vm->classOfClass;
    vm->classOfClass->objHeader.class = vm->classOfClass; // 元信息类回路，meta类终点

    // 执行核心模块 CORE_MODULE
    ExecuteModule(vm, CORE_MODULE, coreModule);
}

/**
 * @brief object取反
*/
static boolean PrimObjectNot(VM *vm UNUSED, Value *args)
{
    RET_VALUE(VT_TO_VALUE(VT_FALSE));
}

static boolean PrimObjectEqual(VM *vm UNUSED, Value *args)
{
    Value boolValue = BOOL_TO_VALUE(ValueIsEqual(args[0], args[1]));
    RET_VALUE(boolValue);
}

static boolean PrimObjectNotEqual(VM *vm UNUSED, Value *args)
{
    Value boolValue = BOOL_TO_VALUE(!ValueIsEqual(args[0], args[1]));
    RET_VALUE(boolValue);
}

/**
 * @brief 类args[0]是否为类args[1]的子类
*/
static boolean PrimObjectIs(VM *vm, Value *args)
{
    if (!VALUE_IS_CLASS(args[1])) {
        RUNTIME_ERROR("Argument must be class!");
    }
    
    Class *thisClass = GetClassObj(vm, args[0]);
    Class *baseClass = (Class *)(args[1].objHeader);

    // 也有可能是多级继承
    while (baseClass != NULL) {
        if (thisClass == baseClass) {
            RET_VALUE(VT_TO_VALUE(VT_TRUE));
        }
        baseClass = baseClass->superClass;
    }

    // 找不到
    RET_VALUE(VT_TO_VALUE(VT_FALSE));
}

/**
 * @brief 返回args[0]所属class的名字
*/
static boolean PrimObjectToString(VM *vm UNUSED, Value *args)
{
    Class *class = args[0].objHeader->class;
    Value nameValue = OBJ_TO_VALUE(class->name);
    RET_VALUE(nameValue);
}

/**
 * @brief 返回对象args[0]所属的类
*/
static boolean PrimObjectType(VM *vm, Value *args)
{
    Class *class = GetClassObj(vm, args[0]);
    RET_OBJ(class);
}

/**
 * @brief 返回类名
*/
static boolean PrimClassName(VM *vm UNUSED, Value *args)
{
    RET_OBJ(VALUE_TO_CLASS(args[0])->name);
}

/**
 * @brief 返回args[0]的基类
*/
static boolean PrimClassSuperType(VM *vm UNUSED, Value *args)
{
    Class *class = VALUE_TO_CLASS(args[0]);
    if (class->superClass != NULL) {
        RET_OBJ(class->superClass);
    }
    RET_VALUE(VT_TO_VALUE(VT_NULL));
}

/**
 * @brief 返回类名
*/
static boolean PrimClassToString(VM *vm UNUSED, Value *args)
{
    RET_OBJ(VALUE_TO_CLASS(args[0])->name);
}

/**
 * @brief 返回args[1]和args[2]是否相等
*/
static boolean PrimObjectMetaSame(VM *vm UNUSED, Value *args)
{
    Value boolValue = BOOL_TO_VALUE(ValueIsEqual(args[1], args[2]));
    RET_VALUE(boolValue);
}

/**
 * @brief 在table中查找符号，找到返回索引
*/
int GetIndexFromSymbolTable(SymbolTable *table, const char *symbol, uint32_t length)
{
    ASSERT(length != 0, "length of symbol is 0!");
    uint32_t index = 0;
    while (index < table->count) {
        if (length == table->datas[index].length && memcmp(table->datas[index].str, symbol, length) == 0) {
            return index;
        }
        index ++;
    }
    return -1;
}

/**
 * @brief 在table中添加符号symbol 返回其索引
*/
int AddSymbol(VM *vm, SymbolTable *table, const char *symbol, uint32_t length)
{
    ASSERT(length != 0, "length of symbol is 0!");
    String string;
    string.str = ALLOCATE_ARRAY(vm, char, length + 1);
    memcpy(string.str, symbol, length);
    string.str[length] = '\0';
    string.length = length;
    StringBufferAdd(vm, table, string);
    return table->count - 1;
}

/**
 * @brief 定义类
*/
static Class* DefineClass(VM *vm, ObjModule *objModule, const char *name)
{
    Class *class = NewRawClass(vm, name, 0);  // 创建类

    // 把类作为普通变量在模块中定义
    DefineModuleVar(vm, objModule, name, strlen(name), OBJ_TO_VALUE(class));
    return class;
}

/**
 * @brief 绑定方法
*/
void BindMethod(VM *vm , Class *class, uint32_t index, Method method)
{
    if (index >= class->methods.count) {
        Method emptyPad = { MT_NONE, {0} };
        MethodBufferFillWrite(vm, &class->methods, emptyPad, index - class->methods.count + 1);
    }
    class->methods.datas[index] = method;
}

/**
 * @brief 绑定基类
*/
void BindSuperClass(VM *vm, Class *subClass, Class *superClass)
{
    subClass->superClass = superClass;
    // 继承基类属性数
    subClass->fieldNum += superClass->fieldNum;

    uint32_t idx = 0;
    while (idx < superClass->methods.count) {
        BindMethod(vm, subClass, idx, superClass->methods.datas[idx]);
        idx ++;
    }
}

/**
 * @brief 从module中获取名为module_name的模块
*/
static ObjModule* GetModule(VM *vm, Value moduleName)
{
    Value value = MapGet(vm->allModules, moduleName);
    if (value.valueType == VT_UNDEFINED) {
        return NULL;
    }

    return VALUE_TO_OBJMODULE(value);
}

/**
 * @brief 确保符号已添加到符号表
*/
int EnsureSymbolExist(VM *vm, SymbolTable *table, const char *symbol, uint32_t length)
{
    int symbolIndex = GetIndexFromSymbolTable(table, symbol, length);
    if (symbolIndex == -1) {
        return add_symbol(vm, table, symbol, length);
    }

    return symbolIndex;
}   

/**
 * @brief 载入模块module_name并编译
*/
static ObjThread* LoadModule(VM *vm, Value moduleName, const char *moduleCode)
{
    ObjModule *module = GetModule(vm, moduleName);
    // 避免重复载入
    if (module == NULL) {
        ObjString *modName = VALUE_TO_OBJSTR(moduleName);
        ASSERT(modName->value.start[modName->value.length] == '\0', "string.value.start is not terminated!");

        module = NewObjModule(vm, modName->value.start);
        MapSet(vm, vm->allModules, moduleName, OBJ_TO_VALUE(module));

        // 继承核心模块中的变量
        ObjModule *coreModule = GetModule(vm, CORE_MODULE);
        uint32_t idx = 0;
        while (idx < coreModule->moduleVarName.count) {
            DefineModuleVar(vm, module, 
                coreModule->moduleVarName.datas[idx].str, 
                strlen(coreModule->moduleVarName.datas[idx].str), 
                coreModule->moduleVarValue.datas[idx]);
            idx ++;
        }
    }
    /**
     * 为函数创建闭包并放到线程中
     * 闭包是函数和其环境组成的实体，为函数提供了自由变量的存储空间
    */
    ObjFn *fn = CompileModule(vm, module, moduleCode); // 生成的指令流存入fn中
    ObjClosure *objClosure = NewObjClosure(vm, fn); // 创建闭包
    ObjThread *moduleThread = NewObjThread(vm, objClosure); // 根据闭包创建线程

    return moduleThread;
}