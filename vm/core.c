/*
 * @Author: LiuHao
 * @Date: 2024-03-14 22:23:23
 * @Description: 
 */
#include "utils.h"
#include "vm.h"
#include "core.h"
#include "compile.h"
#include "unicode.h"
#include <string.h>
#include <sys/stat.h>
#include <ctype.h>
#include <math.h>
#include <errno.h>
#include <time.h>
#include "core.script.inc"

static ObjThread* LoadModule(VM *vm, Value moduleName, const char *moduleCode);
static boolean ValidateFn(VM* vm, Value arg);

char *rootDir = NULL; // 根目录

// 返回值是Value类型，放在args[0]中
#define RET_VALUE(value) \
    do { \
        args[0] = value;\
        return true; \
    } while (0);

// 将obj转换为Value后作为返回值
#define RET_OBJ(obj_ptr) RET_VALUE(OBJ_TO_VALUE(obj_ptr))

#define RET_BOOL(boolean) RET_VALUE(BOOL_TO_VALUE(boolean))
#define RET_NUM(num) RET_VALUE(NUM_TO_VALUE(num))
#define RET_NULL RET_VALUE(VT_TO_VALUE(VT_NULL))
#define RET_TRUE RET_VALUE(VT_TO_VALUE(VT_TRUE))
#define RET_FALSE RET_VALUE(VT_TO_VALUE(VT_FALSE))

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
    return ExecuteInstruction(vm, objThread);
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
    
    Class *thisClass = GetClassOfObj(vm, args[0]);
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
    Class *class = GetClassOfObj(vm, args[0]);
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
        return AddSymbol(vm, table, symbol, length);
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

/**
 * @brief 返回核心模块name的value结构
*/
static Value GetCoreClassValue(ObjModule *objModule, const char *name)
{
    int index = GetIndexFromSymbolTable(&objModule->moduleVarName, name, strlen(name));
    if (index == -1) {
        char id[MAX_ID_LEN] = {'\0'};
        memcpy(id, name, strlen(name));
        RUNTIME_ERROR("Something wrong occur: missing core class \"%s\"!", id);
    }
    return objModule->moduleVarValue.datas[index];
}

static boolean PrimBoolToString(VM *vm, Value *args)
{
    ObjString *objString;
    if (VALUE_TO_BOOL(args[0])) {
        objString = NewObjString(vm, "true", 4U);
    } else {
        objString = NewObjString(vm, "false", 5U);
    }
    RET_OBJ(objString);
}

static boolean PrimBoolNot(VM *vm UNUSED, Value *args)
{
    RET_BOOL(!VALUE_TO_BOOL(args[0]));
}

//将数字转换为字符串
static ObjString* NumToStr(VM* vm, double num) {
   //nan不是一个确定的值,因此nan和nan是不相等的
   if (num != num) {
      return NewObjString(vm, "nan", 3);
   }

   if (num == INFINITY) {
      return NewObjString(vm, "infinity", 8);
   }

   if (num == -INFINITY) {
      return NewObjString(vm, "-infinity", 9);
   }

  //以下24字节的缓冲区足以容纳双精度到字符串的转换
  char buf[24] = {'\0'};
  int len = sprintf(buf, "%.14g", num);
  return NewObjString(vm, buf, len);
}

//判断arg是否为数字
static boolean ValidateNum(VM* vm, Value arg) {
   if (VALUE_IS_NUM(arg)) {
      return true;   
   }
   SET_ERROR_FALSE(vm, "argument must be number!");
}

//判断arg是否为字符串
static boolean ValidateString(VM* vm, Value arg) {
   if (VALUE_IS_OBJSTR(arg)) {
      return true; 
   }
   SET_ERROR_FALSE(vm, "argument must be string!");
}

//将字符串转换为数字
static boolean PrimNumFromString(VM* vm, Value* args) {
   if (!ValidateString(vm, args[1])) {
      return false;
   }
   
   ObjString* objString = VALUE_TO_OBJSTR(args[1]);

   //空字符串返回RETURN_NULL
   if (objString->value.length == 0) {
      RET_NULL;
   }

   ASSERT(objString->value.start[objString->value.length] == '\0', "objString don`t teminate!");

   errno = 0;
   char* endPtr;

   //将字符串转换为double型, 它会自动跳过前面的空白
   double num = strtod(objString->value.start, &endPtr);
   
   //以endPtr是否等于start+length来判断不能转换的字符之后是否全是空白
   while (*endPtr != '\0' && isspace((unsigned char)*endPtr)) {
      endPtr++;   
   }
   
   if (errno == ERANGE) {
      RUNTIME_ERROR("string too large!"); 
   }

   //如果字符串中不能转换的字符不全是空白,字符串非法,返回NULL
   if (endPtr < objString->value.start + objString->value.length) {
      RET_NULL;   
   }

   //至此,检查通过,返回正确结果
   RET_NUM(num);
}

//返回圆周率
static boolean PrimNumPi(VM* vm UNUSED, Value* args UNUSED) {
   RET_NUM(3.14159265358979323846);
}

#define PRIM_NUM_INFIX(name, operator, type) \
   static boolean name(VM* vm, Value* args) {\
      if (!ValidateNum(vm, args[1])) {\
	 return false; \
      }\
      RET_##type(VALUE_TO_NUM(args[0]) operator VALUE_TO_NUM(args[1]));\
   }

PRIM_NUM_INFIX(PrimNumPlus,   +, NUM);
PRIM_NUM_INFIX(PrimNumMinus,  -, NUM);
PRIM_NUM_INFIX(PrimNumMul,    *, NUM);
PRIM_NUM_INFIX(PrimNumDiv,    /, NUM);
PRIM_NUM_INFIX(PrimNumGt,   >, BOOL);
PRIM_NUM_INFIX(PrimNumGe,  >=, BOOL);
PRIM_NUM_INFIX(PrimNumLt,   <, BOOL);
PRIM_NUM_INFIX(PrimNumLe,  <=, BOOL);
#undef PRIM_NUM_INFIX

 #define PRIM_NUM_BIT(name, operator) \
   static boolean name(VM* vm UNUSED, Value* args) {\
      if (!ValidateNum(vm, args[1])) {\
	 return false;\
      }\
      uint32_t leftOperand = VALUE_TO_NUM(args[0]); \
      uint32_t rightOperand = VALUE_TO_NUM(args[1]); \
      RET_NUM(leftOperand operator rightOperand);\
   }

PRIM_NUM_BIT(PrimNumBitAnd, &);
PRIM_NUM_BIT(PrimNumBitOr, |);
PRIM_NUM_BIT(PrimNumBitShiftRight, >>);
PRIM_NUM_BIT(PrimNumBitShiftLeft, <<);
#undef PRIM_NUM_BIT

//使用数学库函数
#define PRIM_NUM_MATH_FN(name, mathFn) \
   static boolean name(VM* vm UNUSED, Value* args) {\
      RET_NUM(mathFn(VALUE_TO_NUM(args[0]))); \
   }

PRIM_NUM_MATH_FN(PrimNumAbs,	 fabs);
PRIM_NUM_MATH_FN(PrimNumAcos,	 acos);
PRIM_NUM_MATH_FN(PrimNumAsin,    asin);
PRIM_NUM_MATH_FN(PrimNumAtan,    atan);
PRIM_NUM_MATH_FN(PrimNumCeil,    ceil);
PRIM_NUM_MATH_FN(PrimNumCos,     cos);
PRIM_NUM_MATH_FN(PrimNumFloor,   floor);
PRIM_NUM_MATH_FN(PrimNumNegate,  -);
PRIM_NUM_MATH_FN(PrimNumSin,     sin);
PRIM_NUM_MATH_FN(PrimNumSqrt,    sqrt);  //开方
PRIM_NUM_MATH_FN(PrimNumTan,     tan);
#undef PRIM_NUM_MATH_FN

//这里用fmod实现浮点取模
static boolean PrimNumMod(VM* vm UNUSED, Value* args) {
   if (!ValidateNum(vm, args[1])) {
      return false;
   }
   RET_NUM(fmod(VALUE_TO_NUM(args[0]), VALUE_TO_NUM(args[1])));
}

//数字取反
static boolean PrimNumBitNot(VM* vm UNUSED, Value* args) {
   RET_NUM(~(uint32_t)VALUE_TO_NUM(args[0]));
}

