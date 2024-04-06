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

typedef enum {
    BP_NONE, 
    // 从上往下，优先级越来越高
    BP_LOWEST,
    BP_ASSIGN, // =
    BP_CONDITION, // ? :
    BP_LOGIC_OR,
    BP_LOGIC_AND,
    BP_EQUAL, // == !=
    BP_IS,
    BP_CMP, // < > <= >=
    BP_BIT_OR,
    BP_BIT_AND,
    BP_BIT_SHIFT, // todo
    BP_RANGE, // ..
    BP_TERM, // + -
    BP_FACTOR, // * / %
    BP_UNARY, // - ! ~
    BP_CALL, // . () []
    BP_HIGHEST
} BindPower;

// 指示符函数指针
typedef void (*DenotationFn)(CompileUnit *cu, boolean canAssign);

// 签名函数指针
typedef void (*MethodSignatureFn)(CompileUnit *cu, Signature *signature);

typedef struct {
    const char *id; // 符号
    BindPower lbp; // 左绑定权值
    DenotationFn nud;// 字面量 变量 前缀运算符等不关注左操作数的Token调用方法
    DenotationFn led; // 中缀运算符等关注左操作数的Token调用的方法
    MethodSignatureFn MethodSign; // 表示本符号在类中被视为一个方法，为其生产一个方法签名
} SymbolBindRule; // 符号绑定规则

typedef enum {
    VAR_SCOPE_INVALID, VAR_SCOPE_LOCAL, VAR_SCOPE_UPVALUE, VAR_SCOPE_MODULE
} VarScopeType;  // 标识变量作用域

typedef struct {
    VarScopeType scopeType;
    int index; // 根据scopeType的值，此索引可能指向局部变量或者upvalue或模块变量
} Variable;

static void Expression(CompileUnit *cu, BindPower rbp);
static uint32_t AddConstant(CompileUnit *cu, Value constant);
static void CompileProgram(CompileUnit *cu);

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
 * @brief 生成加载常量的指令
*/
static void EmitLoadConstant(CompileUnit *cu, Value value)
{
    int index = AddConstant(cu, value);
    WriteOpcodeShortOperand(cu, OPCODE_LOAD_CONSTANT, index);
}

/**
 * @brief 数字和字符串.nud() 编译字面量
*/
static void Literal(CompileUnit *cu, boolean canAssign UNUSED)
{
    emit_load_constant(cu, cu->curParser->preToken.value);
}

/**
 * id bp(lbp)  
*/
// 不关注左操作数的符号成为前缀符号
// 用于如字面量 变量名 前缀符号等非运算符
#define PREFIX_SYMBOL(nud) { NULL, BP_NONE, nud, NULL, NULL }
// 前缀运算符 如！
#define PREFIX_OPERATOR(id) { id, BP_NONE, UnaryOperator, NULL, UnaryMethodSignature }
// 关注左操作数的符号成为中缀符号
// 数组[ 函数（，实例与方法之间的.等
#define INFIX_SYMBOL(lbp, led) { NULL, lbp, NULL, led, NULL }
// 中缀运算符
#define INFIX_OPERATOR(id, lbp) { id, lbp, NULL, InfixOperator, InfixMethodSignature }
// 既可左前缀也可以左中缀的运算符 如-
#define MIX_OPERATOR(id) { id , BP_TERM, UnaryOperator, InfixOperator, MixMethodSignature }
// 占位用的
#define UNUSED_RULE { NULL, BP_NONE, NULL, NULL, NULL }

/**
 * @brief 将Signature转换为字符串，返回字符串长度
*/
static uint32_t SignToString(Signature *sign, char *buf)
{
    uint32_t pos = 0;
    // 复制方法名xxx到buf 到sign
    memcpy(buf + pos, sign->name, sign->length);
    pos += sign->length;

    switch (sign->type) {
        // SIGN_GETTER形式：xxx，无参数
        case SIGN_GETTER:
            break;
        // SIGN_SETTER形式：xxx=(_)
        case SIGN_SETTER: {
            buf[pos ++] = '=';
            buf[pos ++] = '_';
            buf[pos ++] = ')';
            break;
        }
        // SIGN_CONSTRUCT和SIGN_METHOD形式：xxx(_,...)
        case SIGN_CONSTRUCT: {
            buf[pos ++] = '(';
            uint32_t idx = 0;
            while (idx < sign->argNum) {
                buf[pos ++] = '_';
                buf[pos ++] = ',';
                idx ++;
            }
            if (idx == 0) {
                // 说明没有参数
                buf[pos ++] = ')';
            } else {
                buf[pos - 1] = ')';
            }
            break;
        }
        // SIGN_SUBSCRIPT形式：xxx[_,...]
        case SIGN_SUBSCRIPT:
        {
            buf[pos ++] = '[';
            uint32_t idx = 0;
            while (idx < sign->argNum) {
                buf[pos ++] = '_';
                buf[pos ++] = ',';
                idx ++;
            }
            if (idx == 0) {
                // 说明没有参数
                buf[pos ++] = ']';
            } else {
                buf[pos - 1] = ']';
            }
            break;
        }
        // SIGN_SUBSCRIPT_SETTER形式：xxx[_,...]=(_)
        case SIGN_SUBSCRIPT_SETTER: {
            buf[pos ++] = '[';
            uint32_t idx = 0;
            while (idx < sign->argNum) {
                buf[pos ++] = '_';
                buf[pos ++] = ',';
                idx ++;
            }
            if (idx == 0) {
                // 说明没有参数
                buf[pos ++] = ']';
            } else {
                buf[pos - 1] = ']';
            }
            buf[pos ++] = '=';
            buf[pos ++] = '(';
            buf[pos ++] = '_';
            buf[pos ++] = ')';
            break;
        }
    }
    buf[pos] = '\0';
    return pos; // 返回签名串的长度
}

/**
 * @brief 语法分析的核心 Expression只负责计算表达式的结果 包括数字表达式与其他操作符组成的表达式 OP
 * @param cu 编译单元
 * @param rbp 算符
 * @category TDOP
*/
static void Expression(CompileUnit *cu, BindPower rbp)
{
    /**
     * Rules数组对每个token的Expression是按照token列表来排列的
     * 根据token获取对应的SymbolBindRule
    */
    DenotationFn nud = Rules[cu->curParser->curToken.type].nud;

    ASSERT(nud != NULL, "nud is NULL!");

    GetNextToken(cu->curParser); 

    boolean canAssign = rbp < BP_ASSIGN;
 
    nud(cu, canAssign);

    while (rbp < Rules[cu->curParser->curToken.type].lbp)
    {
        DenotationFn led = Rules[cu->curParser->curToken.type].led;
        GetNextToken(cu->curParser); 
        led(cu, canAssign);
    }
}

/**
 * @brief 通过签名编译方法调用
*/
static void EmitCallBySignature(CompileUnit *cu, Signature *sign, OpCode opcode)
{
    char signBuffer[MAX_SIGN_LEN];
    uint32_t length = SignToString(sign, signBuffer);

    int symbolIndex = EnsureSymbolExist(cu->curParser->vm, 
                        &cu->curParser->vm->allMethodNames, signBuffer, length);

    WriteOpcodeShortOperand(cu, opcode + sign->argNum, symbolIndex);

    // 此时在常量表中预创建一个空slot位，将来绑定方法时再装入基类
    if (opcode == OPCODE_SUPER0)
    {
        WriteShortOperand(cu, AddConstant(cu, VT_TO_VALUE(VT_NULL)));
    }
}

/**
 * @brief 生成方法调用的指令，仅限callX指令
*/
static void EmitCall(CompileUnit *cu, int numArgs, const char *name, int length)
{
    int symbolIndex = EnsureSymbolExist(cu->curParser->vm, &cu->curParser->vm->allMethodNames, name, length);
    WriteOpcodeShortOperand(cu, OPCODE_CALL0 + numArgs, symbolIndex);
}

/**
 * @brief 中缀运算符 .led方法
*/
static void InfixOperator(CompileUnit *cu, boolean canAssign UNUSED)
{
    SymbolBindRule *rule = &Rules[cu->curParser->preToken.type];

    // 中缀运算符对左右操作数的绑定权值一样
    BindPower rbp = rule->lbp;
    Expression(cu, rbp); // 解析右操作数

    Signature sign = { SIGN_METHOD, rule->id, strlen(rule->id), 1 };
    EmitCallBySignature(cu, &sign, OPCODE_CALL0);

}

/**
 * @brief 前缀运算符  .nud方法 不关注左操作数
 * 进入任何一个符号的led或nud方法时，preToken都是该方法所属于的符号（操作符）,curToken是操作数
*/
static void UnaryOperator(CompileUnit *cu, boolean canAssign UNUSED)
{
    SymbolBindRule *rule = &Rules[cu->curParser->preToken.type];

    // BP_UNARY作为rbp去调用Expression解析右边操作数
    Expression(cu, BP_UNARY);

    // 生成调用前缀运算符的指令
    // 0个参数，前缀运算符都是1个字符，长度是1
    EmitCall(cu, 0, rule->id, 1);
}

/**
 * @brief 添加局部变量到cu
*/
static uint32_t AddLocalVar(CompileUnit *cu, const char *name, uint32_t length)
{
    LocalVar *var = &(cu->localVars[cu->localVarNum]);
    var->name = name;
    var->length = length;
    var->scopeDepth = cu->scopeDepth;
    var->isUpvalue = false;
    return cu->localVarNum ++;
}

/**
 * @brief 声明局部变量
*/
static int DeclareLocalVar(CompileUnit *cu, const char *name, uint32_t length)
{
    if (cu->localVarNum >= MAX_LOCAL_VAR_NUM) { // 达到局部变量存储上限
        COMPILE_ERROR(cu->curParser, "The max length of local variable of one scope is %d", MAX_LOCAL_VAR_NUM);
    }
    int idx = (int)cu->localVarNum - 1;
    while (idx >= 0) {
        LocalVar *var = &cu->localVars[idx];

        // 如果到了父作用域就退出
        if (var->scopeDepth < cu->scopeDepth) {
            break;
        }
        if (var->length == length && memcmp(var->name, name, length) == 0) {
            char id[MAX_ID_LEN] = { '\0' };
            memcpy(id, name, length);
            COMPILE_ERROR(cu->curParser, "Identifier \"%s\" redefinition!", id);
        }
        idx --;
    }
    // 检查过后声明该局部变量
    return AddLocalVar(cu, name, length);
}

