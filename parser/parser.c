/*
 * @Author: LiuHao
 * @Date: 2024-03-14 22:30:27
 * @Description: 
 */
#include "parser.h"
#include "utils.h"
#include "common.h"
#include "unicode.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

typedef struct KeywordToken {
    char *keyword;
    uint8_t length;
    TokenType token;
} KeywordToken;  // 关键字(保留字)结构

KeywordToken g_keywordToken[] = {
    {"var",      3, TOKEN_VAR},
    {"fun",      3, TOKEN_FUN},
    {"if",       2, TOKEN_IF},
    {"else",     4, TOKEN_ELSE},
    {"true",     4, TOKEN_TRUE},
    {"false",    5, TOKEN_FALSE},
    {"while",    5, TOKEN_WHILE},
    {"for",      3, TOKEN_FOR},
    {"break",    5, TOKEN_BREAK},
    {"continue", 8, TOKEN_CONTINUE},
    {"return",   6, TOKEN_RETURN},
    {"null",     4, TOKEN_NULL},
    {"class",    5, TOKEN_CLASS},
    {"is",       2, TOKEN_IS},
    {"static",   6, TOKEN_STATIC},
    {"this",     4, TOKEN_THIS},
    {"super",    5, TOKEN_SUPER},
    {"import",   6, TOKEN_IMPORT},
    {NULL,       0, TOKEN_UNKNOWN}
};

/**
 * @brief 判断start是否为关键字并返回相应的token
*/
static TokenType IdOkKeyword(const char *start, uint32_t length)
{
    uint32_t idx = 0;
    while (g_keywordToken[idx].keyword != NULL) {
        if (g_keywordToken[idx].length == length && \
            memcmp(g_keywordToken[idx].keyword, start, length) == 0) {
            return g_keywordToken[idx].token;
        }
        idx ++;
    }
    return TOKEN_ID;
}

/**
 * @brief 查看下一个字符
*/
char LookAheadChar(Parser *parser)
{
    return *(parser->nextCharPtr);
}

/**
 * @brief 移动到下一个字符
*/
static void GetNextChar(Parser *parser)
{
    parser->curChar = *(parser->nextCharPtr ++);
}

/**
 * @brief 查看下一个字符是否为期望的，如果是就读进来，否则返回false
*/
static boolean MatchNextChar(Parser *parser, char expectedChar)
{
    if (LookAheadChar(parser) == expectedChar) {
        GetNextChar(parser);
        return true;
    }
    return false;
}

/**
 * @brief 跳过连续的空白字符
*/
static void SkipBlanks(Parser *parser)
{
    while (isspace(parser->curChar)) {
        if (parser->curChar == '\n') {
            parser->curToken.lineNo ++;
        }
        GetNextChar(parser);
    }
}

/**
 * @brief 跳过一行
*/
static void SkipALine(Parser *parser) {
    GetNextChar(parser);
    while (parser->curChar != '\0') {
        if (parser->curChar == '\n') {
            parser->curToken.lineNo ++;
            GetNextChar(parser);
            break;
        }
        GetNextChar(parser);
    }
}

/**
 * @brief 跳过行注释或区块注释
*/
static void SkipComment(Parser *parser)
{
    char nextChar = LookAheadChar(parser);
    if (parser->curChar == '/')
    {
        SkipALine(parser);
    }
    else
    {
        while (nextChar != '*' && nextChar != '\0')
        {
            GetNextChar(parser);
            if (parser->curChar == '\n')
            {
                parser->curToken.lineNo ++;
            }
            nextChar = LookAheadChar(parser);
        }
        if (MatchNextChar(parser, '*'))
        {
            if (!MatchNextChar(parser, '/'))
            {
                LEX_ERROR(parser, "expect '/' after '*'!");
            }
            GetNextChar(parser);
        }
        else
        {
            LEX_ERROR(parser, "expect '*/' before file end!");
        }
    }
    SkipBlanks(parser); // 注释之后有可能会有空白字符
}