//[数字from..数字to]
static boolean PrimNumRange(VM* vm UNUSED, Value* args) {
   if (!ValidateNum(vm, args[1])) {
      return false;
   }

   double from = VALUE_TO_NUM(args[0]); 
   double to = VALUE_TO_NUM(args[1]); 
   RET_OBJ(NewObjRange(vm, from, to));
}

//atan2(args[1])
static boolean PrimNumAtan2(VM* vm UNUSED, Value* args) {
   if (!ValidateNum(vm, args[1])) {
      return false;
   }
   
   RET_NUM(atan2(VALUE_TO_NUM(args[0]), VALUE_TO_NUM(args[1])));
}

//返回小数部分
static boolean PrimNumFraction(VM* vm UNUSED, Value* args) {
   double dummyInteger;
   RET_NUM(modf(VALUE_TO_NUM(args[0]), &dummyInteger));
}

//判断数字是否无穷大,不区分正负无穷大 
static boolean PrimNumIsInfinity(VM* vm UNUSED, Value* args) {
   RET_BOOL(isinf(VALUE_TO_NUM(args[0])));  
}

//判断是否为数字
static boolean PrimNumIsInteger(VM* vm UNUSED, Value* args) {
   double num = VALUE_TO_NUM(args[0]);
   //如果是nan(不是一个数字)或无限大的数字就返回false
   if (isnan(num) || isinf(num)) {
      RET_FALSE;
   }
   RET_BOOL(trunc(num) == num);
}

//判断数字是否为nan
static boolean PrimNumIsNan(VM* vm UNUSED, Value* args) {
   RET_BOOL(isnan(VALUE_TO_NUM(args[0])));
}

//数字转换为字符串
static boolean PrimNumToString(VM* vm UNUSED, Value* args) {
   RET_OBJ(NumToStr(vm, VALUE_TO_NUM(args[0])));
}

//取数字的整数部分
static boolean PrimNumTruncate(VM* vm UNUSED, Value* args) {
   double integer;
   modf(VALUE_TO_NUM(args[0]), &integer);
   RET_NUM(integer);
}

//判断两个数字是否相等
static boolean PrimNumEqual(VM* vm UNUSED, Value* args) {
   if (!ValidateNum(vm, args[1])) {
      RET_FALSE;
   }

   RET_BOOL(VALUE_TO_NUM(args[0]) == VALUE_TO_NUM(args[1]));
}

//判断两个数字是否不等
static boolean PrimNumNotEqual(VM* vm UNUSED, Value* args) {
   if (!ValidateNum(vm, args[1])) {
      RET_TRUE;
   }
   RET_BOOL(VALUE_TO_NUM(args[0]) != VALUE_TO_NUM(args[1]));
}

//null取非
static boolean PrimNullNot(VM* vm UNUSED, Value* args UNUSED) {
   RET_VALUE(BOOL_TO_VALUE(true));
}

//null的字符串化
static boolean PrimNullToString(VM* vm, Value* args UNUSED) {
   ObjString* objString = NewObjString(vm, "null", 4);
   RET_OBJ(objString);
}

//Fn.new(_):新建一个函数对象
static boolean PrimFnNew(VM* vm, Value* args) {
   //代码块为参数必为闭包
   if (!ValidateFn(vm, args[1])) return false;

   //直接返回函数闭包
   RET_VALUE(args[1]);
}

//绑定fn.call的重载
static void BindFnOverloadCall(VM* vm, const char* sign) {
   uint32_t index = EnsureSymbolExist(vm, &vm->allMethodNames, sign, strlen(sign));
   //构造method
   Method method = {MT_FN_CALL, {0}};
   BindMethod(vm, vm->fnClass, index, method);
}

//校验arg是否为函数
static boolean ValidateFn(VM* vm, Value arg) {
   if (VALUE_TO_OBJCLOSURE(arg)) {
      return true;
   }
   vm->curThread->errorObj = 
      OBJ_TO_VALUE(NewObjString(vm, "argument must be a function!", 28));
   return false;
}

//Thread.new(func):创建一个thread实例
static boolean PrimThreadNew(VM* vm, Value* args) {
   //代码块为参数必为闭包
   if (!ValidateFn(vm, args[1])) {
      return false;
   }
   
   ObjThread* objThread = NewObjThread(vm, VALUE_TO_OBJCLOSURE(args[1]));

   //使stack[0]为接收者,保持栈平衡
   objThread->stack[0] = VT_TO_VALUE(VT_NULL);
   objThread->esp++;
   RET_OBJ(objThread);
}

//Thread.abort(err):以错误信息err为参数退出线程
static boolean PrimThreadAbort(VM* vm, Value* args) {
   //此函数后续未处理,暂时放着
   vm->curThread->errorObj = args[1]; //保存退出参数
   return VALUE_IS_NULL(args[1]);
}

//Thread.current:返回当前的线程
static boolean PrimThreadCurrent(VM* vm, Value* args UNUSED) {
   RET_OBJ(vm->curThread);
}

//Thread.suspend():挂起线程,退出解析器
static boolean PrimThreadSuspend(VM* vm, Value* args UNUSED) {
   //目前suspend操作只会退出虚拟机,
   //使curThread为NULL,虚拟机将退出
   vm->curThread = NULL;
   return false;
}

//Thread.yield(arg)带参数让出cpu
static boolean PrimThreadYieldWithArg(VM* vm, Value* args) {
   struct ObjThread* curThread = vm->curThread;
   vm->curThread = curThread->caller;   //使cpu控制权回到主调方

   curThread->caller = NULL;  //与调用者断开联系

   if (vm->curThread != NULL) { 
      //如果当前线程有主调方,就将当前线程的返回值放在主调方的栈顶
      vm->curThread->esp[-1] = args[1];
      
      //对于"thread.yield(arg)"来说, 回收arg的空间,
      //保留thread参数所在的空间,将来唤醒时用于存储yield结果
      curThread->esp--;
   }
   return false;
}

//Thread.yield() 无参数让出cpu
static boolean PrimThreadYieldWithoutArg(VM* vm, Value* args UNUSED) {
   ObjThread* curThread = vm->curThread;
   vm->curThread = curThread->caller;   //使cpu控制权回到主调方

   curThread->caller = NULL;  //与调用者断开联系

   if (vm->curThread != NULL) { 
      //为保持通用的栈结构,如果当前线程有主调方,
      //就将空值做为返回值放在主调方的栈顶
      vm->curThread->esp[-1] = VT_TO_VALUE(VT_NULL) ;
   }
   return false;
}

//切换到下一个线程nextThread
static boolean SwitchThread(VM* vm, 
      ObjThread* nextThread, Value* args, boolean withArg) {
   //在下一线程nextThread执行之前,其主调线程应该为空
   if (nextThread->caller != NULL) {
      RUNTIME_ERROR("thread has been called!");
   }
   nextThread->caller = vm->curThread;

   if (nextThread->usedFrameNum == 0) {
      //只有已经运行完毕的thread的usedFrameNum才为0
      SET_ERROR_FALSE(vm, "a finished thread can`t be switched to!");
   }

   if (!VALUE_IS_NULL(nextThread->errorObj)) {
      //Thread.abort(arg)会设置errorObj, 不能切换到abort的线程
      SET_ERROR_FALSE(vm, "a aborted thread can`t be switched to!");
   }

   //如果call有参数,回收参数的空间,
   //只保留次栈顶用于存储nextThread返回后的结果
   if (withArg) {
      vm->curThread->esp--;
   }

   ASSERT(nextThread->esp > nextThread->stack, "esp should be greater than stack!"); 
   //nextThread.call(arg)中的arg做为nextThread.yield的返回值
   //存储到nextThread的栈顶,否则压入null保持栈平衡
   nextThread->esp[-1] = withArg ? args[1] : VT_TO_VALUE(VT_NULL);

   //使当前线程指向nextThread,使之成为就绪
   vm->curThread = nextThread;

   //返回false以进入vm中的切换线程流程
   return false;
}

//objThread.call()
static boolean PrimThreadCallWithoutArg(VM* vm, Value* args) {
   return SwitchThread(vm, VALUE_TO_OBJTHREAD(args[0]), args, false);
}

//objThread.call(arg)
static boolean PrimThreadCallWithArg(VM* vm, Value* args) {
   return SwitchThread(vm, VALUE_TO_OBJTHREAD(args[0]), args, true);
}

//objThread.isDone返回线程是否运行完成
static boolean PrimThreadIsDone(VM* vm UNUSED, Value* args) {
   //获取.isDone的调用者
   ObjThread* objThread = VALUE_TO_OBJTHREAD(args[0]);
   RET_BOOL(objThread->usedFrameNum == 0 || !VALUE_IS_NULL(objThread->errorObj));
}

