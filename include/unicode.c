#include "unicode.h"
#include "common.h"

/**
 * @brief 返回value按照UTF-8编码后的字节数
*/
uint32_t GetByteNumOfEncodeUtf8(int value)
{
    ASSERT(value > 0, "Can't encode nagative value!");
    if (value <= 0x7f) {
        return 1;
    }
    if (value <= 0x7ff) {
        return 2;
    }
    if (value <= 0xffff) {
        return 3;
    }
    if (value <= 0x10ffff) {
        return 4;
    }
    return 0; // 超过范围返回0
}

/**
 * @brief 返回解码UTF-8的字节数
*/
uint32_t GetByteNumOfDecodeUtf8(uint8_t byte)
{
    if ((byte & 0xc0) == 0x80) {
        return 0;
    }
    if ((byte & 0xf8) == 0xf0) {
        return 4;
    }
    if ((byte & 0xf0) == 0xe0) {
        return 3;
    }
    if ((byte & 0xe0) == 0xc0) {
        return 2;
    }
    return 1;
}

/**
 * @brief 把value编码为UTF-8后写入缓冲区buf，返回写入的字节数
*/
uint8_t EncodeUtf8(uint8_t *buf, int value)
{
    ASSERT(value > 0, "Can't encode nagative value!");
    // 按照大端字节序写入缓冲区
    if (value <= 0x7f) {
        *buf = value & 0x7f;
        return 1;
    } else if (value <= 0x7ff) {
        *buf ++ = 0xc0 | ((value & 0x7c0) >> 6);
        *buf = 0x80 | (value & 0x3f);
        return 2;
    } else if (value <= 0xffff) {
        *buf ++ = 0xe0 | ((value & 0xf000) >> 12);
        *buf ++ = 0x80 | ((value & 0xfc0) >> 6);
        *buf = 0x80 | (value & 0x3f);
        return 3;
    } else if (value <= 0x10ffff) {
        *buf ++ = 0xf0 | ((value & 0x1c0000) >> 18);
        *buf ++ = 0x80 | ((value & 0x3f000) >> 12);
        *buf ++ = 0x80 | ((value & 0xfc0) >> 6);
        *buf ++ = 0x80 | (value & 0x3f);
        return 4; 
    }
    NOT_REACHED();
    return 0;
}

/**
 * @brief 解码以bytePtr为起始地址的UTF-8序列，其最大长度为length，否则返回-1
*/
int DecodeUtf8(const uint8_t *bytePtr, uint32_t length)
{
    if (*bytePtr <= 0x7f) {
        return *bytePtr;
    }
    int value;
    uint32_t remainingBytes;
    if ((*bytePtr & 0xe0) == 0xc0) {
        value = *bytePtr & 0x1f;
        remainingBytes = 1;
    } else if ((*bytePtr & 0xf0) == 0xe0) {
        value = *bytePtr & 0x0f;
        remainingBytes = 2;
    } else if ((*bytePtr & 0xf8) == 0xf0) {
        value = *bytePtr & 0x07;
        remainingBytes = 3;
    } else {
        return -1;
    }
    if (remainingBytes > length - 1) {
        return -1;
    }
    // 再读取低字节中的数据
    while (remainingBytes > 0) {
        bytePtr ++;
        remainingBytes --;
        // 高2位必须是10
        if ((*bytePtr & 0xc0) != 0x80) {
            return -1;
        }
        // 从次高字节往低字节，不断累加字节的低6位
        value = value << 6 | (*bytePtr & 0x3f);
    }
    return value;
}