/**
 * @brief 根据作用域声明变量
*/
static int DeclareVariable(CompileUnit *cu, const char *name, uint32_t length)
{
    // 若当前是模块作用域就声明为模块变量
    if (cu->scopeDepth == -1) {
        int index = DefineModuleVar(cu->curParser->vm, cu->curParser->curModule, name, length, VT_TO_VALUE(VT_NULL));
        if (index == -1) { // 重定义
            char id[MAX_ID_LEN] = { '\0' };
            memcpy(id, name, length);
            COMPILE_ERROR(cu->curParser, "Identifier \"%s\" redefinition!", id);  
        }
    }

    // 否则是局部作用域
    return DeclareLocalVar(cu, name, length);
}

/**
 * @brief 为单运算符方法创建签名
*/
static void UnaryMethodSignature(CompileUnit *cu UNUSED, Signature *sign UNUSED)
{
    // 名称在调用前一句完成，只修改类型
    sign->type = SIGN_GETTER;
}

/**
 * @brief 为中缀运算符创建签名
*/
static void InfixMethodSignature(CompileUnit *cu, Signature *sign)
{
    sign->type = SIGN_METHOD; // 在类中的运算符都是方法，类型是SIGN_METHOD

    sign->argNum = 1;  // 中缀运算符只有一个参数，故初始为1
    ConsumeCurToken(cu->curParser, TOKEN_LEFT_PAREN, "expect '(' after infix operator!");
    ConsumeCurToken(cu->curParser, TOKEN_ID, "expect variable name!");
    DeclareVariable(cu, cu->curParser->preToken.start, cu->curParser->preToken.length);
    ConsumeCurToken(cu->curParser, TOKEN_RIGHT_PAREN, "expect '(' after infix parameter!");
}

/**
 * @brief 为既做单运算符又做中缀运算符的符号方法创建签名
*/
static void MixMethodSignature(CompileUnit *cu, Signature *sign)
{
    // 假设是单运算符方法，因此默认为getter
    sign->type = SIGN_GETTER;

    // 若后面有（，说明其为中缀运算符，则为SIGN_METHOD
    if (MatchToken(cu->curParser, TOKEN_LEFT_PAREN)) {
        sign->type = SIGN_METHOD;
        sign->argNum = 1;
        ConsumeCurToken(cu->curParser, TOKEN_ID, "expect variable name!");
        DeclareVariable(cu, cu->curParser->preToken.start, cu->curParser->preToken.length);
        ConsumeCurToken(cu->curParser, TOKEN_RIGHT_PAREN, "expect '(' after infix parameter!");
    }
}

/**
 * @brief 声明模块变量 只做声明 不做重定义检查
*/
static int DeclareModuleVar(VM *vm, ObjModule *obj_module, const char *name, uint32_t length, Value value)
{
    ValueBufferAdd(vm, &obj_module->moduleVarValue, value);
    return AddSymbol(vm, &obj_module->moduleVarName, name, length);
}

/**
 * @brief 返回包含cu->enclosing最近的编译单元
*/
static CompileUnit* GetEnclosingClassBKUnit(CompileUnit *cu)
{
    while (cu != NULL) {
        if (cu->enclosingClassBK != NULL)
        {
            return cu;
        }
        cu = cu->enclosingUnit;
    }
    return NULL;
}

/**
 * @brief 返回包含cu最近的classbk
*/
static ClassBookKeep* GetEnclosingClassBK(CompileUnit *cu)
{
    CompileUnit *ncu = GetEnclosingClassBKUnit(cu);
    if (ncu != NULL)
    {
        return ncu->enclosingClassBK;
    }
    return NULL;
}

/**
 * @brief 为实参列表中的各个实参生成加载实参的指令
*/
static void ProcessArgList(CompileUnit *cu, Signature *sign)
{
    ASSERT(cu->curParser->curToken.type != TOKEN_RIGHT_PAREN && 
            cu->curParser->curToken.type != TOKEN_RIGHT_BRACKET, "Empty argument list!");
    do {
        if (++ sign->argNum > MAX_ARG_NUM) {
            COMPILE_ERROR(cu->curParser, "The max number of argument is %d!", MAX_ARG_NUM);
        }
        Expression(cu, BP_LOWEST); // 计算每个实参
    } while (MatchToken(cu->curParser, TOKEN_COMMA));
}

/**
 * @brief 声明形参列表中的各个形参
*/
static void ProcessParaList(CompileUnit *cu, Signature *sign)
{
    ASSERT(cu->curParser->curToken.type != TOKEN_RIGHT_PAREN && 
            cu->curParser->curToken.type != TOKEN_RIGHT_BRACKET, "empty argument list!");
    do {
        if (++ sign->argNum > MAX_ARG_NUM) {
            ConsumeCurToken(cu->curParser, TOKEN_ID, "expect variable name!");
            DeclareVariable(cu, cu->curParser->preToken.start, cu->curParser->preToken.length);
        }
    } while (MatchToken(cu->curParser, TOKEN_COMMA));
}

/**
 * @brief 尝试编译setter
*/
static boolean TrySetter(CompileUnit *cu, Signature *sign)
{
    // 进入本函数时候preToken是方法名
    if (!MatchToken(cu->curParser, TOKEN_ASSIGN)) {
        return false;
    }

    if (sign->type == SIGN_SUBSCRIPT) {
        sign->type = SIGN_SUBSCRIPT_SETTER;
    } else {
        sign->type = SIGN_SETTER;
    }
    // 读取等号右边的形参左边的(
    ConsumeCurToken(cu->curParser, TOKEN_LEFT_PAREN, "expect '(' after '='!");
    // 读取形参
    ConsumeCurToken(cu->curParser, TOKEN_ID, "expect ID!");
    // 声明形参
    DeclareVariable(cu, cu->curParser->preToken.start, cu->curParser->preToken.length);
    consumeCurToken(cu->curParser, TOKEN_RIGHT_PAREN, "expect ')' after argument list!");
    sign->argNum ++;
    return true;
}

/**
 * @brief 标识符的签名函数
*/
static void IdMethodSignature(CompileUnit *cu, Signature *sign)
{
    sign->type = SIGN_GETTER; // 刚识别到id，默认为getter

    // new方法为构造函数
    if (sign->length == 3 && memcmp(sign->name, "new", 3) == 0) {
        if (MatchToken(cu->curParser, TOKEN_ASSIGN)) {
            COMPILE_ERROR(cu->curParser, "constructor shouldn't be setter!");
        }
        // 构造函数必须是标准的 即new(_,...)
        if (!MatchToken(cu->curParser, TOKEN_LEFT_PAREN)) {
            COMPILE_ERROR(cu->curParser, "construtor must be method!");
        }
        sign->type = SIGN_CONSTRUCT;
        if (MatchToken(cu->curParser, TOKEN_RIGHT_PAREN)) {
            return ;
        }
    } else {
        if (TrySetter(cu, sign)) {
            return ;
        }
        if (!MatchToken(cu->curParser, TOKEN_LEFT_PAREN)) {
            return ;
        }
        // 值此一般形式的SIGN_METHOD，形式为name(paralist)
        sign->type = SIGN_METHOD;
        if (MatchToken(cu->curParser, TOKEN_LEFT_PAREN)) {
            return ;
        }
    }
    // 下面处理形参
    ProcessArgList(cu, sign);
    ConsumeCurToken(cu->curParser, TOKEN_RIGHT_PAREN, "expect ')' after argument list!");
}

/**
 * @brief 查找局部变量
*/
static int FindLocal(CompileUnit *cu, const char *name, uint32_t length)
{
    int index = cu->localVarNum - 1;
    while (index >= 0) {
        if (cu->localVars[index].length == length && memcmp(cu->localVars[index].name, name, length) == 0) {
            return index;
        }
        index --;
    }
    return -1;
}

/**
 * @brief 添加upvalue到cu->upvalue中
 * isEnclosingLocalVar表示是否为直接外层编译单元中的局部变量，如果不是，那么形参index表示此upvalue是直接外层编译单元
 * 中的upvalue的索引
*/
static int AddUpvalue(CompileUnit *cu, boolean isEnclosingLocalVar, uint32_t index)
{
    uint32_t idx = 0;
    // 查看是否已存在
    while (idx < cu->compileUnitFn->upvalueNum) {
        if (cu->upvalues[idx].index == index && cu->upvalues[idx].isEnclosingLocalVar == isEnclosingLocalVar) {
            return idx;
        }
        idx ++;
    }
    // 不存在则添加
    cu->upvalues[cu->compileUnitFn->upvalueNum].isEnclosingLocalVar = isEnclosingLocalVar;
    cu->upvalues[cu->compileUnitFn->upvalueNum].index = index;
    return cu->compileUnitFn->upvalueNum ++;
}

/**
 * @brief 查找name指代的upvalue后添加到cu->upvalue，返回其索引，否则返回-1
 * FindUpvalue会递归调用，查找upvalue时需要逐层查找
*/
static int FindUpvalue(CompileUnit *cu, const char *name, uint32_t length)
{   
    if (cu->enclosingUnit == NULL) { // 到了最外层，递归结束
        return -1;
    }
    /**
     * 静态域是作为模块中的局部变量存在的，模块中的所有类的静态域都定义在同一模块中
     * 以 Cls 静态域的形式作为模块中的局部变量名，因此在内部，标识符中包括空格就是静态域的名字
     * cu->enclosingUnit->enclosingClassBK != NULL表明是方法的编译单元
    */
    // TODO;
    if (!strchr(name, ' ') && cu->enclosingUnit->enclosingClassBK != NULL) {
        return -1;
    }
    int directOuterLocalIndex = FindUpvalue(cu->enclosingUnit, name, length);
    if (directOuterLocalIndex != -1) {
        cu->enclosingUnit->localVars[directOuterLocalIndex].isUpvalue = true;
        return AddUpvalue(cu, true, (uint32_t)directOuterLocalIndex);
    }
    // 向外层查找
    int directOuterUpvalueIndex = FindUpvalue(cu->enclosingUnit, name, length);
    if (directOuterUpvalueIndex != -1) {
        return AddUpvalue(cu, false, (uint32_t)directOuterUpvalueIndex);
    }
    return -1;
}

/**
 * @brief 从局部变量和upvalue查找符号name
*/
static Variable GetVarFromLocalOrUpvalue(CompileUnit *cu, const char* name, uint32_t length)
{
    Variable var;
    var.scopeType = VAR_SCOPE_INVALID;
    var.index = FindLocal(cu, name, length); // 从局部变量查找
    if (var.index != -1) {
        var.scopeType = VAR_SCOPE_LOCAL;
        return var;
    }
    var.index = FindUpvalue(cu, name, length); // 从upvalue中查找
    if (var.index != -1) {
        var.scopeType = VAR_SCOPE_UPVALUE;
    }
    return var;
}

