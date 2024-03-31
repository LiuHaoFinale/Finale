/*
 * @Author: LiuHao
 * @Date: 2024-03-15 23:22:00
 * @Description: 
 */
#ifndef _OBJECT_MAP_H
#define _OBJECT_MAP_H

#include "header_obj.h"

#define MAP_LOAD_PERCENT 0.8

typedef struct {
    Value key;
    Value value;
} Entry; // key value对儿

typedef struct {
    ObjHeader objHeader;
    uint32_t count;
    uint32_t capacity; // map的容量
    Entry *entries; // Entry数组
} ObjMap;

ObjMap* NewObjMap(VM *vm);

void MapSet(VM *vm, ObjMap *objMap, Value key, Value value);
Value MapGet(ObjMap *objMap, Value key);
void ClearMap(VM *vm, ObjMap *objMap);
Value RemoveKey(VM *vm, ObjMap *objMap, Value key);

#endif