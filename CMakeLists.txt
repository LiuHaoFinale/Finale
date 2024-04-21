cmake_minimum_required(VERSION 3.0)

project(finale)

set(LEX_BIN finale_lex)
set(GRAMMAR_BIN finale_grammar)
set(FINALE_BIN finale)

add_compile_definitions(DEBUG_TEST)

include_directories(cli)
include_directories(debug)
include_directories(include)
include_directories(object)
include_directories(parser)
include_directories(vm)
include_directories(compile)
include_directories(object/class)
include_directories(gc)

# 通用设置文件输出目录
# set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/../output)

aux_source_directory(object/class CLASS_SRC)

add_executable(${LEX_BIN} EXCLUDE_FROM_ALL
    main.c
    include/unicode.c include/utils.c
    parser/parser.c
    compile/compile.c
    vm/vm.c vm/core.c
    object/class.c object/header_obj.c
    gc/gc.c
    ${CLASS_SRC}
)
target_link_libraries(${LEX_BIN} PRIVATE m)
add_executable(${GRAMMAR_BIN} EXCLUDE_FROM_ALL
    main.c
    include/unicode.c include/utils.c
    parser/parser.c
    compile/compile.c
    vm/vm.c vm/core.c
    object/class.c object/header_obj.c
    gc/gc.c
    ${CLASS_SRC}
)

add_executable(${FINALE_BIN} EXCLUDE_FROM_ALL
    main.c
)

set_target_properties(${LEX_BIN} PROPERTIES 
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/../output/lex"
)

set_target_properties(${GRAMMAR_BIN} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/../output/grammar"
)

set_target_properties(${FINALE} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/../output/finale"
)