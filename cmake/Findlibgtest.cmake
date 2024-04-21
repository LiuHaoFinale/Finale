# Findlibgtest.cmake

# 查找 libgtest 头文件路径
find_path(libgtest_INCLUDE_DIR NAMES gtest.h PATHS "${CMAKE_SOURCE_DIR}/../open_source/gtest/include")

# 查找 libgtest 静态库路径
find_library(libgtest_LIBRARY NAMES libgtest.a PATHS "${CMAKE_SOURCE_DIR}/../open_source/gtest/lib")

# 检查是否找到 libtest 头文件和静态库
if(libgtest_INCLUDE_DIR AND libgtest_LIBRARY)
    # 导出 libgtest_INCLUDE_DIR 和 libgtest_LIBRARY 变量
    set(libgtest_INCLUDE_DIRS ${libgtest_INCLUDE_DIR})
    set(libgtest_LIBRARIES ${libgtest_LIBRARY})
    
    # 导出 libgtest_FOUND 变量
    set(libgtest_FOUND TRUE)
else()
    # 如果没有找到 libgtest 头文件或静态库，显示错误信息
    set(libgtest_FOUND FALSE)
    message(FATAL_ERROR "libgtest not found!")
endif()

# 导出 libgtest_FOUND、libgtest_INCLUDE_DIRS 和 libgtest_LIBRARIES 变量
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(libgtest DEFAULT_MSG libgtest_LIBRARIES libgtest_INCLUDE_DIRS)
