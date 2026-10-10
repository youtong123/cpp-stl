// bench_vector.cpp — mystl::vector vs std::vector 的 push_back 基准
// 同一台机器、同一编译器、同一优化等级（-O2），取 3 轮最佳值。
// 输出可直接抄进 README（数据必须实测，不允许编造）。
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <vector>

#include "mystl/vector.h"

namespace {

const std::size_t kCount = 10000000; // 1000 万个 int
const int kRounds = 3;

volatile long long g_sink = 0; // 防优化：把结果累进 volatile

using Clock = std::chrono::steady_clock;

template <typename Vec>
long long fill_push_back(std::size_t n) {
    Vec v;
    for (std::size_t i = 0; i < n; ++i) {
        v.push_back(static_cast<int>(i));
    }
    long long s = 0;
    for (std::size_t i = 0; i < n; i += 64) {
        s += v[i];
    }
    return s;
}

template <typename Vec>
long long fill_push_back_reserved(std::size_t n) {
    Vec v;
    v.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        v.push_back(static_cast<int>(i));
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
    std::printf("%-24s %9.1f ms\n", name, ms);
}

} // namespace

int main() {
    std::printf("== vector push_back %zu 个 int（best of %d）\n", kCount, kRounds);

    double m1 = best_of_ms(kRounds, []() { g_sink += fill_push_back<mystl::vector<int>>(kCount); });
    double m2 = best_of_ms(kRounds, []() { g_sink += fill_push_back<std::vector<int>>(kCount); });
    double m3 = best_of_ms(kRounds, []() { g_sink += fill_push_back_reserved<mystl::vector<int>>(kCount); });
    double m4 = best_of_ms(kRounds, []() { g_sink += fill_push_back_reserved<std::vector<int>>(kCount); });

    report("mystl  无 reserve", m1);
    report("std    无 reserve", m2);
    report("mystl  先 reserve", m3);
    report("std    先 reserve", m4);

    std::printf("checksum=%lld\n", static_cast<long long>(g_sink));
    return 0;
}
