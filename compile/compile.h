/*
 * @Author: LiuHao
 * @Date: 2024-03-16 21:44:14
 * @Description: 
 */
#ifndef _COMPILER_COMPILER_H
#define _COMPILER_COMPILER_H

#include "obj_fn.h"

#define MAX_LOCAL_VAR_NUM   128
#define MAX_UPVALUE_NUM     128
#define MAX_ID_LEN          128  // 变量名最大长度

#define MAX_METHOD_NAME_LEN MAX_ID_LEN
#define MAX_ARG_NUM 16

// 函数名长度+'('+n个参数+(n-1)个参数分隔符','+')'
#define MAX_SIGN_LEN MAX_METHOD_NAME_LEN + MAX_ARG_NUM * 2 + 1

#define MAX_FIELD_NUM 128

typedef struct {
    // 如果此upvalue是直接外层函数的局部变量就置为true
    boolean isEnclosingLocalVar;
    // 外层函数中局部变量的索引或者外层函数中的upvalue索引
    // 这去决定于is_enclosing_local_var的值
    uint32_t index;
} Upvalue;

typedef struct {
    const char *name;
    uint32_t length;
    int scopeDepth; // 局部变量作用域
    // 表示本函数中的局部变量是否是其内层函数所引用的upvalue
    // 则为true
    boolean isUpvalue;
} LocalVar; // 局部变量

typedef enum {
    /**
     * SIGN_CONSTRUCT
     * SIGN_METHOD
     * SIGN_GETTER
     * SIGN_SETTER
     * SIGN_SUBSCRIPT 表示getter形式的下标，即把[]处理为方法，如list[1]
     * SIGN_SUBSCRIPT_SETTER 同上list[1]="1"
    */
    SIGN_CONSTRUCT, SIGN_METHOD, SIGN_GETTER, SIGN_SETTER, SIGN_SUBSCRIPT, SIGN_SUBSCRIPT_SETTER
} SignatureType; // 方法的签名

typedef struct {
    SignatureType type;
    const char *name;
    uint32_t length;
    uint32_t argNum;
} Signature; // 签名

typedef struct loop {
    int condStartIndex; // 条件地址
    int bodyStartIndex; // 起始地址
    int scopeDepth; 
    int exitIndex; // 不满足时的目标地址
    struct loop *enclosingLoop; // 外层循环
} Loop; // loop结构

typedef struct {
    ObjString *name;
    SymbolTable fields; // 类属性符号表
    boolean inStatic; // 当前编译静态方法
    IntegerBuffer instantMethods; // 实例方法
    IntegerBuffer staticMethods; // 静态方法
    Signature *signature; // 当前正在编译的签名
} ClassBookKeep; // 用于记录类编译时的信息

typedef struct compileUnit CompileUnit;

int DefineModuleVar(VM *vm, ObjModule *objModule, const char *name, uint32_t length, Value value);
ObjFn* CompileModule(VM *vm, ObjModule *objModule, const char *moduleCore);

#endif