//确认value是否为整数
static boolean ValidateIntValue(VM* vm, double value) {
   if (trunc(value) == value) {
      return true; 
   }
   SET_ERROR_FALSE(vm, "argument must be integer!");
}

//校验arg是否为整数
static boolean ValidateInt(VM* vm, Value arg) {
   //首先得是数字
   if (!ValidateNum(vm, arg)) {
      return false;
   }

   //再校验数值
   return ValidateIntValue(vm, VALUE_TO_NUM(arg));
}

//校验参数index是否是落在"[0, length)"之间的整数
static uint32_t ValidateIndexValue(VM* vm, double index, uint32_t length) {
   //索引必须是数字
   if (!ValidateIntValue(vm, index)) {
      return UINT32_MAX;
   }

   //支持负数索引,负数是从后往前索引
   //转换其对应的正数索引.如果校验失败则返回UINT32_MAX
   if (index < 0) {
      index += length;
   }

   //索引应该落在[0,length)
   if (index >= 0 && index < length) {
      return (uint32_t)index;
   }
  
   //执行到此说明超出范围
   vm->curThread->errorObj = 
      OBJ_TO_VALUE(NewObjString(vm, "index out of bound!", 19));
   return UINT32_MAX;
}

//验证index有效性
static uint32_t ValidateIndex(VM* vm, Value index, uint32_t length) {
   if (!ValidateNum(vm, index)) {
      return UINT32_MAX; 
   }
   return ValidateIndexValue(vm, VALUE_TO_NUM(index), length);
}

//从码点value创建字符串
static Value MakeStringFromCodePoint(VM* vm, int value) {
   uint32_t byteNum = GetByteNumOfEncodeUtf8(value);
   ASSERT(byteNum != 0, "utf8 encode bytes should be between 1 and 4!");

   //+1是为了结尾的'\0'
   ObjString* objString = ALLOCATE_EXTRA(vm, ObjString, byteNum + 1);

   if (objString == NULL) {
      MEM_ERROR("allocate memory failed in runtime!"); 
   }

   InitObjHeader(vm, &objString->objHeader, OT_STRING, vm->stringClass);
   objString->value.length = byteNum;
   objString->value.start[byteNum] = '\0';
   EncodeUtf8((uint8_t*)objString->value.start, value);
   HashObjString(objString);
   return OBJ_TO_VALUE(objString);
}

//用索引index处的字符创建字符串对象
static Value StringCodePointAt(VM* vm, ObjString* objString, uint32_t index) {
   ASSERT(index < objString->value.length, "index out of bound!");  
   int codePoint = DecodeUtf8((uint8_t*)objString->value.start + index,
	 objString->value.length - index);

   //若不是有效的utf8序列,将其处理为单个裸字符
   if (codePoint == -1) {
      return OBJ_TO_VALUE(NewObjString(vm, &objString->value.start[index], 1));
   }

   return MakeStringFromCodePoint(vm, codePoint);
}

//计算objRange中元素的起始索引及索引方向
static uint32_t CalculateRange(VM* vm, 
      ObjRange* objRange, uint32_t* countPtr, int* directionPtr) {

   uint32_t from = ValidateIndexValue(vm, objRange->from, *countPtr);
   if (from == UINT32_MAX) {
      return UINT32_MAX; 
   }

   uint32_t to = ValidateIndexValue(vm, objRange->to, *countPtr);
   if (to == UINT32_MAX) {
      return UINT32_MAX; 
   }

   //如果from和to为负值,经过ValidateIndexValue已经变成了相应的正索引
   *directionPtr = from < to ? 1 : -1;
   *countPtr = abs((int)(from - to)) + 1;
   return from;
}

//以utf8编码从source中起始为startIndex,方向为direction的count个字符创建字符串
static ObjString* NewObjStringFromSub(VM* vm, ObjString* sourceStr,
      int startIndex, uint32_t count, int direction) {

   uint8_t* source = (uint8_t*)sourceStr->value.start;
   uint32_t totalLength = 0, idx = 0;

   //计算count个utf8编码的字符总共需要的字节数,后面好申请空间
   while (idx < count) {
      totalLength += GetByteNumOfDecodeUtf8(source[startIndex + idx * direction]);
      idx++;
   }

   //+1是为了结尾的'\0'
   ObjString* result = ALLOCATE_EXTRA(vm, ObjString, totalLength + 1);

   if (result == NULL) {
      MEM_ERROR("allocate memory failed in runtime!"); 
   }
   InitObjHeader(vm, &result->objHeader, OT_STRING, vm->stringClass);
   result->value.start[totalLength] = '\0';
   result->value.length = totalLength;

   uint8_t* dest = (uint8_t*)result->value.start;
   idx = 0; 
   while (idx < count) {
      int index = startIndex + idx * direction;
      //解码,获取字符数据
      int codePoint = DecodeUtf8(source + index, sourceStr->value.length - index);
      if (codePoint != -1) {
         //再将数据按照utf8编码,写入result
         dest += EncodeUtf8(dest, codePoint);
      }
      idx++;
   }

   HashObjString(result);
   return result;
}

//使用Boyer-Moore-Horspool字符串匹配算法在haystack中查找needle,大海捞针
static int FindString(ObjString* haystack, ObjString* needle) {
   //如果待查找的patten为空则为找到
   if (needle->value.length == 0) {
      return 0;    //返回起始下标0
   }

   //若待搜索的字符串比原串还长 肯定搜不到
   if (needle->value.length > haystack->value.length) {
      return -1; 
   }

   //构建"bad-character shift表"以确定窗口滑动的距离
   //数组shift的值便是滑动距离
   uint32_t shift[UINT8_MAX];
   //needle中最后一个字符的下标
   uint32_t needleEnd = needle->value.length - 1;

   //一、 先假定"bad character"不属于needle(即pattern),
   //对于这种情况,滑动窗口跨过整个needle 
   uint32_t idx = 0;
   while (idx < UINT8_MAX) {
      // 默认为滑过整个needle的长度
      shift[idx] = needle->value.length;
      idx++;
   }

   //二、假定haystack中与needle不匹配的字符在needle中之前已匹配过的位置出现过
   //就滑动窗口以使该字符与在needle中匹配该字符的最末位置对齐。
   //这里预先确定需要滑动的距离 
   idx = 0; 
   while (idx < needleEnd) {
      char c = needle->value.start[idx];
      //idx从前往后遍历needle,当needle中有重复的字符c时,
      //后面的字符c会覆盖前面的同名字符c,这保证了数组shilf中字符是needle中最末位置的字符,
      //从而保证了shilf[c]的值是needle中最末端同名字符与needle末端的偏移量
      shift[(uint8_t)c] = needleEnd - idx;
      idx++;
   }

   //Boyer-Moore-Horspool是从后往前比较,这是处理bad-character高效的地方,
   //因此获取needle中最后一个字符,用于同haystack的窗口中最后一个字符比较
   char lastChar = needle->value.start[needleEnd];

   //长度差便是滑动窗口的滑动范围
   uint32_t range = haystack->value.length - needle->value.length;

   //从haystack中扫描needle,寻找第1个匹配的字符 如果遍历完了就停止
   idx = 0;
   while (idx <= range) {
      //拿needle中最后一个字符同haystack窗口的最后一个字符比较
      //(因为Boyer-Moore-Horspool是从后往前比较), 如果匹配,看整个needle是否匹配
      char c = haystack->value.start[idx + needleEnd];
      if (lastChar == c &&
        memcmp(haystack->value.start + idx, needle->value.start, needleEnd) == 0) {
         //找到了就返回匹配的位置
         return idx;
      }

      //否则就向前滑动继续下一伦比较 
      idx += shift[(uint8_t)c];
   }

   //未找到就返回-1
   return -1;
}

//objString.fromCodePoint(_):从码点建立字符串
static boolean PrimStringFromCodePoint(VM* vm, Value* args) {

   if (!ValidateInt(vm, args[1])) {
      return false;
   }
   
   int codePoint = (int)VALUE_TO_NUM(args[1]);
   if (codePoint < 0) {
      SET_ERROR_FALSE(vm, "code point can`t be negetive!");
   }

   if (codePoint > 0x10ffff) {
      SET_ERROR_FALSE(vm, "code point must be between 0 and 0x10ffff!");
   }
   
   RET_VALUE(MakeStringFromCodePoint(vm, codePoint));
}

