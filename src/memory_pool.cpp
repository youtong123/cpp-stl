#include "mystl/memory_pool.h"

#include <cstdlib>
#include <cstring>
#include <new>

namespace mystl {

// 静态成员定义
const std::size_t MemoryPool::kAlign;
const std::size_t MemoryPool::kMaxBytes;
const std::size_t MemoryPool::kFreeListSize;

MemoryPool::FreeNode* MemoryPool::free_list_[MemoryPool::kFreeListSize] = {nullptr};
char* MemoryPool::start_free_ = nullptr;
char* MemoryPool::end_free_ = nullptr;
std::size_t MemoryPool::heap_size_ = 0;
MemoryPool::ChunkNode* MemoryPool::chunks_ = nullptr;
std::mutex MemoryPool::mutex_;

void* MemoryPool::allocate(std::size_t n) {
    if (n == 0) {
        n = 1; // 保证至少有一个 FreeNode 的空间存放链表指针
    }
    if (n > kMaxBytes) { // 第一级：大块直接走 malloc
        void* p = std::malloc(n);
        if (!p) {
            throw std::bad_alloc();
        }
        return p;
    }
    // 第二级：小块走自由链表
    std::lock_guard<std::mutex> lock(mutex_);
    FreeNode*& head = free_list_[index(n)];
    if (!head) {
        return refill(round_up(n)); // 该链表为空：批量补充若干块
    }
    FreeNode* result = head;
    head = head->next;
    return result;
}

void MemoryPool::deallocate(void* p, std::size_t n) {
    if (!p) {
        return;
    }
    if (n == 0) {
        n = 1; // 与 allocate(0) 的行为保持一致（0 按 1 处理），也避免 index(0) 下溢
    }
    if (n > kMaxBytes) { // 大块归还系统
        std::free(p);
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    FreeNode* node = static_cast<FreeNode*>(p);
    std::size_t idx = index(n);
    node->next = free_list_[idx];
    free_list_[idx] = node;
}

void* MemoryPool::reallocate(void* p, std::size_t old_size, std::size_t new_size) {
    if (old_size > kMaxBytes && new_size > kMaxBytes) { // 大块放宽：交给 realloc
        void* q = std::realloc(p, new_size);
        if (!q) {
            throw std::bad_alloc();
        }
        return q;
    }
    if (round_up(old_size) == round_up(new_size)) {
        return p; // 同一尺寸档位，原地复用
    }
    // 跨档位：分配新块 + 拷贝较小长度 + 释放旧块
    void* q = allocate(new_size); // allocate/deallocate 各自加锁，不嵌套
    std::memcpy(q, p, old_size < new_size ? old_size : new_size);
    deallocate(p, old_size);
    return q;
}

// 调用方需持有锁。默认一次补充 20 块：1 块返回调用方，其余挂到自由链表。
void* MemoryPool::refill(std::size_t aligned) {
    int nobjs = 20;
    char* chunk = chunk_alloc(aligned, nobjs);
    if (nobjs == 1) {
        return chunk; // chunk 只能切出 1 块
    }
    FreeNode* result = reinterpret_cast<FreeNode*>(chunk);
    FreeNode* cur = reinterpret_cast<FreeNode*>(chunk + aligned);
    free_list_[index(aligned)] = cur;
    for (int i = 1;; ++i) {
        FreeNode* next = reinterpret_cast<FreeNode*>(reinterpret_cast<char*>(cur) + aligned);
        if (i == nobjs - 1) {
            cur->next = nullptr;
            break;
        }
        cur->next = next;
        cur = next;
    }
    return result;
}

// 调用方需持有锁。向系统要内存时给足余量：2 倍当前需求 + 已用堆量的 1/16，
// 让未来的分配尽量落在同一块 chunk 里（减少系统调用次数）。
char* MemoryPool::chunk_alloc(std::size_t size, int& nobjs) {
    std::size_t total = size * static_cast<std::size_t>(nobjs);
    std::size_t bytes_left = static_cast<std::size_t>(end_free_ - start_free_);

    if (bytes_left >= total) { // chunk 余量足够
        char* result = start_free_;
        start_free_ += total;
        return result;
    }
    if (bytes_left >= size) { // 余量不足但至少能切 1 块
        nobjs = static_cast<int>(bytes_left / size);
        total = size * static_cast<std::size_t>(nobjs);
        char* result = start_free_;
        start_free_ += total;
        return result;
    }

    // chunk 余量不够了：把零头挂到合适的自由链表中，避免浪费
    if (bytes_left > 0) {
        std::size_t idx = index(bytes_left);
        reinterpret_cast<FreeNode*>(start_free_)->next = free_list_[idx];
        free_list_[idx] = reinterpret_cast<FreeNode*>(start_free_);
    }

    std::size_t bytes_to_get = 2 * total + round_up(heap_size_ >> 4);
    char* chunk = static_cast<char*>(std::malloc(bytes_to_get));
    if (!chunk) {
        // 简化版：不回落其他自由链表，直接失败
        throw std::bad_alloc();
    }
    // 记账：release_all 需要释放全部申请过的 chunk
    ChunkNode* node = static_cast<ChunkNode*>(std::malloc(sizeof(ChunkNode)));
    if (!node) {
        std::free(chunk);
        throw std::bad_alloc();
    }
    node->base = chunk;
    node->next = chunks_;
    chunks_ = node;

    start_free_ = chunk;
    heap_size_ += bytes_to_get;
    end_free_ = start_free_ + bytes_to_get;
    return chunk_alloc(size, nobjs); // 新 chunk 就位后重试
}

// 释放全部 chunk 并复位池状态。前提：所有池分配均已归还（见头文件说明）。
void MemoryPool::release_all() {
    std::lock_guard<std::mutex> lock(mutex_);
    ChunkNode* node = chunks_;
    while (node != nullptr) {
        ChunkNode* next = node->next;
        std::free(node->base);
        std::free(node);
        node = next;
    }
    chunks_ = nullptr;
    start_free_ = nullptr;
    end_free_ = nullptr;
    heap_size_ = 0;
    for (std::size_t i = 0; i < kFreeListSize; ++i) {
        free_list_[i] = nullptr;
    }
}

} // namespace mystl
