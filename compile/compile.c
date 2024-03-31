/*
 * @Author: LiuHao
 * @Date: 2024-03-31 14:54:50
 * @Description: 
 */

#include "compile.h"
#include "parser.h"
#include "core.h"
#include <string.h>

#ifdef DBEUG
    #include "debug.h"
#endif

struct compileUnit {
    ObjFn *compileUnitFn; 
    LocalVar localVars[MAX_LOCAL_VAR_NUM];
    uint32_t localVarNum; 
    Upvalue upvalues[MAX_UPVALUE_NUM]; // 记录本层函数所引用的upvalue
    int scopeDepth;
    uint32_t stackSlotsNum; // 当前使用的slot个数
    Loop *curLoop;
    ClassBookKeep *enclosingClassBK;
    struct compileUnit *enclosingUnit; // 直接外层编译单元
    Parser *curParser;
};

 /**
 * @brief 在模块objModule中定义名为name，值为value的模块变量
 *          模块变量存储在objModule结构体中
*/
int DefineModuleVar(VM *vm, ObjModule *objModule, const char *name, uint32_t length, Value value)
{
    if (length > MAX_ID_LEN) {
        char id[MAX_ID_LEN] = {'\0'};
        memcpy(id, name, length);
        /**
         * 本函数可能是在编译源码文件之前调用的，那时还没有创建parser
        */
        if (vm->curParser != NULL) {
            COMPILE_ERROR(vm->curParser, "length of identifier \"%s\" should be no more than %d", id, MAX_ID_LEN);
        } else { // 编译源码前调用，比如加载核心模块时会调用此函数
            MEM_ERROR("length of identifier \"%s\" should be no more than %d", id, MAX_ID_LEN);
        }
    }
    int symbolIndex= GetIndexFromSymbolTable(&objModule->moduleVarName, name, length);
    if (symbolIndex == -1) {
        symbolIndex = addSymbol(vm, &objModule->moduleVarName, name, length);
        ValueBufferAdd(vm, &objModule->moduleVarValue, value);
    } else if (VALUE_IS_NUM(objModule->moduleVarValue.datas[symbolIndex])) {
        objModule->moduleVarValue.datas[symbolIndex] = value;
    } else {
        symbolIndex = -1;
    }
    return symbolIndex;          
}

#define OPCODE_SLOTS(opCode, effect) effect,
static const int opCodeSlotsUsed[] = {
    #include "opcode.inc"
};
#undef OPCODE_SLOTS

/**
 * @brief 初始化compileunit
*/
static void InitCompileUnit(Parser *parser, CompileUnit *cu, CompileUnit *enclosingUnit, boolean isMethod)
{
    parser->curCompileUnit = cu;
    cu->curParser = parser;
    cu->enclosingUnit = enclosingUnit;
    cu->curLoop = NULL;
    cu->enclosingClassBK = NULL;

    /**
     * 如果没有外层直接编译单元，说明当前属于模块作用域
    */
    if (enclosingUnit == NULL) {
        // 编译代码时是从上到下最外层的模块作用域开始，模块作用域为-1
        cu->scopeDepth = -1;
        // 模块级作用域中没有局部变量
        cu->localVarNum = 0;
    } else {
        if (isMethod) {
            // 如果是类中的方法
            cu->localVars[0].name = "this";
            cu->localVars[0].length = 4;
        } else { // 如果是普通函数 
            // 空出第0个局部变量，保持统一
            cu->localVars[0].name = NULL;
            cu->localVars[0].length = 0;
        }
        // 第0个局部变量比较特殊，使其作用域为-1
        cu->localVars[0].scopeDepth = -1;
        cu->localVars[0].isUpvalue = false;
        cu->localVarNum = 1;
        cu->scopeDepth = 0; // TODO
    }
    // 局部变量保存在栈中，初始时栈中已使用的slot数量等于局部变量的数量
    cu->stackSlotsNum = cu->localVarNum;

    cu->compileUnitFn = NewObjFn(cu->curParser->vm, cu->curParser->curModule, cu->localVarNum);
}

/**
 * @brief 在函数的指令流中写入1字节，返回其索引
*/
static int WriteByte(CompileUnit *cu, int byte)
{
#ifdef DEBUG
    IntegerBufferAdd(cu->curParser->vm, &cu->compileUnitFn->debug->lineNo, cu->curParser->preToken.lineNo);
#endif
    ByteBufferAdd(cu->curParser->vm, &cu->compileUnitFn->instructStream, (uint8_t) byte);
    return cu->compileUnitFn->instructStream.count - 1;
}