//objString+objString: 字符串相加
static boolean PrimStringPlus(VM* vm, Value* args) {
   if (!ValidateString(vm, args[1])) {
      return false;
   }

   ObjString* left = VALUE_TO_OBJSTR(args[0]);
   ObjString* right = VALUE_TO_OBJSTR(args[1]);

   uint32_t totalLength = strlen(left->value.start) + strlen(right->value.start);
   //+1是为了结尾的'\0'
   ObjString* result = ALLOCATE_EXTRA(vm, ObjString, totalLength + 1);
   if (result == NULL) {
      MEM_ERROR("allocate memory failed in runtime!"); 
   }
   InitObjHeader(vm, &result->objHeader, OT_STRING, vm->stringClass);
   memcpy(result->value.start, left->value.start, strlen(left->value.start));
   memcpy(result->value.start + strlen(left->value.start), 
	 right->value.start, strlen(right->value.start));
   result->value.start[totalLength] = '\0';
   result->value.length = totalLength;
   HashObjString(result);

   RET_OBJ(result);
}

//objString[_]:用数字或objRange对象做字符串的subscript
static boolean PrimStringSubscript(VM* vm, Value* args) {
   ObjString* objString = VALUE_TO_OBJSTR(args[0]);
   //数字和objRange都可以做索引,分别判断
   //若索引是数字,就直接索引1个字符,这是最简单的subscript
   if (VALUE_IS_NUM(args[1])) {
      uint32_t index = ValidateIndex(vm, args[1], objString->value.length);
      if (index == UINT32_MAX) {
         return false; 
      }
      RET_VALUE(StringCodePointAt(vm, objString, index));
   }

   //索引要么为数字要么为ObjRange,若不是数字就应该为objRange
   if (!VALUE_IS_OBJRANGE(args[1])) {
      SET_ERROR_FALSE(vm, "subscript should be integer or range!");
   }
   
   //direction是索引的方向,
   //1表示正方向,从前往后.-1表示反方向,从后往前.
   //from若比to大,即从后往前检索字符,direction则为-1
   int direction;

   uint32_t count = objString->value.length;
   //返回的startIndex是objRange.from在objString.value.start中的下标
   uint32_t startIndex = CalculateRange(vm, VALUE_TO_OBJRANGE(args[1]), &count, &direction);
   if (startIndex == UINT32_MAX) { 
      return false;
   }

   RET_OBJ(NewObjStringFromSub(vm, objString, startIndex, count, direction));
}

//objString.byteAt_():返回指定索引的字节
static boolean PrimStringByteAt(VM* vm UNUSED, Value* args) {
   ObjString* objString = VALUE_TO_OBJSTR(args[0]);
   uint32_t index = ValidateIndex(vm, args[1], objString->value.length);
   if (index == UINT32_MAX) {
      return false; 
   }
   //故转换为数字返回
   RET_NUM((uint8_t)objString->value.start[index]);
}

//objString.byteCount_:返回字节数
static boolean PrimStringByteCount(VM* vm UNUSED, Value* args) {
   RET_NUM(VALUE_TO_OBJSTR(args[0])->value.length);
}

//objString.codePointAt_(_):返回指定的CodePoint
static boolean PrimStringCodePointAt(VM* vm UNUSED, Value* args) {
   ObjString* objString = VALUE_TO_OBJSTR(args[0]);
   uint32_t index = ValidateIndex(vm, args[1], objString->value.length);
   if (index == UINT32_MAX) {
      return false; 
   }

   const uint8_t* bytes = (uint8_t*)objString->value.start;
   if ((bytes[index] & 0xc0) == 0x80) {
      //如果index指向的并不是utf8编码的最高字节
      //而是后面的低字节,返回-1提示用户
      RET_NUM(-1);
   }

   //返回解码
   RET_NUM(DecodeUtf8((uint8_t*)objString->value.start + index,
	    objString->value.length - index));
}

//objString.contains(_):判断字符串args[0]中是否包含子字符串args[1]
static boolean PrimStringContains(VM* vm UNUSED, Value* args) {
   if (!ValidateString(vm, args[1])) {
      return false;
   }

   ObjString* objString = VALUE_TO_OBJSTR(args[0]);
   ObjString* pattern = VALUE_TO_OBJSTR(args[1]);
   RET_BOOL(FindString(objString, pattern) != -1);
}

//objString.endsWith(_): 返回字符串是否以args[1]为结束
static boolean PrimStringEndsWith(VM* vm UNUSED, Value* args) {
   if (!ValidateString(vm, args[1])) {
      return false;
   }

   ObjString* objString = VALUE_TO_OBJSTR(args[0]);
   ObjString* pattern = VALUE_TO_OBJSTR(args[1]);

   //若pattern比源串还长,源串必然不包括pattern
   if (pattern->value.length > objString->value.length) {
      RET_FALSE;
   }

   char* cmpIdx = objString->value.start +
   objString->value.length - pattern->value.length;
   RET_BOOL(memcmp(cmpIdx, pattern->value.start, pattern->value.length) == 0);
}

//objString.indexOf(_):检索字符串args[0]中子串args[1]的起始下标
static boolean PrimStringIndexOf(VM* vm UNUSED, Value* args) {
   if (!ValidateString(vm, args[1])) {
      return false;
   }

   ObjString* objString = VALUE_TO_OBJSTR(args[0]);
   ObjString* pattern = VALUE_TO_OBJSTR(args[1]);

   //若pattern比源串还长,源串必然不包括pattern
   if (pattern->value.length > objString->value.length) {
      RET_FALSE;
   }
   
   int index = FindString(objString, pattern);
   RET_NUM(index);
}

//objString.iterate(_):返回下一个utf8字符(不是字节)的迭代器
static boolean PrimStringIterate(VM* vm UNUSED, Value* args) {
   ObjString* objString = VALUE_TO_OBJSTR(args[0]);

   //如果是第一次迭代 迭代索引肯定为空
   if (VALUE_IS_NULL(args[1])) {
      if (objString->value.length == 0) {
	      RET_FALSE;
      }
      RET_NUM(0);
   }

   //迭代器必须是正整数
   if (!ValidateInt(vm, args[1])){
      return false;
   }
   
   double iter = VALUE_TO_NUM(args[1]);
   if (iter < 0) {   
      RET_FALSE;
   }

   uint32_t index = (uint32_t)iter;
   do {
      index++;

      //到了结尾就返回false,表示迭代完毕
      if (index >= objString->value.length) RET_FALSE;

      //读取连续的数据字节,直到下一个Utf8的高字节
   } while ((objString->value.start[index] & 0xc0) == 0x80);  

   RET_NUM(index);
}

//objString.iterateByte_(_): 迭代索引,内部使用
static boolean PrimStringIterateByte(VM* vm UNUSED, Value* args) {
   ObjString* objString = VALUE_TO_OBJSTR(args[0]);

   //如果是第一次迭代 迭代索引肯定为空 直接返回索引0
   if (VALUE_IS_NULL(args[1])) {
      if (objString->value.length == 0) {
	      RET_FALSE;
      }
      RET_NUM(0);
   }

   //迭代器必须是正整数
   if (!ValidateInt(vm, args[1])) {
      return false;
   }

   double iter = VALUE_TO_NUM(args[1]);

   if (iter < 0) {   
      RET_FALSE;
   }

   uint32_t index = (uint32_t)iter;
   index++; //移进到下一个字节的索引
   if (index >= objString->value.length) {
      RET_FALSE;
   }
   
   RET_NUM(index);
}

//objString.iteratorValue(_):返回迭代器对应的value
static boolean PrimStringIteratorValue(VM* vm, Value* args) {
   ObjString* objString = VALUE_TO_OBJSTR(args[0]);
   uint32_t index = ValidateIndex(vm, args[1], objString->value.length);
   if (index == UINT32_MAX) {
      return false; 
   }
   RET_VALUE(StringCodePointAt(vm, objString, index));
}

//objString.startsWith(_): 返回args[0]是否以args[1]为起始
static boolean PrimStringStartsWith(VM* vm UNUSED, Value* args) {
   if (!ValidateString(vm, args[1])) {
      return false;
   }

   ObjString* objString = VALUE_TO_OBJSTR(args[0]);
   ObjString* pattern = VALUE_TO_OBJSTR(args[1]);

   //若pattern比源串还长,源串必然不包括pattern,
   //因此不可能以pattern为起始
   if (pattern->value.length > objString->value.length) {
      RET_FALSE;
   }

   RET_BOOL(memcmp(objString->value.start, 
	    pattern->value.start, pattern->value.length) == 0);
}

