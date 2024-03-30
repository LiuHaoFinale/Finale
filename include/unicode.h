/*
 * @Author: LiuHao
 * @Date: 2024-03-14 22:25:21
 * @Description: 
 */
#ifndef _INCLUDE_UNICODE_UTF_H
#define _INCLUDE_UNICODE_UTF_H

#include <stdint.h>

uint32_t GetByteNumOfEncodeUtf8(int value);
uint32_t GetByteNumOfDecodeUtf8(uint8_t byte);
uint8_t EncodeUtf8(uint8_t *buf, int value);
int DecodeUtf8(const uint8_t *bytePtr, uint32_t length);

#endif // _INCLUDE_UNICODE_UTF_H