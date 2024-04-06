/*
 * @Author: LiuHao
 * @Date: 2024-03-30 22:46:56
 * @Description: 
 */
#include "vm.h"
#include <stdlib.h>
#include "utils.h"
#include "obj_thread.h"
#include "header_obj.h"
#include "compile.h"

void InitVM(VM *vm)
{
    vm->allocatedBytes = 0;
    vm->curParser = NULL;
    vm->allObjects = NULL;
}

VM* NewVM(void)
{
    VM *vm = (VM *)malloc(sizeof(VM));
    if (vm == NULL) {
        MEM_ERROR("Allocate vm Fail!");
    }
    InitVM(vm);
    // BuildCore(vm); // 在读取源码之前先编译核心模块
    return vm;
}

/**
 * @brief 确保stack有效
*/
void EnsureStack(VM *vm, ObjThread *objThread, const uint32_t needSlots)
{
    if (objThread->stackCapacity >= needSlots) {
        return ;
    } else {
        // else
    }
    uint32_t newStackCapacity = CeilToPowerOf2(needSlots);
    ASSERT(newStackCapacity > objThread->stackCapacity, "newStackCapacity error!");

    // 记录原栈底以用于下面判断扩容后的栈是否原地扩容
    Value *oldStackBottom = objThread->stack;
    uint32_t slotSize = sizeof(Value);
    objThread->stack = (Value *)MemManager(vm, objThread->stack, objThread->stackCapacity * slotSize, newStackCapacity);
    objThread->stackCapacity = newStackCapacity;

    // 判断是否原地扩容
    long offset = objThread->stack - oldStackBottom;

    // 说明os无法在原地满足内存需求，充分分配了起始地址
    if (offset != 0) {
        uint32_t idx = 0;
        while (idx < objThread->usedFrameNum) { // 更新各堆栈框架中的起始地址stackStart
            objThread->frames[idx ++].stackStart += offset;
        }
        // 更新upvalue中localVarPtr指向的内存块中的内存地址（栈中slot地址）
        ObjUpvalue *objUpvalue = objThread->openUpvalues;
        while (objUpvalue) {
            objUpvalue->localVarPtr += offset;
            objUpvalue = objUpvalue->next;
        }
        // 更新栈顶
        objThread->esp += offset;
    }
}

/**
 * @brief 为objClosure在ObjThread中创建运行时栈
*/
inline static void CreateFrame(VM *vm, ObjThread *objThread, ObjClosure *objClosure, const int argNum)
{
    if (objThread->usedFrameNum + 1 > objThread->frameCapacity) { // 扩容
        uint32_t newCapacity = objThread->frameCapacity * 2U;
        uint32_t frameSize = sizeof(Frame);
        objThread->frames = (Frame *)MemManager(vm, objThread->frames, 
                frameSize * (objThread->frameCapacity), frameSize * newCapacity);
        objThread->frameCapacity = newCapacity;
    }

    // 栈大小等于栈顶 - 栈底
    uint32_t stackSlots = (uint32_t)(objThread->esp - objThread->stack);
    // 总共需要的栈大小
    uint32_t needSlots = stackSlots + objClosure->fn->maxStackSlotUsedNum;

    EnsureStack(vm, objThread, needSlots);

    // 准备上CPU
    // objThread->esp - argNum是被调用函数的堆栈框架在站中的起始地址
    // 减去了argNum，目的是被调用的闭包函数objClosure可以访问到栈中自己的参数
    // argNum占用的框架在编译过程中已经算在了maxStackSlotUsedNum中了
    PrepareFrame(objThread, objClosure, objThread->esp - argNum); 
}

