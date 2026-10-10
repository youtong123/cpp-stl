#pragma once

// 类型工具：扩容时"移动 or 拷贝"的选择依据
#include <type_traits>

namespace mystl {

// move_if_noexcept：
// 扩容（reallocate）时把旧元素搬到新缓冲区：
//   - 若 T 的移动构造是 noexcept（承诺不抛），用移动（高效）；
//   - 若移动构造可能抛异常、且 T 可拷贝，退回复制（强异常安全：拷贝抛了源缓冲区仍然完好）；
//   - 若不可拷贝（只能移动），没有退路，仍然移动。
// 与 std::move_if_noexcept 语义一致（C++11），此处手写一遍理解其原理。
template <typename T>
typename std::conditional<
    !std::is_nothrow_move_constructible<T>::value && std::is_copy_constructible<T>::value,
    const T&, T&&>::type
move_if_noexcept(T& x) noexcept {
    return static_cast<
        typename std::conditional<
            !std::is_nothrow_move_constructible<T>::value && std::is_copy_constructible<T>::value,
            const T&, T&&>::type>(x);
}

} // namespace mystl
