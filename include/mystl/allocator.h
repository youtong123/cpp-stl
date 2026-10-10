#pragma once

// 默认分配器：对 ::operator new / delete 的薄封装
// 设计要点：把「内存分配」与「对象构造」分成两组接口——
//   allocate / deallocate 只管裸内存；
//   construct / destroy 只管在裸内存上构造/析构对象。
// vector 等容器依赖这一分工（扩容时先分配裸内存、再搬运构造、最后析构释放）。
#include <cstddef>
#include <new>
#include <utility>

namespace mystl {

template <typename T>
class allocator {
public:
    typedef T value_type;
    typedef T* pointer;
    typedef const T* const_pointer;
    typedef T& reference;
    typedef const T& const_reference;
    typedef std::size_t size_type;
    typedef std::ptrdiff_t difference_type;

    // 容器内部可能需要为其他类型分配（如结点），rebind 提供类型映射
    template <typename U>
    struct rebind {
        typedef allocator<U> other;
    };

    allocator() noexcept {}
    allocator(const allocator&) noexcept {}
    template <typename U>
    allocator(const allocator<U>&) noexcept {}
    ~allocator() {}

    // 只分配 n 个 T 的裸内存（不构造对象），失败抛 std::bad_alloc
    pointer allocate(size_type n) {
        if (n == 0) {
            return nullptr;
        }
        void* p = ::operator new(n * sizeof(T));
        return static_cast<pointer>(p);
    }

    // 只释放裸内存（不调用析构，析构由 destroy 负责）
    void deallocate(pointer p, size_type /*n*/) noexcept {
        ::operator delete(p);
    }

    // 在 p 指向的裸内存上就地构造 U（完美转发）
    template <typename U, typename... Args>
    void construct(U* p, Args&&... args) {
        ::new (static_cast<void*>(p)) U(std::forward<Args>(args)...);
    }

    // 显式调用析构
    template <typename U>
    void destroy(U* p) {
        p->~U();
    }
};

// 无状态分配器：任意两个实例相等
template <typename T, typename U>
inline bool operator==(const allocator<T>&, const allocator<U>&) noexcept {
    return true;
}

template <typename T, typename U>
inline bool operator!=(const allocator<T>&, const allocator<U>&) noexcept {
    return false;
}

} // namespace mystl
