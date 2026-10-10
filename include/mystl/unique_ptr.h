#pragma once

// 独占所有权智能指针 unique_ptr<T, Deleter>
//
// 要点：
//   - 独占语义：禁止拷贝（拷贝构造/拷贝赋值 = delete），只允许移动（move-only）；
//   - 移动即所有权转移：源指针置空，目标接管；
//   - 析构时若持有对象则调用删除器（默认为 delete）；
//   - release() 放弃所有权（不析构）；reset() 析构旧的、接管新的。
//   - 支持自定义删除器（仿函数/函数对象），0 开销抽象：删除器是成员，编译期确定。
#include <cstddef>
#include <type_traits>
#include <utility>

namespace mystl {

template <typename T>
struct default_delete {
    default_delete() noexcept = default;
    // 允许 default_delete<U> 隐式转换为 default_delete<T>（U* 可转 T* 时），
    // 配合 unique_ptr 的「派生 → 基类」转换移动构造使用
    template <typename U,
              typename = typename std::enable_if<std::is_convertible<U*, T*>::value>::type>
    default_delete(const default_delete<U>&) noexcept {}
    void operator()(T* p) const {
        delete p;
    }
};

template <typename T, typename Deleter = default_delete<T>>
class unique_ptr {
public:
    typedef T* pointer;
    typedef T element_type;
    typedef Deleter deleter_type;

    // ---------------- 构造 / 析构 ----------------
    unique_ptr() noexcept : ptr_(nullptr), deleter_() {}
    explicit unique_ptr(pointer p) noexcept : ptr_(p), deleter_() {}
    unique_ptr(pointer p, const Deleter& d) noexcept : ptr_(p), deleter_(d) {}

    // 移动构造：独占权转移，源置空
    unique_ptr(unique_ptr&& other) noexcept
        : ptr_(other.ptr_), deleter_(std::move(other.deleter_)) {
        other.ptr_ = nullptr;
    }

    // 由 unique_ptr<U, E> 移动构造（U* 可转 T* 且删除器 E 可转 Deleter 时）：
    // 支持 unique_ptr<Base> b(std::move(d))，d 为 unique_ptr<Derived>
    template <typename U, typename E,
              typename = typename std::enable_if<
                  std::is_convertible<U*, T*>::value &&
                  std::is_convertible<E, Deleter>::value>::type>
    unique_ptr(unique_ptr<U, E>&& other) noexcept
        : ptr_(other.release()), deleter_(std::move(other.deleter_)) {}

    // 独占：禁止拷贝
    unique_ptr(const unique_ptr&) = delete;
    unique_ptr& operator=(const unique_ptr&) = delete;

    unique_ptr& operator=(unique_ptr&& other) noexcept {
        if (this != &other) {
            reset(other.release());
            deleter_ = std::move(other.deleter_);
        }
        return *this;
    }

    ~unique_ptr() {
        if (ptr_ != nullptr) {
            deleter_(ptr_);
        }
    }

    // ---------------- 观察 ----------------
    pointer get() const noexcept { return ptr_; }
    Deleter& get_deleter() noexcept { return deleter_; }
    const Deleter& get_deleter() const noexcept { return deleter_; }
    explicit operator bool() const noexcept { return ptr_ != nullptr; }

    T& operator*() const { return *ptr_; }
    pointer operator->() const noexcept { return ptr_; }

    // ---------------- 修改 ----------------
    // 放弃所有权：返回裸指针，自己不析构（调用方接管）
    pointer release() noexcept {
        pointer p = ptr_;
        ptr_ = nullptr;
        return p;
    }

    // 接管新指针（先析构旧的）
    void reset(pointer p = pointer()) noexcept {
        if (ptr_ != nullptr) {
            deleter_(ptr_);
        }
        ptr_ = p;
    }

    void swap(unique_ptr& other) noexcept {
        std::swap(ptr_, other.ptr_);
        std::swap(deleter_, other.deleter_);
    }

private:
    template <typename U, typename E>
    friend class unique_ptr; // 允许 U/E 版本互访（移动构造转换用）

    pointer ptr_;
    Deleter deleter_;
};

template <typename T, typename D>
void swap(unique_ptr<T, D>& a, unique_ptr<T, D>& b) noexcept {
    a.swap(b);
}

} // namespace mystl