//从局部变量,upvalue和模块中查找变量name
static Variable FindVariable(CompileUnit* cu, const char* name, uint32_t length) {

   //先从局部变量和upvalue中查找
   Variable var = GetVarFromLocalOrUpvalue(cu, name, length);
   if (var.index != -1) return var;
  
   //若未找到再从模块变量中查找
   var.index = getIndexFromSymbolTable(
	 &cu->curParser->curModule->moduleVarName, name, length);
   if (var.index != -1) {
      var.scopeType = VAR_SCOPE_MODULE;
   }
   return var;
}

/**
 * @brief 生成把变量var加载到栈的指令
*/
static void EmitLoadVariable(CompileUnit *cu, Variable var)
{
    switch (var.scopeType) {
        case VAR_SCOPE_LOCAL: {
            WriteOpcodeByteoperand(cu, OPCODE_LOAD_LOCAL_VAR, var.index);
            break;
        }
        case VAR_SCOPE_UPVALUE: {
            WriteOpcodeByteoperand(cu, OPCODE_LOAD_UPVALUE, var.index);
            break;
        }
        case VAR_SCOPE_MODULE: {
            WriteOpcodeShortOperand(cu, OPCODE_LOAD_MODULE_VAR, var.index);
            break;
        }
        default:
            NOT_REACHED();
    }
}

/**
 * @brief 为变量var生成存储的指令
*/
static void EmitStoreVariable(CompileUnit *cu, Variable var)
{
    switch (var.scopeType) {
        case VAR_SCOPE_LOCAL: {
            WriteOpcodeByteoperand(cu, OPCODE_STORE_LOCAL_VAR, var.index);
            break;
        }
        case VAR_SCOPE_UPVALUE: {
            WriteOpcodeByteoperand(cu, OPCODE_STORE_UPVALUE, var.index);
            break;
        }
        case VAR_SCOPE_MODULE: {
            WriteOpcodeShortOperand(cu, OPCODE_STORE_MODULE_VAR, var.index);
            break;
        }
        default:
            NOT_REACHED();
    }
}

/**
 * @brief 生成加载或存储的指令
*/
static void EmitLoadOrStoreVariable(CompileUnit *cu, boolean canAssign, Variable var)
{
    if (canAssign && MatchToken(cu->curParser, TOKEN_ASSIGN)) {
        Expression(cu, BP_LOWEST); // 计算=右边的表达式的值
        EmitStoreVariable(cu, var);
    } else {
        EmitLoadVariable(cu, var);
    }
}

/**
 * @brief 生成把实例对象this加载到栈的指令
*/
static void EmitLoadThis(CompileUnit *cu)
{
    Variable var = GetVarFromLocalOrUpvalue(cu, "this", 4);
    ASSERT(var.scopeType != VAR_SCOPE_INVALID, "get variable failed!");
    EmitLoadVariable(cu, var);
}

/**
 * @brief 编译代码块
*/
static void CompileBlock(CompileUnit *cu)
{
    // 进入本函数之前已经读入了{
    while (!MatchToken(cu->curParser, TOKEN_RIGHT_BRACE)) {
        if (PEEK_TOKEN(cu->curParser) == TOKEN_EOF) {
            COMPILE_ERROR(cu->curParser, "Expect ')' at the end of block!");
        }
        CompileProgram(cu);
    }
}

/**
 * @brief 编译函数或方法体
*/
static void CompileBody(CompileUnit *cu, boolean isConstruct)
{
    // 进入本函数之前已经读入了{
    CompileBlock(cu);
    if (isConstruct) {
        WriteOpcodeByteOperand(cu, OPCODE_STORE_LOCAL_VAR, 0);
    } else {
        // 否则加载null占位
        WriteOpcode(cu, OPCODE_PUSH_NULL);
    }
    // 构造函数返回this，否则返回null
    WriteOpcode(cu, OPCODE_RETURN);
}