/**
 * @brief 解析Unicode码点
*/
static void ParseUnicodeCodePoint(Parser *parser, ByteBuffer *buf)
{
    uint32_t idx = 0;
    int value = 0;
    uint8_t digit = 0;

    // 获取数值，u后面跟着4位十六进制数字
    while (idx ++ < 4) {
        GetNextChar(parser);
        if (parser->curChar == '\0') {
            LEX_ERROR(parser, "Unterminated Unicode!");
        }
        if (parser->curChar >= '0' && parser->curChar <= '9') {
            digit = parser->curChar - '0';
        } else if (parser->curChar >= 'a' && parser->curChar <= 'f') {
            digit = parser->curChar - 'a' + 10;
        } else if (parser->curChar >= 'A' && parser->curChar <= 'F') {
            digit = parser->curChar - 'A' + 10;
        } else {
            LEX_ERROR(parser, "Invalid Unicode!");
        }
        value = value * 16 | digit;
    }

    uint32_t byteNum = GetByteNumOfEncodeUtf8(value);
    ASSERT(byteNum != 0, "Utf8 Encode Bytes Should Be Between 1 And 4!");
    // TODO 直接写buf->datas，再次先写入byte_num个0，以保证实现有byte_num个空间
    ByteBufferFillWrite(parser->vm, buf, 0, byteNum);
    // 把value编码为UTF-8后写入缓冲区buf
    EncodeUtf8(buf->datas + buf->count - byteNum, value);
}

/**
 * @brief 解析字符串
*/
static void ParseString(Parser *parser)
{
    ByteBuffer str;
    ByteBufferInit(&str);
    while (true) {
        GetNextChar(parser);
        // \0是字符串结束标识，应该在"之后
        if (parser->curChar == '\0') {
            LEX_ERROR(parser, "unterminated string!");
        }
        if (parser->curChar == '"') {
            parser->curToken.type = TOKEN_STRING;
            break;
        }
        // 内嵌表达式%()
        if (parser->curChar == '%') {
            if (!MatchNextChar(parser, '(')) {
                LEX_ERROR(parser, "\'%\' should followed by \'(\'!");
            }
            // 只支持一层内嵌表达式
            if (parser->interpolationExpectRightParenNum > 0) {
                COMPILE_ERROR(parser, "sorry, I don't support nest interpolate expression!");
            }
            parser->interpolationExpectRightParenNum = 1;
            parser->curToken.type = TOKEN_INTERPOLATION;
            break;
        }
        if (parser->curChar == '\\') {
            GetNextChar(parser);
            switch (parser->curChar) {
                case '0':
                    ByteBufferAdd(parser->vm, &str, '\0');
                    break;
                case 'a':
                    ByteBufferAdd(parser->vm, &str, '\a');
                    break;
                case 'b':
                    ByteBufferAdd(parser->vm, &str, '\b');
                    break;
                case 'f':
                    ByteBufferAdd(parser->vm, &str, '\f');
                    break;
                case 'n':
                    ByteBufferAdd(parser->vm, &str, '\n');
                    break;
                case 'r':
                    ByteBufferAdd(parser->vm, &str, '\r');
                    break;
                case 't':
                    ByteBufferAdd(parser->vm, &str, '\t');
                    break;
                case 'u':
                    ParseUnicodeCodePoint(parser, &str);
                    break;
                case '"':
                    ByteBufferAdd(parser->vm, &str, '"');
                    break;
                case '\\':
                    ByteBufferAdd(parser->vm, &str, '\\');
                    break;
                default:
                    LEX_ERROR(parser, "unsupported escape \\%c", parser->curChar);
                    break;
            }
        }
        else {
            ByteBufferAdd(parser->vm, &str, parser->curChar);
        }
    }
    // 用识别到的字符串新建字符串对象存储到cur_token的value中
    // ObjString *obj_string = new_objString(parser->vm, (const char *)str.datas, str.count);
    // parser->curToken.value = OBJ_TO_VALUE(obj_string);
    ByteBufferClear(parser->vm, &str);
}

/**
 * @brief 解析标识符
*/
static void ParseId(Parser *parser, TokenType type)
{
    while (isalnum(parser->curChar) || parser->curChar == '_') {
        GetNextChar(parser);
    }
    // next_char_ptr会指向第一个不合法字符的下一个字符，因此-1
    uint32_t length = (uint32_t)(parser->nextCharPtr - parser->curToken.start - 1);
    parser->curToken.type = (type != TOKEN_UNKNOWN ? type: IdOkKeyword(parser->curToken.start, length));
    parser->curToken.length = length;
}

/**
 * @brief 解析十六进制数字
*/
static void ParseHexNum(Parser *parser)
{
    while (isxdigit(parser->curChar)) {
        GetNextChar(parser);
    }
}

