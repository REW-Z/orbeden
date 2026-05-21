#pragma once

#include <cstddef>
#include <cstdint>

//整数类型
typedef std::int8_t int8;
typedef std::int16_t int16;
typedef std::int32_t int32;
typedef std::int64_t int64;

typedef std::uint8_t uint8;
typedef std::uint16_t uint16;
typedef std::uint32_t uint32;
typedef std::uint64_t uint64;

//指针和尺寸类型
typedef std::ptrdiff_t isize;
typedef std::size_t usize;
typedef std::intptr_t intptr;
typedef std::uintptr_t uintptr;

//字符类型
typedef char8_t char8;
typedef char16_t char16;
typedef char32_t char32;

//浮点类型
typedef float float32;
typedef double float64;

//原始字节
typedef std::byte byte;

static_assert(sizeof(int8) == 1);
static_assert(sizeof(int16) == 2);
static_assert(sizeof(int32) == 4);
static_assert(sizeof(int64) == 8);

static_assert(sizeof(uint8) == 1);
static_assert(sizeof(uint16) == 2);
static_assert(sizeof(uint32) == 4);
static_assert(sizeof(uint64) == 8);

static_assert(sizeof(float32) == 4);
static_assert(sizeof(float64) == 8);
