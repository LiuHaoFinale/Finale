/*
 * @Author: LiuHao
 * @Date: 2024-03-30 22:46:56
 * @Description: 
 */
#include "vm.h"
#include <stdlib.h>
#include "utils.h"

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