/**
 * @brief 解析十进制数字
*/
static void ParseDecNum(Parser *parser)
{
    while (isdigit(parser->curChar)) {
        GetNextChar(parser);
    }

    if (parser->curChar == '.' && isdigit(LookAheadChar(parser))) {
        GetNextChar(parser);
        while (isdigit(parser->curChar)) {
            GetNextChar(parser);
        }
    }
}

/**
 * @brief 解析八进制
*/
static void ParseOctNum(Parser *parser)
{
    while (parser->curChar >= '0' && parser->curChar < '8') {
        GetNextChar(parser);
    }
}

/**
 * @brief 解析八进制 十进制 十六进制，仅支持前缀形式，后缀形式不支持
*/
static void ParserNum(Parser *parser)
{
    if (parser->curChar == '0' && MatchNextChar(parser, 'x')) {
        GetNextChar(parser);
        ParseHexNum(parser);
        // parser->curToken.value = NUM_TO_VALUE(strtol(parser->curToken.start, NULL, 16));
    } else if (parser->curChar == '0' && isdigit(LookAheadChar(parser))) {
        ParseOctNum(parser);
        // parser->curToken.value = NUM_TO_VALUE(strtol(parser->curToken.start, NULL, 8));
    } else {
        ParseDecNum(parser);
        // parser->curToken.value = NUM_TO_VALUE(strtod(parser->curToken.start, NULL));
    }
    // next_char_ptr会指向第一个不合法字符的下一个字符，因此-1
    parser->curToken.length = (uint32_t)(parser->nextCharPtr - parser->curToken.start - 1);
    parser->curToken.type = TOKEN_NUM;
}

/**
 * @brief 获取下一个token
*/
void GetNextToken(Parser *parser)
{
    parser->preToken = parser->curToken;
    SkipBlanks(parser);
    parser->curToken.type = TOKEN_EOF;  // 默认文件结束
    parser->curToken.length = 0;
    parser->curToken.start = parser->nextCharPtr - 1;
    // parser->curToken.value = 0;
    while (parser->curChar != '\0') {
        switch (parser->curChar) {
            case ',':
                parser->curToken.type = TOKEN_COMMA;
                break;
            case ':':
                parser->curToken.type = TOKEN_COLON;
                break;
            case '(':
                if (parser->interpolationExpectRightParenNum > 0) {
                    parser->interpolationExpectRightParenNum ++;
                }
                parser->curToken.type = TOKEN_LEFT_PAREN;
                break;
            case ')':
                if (parser->interpolationExpectRightParenNum > 0) {
                    parser->interpolationExpectRightParenNum --;
                    if (parser->interpolationExpectRightParenNum == 0) {
                        ParseString(parser);
                        break;
                    }
                }
                parser->curToken.type = TOKEN_RIGHT_PAREN;
                break;
            case '[':
                parser->curToken.type = TOKEN_LEFT_BRACKET;
                break;
            case ']':
                parser->curToken.type = TOKEN_RIGHT_BRACKET;
                break;
            case '{':
                parser->curToken.type = TOKEN_LEFT_BRACE;
                break;
            case '}':
                parser->curToken.type = TOKEN_RIGHT_BRACE;
                break;
            case '.':
                if (MatchNextChar(parser, '.')) {
                    parser->curToken.type = TOKEN_DOT_DOT;
                } else {
                    parser->curToken.type = TOKEN_DOT;
                }
                break;
            case '=':
                if (MatchNextChar(parser, '=')) {
                    parser->curToken.type = TOKEN_EQUAL;
                } else {
                    parser->curToken.type = TOKEN_ASSIGN;
                }
                break;
            case '+':
                parser->curToken.type = TOKEN_ADD;
                break;
            case '-':
                parser->curToken.type = TOKEN_SUB;
            case '*':
                parser->curToken.type = TOKEN_MUL;
                break;
            case '/':
                if (MatchNextChar(parser, '/') || MatchNextChar(parser, '*')) {
                    SkipComment(parser);
                    // 重置下一个token起始地址
                    parser->curToken.start = parser->nextCharPtr - 1;
                    continue;
                } else {
                    parser->curToken.type = TOKEN_DIV;
                }
                break;
            case '%':
                parser->curToken.type = TOKEN_MOD;
                break;
            case '&':
                if (MatchNextChar(parser, '&')) {
                    parser->curToken.type = TOKEN_LOGIC_AND;
                } else {
                    parser->curToken.type = TOKEN_BIT_AND;
                }
                break;
            case '|':
                if (MatchNextChar(parser, '|')) {
                    parser->curToken.type = TOKEN_LOGIC_OR;
                } else {
                    parser->curToken.type = TOKEN_BIT_OR;
                }
                break;
            case '~':
                parser->curToken.type = TOKEN_BIT_NOT;
                break;
            case '?':
                parser->curToken.type = TOKEN_QUESTION;
                break;
            case '>':
                if (MatchNextChar(parser, '=')) {
                    parser->curToken.type = TOKEN_GREAT_EQUAL;
                } else if (MatchNextChar(parser, '>')) {
                    parser->curToken.type = TOKEN_BIT_SHIFT_RIGHT;
                } else {
                    parser->curToken.type = TOKEN_GREAT;
                }
                break;
            case '<':
                if (MatchNextChar(parser, '=')) {
                    parser->curToken.type = TOKEN_LESS_EQUAL;
                } else if (MatchNextChar(parser, '<')) {
                    parser->curToken.type = TOKEN_BIT_SHIFT_LEFT;
                } else {
                    parser->curToken.type = TOKEN_LESS;
                }
                break;
            case '!':
                if (MatchNextChar(parser, '=')) {
                    parser->curToken.type = TOKEN_NOT_EQUAL;
                } else {
                    parser->curToken.type = TOKEN_LOGIC_NOT;
                }
                break;
            case '"':
                ParseString(parser);
                break;
            default:
                // 处理变量名或者数字
                // 变量名首字符是字母或下划线_
                if (isalpha(parser->curChar) || parser->curChar == '_') {
                    // 去解析变量名其余的部分
                    ParseId(parser, TOKEN_UNKNOWN);
                } else if (isdigit(parser->curChar)) {
                    ParserNum(parser);
                } else {
                    if (parser->curChar == '#' && MatchNextChar(parser, '!')) { // 魔数#!
                        SkipALine(parser);
                        parser->curToken.start = parser->nextCharPtr - 1;
                        // 重置下一个token起始地址
                        continue;
                    }
                    LEX_ERROR(parser, "Unsupported Char: \'%c\', Quit", parser->curChar);
                }
                return ;
        }
        // 大部分case出口
        parser->curToken.length = (uint32_t)(parser->nextCharPtr - parser->curToken.start);
        GetNextChar(parser);
        return;
    }
}