//objString.toString:获得自己的字符串
static boolean PrimStringToString(VM* vm UNUSED, Value* args) {
   RET_VALUE(args[0]);
}

//objList.new():创建1个新的liist
static boolean PrimListNew(VM* vm, Value* args UNUSED) {
   RET_OBJ(NewObjList(vm, 0));
}

//objList[_]:索引list元素
static boolean PrimListSubscript(VM* vm, Value* args) {
   ObjList* objList = VALUE_TO_OBJLIST(args[0]);

   //数字和objRange都可以做索引,分别判断
   //若索引是数字,就直接索引1个字符,这是最简单的subscript
   if (VALUE_IS_NUM(args[1])) {
      uint32_t index = ValidateIndex(vm, args[1], objList->elements.count);
      if (index == UINT32_MAX) {
	      return false; 
      }
      RET_VALUE(objList->elements.datas[index]);
   }

   //索引要么为数字要么为ObjRange,若不是数字就应该为objRange
   if (!VALUE_IS_OBJRANGE(args[1])) {
      SET_ERROR_FALSE(vm, "subscript should be integer or range!");  
   }

   int direction;

   uint32_t count = objList->elements.count;

   //返回的startIndex是objRange.from在objList.elements.data中的下标
   uint32_t startIndex = CalculateRange(vm, VALUE_TO_OBJRANGE(args[1]), &count, &direction);

   //新建一个list 存储该range在原来list中索引的元素
   ObjList* result = NewObjList(vm, count);  
   uint32_t idx = 0;
   while (idx < count) {
      //direction为-1表示从后往前倒序赋值
      //如var l = [a,b,c,d,e,f,g]; l[5..3]表示[f,e,d]
      result->elements.datas[idx] = objList->elements.datas[startIndex + idx * direction];   
      idx++;
   }
   RET_OBJ(result);
}

//objList[_]=(_):只支持数字做为subscript
static boolean PrimListSubscriptSetter(VM* vm UNUSED, Value* args) {  
   //获取对象
   ObjList* objList = VALUE_TO_OBJLIST(args[0]);

   //获取subscript
   uint32_t index = ValidateIndex(vm, args[1], objList->elements.count);
   if (index == UINT32_MAX) {
      return false; 
   }

   //直接赋值
   objList->elements.datas[index] = args[2];

   RET_VALUE(args[2]); //把参数2做为返回值
}

//objList.add(_):直接追加到list中
static boolean PrimListAdd(VM* vm, Value* args) {
   ObjList* objList = VALUE_TO_OBJLIST(args[0]);
   ValueBufferAdd(vm, &objList->elements, args[1]);
   RET_VALUE(args[1]); //把参数1做为返回值
}

//objList.addCore_(_):编译内部使用的,用于编译列表直接量
static boolean PrimListAddCore(VM* vm, Value* args) {
   ObjList* objList = VALUE_TO_OBJLIST(args[0]);
   ValueBufferAdd(vm, &objList->elements, args[1]);  
   RET_VALUE(args[0]); //返回列表自身
}

//objList.clear():清空list
static boolean PrimListClear(VM* vm, Value* args) {
   ObjList* objList = VALUE_TO_OBJLIST(args[0]);
   ValueBufferClear(vm, &objList->elements);
   RET_NULL;
}

//objList.count:返回list中元素个数
static boolean PrimListCount(VM* vm UNUSED, Value* args) {
   RET_NUM(VALUE_TO_OBJLIST(args[0])->elements.count);
}

//objList.insert(_,_):插入元素
static boolean PrimListInsert(VM* vm, Value* args) {
   ObjList* objList = VALUE_TO_OBJLIST(args[0]);
   //+1确保可以在最后插入
   uint32_t index = ValidateIndex(vm, args[1], objList->elements.count + 1);
   if (index == UINT32_MAX) {
      return false; 
   }
   InsertElement(vm, objList, index, args[2]);
   RET_VALUE(args[2]);  //参数2做为返回值
}

//objList.iterate(_):迭代list
static boolean PrimListIterate(VM* vm, Value* args) {
   ObjList* objList = VALUE_TO_OBJLIST(args[0]);

   //如果是第一次迭代 迭代索引肯定为空 直接返回索引0
   if (VALUE_IS_NULL(args[1])) {
      if (objList->elements.count == 0) {
	      RET_FALSE;
      }
      RET_NUM(0);
   }

   //确保迭代器是整数
   if (!ValidateInt(vm, args[1])) {
      return false; 
   }

   double iter = VALUE_TO_NUM(args[1]);
   //如果迭代完了就终止
   if (iter < 0 || iter >= objList->elements.count - 1) {
      RET_FALSE;
   }
   
   RET_NUM(iter + 1);   //返回下一个
}

//objList.iteratorValue(_):返回迭代值
static boolean PrimListIteratorValue(VM* vm, Value* args) {
   //获取实例对象
   ObjList* objList = VALUE_TO_OBJLIST(args[0]);
   
   uint32_t index = ValidateIndex(vm, args[1], objList->elements.count);
   if (index == UINT32_MAX) {
      return false; 
   }

   RET_VALUE(objList->elements.datas[index]);
}

//objList.removeAt(_):删除指定位置的元素
static boolean PrimListRemoveAt(VM* vm, Value* args) {
   //获取实例对象
   ObjList* objList = VALUE_TO_OBJLIST(args[0]);

   uint32_t index = ValidateIndex(vm, args[1], objList->elements.count);
   if (index == UINT32_MAX) {
      return false; 
   }

   RET_VALUE(RemoveElement(vm, objList, index));
}

//校验key合法性
static boolean ValidateKey(VM* vm, Value arg) {
   if (VALUE_IS_TRUE(arg)     ||
	 VALUE_IS_FALSE(arg)  ||
	 VALUE_IS_NULL(arg)   ||
	 VALUE_IS_NUM(arg)    || 
	 VALUE_IS_OBJSTR(arg) ||
	 VALUE_IS_OBJRANGE(arg)  ||
	 VALUE_IS_CLASS(arg)) {
	 return true;
   }
   SET_ERROR_FALSE(vm, "key must be value type!");
}

//objMap[_]:返回map[key]对应的value
static boolean PrimMapSubscript(VM* vm, Value* args) {
   //校验key的合法性
   if (!ValidateKey(vm, args[1])) {
      return false;  //出错了,切换线程
   }

   //获得map对象实例
   ObjMap* objMap = VALUE_TO_OBJMAP(args[0]); 

   //从map中查找key(args[1])对应的value
   Value value = MapGet(objMap, args[1]);

   //若没有相应的key则返回NULL
   if (VALUE_IS_UNDEFINED(value)) {
      RET_NULL;
   }

   RET_VALUE(value);
}

//objMap[_]=(_):map[key]=value
static boolean PrimMapSubscriptSetter(VM* vm, Value* args) {
   //校验key的合法性
   if (!ValidateKey(vm, args[1])) {
      return false;  //出错了,切换线程
   }

   //获得map对象实例
   ObjMap* objMap = VALUE_TO_OBJMAP(args[0]); 

   //在map中将key和value关联
   //即map[key]=value
   MapSet(vm, objMap, args[1], args[2]);

   RET_VALUE(args[2]); //返回value 
}

//objMap.addCore_(_,_):编译器编译map字面量时内部使用的,
//在map中添加(key-value)对儿并返回map自身 
static boolean PrimMapAddCore(VM* vm, Value* args) {
   if (!ValidateKey(vm, args[1])) {
      return false;  //出错了,切换线程
   }

   //获得map对象实例
   ObjMap* objMap = VALUE_TO_OBJMAP(args[0]); 

   //在map中将key和value关联
   //即map[key]=value
   MapSet(vm, objMap, args[1], args[2]);

   RET_VALUE(args[0]);  //返回map对象自身
}

//objMap.clear():清除map
static boolean PrimMapClear(VM* vm, Value* args) {
   ClearMap(vm, VALUE_TO_OBJMAP(args[0]));
   RET_NULL;
}

//objMap.containsKey(_):判断map即args[0]是否包含key即args[1]
static boolean PrimMapContainsKey(VM* vm, Value* args) {
   if (!ValidateKey(vm, args[1])) {
      return false;  //出错了,切换线程
   }

   //直接去get该key,判断是否get成功
   RET_BOOL(!VALUE_IS_UNDEFINED(MapGet(VALUE_TO_OBJMAP(args[0]), args[1])));
}

