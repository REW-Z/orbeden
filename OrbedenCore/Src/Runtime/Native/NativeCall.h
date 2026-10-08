#pragma once

#if defined(_WIN32)
#define ORBEDEN_NATIVE_CALL __cdecl
#else
#define ORBEDEN_NATIVE_CALL
#endif

// 字符串 ABI 约定：uint8 指针保存合法 UTF-8，长度按字节计算且不包含结尾零字符。
