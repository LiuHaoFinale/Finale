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

static void RunFile(const char *path)
{
    const char *lastSlash = strrchr(path, '/');  // 用于判断path路径是否是当前路径的形式
    if (lastSlash != NULL) 
    {
        char *root = (char *)malloc(lastSlash - path + 2);
        memcpy(root, path, lastSlash - path + 1);
        root[lastSlash - path + 1] = '\0';
        rootDir = root;
    }
    VM *vm = NewVM();
    const char *sourceCode = ReadFile(path);  // 读取源码

    Parser parser;
    InitParser(vm, &parser, path, sourceCode);

    #include "llt/token.list"
    while (parser.curToken.type != TOKEN_EOF) // 循环打印词法分析器对脚本源码的识别结果
    {
        GetNextToken(&parser);
        LOG_SHOW(GREEN "%dL: %s [" NONE, parser.curToken.lineNo, tokenArray[parser.curToken.type]);
        uint32_t idx = 0;
        while (idx < parser.curToken.length) {
            LOG_SHOW(GREEN "%c" NONE, *(parser.curToken.start + idx ++));
        }
        LOG_SHOW(GREEN ")\n" NONE);
    }
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