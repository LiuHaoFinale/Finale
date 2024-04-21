/*
 * @Author: LiuHao
 * @Date: 2024-03-31 14:44:35
 * @Description: 
 */
#ifndef _OBJECT_OBJ_STRING_H
#define _OBJECT_OBJ_STRING_H

#include "header_obj.h"

typedef struct {
    ObjHeader objHeader;
    uint32_t hashCode;// 字符从哈希值
    // typedef struct {
    //     uint32_t length; // 除结束\0之外的字符个数
    //     char start[0]; // 类似c99中的柔性数组
    // } CharValue; // 字符串缓冲区
    CharValue value;
} ObjString;

uint32_t HashString(char *str, uint32_t length);
void HashObjString(ObjString *objString);
ObjString* NewObjString(VM *vm, const char *str, uint32_t length);

#endif