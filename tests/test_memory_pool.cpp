// test_memory_pool.cpp — mystl::MemoryPool 的断言测试
#include <cstdint>
#include <cstring>
#include <vector>

#include "mystl/memory_pool.h"
#include "test_util.h"

using mystl::MemoryPool;

namespace {

// 把 p 的前 n 字节全部写成 v，再整体校验一遍
void fill_check(void* p, std::size_t n, unsigned char v) {
    unsigned char* b = static_cast<unsigned char*>(p);
    std::memset(b, v, n);
    bool ok = true;
    for (std::size_t i = 0; i < n; ++i) {
        if (b[i] != v) {
            ok = false;
            break;
        }
    }
    CHECK(ok);
}

bool aligned8(const void* p) {
    return reinterpret_cast<std::uintptr_t>(p) % MemoryPool::kAlign == 0;
}

} // namespace

int main() {
    TEST_SECTION("对齐与可用性：1..128 全尺寸档");
    {
        for (std::size_t n = 1; n <= MemoryPool::kMaxBytes; ++n) {
            void* p = MemoryPool::allocate(n);
            CHECK(p != nullptr);
            CHECK(aligned8(p));
            std::memset(p, 0xAB, n);
            MemoryPool::deallocate(p, n);
        }
    }

    TEST_SECTION("LIFO 复用：后释放的先被分配");
    {
        void* p1 = MemoryPool::allocate(16);
        void* p2 = MemoryPool::allocate(16);
        void* p3 = MemoryPool::allocate(16);
        CHECK(p1 != nullptr && p2 != nullptr && p3 != nullptr);
        CHECK(p1 != p2 && p2 != p3 && p1 != p3);

        MemoryPool::deallocate(p2, 16); // 先还 p2，再还 p1 → 链表头是 p1
        MemoryPool::deallocate(p1, 16);
        CHECK_EQ(MemoryPool::allocate(16), p1); // 最近释放的先被复用
        CHECK_EQ(MemoryPool::allocate(16), p2);

        MemoryPool::deallocate(p1, 16);
        MemoryPool::deallocate(p2, 16);
        MemoryPool::deallocate(p3, 16);
    }

    TEST_SECTION("100 块同时存活：互不重叠、内容独立");
    {
        const int kCount = 100;
        std::vector<void*> ps;
        for (int i = 0; i < kCount; ++i) {
            ps.push_back(MemoryPool::allocate(16));
            CHECK(ps.back() != nullptr);
            std::memset(ps.back(), i & 0xFF, 16);
        }
        // 两两互不相同（没有任何两块是同一块内存）
        bool distinct = true;
        for (int i = 0; i < kCount && distinct; ++i) {
            for (int j = i + 1; j < kCount; ++j) {
                if (ps[i] == ps[j]) {
                    distinct = false;
                    break;
                }
            }
        }
        CHECK(distinct);
        // 内容没有被邻居踩坏
        for (int i = 0; i < kCount; ++i) {
            unsigned char* b = static_cast<unsigned char*>(ps[i]);
            bool ok = true;
            for (int j = 0; j < 16; ++j) {
                if (b[j] != static_cast<unsigned char>(i & 0xFF)) {
                    ok = false;
                    break;
                }
            }
            CHECK(ok);
        }
        for (int i = 0; i < kCount; ++i) {
            MemoryPool::deallocate(ps[i], 16);
        }
    }

    TEST_SECTION("大块路径（>128B）与 0 字节边界");
    {
        void* big = MemoryPool::allocate(129); // 第一级：malloc
        CHECK(big != nullptr);
        std::memset(big, 0x5A, 129);
        MemoryPool::deallocate(big, 129);

        void* huge = MemoryPool::allocate(4096);
        CHECK(huge != nullptr);
        std::memset(huge, 0x77, 4096);
        MemoryPool::deallocate(huge, 4096);

        void* zero = MemoryPool::allocate(0); // 0 按 1 字节处理
        CHECK(zero != nullptr);
        MemoryPool::deallocate(zero, 0); // deallocate(p, 0) 与 allocate(0) 行为一致

        MemoryPool::deallocate(nullptr, 16); // 空指针安全
    }

    TEST_SECTION("reallocate：同档位原地复用，跨档位搬迁保内容");
    {
        void* p = MemoryPool::allocate(25);
        fill_check(p, 25, 0x11);
        CHECK_EQ(MemoryPool::reallocate(p, 25, 31), p); // round_up(25)==round_up(31)==32：原地
        MemoryPool::deallocate(p, 31);

        void* q = MemoryPool::allocate(16);
        fill_check(q, 16, 0x22);
        void* q2 = MemoryPool::reallocate(q, 16, 100); // 16 → 104 档：分配新块 + 搬迁
        CHECK(q2 != nullptr);
        CHECK(q2 != q);
        unsigned char* qb = static_cast<unsigned char*>(q2);
        bool kept = true;
        for (int i = 0; i < 16; ++i) {
            if (qb[i] != 0x22) {
                kept = false;
                break;
            }
        }
        CHECK(kept);
        MemoryPool::deallocate(q2, 100);

        void* r = MemoryPool::allocate(64); // 小块 → 大块（>128 走 malloc）
        fill_check(r, 64, 0x33);
        void* r2 = MemoryPool::reallocate(r, 64, 300);
        CHECK(r2 != nullptr);
        unsigned char* rb = static_cast<unsigned char*>(r2);
        bool kept2 = true;
        for (int i = 0; i < 64; ++i) {
            if (rb[i] != 0x33) {
                kept2 = false;
                break;
            }
        }
        CHECK(kept2);
        MemoryPool::deallocate(r2, 300);

        void* s = MemoryPool::allocate(200); // 大块 → 大块：交给 realloc
        fill_check(s, 200, 0x44);
        void* s2 = MemoryPool::reallocate(s, 200, 300);
        CHECK(s2 != nullptr);
        unsigned char* sb = static_cast<unsigned char*>(s2);
        bool kept3 = true;
        for (int i = 0; i < 200; ++i) {
            if (sb[i] != 0x44) {
                kept3 = false;
                break;
            }
        }
        CHECK(kept3);
        MemoryPool::deallocate(s2, 300);
    }

    TEST_SECTION("随机压力：20000 次混合分配/释放（LCG 确定性序列）");
    {
        std::uint32_t state = 12345u;
        std::vector<void*> live;
        std::vector<std::size_t> sizes;
        std::vector<unsigned char> marks;
        std::size_t alloc_count = 0;

        for (int i = 0; i < 20000; ++i) {
            state = state * 1664525u + 1013904223u;
            bool do_free = !live.empty() && ((state & 3u) == 0u || live.size() >= 512u);
            if (do_free) {
                std::size_t idx = (state >> 8) % live.size();
                unsigned char* b = static_cast<unsigned char*>(live[idx]);
                std::size_t n = sizes[idx];
                CHECK(b[0] == marks[idx]);     // 整块内容没有被踩
                CHECK(b[n - 1] == marks[idx]);
                MemoryPool::deallocate(live[idx], n);
                live[idx] = live.back();
                sizes[idx] = sizes.back();
                marks[idx] = marks.back();
                live.pop_back();
                sizes.pop_back();
                marks.pop_back();
            } else {
                std::size_t n = 1 + (state >> 16) % 200; // 1..200 字节，两种路径都覆盖
                void* p = MemoryPool::allocate(n);
                CHECK(p != nullptr);
                CHECK(aligned8(p));
                unsigned char mark = static_cast<unsigned char>((state >> 24) & 0xFF);
                std::memset(p, mark, n);
                live.push_back(p);
                sizes.push_back(n);
                marks.push_back(mark);
                ++alloc_count;
            }
        }
        CHECK(alloc_count > 0);
        CHECK(live.size() <= 512u);
        for (std::size_t i = 0; i < live.size(); ++i) {
            MemoryPool::deallocate(live[i], sizes[i]);
        }
    }

    TEST_SECTION("release_all：显式归还全部 chunk，池可继续使用");
    {
        void* p = MemoryPool::allocate(32);
        MemoryPool::deallocate(p, 32);
        MemoryPool::release_all(); // 归还全部 chunk（Valgrind 靠此路径清零）

        void* q = MemoryPool::allocate(48); // 归还后池照常工作
        CHECK(q != nullptr);
        CHECK(aligned8(q));
        MemoryPool::deallocate(q, 48);

        MemoryPool::release_all(); // 收尾清零
    }

    TEST_MAIN_END();
}
