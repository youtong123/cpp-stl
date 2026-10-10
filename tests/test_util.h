#pragma once

// 极简断言测试框架：printf 报错 + 计数汇总。
// 每个测试是可执行文件（独立 TU），所以用 static 计数即可，无需多 TU 共享。
#include <cstdio>
#include <iostream>

static int g_test_failures = 0;
static int g_test_checks = 0;
static const char* g_current_test = "(top level)";

// 标记当前测试段（失败信息里会带上段名）
#define TEST_SECTION(name)              \
    do {                                \
        g_current_test = (name);        \
        std::printf("== %s\n", (name)); \
    } while (0)

#define CHECK(cond)                                                        \
    do {                                                                   \
        ++g_test_checks;                                                   \
        if (!(cond)) {                                                     \
            ++g_test_failures;                                             \
            std::printf("FAIL %s:%d [%s] CHECK(%s)\n", __FILE__, __LINE__, \
                        g_current_test, #cond);                            \
        }                                                                  \
    } while (0)

#define CHECK_EQ(a, b)                                                  \
    do {                                                                \
        ++g_test_checks;                                                \
        auto va_ = (a);                                                 \
        auto vb_ = (b);                                                 \
        if (!(va_ == vb_)) {                                            \
            ++g_test_failures;                                          \
            std::printf("FAIL %s:%d [%s] CHECK_EQ(%s, %s)\n", __FILE__, \
                        __LINE__, g_current_test, #a, #b);              \
            std::cout << "    lhs = " << va_ << "\n    rhs = " << vb_   \
                      << std::endl;                                     \
        }                                                               \
    } while (0)

#define CHECK_THROWS(expr, extype)                                          \
    do {                                                                    \
        ++g_test_checks;                                                    \
        bool caught_ = false;                                               \
        try {                                                               \
            (void)(expr);                                                   \
        } catch (const extype&) {                                           \
            caught_ = true;                                                 \
        } catch (...) {                                                     \
        }                                                                   \
        if (!caught_) {                                                     \
            ++g_test_failures;                                              \
            std::printf("FAIL %s:%d [%s] CHECK_THROWS(%s, %s)\n", __FILE__, \
                        __LINE__, g_current_test, #expr, #extype);          \
        }                                                                   \
    } while (0)

// 每个测试 main 末尾调用：打印汇总，返回 0/1
#define TEST_MAIN_END()                                                   \
    do {                                                                  \
        if (g_test_failures == 0) {                                       \
            std::printf("ALL TESTS PASSED (%d checks)\n", g_test_checks); \
            return 0;                                                     \
        }                                                                 \
        std::printf("TESTS FAILED: %d of %d checks failed\n",             \
                    g_test_failures, g_test_checks);                      \
        return 1;                                                         \
    } while (0)
