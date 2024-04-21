/*
 * @Author: LiuHao
 * @Date: 2024-04-21 16:33:19
 * @Description: 
 */
#ifndef __GC_GC_H__
#define __GC_GC_H__

#include "vm.h"

void GrayObject(VM* vm, ObjHeader* obj);
void GrayValue(VM* vm, Value value);
void StartGC(VM* vm);
void FreeObject(VM* vm, ObjHeader* obj);

#endif // !__GC_GC_H__