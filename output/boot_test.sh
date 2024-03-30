# !/bin/bash

# 检查参数数量是否正确
if [ $# -ne 1 ] && [ $# -ne 2 ]; then
    echo "Usage: $0 {lex|grammar|finale [file]}"
    exit 1
fi

# 检查第一个参数
case "$1" in
    lex)
        cd "$1"
        ./finale_lex lex.spr
        ;;
    grammar)
        cd "$1"
        ./finale_grammar grammar.spr
        ;;
    finale)
        cd "$1"
        # 检查是否提供了第二个参数
        if [ $# -ne 2 ]; then
            echo "Usage: $0 finale file"
            exit 1
        fi
        ./finale "$2"
        ;;
    *)
        echo "Invalid argument: $1"
        echo "Usage: $0 {lex|grammar|finale [file]}"
        exit 1
        ;;
esac

exit 0
