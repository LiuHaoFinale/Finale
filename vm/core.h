/*
 * @Author: LiuHao
 * @Date: 2024-03-14 22:23:26
 * @Description: 
 */


#ifndef _VM_CORE_H
#define _VM_CORE_H

extern char *rootDir;

#define CORE_MODULE VT_TO_VALUE(VT_NULL)

char* ReadFile(const char *path);
void BindSuperClass(VM *vm, Class *subClass, Class *superClass);
void BindMethod(VM *vm , Class *class, uint32_t index, Method method);
static Class* DefineClass(VM *vm, ObjModule *objModule, const char *name);
int AddSymbol(VM *vm, SymbolTable *table, const char *symbol, uint32_t length);
int GetIndexFromSymbolTable(SymbolTable *table, const char *symbol, uint32_t length);
int EnsureSymbolExist(VM *vm, SymbolTable *table, const char *symbol, uint32_t length);
void BuildCore(VM *vm);
VMResult ExecuteModule(VM *vm, Value moduleName, const char *moduleCode);
#endif // _VM_CORE_H