/**
 * @brief 结束cu的编译工作，在其外层编译单元中为其创建闭包
*/
#if DEBUG
static ObjFn* EndCompileUnit(CompileUnit *cu, const char *debugName, uint32_t debugNameLen)
{
    BindDebugFnName(cu->curParser->vm, cu->compileUnitFn->debug, debugName, debugNameLen);
#else
static ObjFn* EndCompileUnit(CompileUnit *cu) {
#endif
    WriteOpcode(cu, OPCODE_END); // 标识单元编译结束
    if (cu->enclosingUnit != NULL) {
        // 把当前编译的objfn作为常量添加到父编译单元的常量表
        // 编译单元本质上是指令流单元，ObjFn对象才能用于存储指令流，因此编译单元被编译后的结果肯定是一个ObjFn
        uint32_t index = AddConstant(cu->enclosingUnit, OBJ_TO_VALUE(cu->compileUnitFn));
        // 内层函数以闭包形式在外层函数中存在
        // 在外层函数的指令流中添加“为当前内层函数创建闭包的指令”
        WriteOpcodeShortOperand(cu->enclosingUnit, OPCODE_CREATE_CLASS, index);
        // 为vm在创建闭包时判断引用的是局部变量还是upvalue
        // 下面为每个upvalue生成参数
        index = 0;
        while (index < cu->compileUnitFn->upvalueNum) {
            WriteByte(cu->enclosingUnit, cu->upvalues[index].isEnclosingLocalVar ? 1 : 0);
            WriteByte(cu->enclosingUnit, cu->upvalues[index].index);
            index ++;
        }
    }
    // 下调本编译单元，使当前编译单元指向外层编译单元
    cu->curParser->curCompileUnit = cu->enclosingUnit;
    // 编译单元被编译的结果就是一个objfn
    return cu->compileUnitFn;
}

/**
 * 此部分是文法编译部分
*/

/**
 * @brief 生成getter或一般method调用指令
*/
static void EmitGetterMethodCall(CompileUnit *cu, Signature *sign, OpCode opcode)
{
    Signature newSign;
    newSign.type = SIGN_GETTER;
    newSign.name = sign->name;
    newSign.length = sign->length;
    newSign.argNum = 0;
    // 如果是method，在生成调用方法的指令前必须把参数入栈
    if (MatchToken(cu->curParser, TOKEN_LEFT_PAREN)) {
        newSign.type = SIGN_METHOD;
        if (!MatchToken(cu->curParser, TOKEN_RIGHT_PAREN)) { // 有参数
            ProcessArgList(cu, &newSign);
            ConsumeCurToken(cu->curParser, TOKEN_RIGHT_PAREN, "expect ')' after argument list!");
        }
    }
    if (MatchToken(cu->curParser, TOKEN_LEFT_BRACE)) { // 块参数
        newSign.argNum ++;
        newSign.type = SIGN_METHOD;
        CompileUnit fnCu;
        InitCompileUnit(cu->curParser, &fnCu, cu, false);
        Signature tmpFnSign = { SIGN_METHOD, "", 0, 0 };
        if (MatchToken(cu->curParser, TOKEN_BIT_OR)) { // 块参数的参数
            ProcessParaList(&fnCu, &tmpFnSign);
            ConsumeCurToken(cu->curParser, TOKEN_BIT_OR, "expect '|' after argument list!");
        }
        fnCu.compileUnitFn->argNum = tmpFnSign.argNum;
        // 编译函数体
        CompileBody(&fnCu, false);
#if DEBUG   
        char fnName[MAX_SIGNLEN + 10] = { '\0' };
        uint32_t len = SignToString(&newSign, fnName);
        memmove(fnName + len, " block arg", 10);
        EndCompileUnit(&fnCu, fnName, len + 10);
#else
        EndCompileUnit(&fnCu);
#endif
    }
    // 如果是在构造函数中调用了super则会执行到此，构造函数中调用的方法只能是super
    if (sign->type == SIGN_CONSTRUCT) {
        if (newSign.type != SIGN_METHOD) {
            COMPILE_ERROR(cu->curParser, "The form of supercall is super() pr super(argument)");
        }
        newSign.type = SIGN_CONSTRUCT;
    }
    // 根据签名生成调用指令
    EmitCallBySignature(cu, &newSign, opcode);
}

/**
 * @brief 生成方法调用指令，包括getter和setter
*/
static void EmitMethodCall(CompileUnit *cu, const char *name, uint32_t length, OpCode opcode, boolean canAssign)
{
    Signature sign;
    sign.type = SIGN_GETTER;
    sign.name = name;
    sign.length = length;
    // 若是setter则生成调用setter的指令
    if (MatchToken(cu->curParser, TOKEN_ASSIGN) && canAssign) {
        sign.type = SIGN_SETTER;
        sign.argNum = 1; // 只接受一个参数
        Expression(cu, BP_LOWEST); // 载入实参(即=右边所赋的值)
        EmitCallBySignature(cu, &sign, opcode);
    } else {
        EmitGetterMethodCall(cu, &sign, opcode);
    }
}

/**
 * @brief 小写字符开头便是局部变量
*/
static boolean IsLocalName(const char *name)
{   
    return (name[0] >= 'a' && name[0] <= 'z');
}

/**
 * @brief 标识符.nud()：变量名或方法名
*/
static void ID(CompileUnit *cu, boolean canAssign)
{
    // 备份变量名
    Token name = cu->curParser->preToken;
    ClassBookKeep *classBK = GetEnclosingClassBK(cu);
    // 顺序
    // 函数-》局部变量和upvalue-》实例域-》静态域-》类getter方法调用-》模块变量
    // 处理函数调用
    if (cu->enclosingUnit == NULL && MatchToken(cu->curParser, TOKEN_LEFT_PAREN)) {
        // 函数名加上Fn 前缀作为模块变量名
        char id[MAX_ID_LEN] = { '\0' };
        memmove(id, "Fn ", 3); // 在处理用函数fun定义的函数时，会把函数名前加上前缀Fn后必当做模块变量来存储
        memmove(id + 3, name.start, name.length);
        Variable var;
        var.scopeType = VAR_SCOPE_MODULE;
        var.index = GetIndexFromSymbolTable(&cu->curParser->curModule->moduleVarName, id, strlen(id));
        if (var.index == -1) {
            memmove(id, name.start, name.length);
            id[name.length] = '\0';
            COMPILE_ERROR(cu->curParser, "undefined function: '%s'!", id);
        }
        // 把模块变量即函数闭包加载到栈
        EmitLoadVariable(cu, var);
        Signature sign;
        sign.type = SIGN_METHOD;
        sign.name = "call";
        sign.length = 4;
        sign.argNum = 0;
        if (!MatchToken(cu->curParser, TOKEN_RIGHT_PAREN)) {
            // 压入实参
            ProcessArgList(cu, &sign);
            ConsumeCurToken(cu->curParser, TOKEN_RIGHT_PAREN, "expect ')' after argument list!");
        }
        // 生成调用指令以调用函数
        EmitCallBySignature(cu, &sign, OPCODE_CALL0);
    } else { // 否则按照各种变量来处理
        // 按照局部变量或upvalue来处理
        Variable var = GetVarFromLocalOrUpvalue(cu, name.start, name.length);
        if (var.index != -1) {
            EmitLoadOrStoreVariable(cu, canAssign, var);
            return ; 
        }
        // 按照实例域来处理
        if (classBK != NULL) {
            int fieldIndex = GetIndexFromSymbolTable(&classBK->fields, name.start, name.length);
            if (fieldIndex != -1) {
                boolean isRead = true;
                if (canAssign && MatchToken(cu->curParser, TOKEN_ASSIGN)) {
                    isRead = false;
                    Expression(cu, BP_LOWEST);
                }
                // 如果当前正在编译类方法，则直接在该实例对象中加载filed
                if (cu->enclosingUnit != NULL) {
                    WriteOpcodeByteOperand(cu, isRead ? OPCODE_LOAD_THIS_FILED: OPCODE_STORE_THIS_FIELD, fieldIndex);
                } else {
                    EmitLoadThis(cu);
                    WriteOpcodeByteOperand(cu, isRead ? OPCODE_LOAD_THIS_FILED: OPCODE_STORE_THIS_FIELD, fieldIndex);
                }
                return ;
            }
        }
        // 按照静态域查找
        if (classBK != NULL) {
            char *staticFieldId = ALLOCATE_ARRAY(cu->curParser->vm, char, MAX_ID_LEN);
            memset(staticFieldId, 0, MAX_ID_LEN);
            uint32_t staticFieldIdLen;
            char clsName = classBK->name->value.start;
            uint32_t clsLen = classBK->name->value.length;
            memmove(staticFieldId, "Cls", 3);
            // TODO:
            memmove(staticFieldId + 3, clsName, clsLen);
            memmove(staticFieldId + 3 + clsLen, " ", 1);
            const char tkName = name.start;
            uint32_t tkLen = name.length;
            memmove(staticFieldId + 4 + clsLen, tkName, tkLen);
            staticFieldIdLen = strlen(staticFieldId);
            var = GetVarFromLocalOrUpvalue(cu, staticFieldId, staticFieldIdLen);
            DEALLOCATE_ARRAY(cu->curParser->vm, staticFieldId, MAX_ID_LEN);
            if (var.index != -1) {
                EmitLoadOrStoreVariable(cu, canAssign, var);
                return ;
            }
        }
        // 该标识符是同类中的其他方法调用
        if (classBK != NULL && isLocalName(name.start)) {
            EmitLoadThis(cu); // 确保args[0]是this对象
            EmitMethodCall(cu, name.start, name.length, OPCODE_CALL0, canAssign);
            return ;
        }
        // 按照模块变量处理
        var.scopeType = VAR_SCOPE_MODULE;
        var.index = GetIndexFromSymbolTable(&cu->curParser->curModule->moduleVarName, name.start, name.length);
        if (var.index == -1) {
            // 模块变量属于模块作用域，若当前引用处之前未定义该模块变量
            // 说不定在后面有其定义，因此暂时先声明
            char fnName[MAX_SIGN_LEN + 4] = { '\0' };
            memmove(fnName, "Fn ", 3);
            memmove(fnName + 3, name.start, name.length);
            var.index = GetIndexFromSymbolTable(&cu->curParser->curModule->moduleVarName, fnName, strlen(fnName));
            // 若不是函数名，那可能是该模块变量定义在引用处的后面
            // 先将行号作为该变量去声明
            if (var.index == -1) {
                var.index = declare_module_var(cu->curParser->vm, cu->curParser->curModule, 
                                name.start, name.length, NUM_TO_VALUE(cu->curParser->curToken.lineNo));
            }
        }
        EmitLoadOrStoreVariable(cu, canAssign, var);
    }
}

/**
 * @brief 生成加载类的指令
*/
static void EmitLoadModuleVar(CompileUnit *cu, const char *name)
{
    int index = GetIndexFromSymbolTable(&cu->curParser->curModule->moduleVarName, name, strlen(name));
    ASSERT(index != -1, "symbol should have been defined");
    WriteOpcodeShortOperand(cu, OPCODE_LOAD_MODULE_VAR, index);
}

/**
 * @brief 内嵌表达式
*/
static void StringInterpolation(CompileUnit *cu, boolean canAssign UNUSED)
{
    /**
     * a%(b+c) d%(e) f
     * 会按照如下形式编译
     * ["a", b+c, "d",e,"f"].join()
     * a和d是TOKEN_INTERPOLATION bcd都是TOKEN)ID f是TOKEN_STRING
    */
   EmitLoadModuleVar(cu, "List");
   EmitCall(cu, 0, "new()", 5);
   // 每次处理字符串中的一个内嵌表达式
   do {
        Literal(cu, false); // 
        EmitCall(cu, 1, "addCore_(_)", 11);
        Expression(cu, BP_LOWEST);
        EmitCall(cu, 1, "addCore_(_)", 11);
   } while (MatchToken(cu->curParser, TOKEN_INTERPOLATION));
   ConsumeCurToken(cu->curParser, TOKEN_STRING, "expect string at the end of interpolatation");
   Literal(cu, false);
   EmitCall(cu, 1, "addCore_(_)", 11);
   EmitCall(cu, 0, "join()", 6);
}

/**
 * @brief 编译bool
*/
static void Boolean(CompileUnit *cu, boolean canAssign UNUSED)
{
    OpCode opcode = cu->curParser->preToken.type == TOKEN_TRUE ? OPCODE_PUSH_TRUE: OPCODE_PUSH_FALSE;
    WriteOpcode(cu, opcode);
}

/**
 * @brief 生成OPCODE_PUSH_NULL指令
*/
static void Null(CompileUnit *cu, boolean canAssign UNUSED)
{
    WriteOpcode(cu, OPCODE_PUSH_NULL);
}

/**
 * @brief "this".nud()
*/
static void This(CompileUnit *cu, boolean canAssign UNUSED)
{
    if (GetEnclosingClassBK(cu) == NULL) {
        COMPILE_ERROR(cu->curParser, "this must be inside a class method!");
    }
    EmitLoadThis(cu);
}

/**
 * @brief “super”.nud()
*/
static void Super(CompileUnit *cu, boolean canAssign)
{
    ClassBookKeep *enclosingClassBK = GetEnclosingClassBK(cu);
    if (enclosingClassBK = NULL) {
        COMPILE_ERROR(cu->curParser, "can't invoke super outside a class method!");
    }
    EmitLoadThis(cu); // 此处加载this，确保arg[0]始终是this
    // 判断形式super.methodname()
    if (MatchToken(cu->curParser, TOKEN_DOT)) {
        ConsumeCurToken(cu->curParser, TOKEN_ID, "expect name after '.'!");
        EmitMethodCall(cu, cu->curParser->preToken.start, cu->curParser->preToken.length, OPCODE_SUPER0, canAssign);
    } else {
        // super()调用基类中关键字super所在子类方法同名的方法
        EmitGetterMethodCall(cu, enclosingClassBK->signature, OPCODE_SUPER0);
    }
}

/**
 * @brief 编译圆括号
*/
static void Parenthesis(CompileUnit *cu, boolean canAssign UNUSED)
{
    Expression(cu, BP_LOWEST);
    ConsumeCurToken(cu->curParser, TOKEN_RIGHT_PAREN, "expect ')' after Expression!");
}

/**
 * @brief '['.nud() 
*/
static void ListLiteral(CompileUnit *cu, boolean canAssign UNUSED)
{
    EmitLoadModuleVar(cu, "List");
    EmitCall(cu, 0, "new()", 5);
    do {
        if (PEEK_TOKEN(cu->curParser) == TOKEN_RIGHT_BRACKET) {
            break;
        }
        Expression(cu, BP_LOWEST);
        EmitCall(cu, 1, "addCore_(_)", 11);
    } while (MatchToken(cu->curParser, TOKEN_COMMA));
    ConsumeCurToken(cu->curParser, TOKEN_RIGHT_BRACKET, "expect ')' after list element!");
}

/**
 * @brief '['.lef() 用于索引
*/
static void Subscript(CompileUnit *cu, boolean canAssign)
{
    if (MatchToken(cu->curParser, TOKEN_RIGHT_BRACKET)) {
        COMPILE_ERROR(cu->curParser, "need argument in the '[]'!");
    }
    Signature sign = {SIGN_SUBSCRIPT, "", 0, 0};
    // 读取参数并把参数加载到栈，统计参数个数
    ProcessArgList(cu, &sign);
    ConsumeCurToken(cu->curParser, TOKEN_RIGHT_BRACKET, "expect ']' after argument list!");
    if (canAssign && MatchToken(cu->curParser, TOKEN_ASSIGN)) {
        sign.type = SIGN_SUBSCRIPT_SETTER;
        if (++ sign.argNum > MAX_ARG_NUM) {
            COMPILE_ERROR(cu->curParser, "the max number of argument is %d!", MAX_ARG_NUM);
        }
        Expression(cu, BP_LOWEST); // 获取=右边的表达式
    }
    EmitCallBySignature(cu, &sign, OPCODE_CALL0);
}   

/**
 * @brief 为下标操作符[编译签名
*/
static void SubscriptMethodSignature(CompileUnit *cu, Signature *sign)
{
    sign->type = SIGN_SUBSCRIPT;
    sign->type = 0;
    ProcessParaList(cu, sign);
    ConsumeCurToken(cu->curParser, TOKEN_RIGHT_BRACKET, "expect ']' after index list!");
    TrySetter(cu, sign);  // 判断]后面是否接=为setter
}

static void CallEntry(CompileUnit *cu, boolean canAssign) 
{
    // 本函数是'.'.led() 当前的token是TOKEN_ID
    ConsumeCurToken(cu->curParser, TOKEN_ID, "expect method name after '.'!");
    EmitMethodCall(cu, cu->curParser->preToken.start, cu->curParser->preToken.length, OPCODE_CALL0, canAssign);
}

/**
 * @brief map对象字面量
*/
static void MapLiteral(CompileUnit *cu, boolean canAssign UNUSED)
{
    EmitLoadModuleVar(cu, "Map");
    EmitCall(cu, 0, "new()", 5);
    do {
        if (PEEK_TOKEN(cu->curParser) == TOKEN_RIGHT_BRACE) {
            break;
        }
        Expression(cu, BP_UNARY);
        // 读入key后面的冒号
        ConsumeCurToken(cu->curParser, TOKEN_COLON, "expect ':' after key!");
        Expression(cu, BP_LOWEST);
        EmitCall(cu, 2, "addCore_(_,_)", 13);
    } while (MatchToken(cu->curParser, TOKEN_COMMA));
    ConsumeCurToken(cu->curParser, TOKEN_RIGHT_BRACE, "map literal should end with \')\'!");
}
/**
 * @brief 用占位符作为参数指令
*/
static uint32_t EmitInstrWithPlaceHolder(CompileUnit *cu, OpCode opcode)
{
    WriteOpcode(cu, opcode);
    WriteByte(cu, 0xff);// 先写入高位的0xff
    // 再写入低位的0xff后，减1返回高位地址，此地址用来回填
    // 大端模式
    return WriteByte(cu, 0xff) - 1;
}

/**
 * @brief 用跳转到当前字节码结束地址的偏移量去替换占位符参数0xffff
*/
static void PatchPlaceHolder(CompileUnit *cu, uint32_t abs_index)
{
    uint32_t offset = cu->compileUnitFn->instructStream.count - abs_index - 2;
    cu->compileUnitFn->instructStream.datas[abs_index] = (offset >> 8) & 0xff;
    cu->compileUnitFn->instructStream.datas[abs_index + 1] = offset & 0xff;
}

/**
 * @brief ‘||’.led()
*/
static void LogicOr(CompileUnit *cu, boolean canAssign UNUSED)
{
    // 此时栈顶是条件表达式的结果，即||的左操作数
    // 操作码OPCODE_OR会到栈顶获取条件
    uint32_t placeHolderIndex = EmitInstrWithPlaceHolder(cu, OPCODE_OR);
    // 生成计算右操作数的指令
    Expression(cu, BP_LOGIC_OR);
    // 用右表达式的实际结束地址回填OPCODE_OR操作码的占位符
    PatchPlaceHolder(cu, placeHolderIndex);
}   

/**
 * @brief ‘&&’.nud()
*/
static void LogicAnd(CompileUnit *cu, boolean canAssign UNUSED)
{
    // 此时栈顶是条件表达式的结果，即||的左操作数
    // 操作码OPCODE_OR会到栈顶获取条件
    uint32_t placeHolderIndex = EmitInstrWithPlaceHolder(cu, OPCODE_AND);
    // 生成计算右操作数的指令
    Expression(cu, OPCODE_AND);
    // 用右表达式的实际结束地址回填OPCODE_OR操作码的占位符
    PatchPlaceHolder(cu, placeHolderIndex);
}   

/**
 * @brief “？：”.led()
*/
static void Condition(CompileUnit *cu, boolean canAssign UNUSED)
{
    uint32_t falseBranchStart = EmitInstrWithPlaceHolder(cu, OPCODE_JUMP_IF_FALSE);
    // 编译true分支
    Expression(cu, BP_LOWEST);
    ConsumeCurToken(cu->curParser, TOKEN_COLON, "expect ':' after true branch!");
    uint32_t falseBranchEnd = EmitInstrWithPlaceHolder(cu, OPCODE_JUMP);
    // 编译true分支已经结束，此时知道了true分支的结束地址
    // 编译false分支之前需先回填false_branch_start
    PatchPlaceHolder(cu, falseBranchStart);
    Expression(cu, BP_LOWEST);
    PatchPlaceHolder(cu, falseBranchEnd);
}

/**
 * @brief 编译变量定义 不支持多个变量定义 如var a=1,b=2
 * @warning 调用本函数前已经读入了关键字var，此时curToken是后面的变量名
*/
static void CompileVarDefineition(CompileUnit *cu, boolean isStatic)
{
    ConsumeCurToken(cu->curParser, TOKEN_ID, "missing variable name!");
    Token name = cu->curParser->preToken;
    if (cu->curParser->curToken.type == TOKEN_COMMA) {
        COMPILE_ERROR(cu->curParser, "'var' only support declaring a variable!");
    }
    // 先判断是否是类中的域定义，确保cu是模块cu
    // 当编译器编译类时，便会把enclosingClassBK置为当前classBK
    if (cu->enclosingUnit == NULL && cu->enclosingClassBK != NULL) {
        if (isStatic) { // 按照静态域
            char *staticFieldId = ALLOCATE_ARRAY(cu->curParser->vm, char, MAX_ID_LEN);
            memset(staticFieldId, 0, MAX_ID_LEN);
            uint32_t staticFieldIdLen;
            char *clsName = cu->enclosingClassBK->name->value.start;
            uint32_t clsLen = cu->enclosingClassBK->name->value.length;
            // 用前缀'cls+类名+变量名'作为静态域在模块编译单元中的局部变量
            // 将静态域看做是模块的局部变量，为了解决多个类的静态域变量同名问题，故采用cls+类名+变量名的格式
            memmove(staticFieldId, "Cls", 3);
            memmove(staticFieldId + 3, clsName, clsLen);
            memmove(staticFieldId + 3 + clsLen, " ", 1);
            const char *tkName = name.start;
            uint32_t tkLen = name.length;
            memmove(staticFieldId + 4 + clsLen, tkName, tkLen);
            staticFieldIdLen = strlen(staticFieldId);
            if (FindLocal(cu, staticFieldId, staticFieldIdLen) == -1) {
                int index = declare_local_var(cu, staticFieldId, staticFieldIdLen);
                WriteOpcode(cu, OPCODE_PUSH_NULL);
                ASSERT(cu->scopeDepth == 0, "should in class scope!");
                DefineVariable(cu, index);
                // 静态域可初始化
                // TODO:
                Variable var = FindVariable(cu, staticFieldId, staticFieldIdLen);
                if (MatchToken(cu->curParser, TOKEN_ASSIGN)) {
                    Expression(cu, BP_LOWEST);
                    EmitStoreVariable(cu, var);
                }
            } else {
                COMPILE_ERROR(cu->curParser, "static field '%s' redefinition!", strchr(staticFieldId, ' ') + 1);
            }
        } else { // 定义实例域
            ClassBookKeep *classBK = GetEnclosingClassBK(cu);  // 返回cu的bk
            int fieldIndex = GetIndexFromSymbolTable(&classBK->fields, name.start, name.length);
            if (fieldIndex == -1) {
                fieldIndex = AddSymbol(cu->curParser->vm, &classBK->fields, name.start, name.length);
            } else {
                if (fieldIndex > MAX_FIELD_NUM) {
                    COMPILE_ERROR(cu->curParser, "the max number of instance field is %d!", MAX_FIELD_NUM);
                } else {
                    char id[MAX_ID_LEN] = { '\0' };
                    memcpy(id, name.start, name.length);
                    COMPILE_ERROR(cu->curParser, "instance field '%s' redefinition!", id);
                }
                if (MatchToken(cu->curParser, TOKEN_ASSIGN))
                {
                    COMPILE_ERROR(cu->curParser, "instance field isn't allowed initialization!");
                }
            }
        }
        return ;
    }
    // 按一般变量定义
    if (MatchToken(cu->curParser, TOKEN_ASSIGN)) {
        // 若在定义时赋值就解析表达式，结果会留到栈顶
        Expression(cu, BP_LOWEST);
    } else {
        // 否则就初始为NULL， 即在栈顶压入NULL
        // 也是为了与上面显示初始化保持相同栈结构
        WriteOpcode(cu, OPCODE_PUSH_NULL);
    }
    uint32_t index = DeclareVariable(cu, name.start, name.length);
    DefineVariable(cu, index);
}

/**
 * @brief 编译if语句
*/
static void CompileIfStatement(CompileUnit *cu)
{
    // 当前的token为if
    ConsumeCurToken(cu->curParser, TOKEN_LEFT_PAREN, "missing '(' after if!");
    Expression(cu, BP_LOWEST); // 生成计算if条件表达式的指令步骤
    ConsumeCurToken(cu->curParser, TOKEN_RIGHT_PAREN, "missing ')' before '{' in if!");
    // 条件为假，为分支的起始地址设置占位符、
    uint32_t falseBranchStart = EmitInstrWithPlaceHolder(cu, OPCODE_JUMP_IF_FALSE);
    // 编译then分支
    // 代码块前后的{}由compilestatement负责读取
    CompileStatement(cu);
    // 如果有else分支
    if (MatchToken(cu->curParser, TOKEN_ELSE)) {
        // 添加跳过else分支的跳转指令
        uint32_t falseBranchEnd = EmitInstrWithPlaceHolder(cu, OPCODE_JUMP);
        // 进入else分支编译之前，先回填false_branche_start
        PatchPlaceHolder(cu, falseBranchStart);
        // 编译else分支
        CompileStatement(cu);
        // 此时知道了false块的结束地址，回填false_branch_end
        PatchPlaceHolder(cu, falseBranchEnd);
    } else {
        // 跳过整个true分支
        PatchPlaceHolder(cu, falseBranchStart);
    }
}

/**
 * @brief 开始循环，进入循环体的相关设置
*/
static void EnterLoopSetting(CompileUnit *cu, Loop *loop)
{
    loop->condStartIndex = cu->compileUnitFn->instructStream.count - 1;
    loop->scopeDepth = cu->scopeDepth;
    //在当前循环层中嵌套新的循环层，当前层成为内嵌层的外层
    loop->enclosingLoop = cu->curLoop;
    // 使cu->curLoop指向新的内层
    cu->curLoop = loop;
}

/**
 * @brief 编译循环体
*/
static void CompileLoopBody(CompileUnit *cu)
{
    cu->curLoop->bodyStartIndex = cu->compileUnitFn->instructStream.count;
    CompileStatement(cu);
}

/**
 * @brief 获得ip所指向的操作码的操作数占用的字节数
*/
uint32_t GetBytesOfOperand(Byte *instrStream, Value *constants, int ip)
{
    switch ((OpCode)instrStream[ip]) {
        case OPCODE_CONSTRUCT:
        case OPCODE_RETURN:
        case OPCODE_END:
        case OPCODE_CLOSE_UPVALUE:
        case OPCODE_PUSH_FALSE:
        case OPCODE_PUSH_NULL:
        case OPCODE_PUSH_TRUE:
        case OPCODE_POP:
            return 0;
        case OPCODE_CREATE_CLASS:
        case OPCODE_LOAD_THIS_FILED:
        case OPCODE_STORE_THIS_FIELD:
        case OPCODE_LOAD_LOCAL_VAR:
        case OPCODE_STORE_LOCAL_VAR:
        case OPCODE_LOAD_UPVALUE:
        case OPCODE_STORE_UPVALUE:
            return 1;
        case OPCODE_CALL0:
        case OPCODE_CALL1:
        case OPCODE_CALL2:
        case OPCODE_CALL3:
        case OPCODE_CALL4:
        case OPCODE_CALL5:
        case OPCODE_CALL6:
        case OPCODE_CALL7:
        case OPCODE_CALL8:
        case OPCODE_CALL9:
        case OPCODE_CALL10:
        case OPCODE_CALL11:
        case OPCODE_CALL12:
        case OPCODE_CALL13:
        case OPCODE_CALL14:
        case OPCODE_CALL15:
        case OPCODE_CALL16:
        case OPCODE_LOAD_CONSTANT:
        case OPCODE_LOAD_MODULE_VAR:
        case OPCODE_STORE_MODULE_VAR:
        case OPCODE_LOOP:
        case OPCODE_JUMP:
        case OPCODE_JUMP_IF_FALSE:
        case OPCODE_AND:
        case OPCODE_OR:
        case OPCODE_INSTANCE_METHOD:
        case OPCODE_STATIC_METHOD:
            return 2;
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
        case OPCODE_SUPER16:
            return 4;
        case OPCODE_CREATE_CLOSURE:{
            // 获得操作码OPCODE_CLOSURE操作数，2B
            // 该操作数是待创建闭包的函数在常量表中的索引
            uint32_t fn_idx = (instrStream[ip + 1] << 8) | instrStream[ip + 2];
            // 每个upvalue有一对儿参数
            // 2指的是fn_idx在指令流中占用的空间
            /**
             * 在创建upvalue时有两个参数，分别是：是否是直接外层的局部变量，在直接外层的索引
            */
            return 2 + (VALUE_TO_OBJFN(constants[fn_idx]))->upvalueNum * 2;
        }
        default:
            NOT_REACHED();
    }
}

/**
 * @brief 离开循环体的相关设置
*/
static void LeaveLoopPatch(CompileUnit *cu)
{
    // 获取往回跳转的偏移量，偏移量都是正数
    int loop_back_offset = cu->compileUnitFn->instructStream.count - cu->curLoop->condStartIndex + 2;
    // 生辰向回跳转的指令，即ip -= loop_back_offset
    WriteOpcodeShortOperand(cu, OPCODE_LOOP, loop_back_offset);
    // 回填循环体的结束地址
    PatchPlaceHolder(cu, cu->curLoop->exitIndex);
    // 下面在循环体中回填break的占位符OPCODE_END
    // 循环体开始地址
    uint32_t idx = cu->curLoop->bodyStartIndex;
    // 循环体结束地址
    uint32_t loopEndIndex = cu->compileUnitFn->instructStream.count;
    while (idx < loopEndIndex) {
        if (OPCODE_END == cu->compileUnitFn->instructStream.datas[idx]) {
            PatchPlaceHolder(cu, idx + 1);
            idx += 3;
        } else {
            idx += 1 + GetBytesOfOperand(cu->compileUnitFn->instructStream.datas, cu->compileUnitFn->constants.datas, idx);
        }
    }
    // 退出当前循环体，即恢复cu->curLoop为当前循环层的外层循环
    cu->curLoop = cu->curLoop->enclosingLoop;
}

/**
 * @brief 编译while循环
*/
static void CompileWhileStatement(CompileUnit *cu)
{
    Loop loop;
    EnterLoopSetting(cu, &loop);
    ConsumeCurToken(cu->curParser, TOKEN_LEFT_PAREN, "expect '(' befor condition");
    Expression(cu, BP_LOWEST);
    ConsumeCurToken(cu->curParser, TOKEN_RIGHT_PAREN, "expect ')' befor condition");
    // 先把条件失败时跳转的目标地址占位
    loop.exitIndex = EmitInstrWithPlaceHolder(cu, OPCODE_JUMP_IF_FALSE);
    CompileLoopBody(cu);
    LeaveLoopPatch(cu);
}

/**
 * @brief 丢掉作用域scopeDepth之内的局部变量 即找出作用域大于scopeDepth的变量并丢弃
 * X层的局部变量对于嵌套的X+1层来说，也许是upvalue，所以丢弃再次的意思是把局部变量弹出或者把upvalue关闭
 * 当跳出作用域时，其内部作用域也就失效了，因此被丢弃
*/
static uint32_t DiscardLocalVar(CompileUnit *cu, int scopeDepth)
{
    ASSERT(cu->scopeDepth > -1, "upmost scope can't exit!");
    int localIdx = cu->localVarNum - 1;
    while (localIdx >= 0 && cu->localVars[localIdx].scopeDepth >= scopeDepth)
    {
        if (cu->localVars[localIdx].isUpvalue) {
            WriteByte(cu, OPCODE_CLOSE_UPVALUE);
        } else {
            WriteByte(cu, OPCODE_POP);
        }
        localIdx --;
    }
    // 返回丢掉的局部变量个数
    return cu->localVarNum - 1 - localIdx;
}

/**
 * @brief 编译return
*/
inline static void CompileReturn(CompileUnit *cu)
{
    if (PEEK_TOKEN(cu->curParser) == TOKEN_RIGHT_BRACE) { // 空返回值
        WriteOpcode(cu, OPCODE_PUSH_NULL);
    } else {
        Expression(cu, BP_LOWEST);
    }
    WriteOpcode(cu, OPCODE_RETURN); // 将上面栈顶的值返回
}

/**
 * @brief 编译break
*/
inline static void CompileBreak(CompileUnit *cu)
{
    if (cu->curLoop == NULL) {
        COMPILE_ERROR(cu->curParser, "break should be used inside a loop!");
    }
    // 在退出循环体之前要丢掉循环体内的局部变量
    DiscardLocalVar(cu, cu->curLoop->scopeDepth + 1);
    // 由于用OPCODE_END标识break占位，此时无需记录占位符的返回地址
    EmitInstrWithPlaceHolder(cu, OPCODE_END);
}

/**
 * 编译continue
*/
inline static void CompileContinue(CompileUnit *cu)
{
    if (cu->curLoop == NULL) {
        COMPILE_ERROR(cu->curParser, "continue should be used inside a loop!");
    }
    // 
    DiscardLocalVar(cu, cu->curLoop->bodyStartIndex + 1);
    int loop_back_offset = cu->compileUnitFn->instructStream.count - cu->curLoop->bodyStartIndex + 2;
    // 生成向回跳转的OPCODE_LOOP指令
    WriteOpcodeShortOperand(cu, OPCODE_LOOP, loop_back_offset);
}

/**
 * @brief 进入新作用域
*/
static void EnterScope(CompileUnit *cu)
{
    // 进入内嵌作用域
    cu->scopeDepth ++;
}

/**
 * @brief 退出作用域
*/
static void LeaveScope(CompileUnit *cu)
{
    if (cu->enclosingUnit != NULL) {
        uint32_t discardNum = DiscardLocalVar(cu, cu->scopeDepth);
        cu->localVarNum -= discardNum;
        cu->stackSlotsNum -= discardNum;
    }
    cu->scopeDepth --;
}

/**
 * @brief 编译for循环  for i in (sequence) {}
 * 会将for变成while
*/
static void CompileForStatement(CompileUnit *cu)
{
    EnterScope(cu); // 为局部变量seq和iter创建作用域
    ConsumeCurToken(cu->curParser, TOKEN_ID, "expect variable after for !");
    const char *loopVarName = cu->curParser->preToken.start;
    uint32_t loopVarLen = cu->curParser->preToken.length;
    ConsumeCurToken(cu->curParser, TOKEN_LEFT_PAREN, "expect '(' befor sequence!");
    // 编译迭代序列
    Expression(cu, BP_LOWEST);
    ConsumeCurToken(cu->curParser, TOKEN_RIGHT_PAREN, "expect ')' after sequence!");
    // 申请一个局部变量seq来存储序列对象
    // 其值就是上面Expression存储到栈中的结果
    uint32_t seqSlots = AddLocalVar(cu, "seq ", 4);
    WriteOpcode(cu, OPCODE_PUSH_NULL);
    // 分配及初始化iter，其值就是上面加载到栈中的NULL
    uint32_t iterSlots = AddLocalVar(cu, "iter ", 5);
    Loop loop;
    EnterLoopSetting(cu, &loop);
    // 为调用seq.iterare(iter)做准备
    // 先压入序列对象seq，即seq.iterate(iter)的seq
    WriteOpcodeByteOperand(cu, OPCODE_LOAD_LOCAL_VAR, seqSlots);
    // 再压入iter
    WriteOpcodeByteOperand(cu, OPCODE_LOAD_LOCAL_VAR, iterSlots);
    // 调用seq.iterare(iter)
    EmitCall(cu, 1, "iterate(_)", 10);
    // seq.iterare(iter)把结果（下一个迭代器）存储到args[0]栈顶，现在将其结果同步到变量iter
    WriteOpcodeByteOperand(cu, OPCODE_LOAD_LOCAL_VAR, iterSlots);
    // 先写入占位符
    loop.exitIndex = EmitInstrWithPlaceHolder(cu, OPCODE_JUMP_IF_FALSE);
    // 为调用seq.iterare(iter)做准备
    // 先压入序列对象seq，即seq.iterate(iter)的seq
    WriteOpcodeByteOperand(cu, OPCODE_LOAD_LOCAL_VAR, seqSlots);
    // 再压入iter
    WriteOpcodeByteOperand(cu, OPCODE_LOAD_LOCAL_VAR, iterSlots);
    // 调用seq.iterare(iter)
    EmitCall(cu, 1, "iterate(_)", 10);
    EnterScope(cu);
    AddLocalVar(cu, loopVarName, loopVarLen);
    CompileLoopBody(cu);
    LeaveScope(cu);
    LeaveLoopPatch(cu);
    LeaveScope(cu);
}

/**
 * @brief 生成存储模块变量的指令 把栈顶数据放入index的模块变量中
*/
static void EmitStoreModuleVar(CompileUnit *cu, int index)
{
    WriteOpcodeShortOperand(cu, OPCODE_STORE_MODULE_VAR, index);
    WriteOpcode(cu, OPCODE_POP);
}

/**
 * @brief 声明方法
*/
static int DeclareMethod(CompileUnit *cu, char *signStr, uint32_t length)
{
    // 确保方法被录入到vm->allMethodNames
    int index = EnsureSymbolExist(cu->curParser->vm, &cu->curParser->vm->allMethodNames, signStr, length);
    IntegerBuffer *methods = cu->enclosingClassBK->inStatic ? 
                    &cu->enclosingClassBK->staticMethods: 
                    &cu->enclosingClassBK->instantMethods;
    // 排除重定义
    uint32_t idx = 0;
    while (idx < methods->count) {
        if (methods->datas[idx] == index) {
            COMPILE_ERROR(cu->curParser, "repeat define method %s in class %s!", signStr, cu->enclosingClassBK->name->value.start);
        }
        idx ++;
    }
    // 若是新定义就加入，这里并不是注册新方法
    IntegerBufferAdd(cu->curParser->vm, methods, index);
    return index;
}

/**
 * @brief 将方法methodIndex指代的方法装入classVar指代的class.method中
*/
static void DefineMethod(CompileUnit *cu, Variable classVar, boolean isStatic, int methodIndex)
{
    // 待绑定的方法在调用本函数之前已经放到栈顶了
    // 将方法所属的类加载到栈顶
    EmitLoadVariable(cu, classVar);
    // 生成OPCODE_STATIC_METHOD或OPCODE_INSTANCE_METHOD
    // 在运行时绑定
    OpCode opcode = isStatic ? OPCODE_STATIC_METHOD: OPCODE_INSTANCE_METHOD;
    WriteOpcodeShortOperand(cu, opcode, methodIndex);
}

/**
 * @brief 分两步创建实例，constructorIndex是构造函数的索引
 * 使用类名.new()
 * 此处注意 spallow语言中不允许对象调用静态方法，静态方法也不能访问实例域
*/
static void EmitCreateInstance(CompileUnit *cu, Signature *sign, uint32_t constructorIndex)
{
    CompileUnit methodCu;
    InitCompileUnit(cu->curParser, &methodCu, cu, true);
    // 生成OPCODE_CONSTRUCT指令，该指令生成新势力存储到stack[0]
    WriteOpcode(&methodCu, OPCODE_CONSTRUCT);
    // 生成OPCODE_CALLX指令，该指令调用新实例的构造函数
    WriteOpcodeShortOperand(&methodCu, (OpCode)(OPCODE_CALL0 + sign->argNum), constructorIndex);
    // 生成return指令，将栈顶中的实例返回
    WriteOpcode(&methodCu, OPCODE_RETURN);
#if DEBUG
    EndCompileUnit(&methodCu, "", 0);
#else
    EndCompileUnit(&methodCu);
#endif
}

/**
 * @brief 编译方法定义
*/
static void CompileMethod(CompileUnit *cu, Variable classVar, boolean isStatic)
{
    cu->enclosingClassBK->inStatic = isStatic;
    // 获得签名函数
    MethodSignatureFn MethodSign = Rules[cu->curParser->curToken.type].MethodSign;
    if (MethodSign == NULL)
    {
        COMPILE_ERROR(cu->curParser, "method need siganture function!");
    }
    Signature sign;
    sign.name = cu->curParser->curToken.start;
    sign.length = cu->curParser->curToken.length;
    sign.argNum = 0;

    cu->enclosingClassBK->signature = &sign;

    GetNextToken(&cu->curParser);

    // 为了将函数或方法自己的指令流和局部变量单独存储
    // 每个函数或方法都有自己的CompileUnit
    CompileUnit methodCu;
    // 编译一个方法，因此形参is_method为true
    // 将当前编译单元cu作为methodCu的直接外层以形参作用域嵌套链
    InitCompileUnit(cu->curParser, &methodCu, cu, true);
    // 构造签名
    MethodSign(&methodCu, &sign);
    ConsumeCurToken(cu->curParser, TOKEN_LEFT_BRACE, "expect '{' at the begining of method body.");
    // 构造函数前不能加关键词static
    if (cu->enclosingClassBK->inStatic && sign.type == SIGN_CONSTRUCT) {
        COMPILE_ERROR(cu->curParser, "construtor is not allowed to be static!");
    }
    char signaturestring[MAX_SIGN_LEN] = { '\0' };
    uint32_t signLen = SignToString(&sign, signaturestring);
    // 将方法声明
    uint32_t methodIndex = DeclareMethod(cu, signaturestring, signLen);
    // 编译方法体指令流到自己的编译单元methodCu
    CompileBody(&methodCu, sign.type == SIGN_CONSTRUCT);
#if DEBUG  
    EndCompileUnit(&methodCu, signaturestring, signLen);
#else
    EndCompileUnit(&methodCu);
#endif
    // 定义方法：将上面创建的方法闭包绑定到类
    DefineMethod(cu, classVar, cu->enclosingClassBK->inStatic, methodIndex);
    if (sign.type == SIGN_CONSTRUCT) {
        sign.type == SIGN_METHOD;
        char signaturestring[MAX_SIGN_LEN] = { '\0' };
        uint32_t signLen = SignToString(&sign, signaturestring);
        uint32_t constructorIndex = 
                        EnsureSymbolExist(cu->curParser->vm, &cu->curParser->vm->allMethodNames, signaturestring, signLen);
        EmitCreateInstance(cu, &sign, methodIndex);
        // 构造函数是静态方法，即类方法
        DefineMethod(cu, classVar, true, constructorIndex);
    }
}

/**
 * 编译类体
*/
static void CompileClassBody(CompileUnit *cu, Variable classVar)
{
    if (MatchToken(cu->curParser, TOKEN_STATIC)) {
        if (MatchToken(cu->curParser, TOKEN_VAR)) { // 处理静态域 static var id
            CompileVarDefineition(cu, true);
        } else { // 处理静态方法 static method_name
            CompileMethod(cu, classVar, true);
        }
    } else if (MatchToken(cu->curParser, TOKEN_VAR)) { // 实例域
        CompileVarDefineition(cu, false);
    } else { // 类方法
        CompileMethod(cu, classVar, false);
    }
}

/**
 * @brief 编译类定义
*/
static void CompileClassDefinition(CompileUnit *cu)
{
    Variable classVar;
    if (cu->scopeDepth != -1) {
        COMPILE_ERROR(cu->curParser, "class definition must be in the module scope!");
    }
    // 生成类名，用于创建类
    classVar.scopeType = VAR_SCOPE_MODULE;
    ConsumeCurToken(cu->curParser, TOKEN_ID, "keyword class should follow by class name!"); // 读入类拧
    classVar.index = DeclareVariable(cu, cu->curParser->preToken.start, cu->curParser->preToken.length);
    // 生成类名，用于创建类
    ObjString *className = NewObjString(cu->curParser->vm, cu->curParser->preToken.start, cu->curParser->preToken.length);
    // 生成加载类名的指令
    emit_load_constant(cu, OBJ_TO_VALUE(className));
    if (MatchToken(cu->curParser, TOKEN_LESS)) { // 类继承
        Expression(cu, BP_CALL); // 把父类名加载到栈顶
    } else { // 默认加载object类为基类
        EmitLoadModuleVar(cu, "object");
    }
    // 创建类需要知道域的个数，此时未知
    int fieldNumIndex = WriteOpcodeByteOperand(cu, OPCODE_CREATE_CLASS, 255);
    // vm执行完OPCODE_CREATE_CLASS后，栈顶留下了创建好的类
    // 因此 现在可以用该类为之前声明的类名className赋值
    if (cu->scopeDepth == -1) {
        EmitStoreModuleVar(cu, classVar.index);
    }
    ClassBookKeep classBK;
    classBK.name = className;
    classBK.inStatic = false; // 默认是false
    String_buffer_init(&classBK.instantMethods);
    Int_buffer_init(&classBK.instantMethods);
    Int_buffer_init(&classBK.staticMethods);
    // 此时cu是模块的编译单元，跟踪当前编译的类
    cu->enclosingClassBK = &classBK;
    // 读入类名后的 {
    ConsumeCurToken(cu->curParser, TOKEN_LEFT_BRACE, "expect '{' after class name in the class declaration!");
    EnterScope(cu);
    // 直到类定义结束}为止
    while (!MatchToken(cu->curParser, TOKEN_RIGHT_BRACE)) {
        CompileClassBody(cu, classVar);
        if (PEEK_TOKEN(cu->curParser) == TOKEN_EOF) {
            COMPILE_ERROR(cu->curParser, "expect '}' at the end of class declaration!");
        }
    }
    // 之前临时写了255个字段，现在回填
    cu->compileUnitFn->instructStream.datas[fieldNumIndex] = classBK.fields.count;
    SymbolTableClear(cu->curParser->vm, &classBK.fields);
    IntegerBufferClear(cu->curParser->vm, &classBK.instantMethods);
    IntegerBufferClear(cu->curParser->vm, &classBK.staticMethods);
    //enclosingClassBK用来表示是否正在编译类
    cu->enclosingClassBK = NULL;
    // 退出作用域，丢弃相关局部变量
    LeaveScope(cu);
}

/**
 * @brief 编译函数定义
 * 格式：
 *  var fn = Fn.new{|形参列表|函数体代码}
*/
static void CompileFunctionDefinition(CompileUnit *cu)
{
    // fun关键字只用在模块作用域中
    if (cu->enclosingUnit != NULL) {
        COMPILE_ERROR(cu->curParser, "'fun' should be in module scope!");
    }
    ConsumeCurToken(cu->curParser, TOKEN_ID, "missing function name!");
    // 函数名加上Fn前缀作为模块变量存储
    char fnName[MAX_SIGN_LEN + 4] = { '\0' };
    memmove(fnName, "Fn ", 3);
    memmove(fnName + 3, cu->curParser->preToken.start, cu->curParser->preToken.length);
    uint32_t fnNameIndex = DeclareVariable(cu, fnName, strlen(fnName));
    // 生成fnCu，专用于存储函数指令流
    CompileUnit fnCu;
    InitCompileUnit(cu->curParser, &fnCu, cu, false);
    Signature tmpFnSign = {SIGN_METHOD, "", 0, 0}; 
    ConsumeCurToken(cu->curParser, TOKEN_LEFT_PAREN, "expect '(' after function name!");
    // 若有形参则将形参声明局部变量
    if (!MatchToken(cu->curParser, TOKEN_RIGHT_PAREN)) {
        ProcessParaList(&fnCu, &tmpFnSign); // 将形参声明为函数的局部变量
        ConsumeCurToken(cu->curParser, TOKEN_RIGHT_PAREN, "expect ')' after parameter list!");
    }
    fnCu.compileUnitFn->argNum = tmpFnSign.argNum;
    ConsumeCurToken(cu->curParser, TOKEN_LEFT_BRACE, "expect '(' at the beginning of method body.");
    // 编译函数体，将指令流写进该函数自己的指令单元fnCu
    CompileBody(&fnCu, false);
#if DEBUG
    EndCompileUnit(&fnCu, fnName, strlen(fnName));
#else
    EndCompileUnit(&fnCu);
#endif
    // 将栈顶的闭包写入变量
    DefineVariable(cu, fnNameIndex);
}

/**
 * @brief 编译import导入
*/
static void CompileImport(CompileUnit *cu)
{
    ConsumeCurToken(cu->curParser, TOKEN_ID, "expect module name after export!");
    // 备份模块名
    Token moduleNameToken = cu->curParser->preToken;
    // 导入模块的拓展名不需要，有可能用户会把模块的拓展名加上
    //如import ad.aa，这时候就要跳过拓展名
    if (cu->curParser->preToken.start[cu->curParser->preToken.length] == '.') {
        printf("\nwarning!!! the imported module needn't extension!, compiler try to ignor it!\n");
        // 跳过拓展名
        GetNextToken(cu->curParser);
        GetNextToken(cu->curParser);
    }
    // 把模块名转为字符串，存储为常量
    ObjString *moduleName = NewObjString(cu->curParser->vm, moduleNameToken.start, moduleNameToken.length);
    uint32_t constModIdx = AddConstant(cu, OBJ_TO_VALUE(moduleName));
    /**import foo
     * 实际形式：System.importModule("foo")
     * import foo for a
     * 实际形式：System.getModuleVariable("foo", "a")
    */
    // 为调用system.importmodule()压入参数system
    EmitLoadModuleVar(cu, "System");
    // 为调用压入foo
    WriteOpcodeShortOperand(cu, OPCODE_LOAD_CONSTANT, constModIdx);
    // 调用
    EmitCall(cu, 1, "importModule(_)", 15);
    // 回收args[0]所在空间
    WriteOpcode(cu, OPCODE_POP);
    // 如果后面没有关键字for就导入结束
    if (!MatchToken(cu->curParser, TOKEN_FOR)){
        return ;
    }
    // 编译循环导入的模块变量（以逗号分隔）
    do {
        ConsumeCurToken(cu->curParser, TOKEN_ID, "expect variable name after 'for' in import!");
        // 在本模块中声明导入的模块变量
        uint32_t var_idx = DeclareVariable(cu, cu->curParser->preToken.start, cu->curParser->preToken.length);
        // 把模块变量转为字符串，存储为常量
        ObjString *constVarName = NewObjString(cu->curParser->vm, cu->curParser->preToken.start, cu->curParser->preToken.length);
        uint32_t constVarIdx = AddConstant(cu, OBJ_TO_VALUE(constVarName));
        // 为了调用System.getModuleVariable("foo", "bar1") 压入system
        EmitLoadModuleVar(cu, "System");
        // 为了调用System.getModuleVariable("foo", "bar1") 压入foo
        WriteOpcodeShortOperand(cu, OPCODE_LOAD_CONSTANT, constModIdx);
        // 为了调用System.getModuleVariable("foo", "bar1") 压入bar1
        WriteOpcodeShortOperand(cu, OPCODE_LOAD_CONSTANT, constModIdx);
        // 调用
        EmitCall(cu, 2, "get_module_variable(_,_)", 22);
        // 栈顶是其返回值
        // 同步到相应变量中
        DefineVariable(cu, var_idx);
    } while (MatchToken(cu->curParser, TOKEN_COMMA));
}

/**
 * @brief 编译语句
*/
static void CompileStatement(CompileUnit *cu)
{
    if (MatchToken(cu->curParser, TOKEN_IF)) {
        CompileIfStatement(cu);
    } else if (MatchToken(cu->curParser, TOKEN_WHILE)) {
        CompileWhileStatement(cu);
    } else if (MatchToken(cu->curParser, TOKEN_BREAK)) {
        CompileBreak(cu);
    } else if (MatchToken(cu->curParser, TOKEN_CONTINUE)) {
        CompileContinue(cu);
    } else if (MatchToken(cu->curParser, TOKEN_RETURN)) {
        CompileReturn(cu);
    } else if (MatchToken(cu->curParser, TOKEN_FOR)) {
        CompileForStatement(cu);
    } else if (MatchToken(cu->curParser, TOKEN_LEFT_BRACE)) {
        // 代码块有单独的作用域
        EnterScope(cu);
        CompileBlock(cu);
        LeaveScope(cu);
    } else {   
        // 若不是以上的语法结构则是单一表达式
        Expression(cu, BP_LOWEST);
        // 表达式的结果不重要，弹出栈顶结果
        WriteOpcode(cu, OPCODE_POP);
    }
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

    // 模块编译完成，生成return null返回
    WriteOpcode(&moduleCu, OPCODE_PUSH_NULL);
    WriteOpcode(&moduleCu, OPCODE_RETURN);

    // 检查在函数id中用行号生命的模块变量是否在引用之后有定义

    // 模块编译完成，当前编译单元置空
    vm->curParser->curCompileUnit = NULL;
    vm->curParser = vm->curParser->parent; // 回到父解析器
#ifdef DEBUG
    return EndCompileUnit(&moduleCu, "(script)", 8);
#else
    return EndCompileUnit(&moduleCu);
#endif
}

static SymbolBindRule Rules[] = 
{
    /* TOKEN_INVALID */         UNUSED_RULE,
    /* TOKEN_NUM */             PREFIX_SYMBOL(Literal),
    /* TOKEN_STRING */          PREFIX_SYMBOL(Literal),
    /* TOKEN_ID  */             {NULL, BP_NONE, ID, NULL, IdMethodSignature},
    /* TOKEN_INTERPOLATION*/    PREFIX_SYMBOL(StringInterpolation),
    /* TOKEN_VAR*/              UNUSED_RULE,
    /* TOKEN_FUN */             UNUSED_RULE,
    /* TOKEN_IF */              UNUSED_RULE,
    /* TOKEN_ELSE */            UNUSED_RULE,
    /* TOKEN_TRUE */            PREFIX_SYMBOL(Boolean),
    /* TOKEN_FALSE */           PREFIX_SYMBOL(Boolean),
    /* TOKEN_WHILE */           UNUSED_RULE,
    /* TOKEN_FOR */             UNUSED_RULE,
    /* TOKEN_BREAK */           UNUSED_RULE,
    /* TOKEN_CONTINUE */        UNUSED_RULE,
    /* TOKEN_RETURN */          UNUSED_RULE,
    /* TOKEN_NULL */            PREFIX_SYMBOL(Null),
    /* TOKEN_CLASS */           UNUSED_RULE,
    /* TOKEN_THIS */            PREFIX_SYMBOL(This),
    /* TOKEN_STATIC */          UNUSED_RULE,
    /* TOKEN_IS */              INFIX_OPERATOR("is", BP_IS),
    /* TOKEN_SUPER */           PREFIX_SYMBOL(Super),
    /* TOKEN_IMPORT */          UNUSED_RULE,
    /* TOKEN_COMMA */           UNUSED_RULE,
    /* TOKEN_LEFT_PAREN */      PREFIX_SYMBOL(Parenthesis),
    /* TOKEN_RIGHT_PAREN */     UNUSED_RULE,
    /* TOKEN_LEFT_BRACKET */    {NULL, BP_CALL, ListLiteral, Subscript, SubscriptMethodSignature},
    /* TOKEN_RIGHT_BRACKET */   UNUSED_RULE,
    /* TOKEN_LEFT_BRACE */      PREFIX_SYMBOL(MapLiteral),
    /* TOKEN_RIGHT_BRACE  */    UNUSED_RULE,
    /* TOKEN_DOT */             INFIX_SYMBOL(BP_CALL, CallEntry),
    /* TOKEN_DOT_DOT*/          INFIX_OPERATOR("..", BP_RANGE),
    /* TOKEN_ADD */             INFIX_OPERATOR("+", BP_TERM),
    /* TOKEN_SUB */             MIX_OPERATOR("-"),
    /* TOKEN_MUL */             INFIX_OPERATOR("*", BP_FACTOR),
    /* TOKEN_DIV */             INFIX_OPERATOR("/", BP_FACTOR),
    /* TOKEN_MOD */             INFIX_OPERATOR("%", BP_FACTOR),
    /* TOKEN_ASSIGN */          UNUSED_RULE,
    /* TOKEN_BIT_AND */         INFIX_OPERATOR("&", BP_BIT_AND),
    /* TOKEN_BIT_OR */          INFIX_OPERATOR("|", BP_BIT_OR),
    /* TOKEN_BIT_NOT */         PREFIX_OPERATOR("~"),
    /* TOKEN_BIT_SHIFT_RIGHT */ INFIX_OPERATOR(">>", BP_BIT_SHIFT),
    /* TOKEN_BIT_SHIFT_LEFT */  INFIX_OPERATOR("<<", BP_BIT_SHIFT),
    /* TOKEN_LOGIC_AND */       INFIX_SYMBOL(BP_LOGIC_AND, LogicAnd),
    /* TOKEN_LOGIC_OR */        INFIX_SYMBOL(BP_LOGIC_OR, LogicOr),
    /* TOKEN_LOGIC_NOT */       PREFIX_OPERATOR("!"),
    /* TOKEN_EQUAL */           INFIX_OPERATOR("==", BP_EQUAL),
    /* TOKEN_NOT_EQUAL */       INFIX_OPERATOR("!=", BP_EQUAL),
    /* TOKEN_GREATE */          INFIX_OPERATOR(">", BP_CMP),
    /* TOKEN_GREATE_EQUAL */    INFIX_OPERATOR(">=", BP_CMP),
    /* TOKEN_LESS */            INFIX_OPERATOR("<", BP_CMP),
    /* TOKEN_LESS_EQUAL */      INFIX_OPERATOR("<=", BP_CMP),
    /* TOKEN_QUESTION */        INFIX_SYMBOL(BP_ASSIGN, Condition),
    /* TOKEN_EOF */             UNUSED_RULE,
};