/**
 * @brief 写入操作码
*/
static void WriteOpcode(CompileUnit *cu, OpCode opcode)
{
    WriteByte(cu, opcode);

    cu->stackSlotsNum += opCodeSlotsUsed[opcode];
    if (cu->stackSlotsNum > cu->compileUnitFn->maxStackSlotUsedNum) {
        cu->compileUnitFn->maxStackSlotUsedNum = cu->stackSlotsNum; // 记录栈使用的峰值
    }
}

/**
 * @brief 写入1字节的操作数
*/
static int WriteByteOperand(CompileUnit *cu, int operand)
{
    return WriteByte(cu, operand);
}

/**
 * @brief 写入2个字节的操作数，按大端字节序写入参数
*/
inline static void WriteShortOperand(CompileUnit *cu, int operand)
{
    WriteByte(cu, (operand >> 8) & 0xff);
    WriteByte(cu, operand & 0xff);
}

/**
 * @brief 写入操作数为1字节大小的指令
*/
static int WriteOpcodeByteOperand(CompileUnit *cu, OpCode opcode, int operand)
{
    WriteOpcode(cu, opcode);
    return WriteByteOperand(cu, operand);
}

/**
 * @brief 写入操作数为2字节大小的指令
*/
static void WriteOpcodeShortOperand(CompileUnit *cu, OpCode opcode, int operand)
{
    WriteOpcode(cu, opcode);
    WriteShortOperand(cu, operand);
}

/**
 * @brief 添加常量并返回其索引
*/
static uint32_t AddConstant(CompileUnit *cu, Value constant)
{
    ValueBufferAdd(cu->curParser->vm, &cu->compileUnitFn->constants, constant);
    return cu->compileUnitFn->constants.count - 1;
}

/**
 * @brief 编译类定义
*/
static void CompileClassDefinition(CompileUnit *cu)
{

}

/**
 * @brief 编译函数定义
*/
static void CompileFunctionDefinition(CompileUnit *cu)
{

}

/**
 * @brief 编译变量定义
*/
static void CompileVarDefinition(CompileUnit *cu, boolean can)
{

}

/**
 * @brief 编译模块导入
*/
static void CompileImport(CompileUnit *cu)
{

}

/**
 * @brief 编译语句
*/
static void CompileStatement(CompileUnit *cu)
{

}

/**
 * @brief 编译程序 编译的入口
*/
static void CompileProgram(CompileUnit *cu)
{
    if (MatchToken(cu->curParser, TOKEN_CLASS)) {
        CompileClassDefinition(cu);
    } else if (MatchToken(cu->curParser, TOKEN_FUN)) {
        CompileFunctionDefinition(cu);
    } else if (MatchToken(cu->curParser, TOKEN_VAR)) {
        CompileVarDefinition(cu, cu->curParser->preToken.type == TOKEN_STATIC);
    } else if (MatchToken(cu->curParser, TOKEN_IMPORT)) {
        CompileImport(cu);
    } else {
        CompileStatement(cu);
    }
}

/**
 * @brief 编译模块
*/
ObjFn* CompileModule(VM *vm, ObjModule *objModule, const char *moduleCore)
{
    // 各源码模块文件需要单独的parser 分配一个parser
    Parser parser;
    parser.parent = vm->curParser;
    // TODO: 局部变量带出去
    vm->curParser = &parser;

    // 获得一个parser，每一个module都有一个parser
    if (objModule->name == NULL) { // 核心模块的name是NULL
        // 核心模块是core.script.inc
        InitParser(vm, &parser, "core.script.inc", moduleCore, objModule);
    } else {
        InitParser(vm, &parser,  (const char *)objModule->name->value.start, moduleCore, objModule);
    }
    
    // 初始化一个compileUnit
    CompileUnit moduleCu;
    // 分配一个compileunit
    InitCompileUnit(&parser, &moduleCu, NULL, false);
    // 记录现在模块变量的数量，后面检查预定义模块变量时可减少遍历
    uint32_t moduleVarNumBefor = objModule->moduleVarValue.count;
    // 初始的parser->curToken.type为TOKEN_UNKNOWN,下面使其指向第一个合法的token
    GetNextToken(&parser);

    // 读Token
    while (!MatchToken(&parser, TOKEN_EOF)) {
        CompileProgram(&moduleCu);
    }

    printf("There is something to do...\n");
    exit(0);
}