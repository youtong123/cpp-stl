#pragma once

// 小块内存池（借鉴 SGI STL 两级分配器思路的简化实现）
//
// 两级结构：
//   第一级：请求 > 128 字节 → 直接走 malloc / free（大块没必要池化）；
//   第二级：请求 <= 128 字节 → 按 8 字节向上取整，落进 16 条自由链表之一；
//           空闲块用「块头存 next 指针」的方式串成链表，分配/释放都是 O(1) 链表操作。
//
// 与逐次 malloc/free 相比：
//   - 省去每次小分配的分配器元数据开销与系统调用/加锁路径；
//   - 空闲块不还给 OS，而是回收到自由链表等待复用（避免频繁 sbrk/brk）；
//   - chunk 不够时会一次性向系统申请一块较大的内存（2 倍需求 + 已用量的 1/16 增量），
//     切分成若干块，既填补当前链表，未来也可继续切给更大尺寸的链表。
//
// 线程安全：所有公开入口以互斥锁保护（SGI 原版是非线程安全的，此为简化增强）。
// 局限：内存只回收不归还 OS（长生命周期进程里属"以空间换时间"）；
//       需要清零时可在进程退出前调用 release_all() 显式归还。
#include <cstddef>
#include <mutex>
#include <new>
#include <utility>

namespace mystl {

class MemoryPool {
public:
    static const std::size_t kAlign = 8;         // 8 字节对齐
    static const std::size_t kMaxBytes = 128;    // 小块上限，超过走 malloc/free
    static const std::size_t kFreeListSize = kMaxBytes / kAlign; // 16 条自由链表

    // 申请 / 释放 n 字节（deallocate 必须原样传回申请时的 n）
    static void* allocate(std::size_t n);
    static void deallocate(void* p, std::size_t n);

    // 就地扩缩容：能原地调整就原地调整，否则分配新块 + 拷贝 + 释放旧块
    static void* reallocate(void* p, std::size_t old_size, std::size_t new_size);

    // 释放全部向系统申请的 chunk（测试收尾 / 进程退出前的可选清理）。
    // 前提：所有池分配都已归还；调用后自由链表清空、池可继续使用，
    // 但回收 chunk 里的历史块指针全部失效。日常运行无需调用——池的设计就是"只借不还"。
    static void release_all();

private:
    // 空闲块结点：复用块自身的头 8 字节存链表指针
    struct FreeNode {
        FreeNode* next;
    };

    // chunk 记账结点：release_all 需要能找到所有从系统申请过的块
    struct ChunkNode {
        void* base;
        ChunkNode* next;
    };

    static std::size_t index(std::size_t n) {        // n∈[1,128] → 链表下标 [0,15]
        return (n + kAlign - 1) / kAlign - 1;
    }
    static std::size_t round_up(std::size_t n) {     // 向上取整到 8 的倍数
        return (n + kAlign - 1) & ~(kAlign - 1);
    }

    // 以下三个函数假定调用方已持有 mutex_
    static void* refill(std::size_t aligned);        // 为某条自由链表批量补充空闲块
    static char* chunk_alloc(std::size_t size, int& nobjs); // 从内存块(chunk)里切 nobjs 块

    static FreeNode* free_list_[kFreeListSize];      // 16 条自由链表的表头
    static char* start_free_;                        // 当前 chunk 的可用起点
    static char* end_free_;                          // 当前 chunk 的终点
    static std::size_t heap_size_;                   // 已向系统申请的堆总量
    static ChunkNode* chunks_;                       // 全部 chunk 的记账链表
    static std::mutex mutex_;                        // 线程安全
};

// ---------------------------------------------------------------------------
// 池式分配器：符合容器所需的最小分配器接口
// 用法：mystl::vector<int, mystl::pool_allocator<int>> v;
// ---------------------------------------------------------------------------
template <typename T>
class pool_allocator {
public:
    typedef T value_type;
    typedef T* pointer;
    typedef const T* const_pointer;
    typedef T& reference;
    typedef const T& const_reference;
    typedef std::size_t size_type;
    typedef std::ptrdiff_t difference_type;

    template <typename U>
    struct rebind {
        typedef pool_allocator<U> other;
    };

    pool_allocator() noexcept {}
    pool_allocator(const pool_allocator&) noexcept {}
    template <typename U>
    pool_allocator(const pool_allocator<U>&) noexcept {}
    ~pool_allocator() {}

    // 内存分配走内存池（大块自动由池内部回落 malloc）
    pointer allocate(size_type n) {
        if (n == 0) {
            return nullptr;
        }
        return static_cast<pointer>(MemoryPool::allocate(n * sizeof(T)));
    }

    void deallocate(pointer p, size_type n) noexcept {
        MemoryPool::deallocate(p, n * sizeof(T));
    }

    template <typename U, typename... Args>
    void construct(U* p, Args&&... args) {
        ::new (static_cast<void*>(p)) U(std::forward<Args>(args)...);
    }

    template <typename U>
    void destroy(U* p) {
        p->~U();
    }
};

// 无状态分配器：任意两个池分配器实例相等
template <typename T, typename U>
inline bool operator==(const pool_allocator<T>&, const pool_allocator<U>&) noexcept {
    return true;
}

template <typename T, typename U>
inline bool operator!=(const pool_allocator<T>&, const pool_allocator<U>&) noexcept {
    return false;
}

} // namespace mystl
