// test_string.cpp — mystl::string 的断言测试
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#include "mystl/string.h"
#include "test_util.h"

using mystl::string;

int main() {
    TEST_SECTION("构造与基本访问");
    {
        string s;
        CHECK(s.empty());
        CHECK_EQ(s.size(), 0u);
        CHECK(s.c_str()[0] == '\0');

        string a("hello");
        CHECK_EQ(a.size(), 5u);
        CHECK_EQ(a.length(), 5u);
        CHECK(std::strcmp(a.c_str(), "hello") == 0);
        CHECK_EQ(a[1], 'e');
        CHECK_EQ(a.at(4), 'o');

        string b(static_cast<const char*>(nullptr));
        CHECK(b.empty());

        string c(a);
        CHECK(c == a);
    }

    TEST_SECTION("追加 / += / 自追加");
    {
        string s("foo");
        s.append("bar");
        CHECK(std::strcmp(s.c_str(), "foobar") == 0);
        s.append(string("!"));
        CHECK(std::strcmp(s.c_str(), "foobar!") == 0);
        s += string("?");
        CHECK(std::strcmp(s.c_str(), "foobar!?") == 0);
        s += 'x';
        CHECK(std::strcmp(s.c_str(), "foobar!?x") == 0);
        s += "end";
        CHECK(std::strcmp(s.c_str(), "foobar!?xend") == 0);

        string t("ab");
        t.append(t); // 自追加（string 版本）
        CHECK(std::strcmp(t.c_str(), "abab") == 0);
        t += t; // 自 +=：4 → 8
        CHECK(std::strcmp(t.c_str(), "abababab") == 0);

        string u("XYZ");
        u.push_back('!');
        CHECK(std::strcmp(u.c_str(), "XYZ!") == 0);
    }

    TEST_SECTION("别名安全：append(c_str()) 及其内部指针");
    {
        string s("0123456789");
        s.append(s.c_str() + 10); // 指向结尾 '\0'，长度 0 → 不变
        CHECK(std::strcmp(s.c_str(), "0123456789") == 0);
        s.append(s.c_str()); // 整串自追加，触发扩容重分配 →  длина 20
        CHECK(std::strcmp(s.c_str(), "01234567890123456789") == 0);
        CHECK_EQ(s.size(), 20u);

        string t("abcdef");
        t.append(t.c_str() + 1); // "bcdef" 从自身缓冲内部追加
        CHECK(std::strcmp(t.c_str(), "abcdefbcdef") == 0);
    }

    TEST_SECTION("operator+ 三种组合");
    {
        string a("foo");
        string b("bar");
        CHECK((a + b) == "foobar");
        CHECK((a + "baz") == "foobaz");
        CHECK(("baz" + a) == "bazfoo");
    }

    TEST_SECTION("比较运算");
    {
        string a("abc");
        string b("abd");
        string c("abc");
        CHECK(a == c);
        CHECK(a != b);
        CHECK(a < b);
        CHECK(b > a);
        CHECK(a <= c);
        CHECK(a >= c);
        CHECK(a == "abc");
        CHECK("abc" == a);
        CHECK(a != "abd");
        CHECK("abd" != a);
        CHECK_EQ(a.compare(c), 0);
        CHECK(a.compare(b) < 0);
        CHECK(b.compare(a) > 0);
        CHECK_EQ(a.compare("abc"), 0);
        string p("ab");
        string q("abc");
        CHECK(p < q); // 前缀较短者更小
    }

    TEST_SECTION("find 家族");
    {
        string s("hello world, hello mystl"); // 长度 24
        CHECK_EQ(s.find("hello"), 0u);
        CHECK_EQ(s.find("hello", 1), 13u);
        CHECK_EQ(s.find("mystl"), 19u);
        CHECK_EQ(s.find("nope"), string::npos);
        CHECK_EQ(s.find('o'), 4u);
        CHECK_EQ(s.find('o', 5), 7u);
        CHECK_EQ(s.find('z'), string::npos);
        CHECK_EQ(s.find(""), 0u);
        CHECK_EQ(s.find("", 100), string::npos);
    }

    TEST_SECTION("at 越界 / resize / reserve 只增不减");
    {
        string s("abc");
        CHECK_THROWS(s.at(3), std::out_of_range);
        CHECK_THROWS(s.at(100), std::out_of_range);
        s.resize(5, 'x');
        CHECK(std::strcmp(s.c_str(), "abcxx") == 0);
        s.resize(2);
        CHECK(std::strcmp(s.c_str(), "ab") == 0);

        string t("ab");
        t.reserve(100);
        CHECK(t.capacity() >= 100u);
        std::size_t cap = t.capacity();
        t.reserve(10); // 小于当前容量：无动作
        CHECK_EQ(t.capacity(), cap);
    }

    TEST_SECTION("深拷贝 / 自赋值 / swap");
    {
        string a("hello");
        string b(a);
        b[0] = 'H';
        CHECK(std::strcmp(a.c_str(), "hello") == 0);
        CHECK(std::strcmp(b.c_str(), "Hello") == 0);

        string& ra = a;
        a = ra; // 自赋值（经引用别名）
        CHECK(std::strcmp(a.c_str(), "hello") == 0);

        a = "world"; // operator=(const char*)
        CHECK(a == "world");

        string c("alias");
        c = c.c_str(); // 赋自身缓冲内容：先做临时再 swap，别名安全
        CHECK(c == "alias");

        string x("aaa");
        string y("bb");
        swap(x, y);
        CHECK(x == "bb");
        CHECK(y == "aaa");
        x.swap(y);
        CHECK(x == "aaa");
        CHECK(y == "bb");
    }

    TEST_SECTION("moved-from 状态与移动语义");
    {
        string a("move me");
        string b(std::move(a));
        CHECK(b == "move me");
        CHECK(a.empty());
        CHECK(a.c_str() != nullptr); // 永不为空指针
        CHECK(std::strcmp(a.c_str(), "") == 0);
        a = "reuse"; // moved-from 可复用
        CHECK(a == "reuse");

        string c;
        c = std::move(b); // 移动赋值
        CHECK(c == "move me");
        CHECK(b.empty());
    }

    TEST_SECTION("长字符串（扩容路径）与 ostream 输出");
    {
        string s;
        for (int i = 0; i < 10000; ++i) {
            s.push_back(static_cast<char>('a' + i % 26));
        }
        CHECK_EQ(s.size(), 10000u);
        CHECK(s.capacity() >= 10000u);
        CHECK_EQ(s[0], 'a');
        CHECK_EQ(s[9999], 'p'); // 9999 % 26 == 15 → 'a' + 15

        std::ostringstream os;
        os << s;
        CHECK_EQ(os.str().size(), 10000u);
        CHECK(os.str() == s.c_str());
    }

    TEST_MAIN_END();
}
