// test_smart_ptr.cpp — mystl::unique_ptr / shared_ptr / weak_ptr 的断言测试
#include <utility>

#include "mystl/shared_ptr.h"
#include "mystl/unique_ptr.h"
#include "test_util.h"

namespace {

// 生命周期计数：验证「恰好析构一次」
struct Tracker {
    static int created;
    static int destroyed;

    int v;
    explicit Tracker(int x = 0) : v(x) { ++created; }
    ~Tracker() { ++destroyed; }

    static void reset() {
        created = destroyed = 0;
    }
    static int alive() {
        return created - destroyed;
    }
};
int Tracker::created = 0;
int Tracker::destroyed = 0;

// 计数删除器（仿函数）：验证自定义删除器被「恰好调用一次」
struct CountDeleter {
    int* calls;
    explicit CountDeleter(int* c) : calls(c) {}
    void operator()(int* p) const {
        ++*calls;
        delete p;
    }
};

// 多态：验证「基类智能指针删除派生对象」时虚析构链完整
struct Base {
    virtual ~Base() { ++base_dtors; }
    virtual int id() const { return 1; }
    static int base_dtors;
};
int Base::base_dtors = 0;

struct Derived : Base {
    explicit Derived(int x) : payload(x) {}
    ~Derived() override { ++derived_dtors; }
    int id() const override { return 2; }
    int payload;
    static int derived_dtors;
};
int Derived::derived_dtors = 0;

// enable_shared_from_this：让被接管的对象能安全拿到指向自己的 shared_ptr
struct Widget : mystl::enable_shared_from_this<Widget> {
    int v;
    explicit Widget(int x = 0) : v(x) {}
};

// 双向结点：演示「强引用成环」与「weak 反向指针打破环」
struct Node {
    int v;
    mystl::shared_ptr<Node> next;
    mystl::weak_ptr<Node> back;
    explicit Node(int x) : v(x) {}
};

} // namespace