//objMap.count:返回map中entry个数
static boolean PrimMapCount(VM* vm UNUSED, Value* args) {
   RET_NUM(VALUE_TO_OBJMAP(args[0])->count);
}

//objMap.remove(_):删除map[key] map是args[0] key是args[1]
static boolean PrimMapRemove(VM* vm, Value* args) {
   if (!ValidateKey(vm, args[1])) {
      return false;  //出错了,切换线程
   }

   RET_VALUE(RemoveKey(vm, VALUE_TO_OBJMAP(args[0]), args[1]));
}

//objMap.iterate_(_):迭代map中的entry,
//返回entry的索引供keyIteratorValue_和valueIteratorValue_做迭代器
static boolean PrimMapIterate(VM* vm, Value* args) {
   //获得map对象实例
   ObjMap* objMap = VALUE_TO_OBJMAP(args[0]); 

   //map中若空则返回false不可迭代
   if (objMap->count == 0) {
      RET_FALSE; 
   }

   //若没有传入迭代器,迭代默认是从第0个entry开始
   uint32_t index = 0;  

   //若不是第一次迭代,传进了迭代器
   if (!VALUE_IS_NULL(args[1])) {
      //iter必须为整数
      if (!ValidateInt(vm, args[1])) {
         //本线程出错了,返回false是为了切换到下一线
         return false;
      }

      //迭代器不能小0
      if (VALUE_TO_NUM(args[1]) < 0) {
	      RET_FALSE; 
      }

      index = (uint32_t)VALUE_TO_NUM(args[1]);
      //迭代器不能越界
      if (index >= objMap->capacity) {
	      RET_FALSE;  
      }

      index++;  //更新迭代器
   }

   //返回下一个正在使用(有效)的entry
   while (index < objMap->capacity) {
      //entries是个数组, 元素是哈希槽,
      //哈希值散布在这些槽中并不连续,因此逐个判断槽位是否在用
      if (!VALUE_IS_UNDEFINED(objMap->entries[index].key)) {
	      RET_NUM(index);    //返回entry索引
      }
      index++;
   }
   
   //若没有有效的entry了就返回false,迭代结束
   RET_FALSE;
}

//objMap.keyIteratorValue_(_): key=map.keyIteratorValue(iter)
static boolean PrimMapKeyIteratorValue(VM* vm, Value* args) {
   ObjMap* objMap = VALUE_TO_OBJMAP(args[0]);
   
   uint32_t index = ValidateIndex(vm, args[1], objMap->capacity);
   if (index == UINT32_MAX) {
      return false; 
   }

   Entry* entry = &objMap->entries[index];
   if (VALUE_IS_UNDEFINED(entry->key)) {
      SET_ERROR_FALSE(vm, "inValid iterator!");
   }

   //返回该key
   RET_VALUE(entry->key);   
}

//objMap.valueIteratorValue_(_): 
//value = map.valueIteratorValue_(iter)
static boolean PrimMapValueIteratorValue(VM* vm, Value* args) {
   ObjMap* objMap = VALUE_TO_OBJMAP(args[0]);
   
   uint32_t index = ValidateIndex(vm, args[1], objMap->capacity);
   if (index == UINT32_MAX) {
      return false; 
   }

   Entry* entry = &objMap->entries[index];
   if (VALUE_IS_UNDEFINED(entry->key)) {
      SET_ERROR_FALSE(vm, "inValid iterator!");
   }

   //返回该key
   RET_VALUE(entry->value);   
}

static boolean PrimRangeFrom(VM* vm UNUSED, Value* args) {
   RET_NUM(VALUE_TO_OBJRANGE(args[0])->from);
}

//objRange.to: 返回range的to
static boolean PrimRangeTo(VM* vm UNUSED, Value* args) {
   RET_NUM(VALUE_TO_OBJRANGE(args[0])->to);
}

//objRange.min: 返回range中from和to较小的值
static boolean PrimRangeMin(VM* vm UNUSED, Value* args) {
   ObjRange* objRange = VALUE_TO_OBJRANGE(args[0]);
   RET_NUM(fmin(objRange->from, objRange->to));
}

//objRange.max: 返回range中from和to较大的值
static boolean PrimRangeMax(VM* vm UNUSED, Value* args) {
   ObjRange* objRange = VALUE_TO_OBJRANGE(args[0]);
   RET_NUM(fmax(objRange->from, objRange->to));
}

//objRange.iterate(_): 迭代range中的值,并不索引
static boolean PrimRangeIterate(VM* vm, Value* args) {
   ObjRange* objRange = VALUE_TO_OBJRANGE(args[0]);

   //若未提供iter说明是第一次迭代,因此返回range->from   
   if (VALUE_IS_NULL(args[1])) {
      RET_NUM(objRange->from);
   }

   //迭代器必须是数字
   if (!ValidateNum(vm, args[1])) {
      return false;
   }

   //获得迭代器
   double iter = VALUE_TO_NUM(args[1]);

   //若是正方向
   if (objRange->from < objRange->to) {
      iter++;
      if (iter > objRange->to) {
	 RET_FALSE;
      }
   } else {  //若是反向迭代
      iter--; 
      if (iter < objRange->to) {
	 RET_FALSE;
      }
   }

   RET_NUM(iter);
}

//objRange.iteratorValue(_): range的迭代就是range中从from到to之间的值
//因此直接返回迭代器就是range的值
static boolean PrimRangeIteratorValue(VM* vm UNUSED, Value* args) {
   ObjRange* objRange = VALUE_TO_OBJRANGE(args[0]);
   double value = VALUE_TO_NUM(args[1]);

   //确保args[1]在from和to的范围中
   //若是正方向
   if (objRange->from < objRange->to) {
      if (value >= objRange->from && value <= objRange->to) {
	 RET_VALUE(args[1]); 
      }
   } else {  //若是反向迭代
      if (value <= objRange->from && value >= objRange->to) {
	 RET_VALUE(args[1]); 
      }
   }
   RET_FALSE;
}

static char* GetFilePath(const char* moduleName) {
   uint32_t rootDirLength = rootDir == NULL ? 0 : strlen(rootDir);
   uint32_t nameLength = strlen(moduleName);
   uint32_t pathLength = rootDirLength + nameLength + strlen(".sp");
   char* path = (char*)malloc(pathLength + 1);

   if (rootDir != NULL) {
       memmove(path, rootDir, rootDirLength);
   }

   memmove(path + rootDirLength, moduleName, nameLength);
   memmove(path + rootDirLength + nameLength, ".sp", 3);
   path[pathLength] = '\0';
   
   return path;
}

//读取模块
static char* ReadModule(const char* moduleName) {
   //1 读取内建模块  先放着
 
   //2 读取自定义模块
   char* modulePath = GetFilePath(moduleName);
   char* moduleCode = ReadFile(modulePath);
   free(modulePath);

   return moduleCode;  //由主调函数将来free此空间
}

//输出字符串
static void PrintString(const char* str) {
   //输出到缓冲区后立即刷新
   printf("%s", str);
   fflush(stdout);
}

//导入模块moduleName,主要是把编译模块并加载到vm->allModules
static Value ImportModule(VM* vm, Value moduleName) {
   //若已经导入则返回NULL_VAL
   if (!VALUE_IS_UNDEFINED(MapGet(vm->allModules, moduleName))) {
      return VT_TO_VALUE(VT_NULL);   
   }
   ObjString* objString = VALUE_TO_OBJSTR(moduleName);
   const char* sourceCode = ReadModule(objString->value.start);

   ObjThread* moduleThread = LoadModule(vm, moduleName, sourceCode);
   return OBJ_TO_VALUE(moduleThread);
}

//在模块moduleName中获取模块变量variableName
static Value GetModuleVariable(VM* vm, Value moduleName, Value variableName) {
   //调用本函数前模块已经被加载了
   ObjModule* objModule = GetModule(vm, moduleName); 
   if (objModule == NULL) {
      ObjString* modName = VALUE_TO_OBJSTR(moduleName);

      //24是下面sprintf中fmt中除%s的字符个数
      ASSERT(modName->value.length < 512 - 24, "id`s buffer not big enough!");
      char id[512] = {'\0'};
      int len = sprintf(id, "module \'%s\' is not loaded!", modName->value.start);
      vm->curThread->errorObj = OBJ_TO_VALUE(NewObjString(vm, id, len));
      return VT_TO_VALUE(VT_NULL);
   }

   ObjString* varName = VALUE_TO_OBJSTR(variableName);

   //从moduleVarName中获得待导入的模块变量
   int index = GetIndexFromSymbolTable(&objModule->moduleVarName,
	     varName->value.start, varName->value.length);

   if (index == -1) {
      //32是下面sprintf中fmt中除%s的字符个数
      ASSERT(varName->value.length < 512 - 32, "id`s buffer not big enough!");
      ObjString* modName = VALUE_TO_OBJSTR(moduleName);
      char id[512] = {'\0'};
      int len = sprintf(id, "variable \'%s\' is not in module \'%s\'!",
	    varName->value.start, modName->value.start);
      vm->curThread->errorObj = OBJ_TO_VALUE(NewObjString(vm, id, len));
      return VT_TO_VALUE(VT_NULL);
   }

   //直接返回对应的模块变量
   return objModule->moduleVarValue.datas[index];
}

