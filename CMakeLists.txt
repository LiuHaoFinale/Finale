cmake_minimum_required(VERSION 3.0)

project(finale)

add_compile_definitions(TEST)

include_directories(cli)
include_directories(debug)
include_directories(include)
include_directories(object)
include_directories(parser)
include_directories(vm)
include_directories(compile)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/../output)

add_executable(finale_lex EXCLUDE_FROM_ALL
    main.c
    include/unicode.c include/utils.c
    parser/parser.c
    vm/core.c vm/vm.c  
)