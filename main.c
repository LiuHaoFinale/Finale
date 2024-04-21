/*
 * @Author: LiuHao
 * @Date: 2024-03-14 22:46:17
 * @Description: 
 */

#include <stdio.h>
#include <string.h>
#include "parser.h"
#include "vm.h"
#include "core.h"
#include "color_print.h"
#include "obj_string.h"

static void RunFile(const char *path)
{
    // path是否包含/，否则为当前路径
    const char *lastSlash = strrchr(path, '/');  // 用于判断path路径是否是当前路径的形式
    if (lastSlash != NULL) {
        char *root = (char *)malloc(lastSlash - path + 2);
        memcpy(root, path, lastSlash - path + 1);
        root[lastSlash - path + 1] = '\0';
        rootDir = root; // 将路径保存在rootDir
    }
    VM *vm = NewVM();
    const char *sourceCode = ReadFile(path);  // 读取源码
    LOG_SHOW(YELLOW"Input File PathName: %s" NONE, path);
    ExecuteModule(vm, OBJ_TO_VALUE(NewObjString(vm, path, strlen(path))), sourceCode);
}

int main(int argc, const char **argv)
{
    if (argc == 1) {
        ;
    } else {
        RunFile(argv[1]);
    }
    return 0;
}