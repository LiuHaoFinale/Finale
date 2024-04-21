/*
 * @Author: LiuHao
 * @Date: 2024-04-21 23:07:01
 * @Description: 
 */
#include "gtest/gtest.h"

#include <string.h>
#include <iostream>
#include <string>

// 测试系统库
class SystemLibrary: public ::testing::Test {
    protected:
        void SetUp() override
        {

        }

        void TearDown() override
        {

        }
};

/**
 * @brief 查找字符最后一次出现的位置的指针
*/
TEST_F(SystemLibrary, cstring_strrchr)
{
    const char *path = "path";
    const char *charPtr = strrchr(path, 't');
    EXPECT_EQ(*charPtr, 't');
}

TEST_F(SystemLibrary, cstring_strrchr2)
{
    const char *path = "/home/sample.spr";
    const char *lastSlash = strrchr(path, '/');  // 用于判断path路径是否是当前路径的形式
    char *root = (char *)malloc(lastSlash - path + 2); // 申请分配内存 5 + 2 = 7，索引是从0开始的
    memcpy(root, path, lastSlash - path + 1); // 5 + 1 复制是从1开始的
    root[lastSlash - path + 1] = '\0'; // 字符指针最后一位是\0
    EXPECT_EQ(static_cast<std::string>(root), "/home/");
}

TEST_F(SystemLibrary, cstring_strrchr3)
{
    const char *path = "sample.spr";
    const char *lastSlash = strrchr(path, '/');  // 用于判断path路径是否是当前路径的形式
    if (lastSlash != NULL) {
        char *root = (char *)malloc(lastSlash - path + 2);
        memcpy(root, path, lastSlash - path + 1);
        root[lastSlash - path + 1] = '\0';
    }
    EXPECT_EQ("", ""); // 如果RUN OK则为当前目录
}

TEST_F(SystemLibrary, cstring_strrchr4)
{
    const char *path = "./sample.spr";
    const char *lastSlash = strrchr(path, '/');  // 用于判断path路径是否是当前路径的形式
    char *root = (char *)malloc(lastSlash - path + 2);
    memcpy(root, path, lastSlash - path + 1);
    root[lastSlash - path + 1] = '\0';
    EXPECT_EQ(static_cast<std::string>(root), "./");
}