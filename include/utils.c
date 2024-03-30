/*
 * @Author: LiuHao
 * @Date: 2024-03-14 22:07:15
 * @Description: 
 */
#include "utils.h"
#include "parser.h"
#include "vm.h"
#include "color_print.h"
#include <stdarg.h>
#include <stdlib.h>

void* MemManager(VM *vm, void *ptr, uint32_t oldSize, uint32_t newSize)
{
    vm->allocatedBytes += newSize - oldSize;
    if (newSize == 0) {
        free(ptr);
        return NULL;
    }
    return realloc(ptr, newSize);
}

/**
 * @brief 找出大于等于v最近的2次幂
*/
uint32_t CeilToPowerOf2(uint32_t v)
{
    v += (v == 0);
    v --;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v ++;
    return v;
}

DEFINE_BUFFER_METHOD(String)
DEFINE_BUFFER_METHOD(Integer)
DEFINE_BUFFER_METHOD(Character)
DEFINE_BUFFER_METHOD(Byte)

/**
 * @brief 通用报错函数
*/
void ErrorReport(void *parser, ErrorType errorType, const char *fmt, ...)
{
    char buffer[DEFAULT_BUFFER_SIZE] = { '\0' };
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buffer, DEFAULT_BUFFER_SIZE, fmt, ap);
    va_end(ap);

    switch (errorType) {
        case ERROR_IO:
        case ERROR_MEM:
            fprintf(stderr, "%s: %d In function %s(): %s\n", __FILE__, __LINE__, __func__, buffer);
            break;
        case ERROR_LEX:
        case ERROR_COMPILE:
            ASSERT(parser != NULL, "Parser is null");
    #ifndef COLOR
            fprintf(stderr, "%s:%d \"%s\"\n", ((Parser *)parser)->file, ((Parser *)parser)->preToken.lineNo, buffer);
    #else
            DEBUG_LOG(RED"%s:%d \"%s\"\n" NONE, ((Parser *)parser)->file, ((Parser *)parser)->preToken.lineNo, buffer);
    #endif
            break;
        case ERROR_RUNTIME:
            fprintf(stderr, "%s\n", buffer);
            break;
        default:
            NOT_REACHED();
    }
    exit(1);
}