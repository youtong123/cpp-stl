#pragma once

// 自实现 string 类（封装字符缓冲区）
//
// 要点：
//   - 缓冲区始终保证「size_ 个字符 + 一个 '\0' 结尾」，c_str() 可直接给 C 接口用；
//   - 容量按倍增策略增长（16 起步、翻倍、不够再按需）；
//   - 深拷贝 + 移动语义：拷贝是 O(n) 全量复制（深拷贝的性能问题），
//     移动构造/移动赋值是 O(1) 指针窃取（移动语义带来的优化）；
//   - 拷贝赋值用 copy-and-swap，自赋值安全；移动赋值 noexcept；
//   - 查找用朴素匹配（学习用途；工程上应是 KMP / BM 等）。
#include <cstddef>
#include <iosfwd>

namespace mystl {

class string {
public:
    typedef char value_type;
    typedef char* iterator;
    typedef const char* const_iterator;
    typedef std::size_t size_type;

    // 表示"未找到"的哨兵值（与 std::string::npos 语义一致）
    static const size_type npos = static_cast<size_type>(-1);

    // ---------------- 构造 / 析构 / 赋值 ----------------
    string();
    string(const char* s); // NOLINT: 有意支持隐式转换
    string(const string& other);
    string(string&& other) noexcept;
    ~string();

    string& operator=(const string& other);          // copy-and-swap
    string& operator=(string&& other) noexcept;      // 窃取缓冲区
    string& operator=(const char* s);                // 别名（s 指向自身缓冲）也安全

    // ---------------- 容量与访问 ----------------
    const char* c_str() const noexcept; // moved-from 状态返回 ""（永不为空指针）
    char* data() noexcept;
    const char* data() const noexcept;
    size_type size() const noexcept;
    size_type length() const noexcept;
    bool empty() const noexcept;
    size_type capacity() const noexcept;
    void clear() noexcept;
    void reserve(size_type n); // 只增不减

    char& operator[](size_type i) noexcept;
    const char& operator[](size_type i) const noexcept;
    char& at(size_type i); // 越界抛 std::out_of_range
    const char& at(size_type i) const;

    // ---------------- 修改 ----------------
    void resize(size_type n, char c = '\0');
    void push_back(char c);
    void append(const string& s);
    void append(const char* s);
    string& operator+=(const string& s);
    string& operator+=(const char* s);
    string& operator+=(char c);
    void swap(string& other) noexcept;

    // ---------------- 比较与查找 ----------------
    int compare(const string& other) const noexcept; // <0 / 0 / >0
    int compare(const char* other) const;
    size_type find(const string& needle, size_type pos = 0) const noexcept;
    size_type find(const char* needle, size_type pos = 0) const;
    size_type find(char c, size_type pos = 0) const noexcept;

private:
    // 保证缓冲区能装下 need_chars 个字符 + 结尾 '\0'（必要时扩容，容量翻倍）
    void ensure_capacity(size_type need_chars);

    char* data_; // 可能为 nullptr（仅 moved-from 状态）
    size_type size_;
    size_type capacity_; // 缓冲区总字节数（含结尾 '\0' 的位置）
};

// ---------------- 非成员函数 ----------------
void swap(string& a, string& b) noexcept;

string operator+(const string& a, const string& b);
string operator+(const string& a, const char* b);
string operator+(const char* a, const string& b);

bool operator==(const string& a, const string& b);
bool operator!=(const string& a, const string& b);
bool operator<(const string& a, const string& b);
bool operator>(const string& a, const string& b);
bool operator<=(const string& a, const string& b);
bool operator>=(const string& a, const string& b);

bool operator==(const string& a, const char* b);
bool operator==(const char* a, const string& b);
bool operator!=(const string& a, const char* b);
bool operator!=(const char* a, const string& b);

std::ostream& operator<<(std::ostream& os, const string& s);

} // namespace mystl