/**
 * @brief upvalue的关闭
 * 本函数要关闭的马上要出作用域的局部变量，因此该作用域及其之内嵌套更深作用域的局部变量都应该回收
 * 地址位于栈顶lastSlot后面的肯定作用域更深，栈顶是向高地址发展的
*/
static void ClosedUpvalue(ObjThread *objThread, Value *lastSlot)
{
    ObjUpvalue *objUpvalue = objThread->openUpvalues; // openUpvalues是在本线程中已经打开过的upvalue的链表首节点
    // objUpvalue->localVarPtr >= lastSlot是需要被关闭的局部变量的条件
    while ((objUpvalue != NULL) && (objUpvalue->localVarPtr >= lastSlot)) {
        objUpvalue->closedUpvalue = *(objUpvalue->localVarPtr); // 被销毁的局部变量会放到closedUpvalue
        objUpvalue->localVarPtr = &(objUpvalue->closedUpvalue); // localVarPtr指向运行时栈中的局部变量改为本结构中的closedUpvalue
        objUpvalue = objUpvalue->next;
    }
    objThread->openUpvalues = objUpvalue;
}

/**
 * @brief 创建线程已打开的upvalue链表，并将localVarPtr所属的upvalue以降序插入到该链表
*/
static ObjUpvalue* CreateOpenUpvalue(VM *vm, ObjThread *objThread, Value *localVarPtr)
{
    if (objThread->openUpvalues == NULL) {
        objThread->openUpvalues = NewObjUpvalue(vm, localVarPtr);
        return objThread->openUpvalues;
    }

    ObjUpvalue *preUpvalue = NULL;
    ObjUpvalue *objUpvalue = objThread->openUpvalues;

    // 降序组织，找到合适的插入位置
    while ((objUpvalue != NULL) && (objUpvalue->localVarPtr > localVarPtr)) {
        preUpvalue = objUpvalue;
        objUpvalue = objUpvalue->next;
    }

    // 如果已插入则返回
    if ((objUpvalue != NULL) && (objUpvalue->localVarPtr == localVarPtr)) {
        return objUpvalue;
    }

    ObjUpvalue *newUpvalue = NewObjUpvalue(vm, localVarPtr);

    if (preUpvalue == NULL) {
        objThread->openUpvalues = newUpvalue;
    } else {
        preUpvalue->next = newUpvalue;
    }

    newUpvalue->next = objUpvalue;
    return newUpvalue; // 返回该节点
}

/**
 * @brief 校验基类合法性
*/
static void IsValidateSuperClass(VM *vm, Value classNameValue, uint32_t fieldNum, Value superClassValue)
{
    if (!VALUE_IS_CLASS(superClassValue)) {
        ObjString *classNameString = VALUE_TO_OBJSTR(classNameValue);
        RUNTIME_ERROR("Class \"$s\" 's superClass is not a valid class!", classNameString->value.start);
    }

    Class *superClass = VALUE_TO_CLASS(superClassValue);
    // 基类不允许内建类
    if ((superClass == vm->stringClass) ||
        (superClass == vm->mapClass)    ||
        (superClass == vm->rangeClass)  ||
        (superClass == vm->listClass)   ||
        (superClass == vm->nullClass)   ||
        (superClass == vm->boolClass)   ||
        (superClass == vm->numClass)    ||
        (superClass == vm->fnClass)     ||
        (superClass == vm->threadClass)) {
        RUNTIME_ERROR("SuperClass mustn't be a build in class!");
    }

    // 子类也有继承基类的域
    // 子类自己的域+基类的域 不可超过最大值
    if ((superClass->fieldNum + fieldNum) > MAX_FIELD_NUM) {
        RUNTIME_ERROR("number of field including super exceed %d!", MAX_FIELD_NUM);
    }
}

