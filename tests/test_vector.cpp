// test_vector.cpp — mystl::vector 的断言测试
#include <stdexcept>
#include <utility>

#include "mystl/memory_pool.h" // pool_allocator
#include "mystl/vector.h"
#include "test_util.h"

namespace {

// 构造/拷贝/移动计数类型：验证 emplace_back 就地构造、扩容搬运策略
struct Counted {
    static int ctors;
    static int copies;
    static int moves;

    int v;
    explicit Counted(int x = 0) : v(x) { ++ctors; }
    Counted(const Counted& o) : v(o.v) { ++copies; }
    Counted(Counted&& o) noexcept : v(o.v) { ++moves; }

    static void reset() {
        ctors = copies = moves = 0;
    }
};
int Counted::ctors = 0;
int Counted::copies = 0;
int Counted::moves = 0;

// 移动构造声明为 noexcept：扩容时应当走「移动」路径
struct NothrowMoveType {
    static int copies;
    static int moves;

    int v;
    explicit NothrowMoveType(int x = 0) : v(x) {}
    NothrowMoveType(const NothrowMoveType& o) : v(o.v) { ++copies; }
    NothrowMoveType(NothrowMoveType&& o) noexcept : v(o.v) { ++moves; }

    static void reset() {
        copies = moves = 0;
    }
};
int NothrowMoveType::copies = 0;
int NothrowMoveType::moves = 0;

// 移动构造可能抛异常（无 noexcept）且可拷贝：扩容时应退回复制
struct ThrowMoveType {
    static int copies;
    static int moves;

    int v;
    explicit ThrowMoveType(int x = 0) : v(x) {}
    ThrowMoveType(const ThrowMoveType& o) : v(o.v) { ++copies; }
    ThrowMoveType(ThrowMoveType&& o) : v(o.v) { ++moves; } // 未标 noexcept

    static void reset() {
        copies = moves = 0;
    }
};
int ThrowMoveType::copies = 0;
int ThrowMoveType::moves = 0;

} // namespace

