/*
 * @Author: LiuHao
 * @Date: 2024-03-14 22:23:23
 * @Description: 
 */
#include "utils.h"
#include "vm.h"
#include "core.h"
// #include "compile.h"
#include <string.h>
#include <sys/stat.h>

char *rootDir = NULL; // 根目录

/**
 * @brief 读取源代码文件
*/
char* ReadFile(const char *path)
{
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        IO_ERROR("Could't open file \"%s\".", path);
    }

    struct stat fileStat;
    stat(path, &fileStat);
    size_t fileSize = fileStat.st_size;
    char *fileContent = (char *)malloc(fileSize + 1);
    if (fileContent == NULL) {
        MEM_ERROR("Could't allocate memory for reading file \"%s\".\n", path);
    }

    size_t numRead = fread(fileContent, sizeof(char), fileSize, file);
    if (numRead < fileSize) {
        IO_ERROR("Could't read file \"%s\".\n", path);
    }
    fileContent[fileSize] = '\0';

    fclose(file);
    // printf("%s", file_content);
    return fileContent;
}