/**
 * @brief 修正部分指令操作数
 * 运行时阶段运行 动态绑定
*/
static void  PatchOperand(Class *class, ObjFn *fn)
{
    int ip = 0;
    OpCode opCode;
    while (true) {
        opCode = (OpCode)(fn->instructStream.datas[ip ++]);
        switch (opCode) {
            case OPCODE_LOAD_FIELD:
            case OPCODE_STORE_FILED:
            case OPCODE_LOAD_THIS_FILED:
            case OPCODE_STORE_THIS_FIELD:
                // 修正子类的field数目，参数是1Byte
                fn->instructStream.datas[ip ++] += class->superClass->fieldNum;
                break;
            case OPCODE_SUPER0:
            case OPCODE_SUPER1:
            case OPCODE_SUPER2:
            case OPCODE_SUPER3:
            case OPCODE_SUPER4:
            case OPCODE_SUPER5:
            case OPCODE_SUPER6:
            case OPCODE_SUPER7:
            case OPCODE_SUPER8:
            case OPCODE_SUPER9:
            case OPCODE_SUPER10:
            case OPCODE_SUPER11:
            case OPCODE_SUPER12:
            case OPCODE_SUPER13:
            case OPCODE_SUPER14:
            case OPCODE_SUPER15:
            case OPCODE_SUPER16: {
                // 指令流1 2字节的method索引，指令流2 2字节的基类常量索引
                ip += 2; // 跳过2字节的method索引
                uint32_t superClassIdx = (fn->instructStream.datas[ip] << 8) | (fn->instructStream.datas[ip + 1] << 8);

                // 回填在函数emitCallBySignature中的占位VT_TO_VALUE(VT_NULL)
                fn->constants.datas[superClassIdx] = OBJ_TO_VALUE(class->superClass);
                ip += 2; // 跳过2字节的基类索引
                break;
            }
            case OPCODE_CREATE_CLOSURE: // TODO:
                uint32_t fnIdx = (fn->instructStream.datas[ip] << 8) | (fn->instructStream.datas[ip + 1] << 8);
                PatchOperand(class, VALUE_TO_OBJFN(fn->constants.datas[fnIdx]));
                ip += GetByteOfOperands(fn->instructStream.datas, fn->constants.datas, ip - 1);
                break;
                break;
            case OPCODE_END: // 用于从当前及递归嵌套闭包时返回
                return ;
            default:
                ip += GetByteOfOperands(fn->instructStream.datas, fn->constants.datas, ip - 1);
                break;
        }
    }
}

/**
 * @brief 确定方法和修正操作数
*/
static void BindMethodAndPatch(VM *vm, OpCode opCode, uint32_t methodIdx, Class *class, Value methodValue)
{
    // 如果是静态方法，就将类指向meta类
    if (opCode == OPCODE_STATIC_METHOD) {
        class = class->objHeader.class;
    }

    Method method;
    method.type = MT_SCRIPT;
    method.obj = VALUE_TO_OBJCLOSURE(methodValue);

    // 修正操作数
    PatchOperand(class, method.obj->fn);
    BindMethod(vm, class, methodIdx, method);
}