//System.clock: 返回以秒为单位的系统时钟
static boolean PrimSystemClock(VM* vm UNUSED, Value* args UNUSED) {
   RET_NUM((double)time(NULL));
}

//System.importModule(_): 导入并编译模块args[1],把模块挂载到vm->allModules
static boolean PrimSystemImportModule(VM* vm, Value* args) {
   if (!ValidateString(vm, args[1])) { //模块名为字符串
      return false;
   }

   //导入模块name并编译 把模块挂载到vm->allModules
   Value result = ImportModule(vm, args[1]);

   //若已经导入过则返回NULL_VAL
   if (VALUE_IS_NULL(result)) {
      RET_NULL;
   }

   //若编译过程中出了问题,切换到下一线程
   if (!VALUE_IS_NULL(vm->curThread->errorObj)) {
      return false;
   }

   //回收1个slot空间
   vm->curThread->esp--;

   ObjThread* nextThread = VALUE_TO_OBJTHREAD(result);
   nextThread->caller = vm->curThread;
   vm->curThread = nextThread;
   //返回false,vm会切换到此新加载模块的线程
   return false;
}

//System.getModuleVariable(_,_): 获取模块args[1]中的模块变量args[2]
static boolean PrimSystemGetModuleVariable(VM* vm, Value* args) {
   if (!ValidateString(vm, args[1])) {
      return false;
   }

   if (!ValidateString(vm, args[2])) {
      return false;
   }
   
   Value result = GetModuleVariable(vm, args[1], args[2]);
   if (VALUE_IS_NULL(result)) {
      //出错了,给vm返回false以切换线程
      return false;
   }

   RET_VALUE(result);
}

//System.writeString_(_): 输出字符串args[1]
static boolean PrimSystemWriteString(VM* vm UNUSED, Value* args) {
   ObjString* objString = VALUE_TO_OBJSTR(args[1]);
   ASSERT(objString->value.start[objString->value.length] == '\0', "string isn`t terminated!");
   PrintString(objString->value.start);
   RET_VALUE(args[1]);
}

//objMap.new():创建map对象
static boolean PrimMapNew(VM* vm, Value* args UNUSED) {
   RET_OBJ(NewObjMap(vm));
}

