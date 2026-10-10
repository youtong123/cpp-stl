#pragma once

// 模板动态数组 vector<T>（仿 STL 手写）
//
// 要点：
//   - 三指针语义用 data_ / size_ / capacity_ 表达；迭代器就是裸指针；
//   - 容量倍增扩容（cap ? cap*2 : 1），push_back 均摊 O(1)；
//   - 扩容搬运旧元素时使用 move_if_noexcept：移动构造 noexcept 才移动，
//     否则拷贝——拷贝抛异常时源缓冲区完好，保证强异常安全；
//   - 完整拷贝控制（rule of five）：深拷贝、移动构造/移动赋值（noexcept），
//     拷贝赋值用 copy-and-swap 天然处理自赋值；
//   - 分配器参数化（默认 allocator<T>，可换 pool_allocator<T>），
//     分配（allocate/deallocate）与构造（construct/destroy）严格分离。
#include <cstddef>
#include <initializer_list>
#include <stdexcept>
#include <utility>

#include "mystl/allocator.h"
#include "mystl/type_util.h"

namespace mystl {

template <typename T, typename Alloc = allocator<T>>
class vector {
public:
    typedef T value_type;
    typedef Alloc allocator_type;
    typedef T* iterator;             // 指针式迭代器
    typedef const T* const_iterator;
    typedef T& reference;
    typedef const T& const_reference;
    typedef std::size_t size_type;
    typedef std::ptrdiff_t difference_type;

    vector() noexcept : data_(nullptr), size_(0), capacity_(0) {}

    explicit vector(size_type n, const T& value = T())
        : data_(nullptr), size_(0), capacity_(0) {
        if (n > 0) {
            data_ = alloc_.allocate(n);
            size_type i = 0;
            try {
                for (; i < n; ++i) {
                    alloc_.construct(data_ + i, value);
                }
            } catch (...) {
                for (size_type j = 0; j < i; ++j) {
                    alloc_.destroy(data_ + j);
                }
                alloc_.deallocate(data_, n);
                throw;
            }
            size_ = capacity_ = n;
        }
    }

    vector(std::initializer_list<T> il)
        : data_(nullptr), size_(0), capacity_(0) {
        reserve(il.size());
        for (const T& x : il) {
            alloc_.construct(data_ + size_, x);
            ++size_;
        }
    }

    // 拷贝构造：深拷贝（新缓冲区，逐个构造）
    vector(const vector& other)
        : data_(nullptr), size_(0), capacity_(0) {
        if (other.size_ > 0) {
            data_ = alloc_.allocate(other.capacity_);
            capacity_ = other.capacity_;
            size_type i = 0;
            try {
                for (; i < other.size_; ++i) {
                    alloc_.construct(data_ + i, other.data_[i]);
                }
            } catch (...) {
                for (size_type j = 0; j < i; ++j) {
                    alloc_.destroy(data_ + j);
                }
                alloc_.deallocate(data_, capacity_);
                throw;
            }
            size_ = other.size_;
        }
    }

    // 移动构造：窃取缓冲区（noexcept），源置空但保持合法
    vector(vector&& other) noexcept
        : data_(other.data_), size_(other.size_), capacity_(other.capacity_) {
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }

    // 拷贝赋值：copy-and-swap，自赋值安全（即使先自我析构也不会炸）
    vector& operator=(const vector& rhs) {
        if (this != &rhs) {
            vector tmp(rhs); // 先完整拷贝成功，再替换（异常安全）
            swap(tmp);
        }
        return *this;
    }

    // 移动赋值：清空自己后窃取（noexcept）
    vector& operator=(vector&& rhs) noexcept {
        if (this != &rhs) {
            destroy_and_deallocate();
            data_ = rhs.data_;
            size_ = rhs.size_;
            capacity_ = rhs.capacity_;
            rhs.data_ = nullptr;
            rhs.size_ = 0;
            rhs.capacity_ = 0;
        }
        return *this;
    }

    ~vector() {
        destroy_and_deallocate();
    }

    // ---------------- 元素访问 ----------------
    reference operator[](size_type i) { return data_[i]; }
    const_reference operator[](size_type i) const { return data_[i]; }

    reference at(size_type i) {
        if (i >= size_) {
            throw std::out_of_range("mystl::vector::at");
        }
        return data_[i];
    }
    const_reference at(size_type i) const {
        if (i >= size_) {
            throw std::out_of_range("mystl::vector::at");
        }
        return data_[i];
    }

    reference front() { return data_[0]; }
    const_reference front() const { return data_[0]; }
    reference back() { return data_[size_ - 1]; }
    const_reference back() const { return data_[size_ - 1]; }
    T* data() noexcept { return data_; }
    const T* data() const noexcept { return data_; }