/**
 * @brief 执行指令
*/
VMResult ExecuteInstruction(VM *vm, register ObjThread *curThread)
{
    vm->curThread = curThread;
    register Frame* curFrame;
    register Value* stackStart;
    register uint8_t* ip;
    ObjFn *objFn;
    OpCode opCode;

    // 定义操作运行时的栈
    // esp是栈中下一个可写入数据的slot
    #define PUSH(value) (*curThread->esp ++ = value)
    #define POP() (*(-- curThread->esp))
    #define DROP() (curThread->esp --)  // 丢掉栈顶元素
    #define PEEK() (*(curThread->esp - 1)) // 获取栈顶数据
    #define PEEK2() (*(curThread->esp - 2)) // 获得次栈顶的数据
    // 读指令
    #define READ_BYTE() (ip ++)
    #define READ_SHORT() (ip += 2, (uint16_t)(ip[-2] << 8 | ip[-1]))
    #define STORE_CUR_FRAME() curFrame->ip = ip // 备份IP

    // 加载最新的frame
    #define LOAD_CUR_FRAME()  \
        curFrame = &curThread->frames[curThread->usedFrameNum - 1]; \
        stackStart = curFrame->stackStart; \
        ip = curFrame->ip; \
        objFn = curFrame->closure->fn;
    #define DECODE loopStart: \
        opCode = READ_BYTE(); \
        switch (opCode)
    #define CASE(shortOpCode) case OPCODE_##shortOpCode
    #define LOOP() goto loopStart

    LOAD_CUR_FRAME();
    DECODE {
        // 第一部分操作码
        CASE(LOAD_LOCAL_VAR):
            PUSH(stackStart[(uint8_t)READ_BYTE()]);
            LOOP();
        CASE(LOAD_THIS_FILED): {

        }
        CASE(POP):
            DROP();
            LOOP();
        CASE(PUSH_NULL):
            PUSH(VT_TO_VALUE(VT_NULL));
            LOOP();
        CASE(PUSH_FALSE):
            PUSH(VT_TO_VALUE(VT_FALSE));
            LOOP();
        CASE(PUSH_TRUE):
            PUSH(VT_TO_VALUE(VT_TRUE));
            LOOP();
        CASE(STORE_LOCAL_VAR):
            stackStart[(uint8_t)READ_BYTE()] = PEEK();
            LOOP();
        CASE(LOAD_CONSTANT):
            PUSH(objFn->constants.datas[(uint16_t)READ_SHORT()]);
            LOOP();
        {
            int argNum, index;
            Value *args;
            Class *class;
            Method *method;
            CASE(CALL0):
            CASE(CALL1):
            CASE(CALL2):
            CASE(CALL3):
            CASE(CALL4):
            CASE(CALL5):
            CASE(CALL6):
            CASE(CALL7):
            CASE(CALL8):
            CASE(CALL9):
            CASE(CALL10):
            CASE(CALL11):
            CASE(CALL12):
            CASE(CALL13):
            CASE(CALL14):
            CASE(CALL15):
            CASE(CALL16): // 指令流 2字节的method索引
                argNum = opCode - OPCODE_CALL0 + 1; // 所调用方法的参数个数
                index = READ_SHORT(); // 方法名的索引
                args = curThread->esp - argNum; // 调用方法的参数数组
                class = GetClassOfObj(vm, args[0]); // 调用方法所在的类
                goto invokeMethod;
            CASE(SUPER0):
            CASE(SUPER1):
            CASE(SUPER2):
            CASE(SUPER3):
            CASE(SUPER4):
            CASE(SUPER5):
            CASE(SUPER6):
            CASE(SUPER7):
            CASE(SUPER8):
            CASE(SUPER9):
            CASE(SUPER10):
            CASE(SUPER11):
            CASE(SUPER12):
            CASE(SUPER13):
            CASE(SUPER14):
            CASE(SUPER15):
            CASE(SUPER16): // 指令流1 2字节的method索引 指令流2 2字节的基类常量索引
                argNum = opCode - OPCODE_SUPER0 + 1;
                index = READ_SHORT(); // 方法名的索引
                args = curThread->esp - argNum; // 调用方法的参数数组
                class = VALUE_TO_CLASS(objFn->constants.datas[(uint16_t)READ_SHORT()]);
            invokeMethod:
                if (((uint32_t)index > class->methods.count) ||
                   ((method = &class->methods.datas[index])->type == MT_NONE)) {
                    RUNTIME_ERROR("Method not found!\n");
                }
                switch (method->type) {
                    case MT_PRIMITIVE: // 原生方法
                        // TODO: 
                    case MT_SCRIPT:  // 脚本方法
                        STORE_CUR_FRAME();
                        CreateFrame(vm, curThread, (ObjClosure *)method->type, argNum);
                        LOAD_CUR_FRAME(); // 加载最新的页帧
                        break;
                    case MT_FN_CALL: // 处理函数调用
                        // TODO: ASSERT()
                        ObjFn *fn = VALUE_TO_OBJCLOSURE(args[0])->fn;
                        // -1是去掉实例this
                        if ((argNum - 1) < fn->argNum) {
                            RUNTIME_ERROR("Argument less");
                        }
                        STORE_CUR_FRAME();
                        CreateFrame(vm, curThread, (ObjClosure *)method->type, argNum);
                        LOAD_CUR_FRAME(); // 加载最新的页帧
                        break;
                    default:
                        NOT_REACHED(); // 不可达
                }
            LOOP();
        }
        CASE(LOAD_UPVALUE): // 指令流1 upvalue的索引
            PUSH(*((curFrame->closure->upvalues[(uint8_t)READ_BYTE()])->localVarPtr));
            LOOP();
        CASE(STORE_UPVALUE):
            *((curFrame->closure->upvalues[(uint8_t)READ_BYTE()])->localVarPtr) = PEEK();
            LOOP();
        CASE(LOAD_MODULE_VAR):
            PUSH(objFn->module->moduleVarValue.datas[(uint16_t)READ_SHORT()]);
            LOOP();
        CASE(STORE_MODULE_VAR):
            objFn->module->moduleVarValue.datas[(uint16_t)READ_SHORT()] = PEEK();
            LOOP();    
        CASE(STORE_THIS_FIELD):
            // 栈顶：field值
            // 指令流 1字节的field索引
            uint8_t fieldIdx = READ_BYTE();
            // TODO: assert()
            ObjInstance *objInstance = VALUE_TO_OBJINSTANCE(stackStart[0]);
            objInstance->fields[fieldIdx] = PEEK();
            LOOP();
        CASE(LOAD_FIELD):
            // 栈顶：实例对象
            // 指令流 1字节的field索引
            uint8_t fieldIdx = READ_BYTE(); // 获取待加兹安的字段索引
            Value receiver = POP(); // 获取消息接收者
            // TODO: assert()
            ObjInstance *objInstance = VALUE_TO_OBJINSTANCE(receiver);
            PUSH(objInstance->fields[fieldIdx]);
            LOOP();
        CASE(STORE_FILED):
            // 栈顶：实例对象 次栈顶：field值
            // 指令流 1字节的field索引
            uint8_t fieldIdx = READ_BYTE(); // 获取待加兹安的字段索引
            Value receiver = POP(); // 获取消息接收者
            // TODO: assert()
            ObjInstance *objInstance = VALUE_TO_OBJINSTANCE(receiver);
            PUSH(objInstance->fields[fieldIdx]);
            LOOP();
        CASE(JUMP): {// 指令流 2字节的跳转正偏移量
            int16_t offset = READ_SHORT();
            // TODO: assert
            ip += offset;
            LOOP();
        }
        CASE(LOOP): {
            int16_t offset = READ_SHORT();
            // TODO: assert
            ip -= offset;
            LOOP();
        }
        CASE(JUMP_IF_FALSE): {
            // 栈顶：跳转条件bool值
            // 指令流 2字节的跳转偏移量
            int16_t offset = READ_SHORT();
            // TODO: assert
            Value condition = POP();
            if (VALUE_IS_FALSE(condition) || VALUE_IS_NULL(condition)) {
                ip -= offset;
            }
            LOOP();
        }
        CASE(AND): {
            // 栈顶：跳转条件bool值
            // 指令流 2字节的跳转偏移量
            int16_t offset = READ_SHORT();
            // TODO: assert
            Value condition = PEEK();
            if (VALUE_IS_FALSE(condition) || VALUE_IS_NULL(condition)) {
                ip -= offset; // 若条件为假则不再计算and的右操作数
            } else {
                DROP();
            }
            LOOP();
        }
        CASE(OR): {
            // 栈顶：跳转条件bool值
            // 指令流 2字节的跳转偏移量
            int16_t offset = READ_SHORT();
            // TODO: assert
            Value condition = PEEK();
            if (VALUE_IS_FALSE(condition) || VALUE_IS_NULL(condition)) {
                DROP();
            } else {
                ip -= offset; // 若条件为真则不再计算or的右操作数
            }
            LOOP();
        }
        CASE(CLOSE_UPVALUE):
            // 栈顶：相当于局部变量
            // 把地址大于栈顶局部变量的upvalue关闭
            ClosedUpvalue(curThread, curThread->esp - 1);
            DROP();
            LOOP();
        CASE(RETURN): {
            // 栈顶 返回值
            Value retVal = POP();
            curThread->usedFrameNum --; // 从函数返回，该堆栈框架使用完毕，增加可用堆栈框架数据
            ClosedUpvalue(curThread, stackStart);

            // 如果一个堆栈框架都没用，说明它没有调用函数或者所有的函数调用都返回了，可以结束它
            if (curThread->usedFrameNum == 0U) {
                // 如果不是被另一个线程调用的，就直接结束
                if (curThread->caller == NULL) {
                    curThread->stack[0] = retVal;
                    curThread->esp = curThread->stack + 1; // 丢弃其他结果
                    return VM_RESULT_SUCCESS;
                } else {
                    // else
                }
                // 恢复主调方线程的调度
                ObjThread *callerThread = curThread->caller;
                curThread->caller = NULL;
                curThread = callerThread;
                vm->curThread = callerThread;

                // 在主调线程的栈顶存储被调线程的执行结果
                curThread->esp[-1] = retVal;
            } else {
                // 将返回值置于运行时栈栈顶
                stackStart[0] = retVal;
                curThread->esp = stackStart + 1; // 回收堆栈
            }
        }
        CASE(CONSTRUCT): {
            // 栈底 stackStart[0]=class
            // TODO:
            ObjInstance *objInstance = NewObjInstance(vm, VALUE_TO_CLASS(stackStart[0]));
            stackStart[0] = OBJ_TO_VALUE(objInstance);
            LOOP();
        }
        CASE(CREATE_CLOSURE): {
            // 指令流 2字节待创建闭包的函数在常量中的索引+函数所用的upvalue数乘2
            
            // endCompileUnit已经将闭包函数添加进了常量表
            ObjFn *fn = VALUE_TO_OBJFN(objFn->constants.datas[READ_SHORT()]);
            ObjClosure * objClosure = NewObjClosure(vm, fn);
            
            // 将创建好的闭包的value结构压到栈顶
            // 先将其压到栈中，后面再创建upvalue，这样可避免在创建upvalue过程中被GC
            PUSH(OBJ_TO_VALUE(objClosure));
            uint32_t idx = 0U;
            while (idx < fn->upvalueNum) {
                // 读入endcompileunit函数最后为每个upvalue写入的数据对儿
                uint8_t isEnclosingLocalVar = READ_BYTE();
                uint8_t index = READ_BYTE();

                if (isEnclosingLocalVar) { // 是直接外层的局部变量
                    objClosure->upvalues[idx] = CreateOpenUpvalue(vm, curThread, curFrame->stackStart + index);
                } else {
                    // 直接从父编译单元中继承
                    objClosure->upvalues[idx] = curFrame->closure->upvalues[index];
                }
                idx ++;
            }
            LOOP();
        }
        CASE(CREATE_CLASS): {
            // 指令流 1字节的field数量
            // 栈顶 基类
            // 次栈顶 子类名
            uint32_t fieldNum = READ_BYTE();
            Value superClass = curThread->esp[-1]; // 基类名
            Value className = curThread->esp[-2]; // 子类名

            // 回收基类所占的栈空间
            // 次栈顶的空间暂时保留，创建的类会直接用该空间
            DROP();
            // 校验基类合法性
            IsValidateSuperClass(vm, className, fieldNum, superClass);
            Class *class = NewClass(vm, VALUE_TO_OBJSTR(className), fieldNum, VALUE_TO_CLASS(superClass));

            // 类存储于栈底
            stackStart[0] = OBJ_TO_VALUE(class);
            LOOP();
        }
        CASE(INSTANCE_METHOD):
        CASE(STATIC_METHOD): {
            // 指令流 待绑定的方法 “名字” 在allMethodName中的索引
            // 在栈顶 待绑定的类
            // 次栈顶 待绑定的方法
            uint32_t methodNameIndex = READ_SHORT();
            Class *class = VALUE_TO_CLASS(PEEK());
            Value method = PEEK2();
            BindMethodAndPatch(vm, opCode, methodNameIndex, class, method);
            DROP();
            DROP();
            LOOP();
        }
        CASE(END):
            NOT_REACHED();
    }
    NOT_REACHED();

    #undef PUSH
    #undef POP
    #undef DROP
    #undef PEEK
    #undef PEEK2
    #undef LOAD_CUR_FRAME
    #undef STORE_CUR_FRAME
    #undef READ_BYTE
    #undef READ_SHORT
}