/**
 * @brief 编译核心模块
 * @details 在读取源码文件之前，先编译核心模块
*/
void BuildCore(VM *vm)
{
    ObjModule *coreModule = NewObjModule(vm, NULL); // NULL为核心模块
    // 创建核心模块，录入vm->allModule  {vt_null: coremodule}
    MapSet(vm, vm->allModules, CORE_MODULE, OBJ_TO_VALUE(coreModule));
  
    // 创建object类并绑定方法 放入coreModule->moduleVarName
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
    ExecuteModule(vm, CORE_MODULE, g_coreModuleCode);

    // bool类定义在inc中，将其挂在Bool类到vm->boolClass
    vm->boolClass = VALUE_TO_CLASS(GetCoreClassValue(coreModule, "Bool"));
    PRIM_METHOD_BIND(vm->boolClass, "ToString", PrimBoolToString);
    PRIM_METHOD_BIND(vm->boolClass, "!", PrimBoolNot);

   //绑定num类方法
   vm->numClass = VALUE_TO_CLASS(GetCoreClassValue(coreModule, "Num"));
   //类方法
   PRIM_METHOD_BIND(vm->numClass->objHeader.class, "fromString(_)", PrimNumFromString);
   PRIM_METHOD_BIND(vm->numClass->objHeader.class, "pi", PrimNumPi);
   //实例方法 
   PRIM_METHOD_BIND(vm->numClass, "+(_)", PrimNumPlus);
   PRIM_METHOD_BIND(vm->numClass, "-(_)", PrimNumMinus);
   PRIM_METHOD_BIND(vm->numClass, "*(_)", PrimNumMul);
   PRIM_METHOD_BIND(vm->numClass, "/(_)", PrimNumDiv);
   PRIM_METHOD_BIND(vm->numClass, ">(_)", PrimNumGt);
   PRIM_METHOD_BIND(vm->numClass, ">=(_)", PrimNumGe);
   PRIM_METHOD_BIND(vm->numClass, "<(_)", PrimNumLt);
   PRIM_METHOD_BIND(vm->numClass, "<=(_)", PrimNumLe);

   //位运算
   PRIM_METHOD_BIND(vm->numClass, "&(_)", PrimNumBitAnd);
   PRIM_METHOD_BIND(vm->numClass, "|(_)", PrimNumBitOr);
   PRIM_METHOD_BIND(vm->numClass, ">>(_)", PrimNumBitShiftRight);
   PRIM_METHOD_BIND(vm->numClass, "<<(_)", PrimNumBitShiftLeft);
   //以上都是通过rules中INFIX_OPERATOR来解析的

   //下面大多数方法是通过rules中'.'对应的led(callEntry)来解析,
   //少数符号依然是INFIX_OPERATOR解析
   PRIM_METHOD_BIND(vm->numClass, "abs", PrimNumAbs);
   PRIM_METHOD_BIND(vm->numClass, "acos", PrimNumAcos);
   PRIM_METHOD_BIND(vm->numClass, "asin", PrimNumAsin);
   PRIM_METHOD_BIND(vm->numClass, "atan", PrimNumAtan);
   PRIM_METHOD_BIND(vm->numClass, "ceil", PrimNumCeil);
   PRIM_METHOD_BIND(vm->numClass, "cos", PrimNumCos);
   PRIM_METHOD_BIND(vm->numClass, "floor", PrimNumFloor);
   PRIM_METHOD_BIND(vm->numClass, "-", PrimNumNegate);
   PRIM_METHOD_BIND(vm->numClass, "sin", PrimNumSin);
   PRIM_METHOD_BIND(vm->numClass, "sqrt", PrimNumSqrt);
   PRIM_METHOD_BIND(vm->numClass, "tan", PrimNumTan);
   PRIM_METHOD_BIND(vm->numClass, "%(_)", PrimNumMod);
   PRIM_METHOD_BIND(vm->numClass, "~", PrimNumBitNot);
   PRIM_METHOD_BIND(vm->numClass, "..(_)", PrimNumRange);
   PRIM_METHOD_BIND(vm->numClass, "atan(_)", PrimNumAtan2);
   PRIM_METHOD_BIND(vm->numClass, "fraction", PrimNumFraction);
   PRIM_METHOD_BIND(vm->numClass, "isInfinity", PrimNumIsInfinity);
   PRIM_METHOD_BIND(vm->numClass, "isInteger", PrimNumIsInteger);
   PRIM_METHOD_BIND(vm->numClass, "isNan", PrimNumIsNan);
   PRIM_METHOD_BIND(vm->numClass, "toString", PrimNumToString);
   PRIM_METHOD_BIND(vm->numClass, "truncate", PrimNumTruncate);
   PRIM_METHOD_BIND(vm->numClass, "==(_)", PrimNumEqual);
   PRIM_METHOD_BIND(vm->numClass, "!=(_)", PrimNumNotEqual);

   //绑定Null类的方法
   vm->nullClass = VALUE_TO_CLASS(GetCoreClassValue(coreModule, "Null"));
   PRIM_METHOD_BIND(vm->nullClass, "!", PrimNullNot);
   PRIM_METHOD_BIND(vm->nullClass, "toString", PrimNullToString);

   //绑定函数类
   vm->fnClass = VALUE_TO_CLASS(GetCoreClassValue(coreModule, "Fn"));
   PRIM_METHOD_BIND(vm->fnClass->objHeader.class, "new(_)", PrimFnNew);

   //绑定call的重载方法
   BindFnOverloadCall(vm, "call()");
   BindFnOverloadCall(vm, "call(_)");
   BindFnOverloadCall(vm, "call(_,_)");
   BindFnOverloadCall(vm, "call(_,_,_)");
   BindFnOverloadCall(vm, "call(_,_,_,_)");
   BindFnOverloadCall(vm, "call(_,_,_,_,_)");
   BindFnOverloadCall(vm, "call(_,_,_,_,_,_)");
   BindFnOverloadCall(vm, "call(_,_,_,_,_,_,_)");
   BindFnOverloadCall(vm, "call(_,_,_,_,_,_,_,_)");
   BindFnOverloadCall(vm, "call(_,_,_,_,_,_,_,_,_)");
   BindFnOverloadCall(vm, "call(_,_,_,_,_,_,_,_,_,_)");
   BindFnOverloadCall(vm, "call(_,_,_,_,_,_,_,_,_,_,_)");
   BindFnOverloadCall(vm, "call(_,_,_,_,_,_,_,_,_,_,_,_)");
   BindFnOverloadCall(vm, "call(_,_,_,_,_,_,_,_,_,_,_,_,_)");
   BindFnOverloadCall(vm, "call(_,_,_,_,_,_,_,_,_,_,_,_,_,_)");
   BindFnOverloadCall(vm, "call(_,_,_,_,_,_,_,_,_,_,_,_,_,_,_)");
   BindFnOverloadCall(vm, "call(_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_)");

   //Thread类也是在core.script.inc中定义的,
   //将其挂载到vm->threadClass并补充原生方法
   vm->threadClass = VALUE_TO_CLASS(GetCoreClassValue(coreModule, "Thread"));
   //以下是类方法
   PRIM_METHOD_BIND(vm->threadClass->objHeader.class, "new(_)", PrimThreadNew);
   PRIM_METHOD_BIND(vm->threadClass->objHeader.class, "abort(_)", PrimThreadAbort);
   PRIM_METHOD_BIND(vm->threadClass->objHeader.class, "current", PrimThreadCurrent);
   PRIM_METHOD_BIND(vm->threadClass->objHeader.class, "suspend()", PrimThreadSuspend);
   PRIM_METHOD_BIND(vm->threadClass->objHeader.class, "yield(_)", PrimThreadYieldWithArg);
   PRIM_METHOD_BIND(vm->threadClass->objHeader.class, "yield()", PrimThreadYieldWithoutArg);
   //以下是实例方法
   PRIM_METHOD_BIND(vm->threadClass, "call()", PrimThreadCallWithoutArg);
   PRIM_METHOD_BIND(vm->threadClass, "call(_)", PrimThreadCallWithArg);
   PRIM_METHOD_BIND(vm->threadClass, "isDone", PrimThreadIsDone);

   //字符串类
   vm->stringClass = VALUE_TO_CLASS(GetCoreClassValue(coreModule, "String"));
   PRIM_METHOD_BIND(vm->stringClass->objHeader.class, "fromCodePoint(_)", PrimStringFromCodePoint);
   PRIM_METHOD_BIND(vm->stringClass, "+(_)", PrimStringPlus);
   PRIM_METHOD_BIND(vm->stringClass, "[_]", PrimStringSubscript);
   PRIM_METHOD_BIND(vm->stringClass, "byteAt_(_)", PrimStringByteAt);
   PRIM_METHOD_BIND(vm->stringClass, "byteCount_", PrimStringByteCount);
   PRIM_METHOD_BIND(vm->stringClass, "codePointAt_(_)", PrimStringCodePointAt);
   PRIM_METHOD_BIND(vm->stringClass, "contains(_)", PrimStringContains);
   PRIM_METHOD_BIND(vm->stringClass, "endsWith(_)", PrimStringEndsWith);
   PRIM_METHOD_BIND(vm->stringClass, "indexOf(_)", PrimStringIndexOf);
   PRIM_METHOD_BIND(vm->stringClass, "iterate(_)", PrimStringIterate);
   PRIM_METHOD_BIND(vm->stringClass, "iterateByte_(_)", PrimStringIterateByte);
   PRIM_METHOD_BIND(vm->stringClass, "iteratorValue(_)", PrimStringIteratorValue);
   PRIM_METHOD_BIND(vm->stringClass, "startsWith(_)", PrimStringStartsWith);
   PRIM_METHOD_BIND(vm->stringClass, "toString", PrimStringToString);
   PRIM_METHOD_BIND(vm->stringClass, "count", PrimStringByteCount);

   //List类
   vm->listClass = VALUE_TO_CLASS(GetCoreClassValue(coreModule, "List"));
   PRIM_METHOD_BIND(vm->listClass->objHeader.class, "new()", PrimListNew);
   PRIM_METHOD_BIND(vm->listClass, "[_]", PrimListSubscript);
   PRIM_METHOD_BIND(vm->listClass, "[_]=(_)", PrimListSubscriptSetter);
   PRIM_METHOD_BIND(vm->listClass, "add(_)", PrimListAdd);
   PRIM_METHOD_BIND(vm->listClass, "addCore_(_)", PrimListAddCore);
   PRIM_METHOD_BIND(vm->listClass, "clear()", PrimListClear);
   PRIM_METHOD_BIND(vm->listClass, "count", PrimListCount);
   PRIM_METHOD_BIND(vm->listClass, "insert(_,_)", PrimListInsert);
   PRIM_METHOD_BIND(vm->listClass, "iterate(_)", PrimListIterate);
   PRIM_METHOD_BIND(vm->listClass, "iteratorValue(_)", PrimListIteratorValue);
   PRIM_METHOD_BIND(vm->listClass, "removeAt(_)", PrimListRemoveAt);

   //map类
   vm->mapClass = VALUE_TO_CLASS(GetCoreClassValue(coreModule, "Map"));
   PRIM_METHOD_BIND(vm->mapClass->objHeader.class, "new()", PrimMapNew);
   PRIM_METHOD_BIND(vm->mapClass, "[_]", PrimMapSubscript);
   PRIM_METHOD_BIND(vm->mapClass, "[_]=(_)", PrimMapSubscriptSetter);
   PRIM_METHOD_BIND(vm->mapClass, "addCore_(_,_)", PrimMapAddCore);
   PRIM_METHOD_BIND(vm->mapClass, "clear()", PrimMapClear);
   PRIM_METHOD_BIND(vm->mapClass, "containsKey(_)", PrimMapContainsKey);
   PRIM_METHOD_BIND(vm->mapClass, "count", PrimMapCount);
   PRIM_METHOD_BIND(vm->mapClass, "remove(_)", PrimMapRemove);
   PRIM_METHOD_BIND(vm->mapClass, "iterate_(_)", PrimMapIterate);
   PRIM_METHOD_BIND(vm->mapClass, "keyIteratorValue_(_)", PrimMapKeyIteratorValue);
   PRIM_METHOD_BIND(vm->mapClass, "valueIteratorValue_(_)", PrimMapValueIteratorValue);

   //range类
   vm->rangeClass = VALUE_TO_CLASS(GetCoreClassValue(coreModule, "Range"));
   PRIM_METHOD_BIND(vm->rangeClass, "from", PrimRangeFrom);
   PRIM_METHOD_BIND(vm->rangeClass, "to", PrimRangeTo);
   PRIM_METHOD_BIND(vm->rangeClass, "min", PrimRangeMin); 
   PRIM_METHOD_BIND(vm->rangeClass, "max", PrimRangeMax);
   PRIM_METHOD_BIND(vm->rangeClass, "iterate(_)", PrimRangeIterate);
   PRIM_METHOD_BIND(vm->rangeClass, "iteratorValue(_)", PrimRangeIteratorValue);

   //system类
   Class* systemClass = VALUE_TO_CLASS(GetCoreClassValue(coreModule, "System"));
   PRIM_METHOD_BIND(systemClass->objHeader.class, "clock", PrimSystemClock);
   PRIM_METHOD_BIND(systemClass->objHeader.class, "importModule(_)", PrimSystemImportModule);
   PRIM_METHOD_BIND(systemClass->objHeader.class, "getModuleVariable(_,_)", PrimSystemGetModuleVariable);
   PRIM_METHOD_BIND(systemClass->objHeader.class, "writeString_(_)", PrimSystemWriteString);

   // 在核心自举创建了很多objstring对象
   ObjHeader *objHeader = vm->allObjects;
   while (objHeader != NULL) {
      if (objHeader->type == OT_STRING) {
         objHeader->class = vm->stringClass;
      }
      objHeader = objHeader->next;
   }
}