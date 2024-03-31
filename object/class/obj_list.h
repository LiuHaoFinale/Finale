/*
 * @Author: LiuHao
 * @Date: 2024-03-15 23:07:54
 * @Description: 
 */
#ifndef _OBJECT_LIST_H
#define _OBJECT_LIST_H

#include "class.h"
#include "vm.h"
#include "header_obj.h"

typedef struct {
    ObjHeader objHeader;
    ValueBuffer elements; 
} ObjList;

ObjList* NewObjList(VM *vm, uint32_t elementNum);
Value RemoveElement(VM *vm, ObjList *objList, uint32_t index);
void InsertElement(VM *vm, ObjList *objList, uint32_t index, Value value);


#endif