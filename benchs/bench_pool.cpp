// bench_pool.cpp — MemoryPool vs ::operator new/delete 的小块分配基准
// 公平性设计：同一 LCG 序列、同一读写字节数，两个实现做完全相同的动作。
// 输出可直接抄进 README（数据必须实测，不允许编造）。
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "mystl/memory_pool.h"
#include "mystl/vector.h"

namespace {

using Clock = std::chrono::steady_clock;

const int kRounds = 3;
const std::size_t kOps = 1000000; // 随机分配/释放操作数
const int kSlots = 64;            // 同时存活的块数上限

volatile long long g_sink = 0; // 防优化

// 池版操作
struct PoolOps {
    static void* alloc(std::size_t n) {
        return mystl::MemoryPool::allocate(n);
    }
    static void dealloc(void* p, std::size_t n) {
        mystl::MemoryPool::deallocate(p, n);
    }
};

// 系统版操作
struct NewOps {
    static void* alloc(std::size_t n) {
        return ::operator new(n);
    }
    static void dealloc(void* p, std::size_t) {
        ::operator delete(p);
    }
};

// 相同的确定性伪随机序列下，执行 kOps 次「分配或释放」。
// 每块都写入首尾字节并在释放时读回，保证内存真的被用过。
template <typename Ops>
long long churn(std::uint32_t seed) {
    std::uint32_t state = seed;
    void* slots[kSlots];
    std::size_t sizes[kSlots];
    for (int i = 0; i < kSlots; ++i) {
        slots[i] = nullptr;
        sizes[i] = 0;
    }
    long long sink = 0;
    for (std::size_t i = 0; i < kOps; ++i) {
        state = state * 1664525u + 1013904223u;
        int slot = static_cast<int>((state >> 16) % static_cast<std::uint32_t>(kSlots));
        if (slots[slot] != nullptr) {
            unsigned char* b = static_cast<unsigned char*>(slots[slot]);
            sink += b[sizes[slot] - 1]; // 读回尾字节
            Ops::dealloc(slots[slot], sizes[slot]);
            slots[slot] = nullptr;
        } else {
            std::size_t n = 1 + (state >> 8) % 200; // 1..200 字节
            unsigned char* b = static_cast<unsigned char*>(Ops::alloc(n));
            b[0] = static_cast<unsigned char>(state);
            b[n - 1] = static_cast<unsigned char>(state >> 8);
            sink += b[0];
            slots[slot] = b;
            sizes[slot] = n;
        }
    }
    for (int i = 0; i < kSlots; ++i) {
        if (slots[i] != nullptr) {
            Ops::dealloc(slots[i], sizes[i]);
        }
    }
    return sink;
}

// vector 增长：同一 push_back 序列，比较池分配器与默认分配器
template <typename Vec>
long long grow(std::size_t n) {
    Vec v;
    for (std::size_t i = 0; i < n; ++i) {
        v.push_back(static_cast<int>(i % 1000));
    }
    long long s = 0;
    for (std::size_t i = 0; i < n; i += 64) {
        s += v[i];
    }
    return s;
}

template <typename F>
double best_of_ms(int rounds, F&& f) {
    double best = 1e300;
    for (int r = 0; r < rounds; ++r) {
        Clock::time_point t0 = Clock::now();
        f();
        Clock::time_point t1 = Clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        if (ms < best) {
            best = ms;
        }
    }
    return best;
}

void report(const char* name, double ms) {
    std::printf("%-28s %9.1f ms\n", name, ms);
}

} // namespace

int main() {
    std::printf("== %zu 次随机小块分配/释放（1..200B，%d 块存活上限，best of %d）\n",
                kOps, kSlots, kRounds);
    double a = best_of_ms(kRounds, []() { g_sink += churn<PoolOps>(12345u); });
    double b = best_of_ms(kRounds, []() { g_sink += churn<NewOps>(12345u); });
    report("MemoryPool", a);
    report("operator new/delete", b);

    const std::size_t kN = 1000000;
    std::printf("== vector 增长 %zu 个 int\n", kN);
    double c = best_of_ms(kRounds, []() {
        g_sink += grow<mystl::vector<int, mystl::pool_allocator<int>>>(kN);
    });
    double d = best_of_ms(kRounds, []() { g_sink += grow<std::vector<int>>(kN); });
    report("mystl pool_allocator", c);
    report("std allocator", d);

    std::printf("checksum=%lld\n", static_cast<long long>(g_sink));
    return 0;
}
