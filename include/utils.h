#ifndef _INCLUDE_UTILS_H
#define _INCLUDE_UTILS_H

#include "common.h"

#define DEFAULT_BUFFER_SIZE 512

void* MemManager(VM *vm, void *ptr, uint32_t oldSize, uint32_t newSize);
uint32_t CeilToPowerOf2(uint32_t v);

#define ALLOCATE(vmPtr, type) \
    (type *)MemManager(vmPtr, NULL, 0, sizeof(type))

#define DEALLOCATE(vmPtr, memPtr) MemManager(vmPtr, memPtr, 0, 0)

// 用于柔性数组的内存分配
#define ALLOCATE_EXTRA(vmPtr, mainType, extraSize) \
    (mainType *)MemManager(vmPtr, NULL, 0, sizeof(mainType) + extraSize)

#define ALLOCATE_ARRAY(vmPtr, type, count) \
    (type *)MemManager(vmPtr, NULL, 0, sizeof(type) * count)

#define DEALLOCATE_ARRAY(vmPtr, arrayPtr, count) \
    MemManager(vmPtr, arrayPtr, sizeof(arrayPtr[0]) * count, 0)

typedef struct {
    char *str;
    uint32_t length;
} String;

typedef struct {
    uint32_t length; // 除结束\0之外的字符个数
    char start[0]; // 类似c99中的柔性数组
} CharValue; // 字符串缓冲区

// 声明buffer类型
#define DECLARE_BUFFER_TYPE(type) \
    typedef struct { \
        type* datas; \
        uint32_t count; \
        uint32_t capacity; \
    } type##Buffer; \
    void type##BufferInit(type##Buffer *buf); \
    void type##BufferFillWrite(VM *vm, type##Buffer *buf, type data, uint32_t fillCount); \
    void type##BufferAdd(VM *vm, type##Buffer *buf, type data); \
    void type##BufferClear(VM *vm, type##Buffer *buf);

// 定义buffer方法
#define DEFINE_BUFFER_METHOD(type) \
    void type##BufferInit(type##Buffer *buf) \
    { \
        buf->datas = NULL; \
        buf->count = buf->capacity = 0; \
    } \
    void type##BufferFillWrite(VM *vm, type##Buffer *buf, type data, uint32_t fillCount) \
    { \
        uint32_t newCounts = buf->capacity + fillCount; \
        if (newCounts > buf->capacity) { \
            size_t oldSize = buf->capacity * sizeof(type); \
            buf->capacity = CeilToPowerOf2(newCounts); \
            size_t newSize = buf->capacity * sizeof(type); \
            ASSERT(newSize > oldSize, "Faint ... memory allocate!"); \
            buf->datas = (type *)MemManager(vm, buf->datas, oldSize, newSize); \
        } \
        uint32_t cnt = 0; \
        while (cnt < fillCount) { \
            buf->datas[buf->count ++] = data; \
            cnt ++; \
        } \
    } \
    void type##BufferAdd(VM *vm, type##Buffer *buf, type data) \
    { \
        type##BufferFillWrite(vm, buf, data, 1); \
    } \
    void type##BufferClear(VM *vm, type##Buffer *buf) \
    { \
        size_t oldSize = buf->capacity * sizeof(buf->datas[0]); \
        MemManager(vm, buf->datas, oldSize, 0); \
        type##BufferInit(buf); \
    }

typedef uint8_t Byte;
typedef char    Character;
typedef int     Integer;
#define SymbolTable StringBuffer

DECLARE_BUFFER_TYPE(String)
DECLARE_BUFFER_TYPE(Character)
DECLARE_BUFFER_TYPE(Byte)
DECLARE_BUFFER_TYPE(Integer)

/**
 * IO错误 内存错误 语法错误 编译错误 运行时错误
*/
typedef enum {
    ERROR_IO,
    ERROR_MEM,
    ERROR_LEX,
    ERROR_COMPILE,
    ERROR_RUNTIME
} ErrorType;

#define IO_ERROR(...) \
    ErrorReport(NULL, ERROR_IO, __VA_ARGS__)
#define MEM_ERROR(...) \
    ErrorReport(NULL, ERROR_MEM, __VA_ARGS__)
#define LEX_ERROR(parser, ...) \
    ErrorReport(NULL, ERROR_LEX, __VA_ARGS__)
#define COMPILE_ERROR(parser, ...) \
    ErrorReport(NULL, ERROR_COMPILE, __VA_ARGS__)
#define RUNTIME_ERROR(...) \
    ErrorReport(NULL, ERROR_RUNTIME, __VA_ARGS__)

void ErrorReport(void *parser, ErrorType error_type, const char *fmt, ...);
uint32_t CeilToPowerOf2(uint32_t v);
#endif