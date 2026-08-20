#pragma once

#include <cstddef>
#include <type_traits>

//C# 与 C++ 函数表使用的固定 ABI pack。
constexpr std::size_t OrbedenNativeApiPack = 8;

//验证仅由指针槽组成的跨语言函数表布局。
#define ORBEDEN_ASSERT_NATIVE_API_TABLE(TYPE, SLOT_COUNT) \
    static_assert(std::is_standard_layout_v<TYPE>, #TYPE " must use standard layout."); \
    static_assert(std::is_trivially_copyable_v<TYPE>, #TYPE " must be trivially copyable."); \
    static_assert(alignof(TYPE) <= OrbedenNativeApiPack, #TYPE " alignment exceeds the ABI pack."); \
    static_assert(sizeof(TYPE) == sizeof(void*) * (SLOT_COUNT), #TYPE " ABI slot count changed.")

//验证函数表字段处于约定的指针槽偏移。
#define ORBEDEN_ASSERT_NATIVE_API_SLOT(TYPE, FIELD, SLOT_INDEX) \
    static_assert(offsetof(TYPE, FIELD) == sizeof(void*) * (SLOT_INDEX), #TYPE "::" #FIELD " ABI offset changed.")
