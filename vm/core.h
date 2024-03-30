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

#endif // _VM_CORE_H