int main() {
    TEST_SECTION("容量序列：1 → 2 → 4 → 4");
    {
        mystl::vector<int> v;
        CHECK_EQ(v.capacity(), 0u);
        v.push_back(1);
        CHECK_EQ(v.capacity(), 1u);
        v.push_back(2);
        CHECK_EQ(v.capacity(), 2u);
        v.push_back(3);
        CHECK_EQ(v.capacity(), 4u);
        v.push_back(4);
        CHECK_EQ(v.capacity(), 4u);
        CHECK_EQ(v.size(), 4u);
        CHECK_EQ(v.front(), 1);
        CHECK_EQ(v.back(), 4);
        CHECK_EQ(v[2], 3);
        v.pop_back();
        CHECK_EQ(v.size(), 3u);
        CHECK_EQ(v.back(), 3);
        CHECK_EQ(v.capacity(), 4u); // pop_back 不缩容
    }

    TEST_SECTION("reserve：只增不减，预分配后 emplace 无搬家");
    {
        Counted::reset();
        mystl::vector<Counted> v;
        v.reserve(16);
        CHECK_EQ(v.capacity(), 16u);
        for (int i = 0; i < 16; ++i) {
            v.emplace_back(i);
        }
        CHECK_EQ(Counted::ctors, 16); // 全部就地构造
        CHECK_EQ(Counted::copies, 0);
        CHECK_EQ(Counted::moves, 0);
        v.reserve(10); // 小于当前容量：无动作
        CHECK_EQ(v.capacity(), 16u);
        CHECK_EQ(v[15].v, 15);
    }

    TEST_SECTION("move_if_noexcept：noexcept 移动的类型扩容走移动");
    {
        NothrowMoveType::reset();
        mystl::vector<NothrowMoveType> v;
        for (int i = 0; i < 5; ++i) {
            v.push_back(NothrowMoveType(i)); // 插入本身走移动：5 次
        }
        CHECK_EQ(NothrowMoveType::copies, 0);
        CHECK_EQ(NothrowMoveType::moves, 5 + 7); // 5 次插入 + 扩容搬运 1+2+4
        CHECK_EQ(v[4].v, 4);
    }

    TEST_SECTION("move_if_noexcept：移动可能抛的类型扩容退回复制");
    {
        ThrowMoveType::reset();
        mystl::vector<ThrowMoveType> v;
        for (int i = 0; i < 5; ++i) {
            v.push_back(ThrowMoveType(i)); // 插入走移动：5 次
        }
        CHECK_EQ(ThrowMoveType::moves, 5);
        CHECK_EQ(ThrowMoveType::copies, 1 + 2 + 4); // 扩容搬运全部是拷贝
        CHECK_EQ(v[4].v, 4);
    }

    TEST_SECTION("深拷贝与独立性");
    {
        mystl::vector<int> a{1, 2, 3};
        mystl::vector<int> b(a);
        CHECK(b.data() != a.data());
        b[0] = 99;
        CHECK_EQ(a[0], 1);
        CHECK_EQ(b[0], 99);
        mystl::vector<int> c{9, 9};
        c = a;
        CHECK_EQ(c.size(), 3u);
        CHECK_EQ(c[0], 1);
        c[0] = 5;
        CHECK_EQ(a[0], 1); // 拷贝赋值后修改互不影响
    }

    TEST_SECTION("自赋值与隐藏自我移动");
    {
        mystl::vector<int> v{1, 2, 3};
        mystl::vector<int>& r = v;
        v = r; // 自拷贝赋值
        CHECK_EQ(v.size(), 3u);
        CHECK_EQ(v[0], 1);
        v = std::move(r); // 隐藏自我移动（经引用别名，规避 -Wself-move）
        CHECK_EQ(v.size(), 3u);
        CHECK_EQ(v[2], 3);
    }

    TEST_SECTION("at 越界抛异常 / resize / clear");
    {
        mystl::vector<int> v{1, 2, 3};
        CHECK_THROWS(v.at(3), std::out_of_range);
        CHECK_THROWS(v.at(100), std::out_of_range);
        CHECK_EQ(v.at(2), 3);
        v.resize(5, 7);
        CHECK_EQ(v.size(), 5u);
        CHECK_EQ(v[3], 7);
        CHECK_EQ(v[4], 7);
        v.resize(2);
        CHECK_EQ(v.size(), 2u);
        v.clear();
        CHECK(v.empty());
        CHECK_EQ(v.size(), 0u);
        v.push_back(11); // clear 后可复用
        CHECK_EQ(v[0], 11);
        CHECK_EQ(v.size(), 1u);
    }

    TEST_SECTION("移动构造：窃取缓冲区，源对象可复用");
    {
        mystl::vector<int> a{1, 2, 3, 4};
        int* p = a.data();
        mystl::vector<int> b(std::move(a));
        CHECK_EQ(b.data(), p); // 指针窃取（O(1)）
        CHECK_EQ(b.size(), 4u);
        CHECK(a.empty());
        CHECK(a.data() == nullptr);
        a.push_back(5); // moved-from 状态可继续使用
        CHECK_EQ(a[0], 5);
    }

    TEST_SECTION("pool_allocator 集成");
    {
        mystl::vector<int, mystl::pool_allocator<int>> v;
        for (int i = 0; i < 100; ++i) {
            v.push_back(i);
        }
        CHECK_EQ(v.size(), 100u);
        long long sum = 0;
        for (auto x : v) {
            sum += x;
        }
        CHECK_EQ(sum, 4950LL);
        v.resize(150, -1); // 跨过 128 字节阈值，走池的大块路径
        CHECK_EQ(v.size(), 150u);
        CHECK_EQ(v[149], -1);
    }

    TEST_SECTION("迭代器（指针式）与比较运算");
    {
        mystl::vector<int> v{3, 1, 2};
        int sum = 0;
        for (int* it = v.begin(); it != v.end(); ++it) {
            sum += *it;
        }
        CHECK_EQ(sum, 6);

        mystl::vector<int> w{3, 1, 2};
        CHECK(v == w);
        w.push_back(9);
        CHECK(v != w);
        mystl::vector<int> z{3, 2, 2};
        CHECK(v != z); // 同 size、内容不同
    }

    // 池分配器用毕：显式归还全部 chunk（池本身是"只借不还"设计，
    // 这里是测试收尾路径，让 Valgrind 能确认零残留）
    mystl::MemoryPool::release_all();

    TEST_MAIN_END();
}