    // ---------------- 迭代器 ----------------
    iterator begin() noexcept { return data_; }
    const_iterator begin() const noexcept { return data_; }
    const_iterator cbegin() const noexcept { return data_; }
    iterator end() noexcept { return data_ + size_; }
    const_iterator end() const noexcept { return data_ + size_; }
    const_iterator cend() const noexcept { return data_ + size_; }

    // ---------------- 容量 ----------------
    bool empty() const noexcept { return size_ == 0; }
    size_type size() const noexcept { return size_; }
    size_type capacity() const noexcept { return capacity_; }

    // 预留容量（只增不减）
    void reserve(size_type n) {
        if (n <= capacity_) {
            return;
        }
        T* new_data = alloc_.allocate(n);
        size_type i = 0;
        try {
            // noexcept 移动构造 → 移动；否则（可拷贝时）拷贝，保强异常安全
            for (; i < size_; ++i) {
                alloc_.construct(new_data + i, mystl::move_if_noexcept(data_[i]));
            }
        } catch (...) {
            for (size_type j = 0; j < i; ++j) {
                alloc_.destroy(new_data + j);
            }
            alloc_.deallocate(new_data, n);
            throw;
        }
        for (size_type j = 0; j < size_; ++j) {
            alloc_.destroy(data_ + j);
        }
        if (data_) {
            alloc_.deallocate(data_, capacity_);
        }
        data_ = new_data;
        capacity_ = n;
    }

    void resize(size_type n) {
        resize(n, T());
    }

    void resize(size_type n, const T& value) {
        if (n < size_) {
            for (size_type i = n; i < size_; ++i) {
                alloc_.destroy(data_ + i);
            }
            size_ = n;
        } else if (n > size_) {
            if (n > capacity_) {
                reserve(grow_size(n));
            }
            for (size_type i = size_; i < n; ++i) {
                alloc_.construct(data_ + i, value);
            }
            size_ = n;
        }
    }

    // ---------------- 修改 ----------------
    void push_back(const T& value) {
        if (size_ == capacity_) {
            reserve(grow_size(size_ + 1));
        }
        alloc_.construct(data_ + size_, value);
        ++size_;
    }

    void push_back(T&& value) {
        if (size_ == capacity_) {
            reserve(grow_size(size_ + 1));
        }
        alloc_.construct(data_ + size_, std::move(value));
        ++size_;
    }

    // 就地构造：参数直接转发给 T 的构造函数，省一次临时对象
    template <typename... Args>
    reference emplace_back(Args&&... args) {
        if (size_ == capacity_) {
            reserve(grow_size(size_ + 1));
        }
        alloc_.construct(data_ + size_, std::forward<Args>(args)...);
        return data_[size_++];
    }

    void pop_back() {
        // 前置条件：!empty()（与 std 一致）
        --size_;
        alloc_.destroy(data_ + size_);
    }

    void clear() noexcept {
        for (size_type i = 0; i < size_; ++i) {
            alloc_.destroy(data_ + i);
        }
        size_ = 0;
    }

    void swap(vector& other) noexcept {
        std::swap(data_, other.data_);
        std::swap(size_, other.size_);
        std::swap(capacity_, other.capacity_);
    }

private:
    // 扩容目标：至少 2 倍，且不小于实际需求
    size_type grow_size(size_type needed) const {
        size_type doubled = capacity_ ? capacity_ * 2 : 1;
        return doubled > needed ? doubled : needed;
    }

    // 析构全部元素（只析构已构造的 size_ 个）并归还裸内存
    void destroy_and_deallocate() noexcept {
        for (size_type i = 0; i < size_; ++i) {
            alloc_.destroy(data_ + i);
        }
        if (data_) {
            alloc_.deallocate(data_, capacity_);
        }
        data_ = nullptr;
        size_ = 0;
        capacity_ = 0;
    }

    T* data_;
    size_type size_;
    size_type capacity_;
    Alloc alloc_;
};

template <typename T, typename Alloc>
void swap(vector<T, Alloc>& a, vector<T, Alloc>& b) noexcept {
    a.swap(b);
}

template <typename T, typename Alloc>
bool operator==(const vector<T, Alloc>& a, const vector<T, Alloc>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (!(a[i] == b[i])) {
            return false;
        }
    }
    return true;
}

template <typename T, typename Alloc>
bool operator!=(const vector<T, Alloc>& a, const vector<T, Alloc>& b) {
    return !(a == b);
}

} // namespace mystl