/**
 * @brief 若当前token为符合期望的则读入下一个token，并返回true
 * 否则不读入token且返回false
*/
boolean MatchToken(Parser *parser, TokenType expected)
{
    if (parser->curToken.type == expected) {
        GetNextToken(parser);
        return true;
    }
    return false;
}

/**
 * @brief 断言当前token为expected并读入下一个token，否则报错error
*/
void ConsumeCurToken(Parser *parser, TokenType expected, const char *errMsg)
{
    if (parser->curToken.type != expected) {
        COMPILE_ERROR(parser, errMsg);
    }
    GetNextToken(parser);
}

/**
 * @brief 断言下一个token为expected，否则报错err_msg
*/
void ConsumeNextToken(Parser *parser, TokenType expected, const char * errMsg)
{
    GetNextToken(parser);
    if (parser->curToken.type != expected)
    {
        COMPILE_ERROR(parser, errMsg);
    }
}

/**
 * @brief 由于source_code未必来自于文件file，有可能只是一个字符串
 * @param file 仅用作跟踪待编译的代码的标识，方便报错
*/
void InitParser(VM *vm, Parser *parser, const char *file, const char *sourceCode, ObjModule *objModule)
{
    parser->file = file;
    parser->sourceCode = sourceCode;
    // 当前字符cur_chart等于源码串的首字符
    parser->curChar = *parser->sourceCode;
    // 使下一个字符的地址next_char_ptr等于源码串首字符的下一个字符
    // 这样在获取下一字符时可以直接从源码串中获取，每次读取一个字符后再使
    // next_char_ptr自动加1就完成了更新，而无需调用get_char之类的函数
    parser->nextCharPtr = parser->sourceCode + 1;
    // 行号
    parser->curToken.lineNo = 1;
    // 默认设置 TOKEN_UNKNOWN
    parser->curToken.type = TOKEN_UNKNOWN;
    parser->curToken.start = NULL;
    parser->curToken.length = 0;
    parser->preToken = parser->curToken;
    parser->interpolationExpectRightParenNum = 0;
    parser->vm = vm;
    parser->curModule = objModule;
}