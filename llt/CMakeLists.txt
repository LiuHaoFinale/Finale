cmake_minimum_required(VERSION 3.1)

project(finale)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/../cmake")

# 查找 libtest 静态库和头文件
find_package(libgtest REQUIRED)

# 添加项目目标
add_executable(gtest_app main_ut.cpp object.cpp system_lib.cpp)

# 包含 libtest 头文件路径
target_include_directories(gtest_app PRIVATE ${libgtest_INCLUDE_DIRS})

# 链接 libtest 静态库
target_link_libraries(gtest_app PRIVATE ${libgtest_LIBRARIES})