int main() {
    TEST_SECTION("unique_ptr 基本语义：独占、观察、重置");
    {
        mystl::unique_ptr<int> u; // 默认空
        CHECK(u.get() == nullptr);
        CHECK(!u);

        mystl::unique_ptr<int> p(new int(42));
        CHECK(p.get() != nullptr);
        CHECK(static_cast<bool>(p));
        CHECK_EQ(*p, 42);
        *p = 43;
        CHECK_EQ(*p, 43);

        int* raw = p.release(); // 放弃所有权：不再自动删除
        CHECK(p.get() == nullptr);
        CHECK_EQ(*raw, 43);
        delete raw;

        p.reset(new int(7)); // 重新接管
        CHECK_EQ(*p, 7);
        p.reset(); // 显式置空
        CHECK(p.get() == nullptr);
    }

    TEST_SECTION("unique_ptr 自定义删除器：恰好调用一次");
    {
        int calls = 0;
        {
            mystl::unique_ptr<int, CountDeleter> u(new int(1), CountDeleter(&calls));
            CHECK_EQ(calls, 0);
            u.reset(new int(2)); // 重置：旧对象经删除器释放
            CHECK_EQ(calls, 1);
            *u = 20;
            CHECK_EQ(u.get_deleter().calls, &calls); // 删除器可访问
        }
        CHECK_EQ(calls, 2); // 作用域结束时释放第二个对象

        int calls2 = 0;
        {
            mystl::unique_ptr<int, CountDeleter> v(new int(3), CountDeleter(&calls2));
            int* raw = v.release(); // release 不调用删除器
            CHECK_EQ(calls2, 0);
            delete raw; // 调用方自己接管
        }
        CHECK_EQ(calls2, 0);
    }

    TEST_SECTION("unique_ptr：派生类 → 基类（虚析构 + 指针转换）");
    {
        Base::base_dtors = 0;
        Derived::derived_dtors = 0;
        {
            mystl::unique_ptr<Derived> d(new Derived(7));
            CHECK_EQ(d->payload, 7);
            mystl::unique_ptr<Base> b(std::move(d)); // 转换移动构造
            CHECK(d.get() == nullptr);               // 所有权已转移
            CHECK(b.get() != nullptr);
            CHECK_EQ(b->id(), 2); // 虚调用
            CHECK(dynamic_cast<Derived*>(b.get()) != nullptr);
        }
        CHECK_EQ(Derived::derived_dtors, 1); // 虚析构链完整触发
        CHECK_EQ(Base::base_dtors, 1);
    }

    TEST_SECTION("unique_ptr 移动语义与别名自我移动");
    {
        mystl::unique_ptr<int> a(new int(5));
        mystl::unique_ptr<int> b(std::move(a));
        CHECK(a.get() == nullptr);
        CHECK_EQ(*b, 5);

        mystl::unique_ptr<int> c;
        c = std::move(b); // 移动赋值
        CHECK(b.get() == nullptr);
        CHECK_EQ(*c, 5);

        mystl::unique_ptr<int>& r = c;
        c = std::move(r); // 经别名自我移动：应为无操作
        CHECK(c.get() != nullptr);
        CHECK_EQ(*c, 5);

        a = std::move(c); // moved-from 可复用
        CHECK_EQ(*a, 5);
    }

    TEST_SECTION("shared_ptr：引用计数与对象生命周期");
    {
        Tracker::reset();
        {
            mystl::shared_ptr<Tracker> sp(new Tracker(1));
            CHECK_EQ(sp.use_count(), 1L);
            CHECK(sp.unique());
            CHECK(static_cast<bool>(sp));
            CHECK_EQ(sp->v, 1);
            CHECK_EQ((*sp).v, 1);

            mystl::shared_ptr<Tracker> sp2(sp); // 拷贝：+1
            CHECK_EQ(sp.use_count(), 2L);
            CHECK(!sp2.unique());
            CHECK_EQ(Tracker::alive(), 1);

            sp.reset(); // 一个强引用离开
            CHECK(sp.get() == nullptr);
            CHECK_EQ(sp2.use_count(), 1L);
            CHECK_EQ(Tracker::alive(), 1); // 对象还活着

            sp = sp2; // 重新共享（copy-and-swap）
            CHECK_EQ(sp.use_count(), 2L);
        }
        CHECK_EQ(Tracker::created, 1);
        CHECK_EQ(Tracker::destroyed, 1); // 恰好析构一次
        CHECK_EQ(Tracker::alive(), 0);
    }

    TEST_SECTION("shared_ptr：make_shared 与自定义删除器");
    {
        Tracker::reset();
        {
            mystl::shared_ptr<Tracker> sp = mystl::make_shared<Tracker>(9);
            CHECK_EQ(sp.use_count(), 1L);
            CHECK_EQ(sp->v, 9);
            mystl::shared_ptr<Tracker> sp2 = sp;
            CHECK_EQ(sp2.use_count(), 2L);
        }
        CHECK_EQ(Tracker::destroyed, 1);

        int calls = 0;
        {
            mystl::shared_ptr<int> sp(new int(3), CountDeleter(&calls));
            CHECK_EQ(*sp, 3);
            CHECK_EQ(calls, 0);
        }
        CHECK_EQ(calls, 1); // 删除器恰好一次
    }

    TEST_SECTION("shared_ptr：多态、Reset 与移动");
    {
        Base::base_dtors = 0;
        Derived::derived_dtors = 0;
        {
            mystl::shared_ptr<Derived> sd(new Derived(11));
            mystl::shared_ptr<Base> sb(sd); // 转换拷贝构造
            CHECK_EQ(sb.use_count(), 2L);
            CHECK_EQ(sb->id(), 2);
            CHECK(sb.get() == sd.get()); // 指向同一对象

            mystl::shared_ptr<Base> sm(std::move(sb)); // 移动构造
            CHECK(sb.get() == nullptr);
            CHECK_EQ(sm.use_count(), 2L); // sd + sm

            sm.reset(); // 显式重置
            CHECK(sm.get() == nullptr);
        }
        CHECK_EQ(Derived::derived_dtors, 1);
        CHECK_EQ(Base::base_dtors, 1);
    }

    TEST_SECTION("shared_ptr：拷贝/移动赋值与自我赋值");
    {
        Tracker::reset();
        mystl::shared_ptr<Tracker> a = mystl::make_shared<Tracker>(1);
        mystl::shared_ptr<Tracker> b = mystl::make_shared<Tracker>(2);

        b = a; // 拷贝赋值：原 b 的对象被释放
        CHECK_EQ(Tracker::destroyed, 1);
        CHECK_EQ(a.use_count(), 2L);

        mystl::shared_ptr<Tracker>& ra = a;
        a = ra; // 经别名自我拷贝赋值：无操作
        CHECK_EQ(a.use_count(), 2L);
        CHECK_EQ(a->v, 1);

        mystl::shared_ptr<Tracker>& rb = b;
        b = std::move(rb); // 经别名自我移动：无操作
        CHECK_EQ(b->v, 1);
        CHECK_EQ(a.use_count(), 2L);

        mystl::shared_ptr<Tracker> c;
        c = std::move(b); // 移动赋值：源置空，计数不变
        CHECK(b.get() == nullptr);
        CHECK_EQ(c->v, 1);
        CHECK_EQ(a.use_count(), 2L);

        a = std::move(c); // a、c 共享同一对象：净计数 -1，对象保活
        CHECK_EQ(a->v, 1);
        CHECK_EQ(a.use_count(), 1L);
    }

    TEST_SECTION("weak_ptr：观察、提升与过期");
    {
        Tracker::reset();
        mystl::weak_ptr<Tracker> w; // 默认空弱引用
        CHECK(w.expired());
        CHECK_EQ(w.use_count(), 0L);
        CHECK(!w.lock());

        {
            mystl::shared_ptr<Tracker> sp = mystl::make_shared<Tracker>(5);
            w = sp; // 由 shared_ptr 赋值：只加弱计数
            CHECK(!w.expired());
            CHECK_EQ(w.use_count(), 1L);

            mystl::shared_ptr<Tracker> locked = w.lock(); // 提升成功：强计数 +1
            CHECK(static_cast<bool>(locked));
            CHECK_EQ(locked->v, 5);
            CHECK_EQ(sp.use_count(), 2L);

            locked.reset();
            CHECK_EQ(sp.use_count(), 1L);
            CHECK_EQ(Tracker::alive(), 1);
        }
        // 强引用全部消失：对象已销毁，弱引用仍可安全观察
        CHECK(w.expired());
        CHECK_EQ(w.use_count(), 0L);
        CHECK(!w.lock());
        CHECK_EQ(Tracker::destroyed, 1);

        w.reset(); // 释放最后的弱引用（控制块随之释放，交由 Valgrind 验证无泄漏）
        CHECK(w.expired());
    }

    TEST_SECTION("enable_shared_from_this：两条接管路径 + 未接管");
    {
        // 路径一：裸指针构造
        {
            mystl::shared_ptr<Widget> sp(new Widget(10));
            mystl::shared_ptr<Widget> self = sp->shared_from_this();
            CHECK(self.get() == sp.get());
            CHECK_EQ(sp.use_count(), 2L);

            mystl::weak_ptr<Widget> ww = sp->weak_from_this();
            CHECK(!ww.expired());
        }

        // 路径二：make_shared
        {
            mystl::shared_ptr<Widget> sp = mystl::make_shared<Widget>(20);
            mystl::shared_ptr<Widget> self = sp->shared_from_this();
            CHECK(self.get() == sp.get());
            CHECK_EQ(self->v, 20);
            CHECK_EQ(sp.use_count(), 2L);
        }

        // 未被 shared_ptr 接管的对象：返回空，而不是悬垂指针
        {
            Widget w(30);
            CHECK(w.shared_from_this().get() == nullptr);
            CHECK(w.weak_from_this().expired());
        }
    }

    TEST_SECTION("循环引用：强环与 weak 打破");
    {
        // 强引用成环：两个计数互相保持，不会自动释放（必须手动断环）
        {
            mystl::shared_ptr<Node> a = mystl::make_shared<Node>(1);
            mystl::shared_ptr<Node> b = mystl::make_shared<Node>(2);
            a->next = b;
            b->next = a; // 强环：a→b→a
            CHECK_EQ(a.use_count(), 2L); // 变量 a + b->next
            CHECK_EQ(b.use_count(), 2L); // 变量 b + a->next

            a->next.reset(); // 手动断环（不断则两块内存都会泄漏，
            b->next.reset(); //  泄漏与否由 Valgrind 在此测试里把关）
        }

        // weak 反向指针：能访问前驱，但不延长其生命周期
        {
            mystl::shared_ptr<Node> a = mystl::make_shared<Node>(10);
            mystl::shared_ptr<Node> b = mystl::make_shared<Node>(20);
            a->next = b; // 强：a 持有 b
            b->back = a; // 弱：b 观察 a（不增强计数）
            CHECK_EQ(b.use_count(), 2L);
            CHECK_EQ(a.use_count(), 1L); // weak 不增加强计数
            CHECK(!b->back.expired());

            a.reset(); // 释放对 b 的强引用链
            CHECK_EQ(b.use_count(), 1L);
            CHECK(b->back.expired()); // 前驱已随之销毁
        }
    }

    TEST_MAIN_END();
}
