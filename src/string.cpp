#include "mystl/string.h"

#include <cstring>
#include <ostream>
#include <stdexcept>
#include <utility>

namespace mystl {

const string::size_type string::npos;

namespace {
const string::size_type kInitCapacity = 16; // 首次分配的缓冲区字节数
} // namespace

// ---------------- 构造 / 析构 / 赋值 ----------------

string::string() : data_(nullptr), size_(0), capacity_(0) {}

string::string(const char* s) : data_(nullptr), size_(0), capacity_(0) {
    if (s == nullptr) {
        return;
    }
    size_type n = std::strlen(s);
    ensure_capacity(n); // n 为 0 时也会分配初始缓冲区，之后 invariant 恒成立
    if (n > 0) {
        std::memcpy(data_, s, n);
    }
    size_ = n;
    data_[size_] = '\0';
}

// 深拷贝：新缓冲区 + 全量复制
string::string(const string& other) : data_(nullptr), size_(0), capacity_(0) {
    ensure_capacity(other.size_);
    if (other.size_ > 0) {
        std::memcpy(data_, other.data_, other.size_);
    }
    size_ = other.size_;
    data_[size_] = '\0';
}

// 移动构造：O(1) 窃取，源对象置为合法的空状态
string::string(string&& other) noexcept
    : data_(other.data_), size_(other.size_), capacity_(other.capacity_) {
    other.data_ = nullptr;
    other.size_ = 0;
    other.capacity_ = 0;
}

string::~string() {
    delete[] data_;
}

// 拷贝赋值：copy-and-swap，自赋值安全、异常安全
string& string::operator=(const string& other) {
    if (this != &other) {
        string tmp(other);
        swap(tmp);
    }
    return *this;
}

// 移动赋值：先归还自己的缓冲，再窃取
string& string::operator=(string&& other) noexcept {
    if (this != &other) {
        delete[] data_;
        data_ = other.data_;
        size_ = other.size_;
        capacity_ = other.capacity_;
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }
    return *this;
}

string& string::operator=(const char* s) {
    // 先构造临时对象，避免 s 指向自身缓冲区时被扩容失效（别名安全）
    string tmp(s);
    swap(tmp);
    return *this;
}

// ---------------- 容量与访问 ----------------

const char* string::c_str() const noexcept {
    return data_ ? data_ : "";
}

char* string::data() noexcept {
    return data_;
}

const char* string::data() const noexcept {
    return data_;
}

string::size_type string::size() const noexcept {
    return size_;
}

string::size_type string::length() const noexcept {
    return size_;
}

bool string::empty() const noexcept {
    return size_ == 0;
}

string::size_type string::capacity() const noexcept {
    return capacity_ ? capacity_ - 1 : 0; // 对外报"字符容量"（不含 '\0' 位）
}

void string::clear() noexcept {
    size_ = 0;
    if (data_) {
        data_[0] = '\0';
    }
}

void string::reserve(size_type n) {
    ensure_capacity(n);
}

char& string::operator[](size_type i) noexcept {
    return data_[i];
}

const char& string::operator[](size_type i) const noexcept {
    return data_[i];
}

char& string::at(size_type i) {
    if (i >= size_) {
        throw std::out_of_range("mystl::string::at");
    }
    return data_[i];
}

const char& string::at(size_type i) const {
    if (i >= size_) {
        throw std::out_of_range("mystl::string::at");
    }
    return data_[i];
}

// ---------------- 修改 ----------------

void string::resize(size_type n, char c) {
    if (n <= size_) {
        size_ = n;
        if (data_) { // 默认构造/ moved-from 状态可能没有缓冲区
            data_[size_] = '\0';
        }
        return;
    }
    ensure_capacity(n);
    std::memset(data_ + size_, c, n - size_);
    size_ = n;
    data_[size_] = '\0';
}

void string::push_back(char c) {
    ensure_capacity(size_ + 1);
    data_[size_] = c;
    ++size_;
    data_[size_] = '\0';
}

void string::append(const string& s) {
    if (&s == this) { // 自追加：先做副本再追加
        string tmp(s);
        append(tmp);
        return;
    }
    if (s.size_ == 0) {
        return;
    }
    ensure_capacity(size_ + s.size_);
    std::memcpy(data_ + size_, s.data_, s.size_);
    size_ += s.size_;
    data_[size_] = '\0';
}

void string::append(const char* s) {
    if (s == nullptr) {
        return;
    }
    size_type n = std::strlen(s);
    if (n == 0) {
        return;
    }
    // 别名安全：s 可能指向本对象缓冲区内部（如 append(c_str())）——
    // 扩容会搬走并释放旧缓冲，先记下偏移量，扩容后重定位到新缓冲再拷贝
    if (data_ != nullptr && s >= data_ && s < data_ + size_) {
        size_type offset = static_cast<size_type>(s - data_);
        ensure_capacity(size_ + n);
        s = data_ + offset;
    } else {
        ensure_capacity(size_ + n);
    }
    // memmove 而不是 memcpy：src 与 dst 位于同一缓冲内、可能重叠
    std::memmove(data_ + size_, s, n);
    size_ += n;
    data_[size_] = '\0';
}

string& string::operator+=(const string& s) {
    append(s);
    return *this;
}

string& string::operator+=(const char* s) {
    append(s);
    return *this;
}

string& string::operator+=(char c) {
    push_back(c);
    return *this;
}

void string::swap(string& other) noexcept {
    std::swap(data_, other.data_);
    std::swap(size_, other.size_);
    std::swap(capacity_, other.capacity_);
}

// ---------------- 比较与查找 ----------------

int string::compare(const string& other) const noexcept {
    size_type n = size_ < other.size_ ? size_ : other.size_;
    int r = 0;
    if (n > 0) {
        r = std::memcmp(data_, other.data_, n); // 两串 size>0 时 data_ 必非空
    }
    if (r != 0) {
        return r < 0 ? -1 : 1;
    }
    if (size_ < other.size_) {
        return -1;
    }
    if (size_ > other.size_) {
        return 1;
    }
    return 0;
}

int string::compare(const char* other) const {
    return compare(string(other ? other : ""));
}

string::size_type string::find(const string& needle, size_type pos) const noexcept {
    if (needle.size_ == 0) {
        return pos <= size_ ? pos : npos;
    }
    if (pos > size_ || needle.size_ > size_ - pos) {
        return npos;
    }
    for (size_type i = pos; i + needle.size_ <= size_; ++i) {
        if (std::memcmp(data_ + i, needle.data_, needle.size_) == 0) {
            return i;
        }
    }
    return npos;
}

string::size_type string::find(const char* needle, size_type pos) const {
    if (needle == nullptr) {
        return npos;
    }
    return find(string(needle), pos);
}

string::size_type string::find(char c, size_type pos) const noexcept {
    if (pos >= size_) {
        return npos;
    }
    const void* p = std::memchr(data_ + pos, c, size_ - pos);
    if (p == nullptr) {
        return npos;
    }
    return static_cast<size_type>(static_cast<const char*>(p) - data_);
}

// ---------------- 私有辅助 ----------------

void string::ensure_capacity(size_type need_chars) {
    size_type required = need_chars + 1; // 留出结尾 '\0'
    if (required <= capacity_) {
        return;
    }
    size_type new_cap = capacity_ ? capacity_ * 2 : kInitCapacity;
    if (new_cap < required) {
        new_cap = required;
    }
    char* new_data = new char[new_cap];
    if (size_ > 0) {
        std::memcpy(new_data, data_, size_);
    }
    delete[] data_;
    data_ = new_data;
    capacity_ = new_cap;
    data_[size_] = '\0';
}

// ---------------- 非成员函数 ----------------

void swap(string& a, string& b) noexcept {
    a.swap(b);
}

string operator+(const string& a, const string& b) {
    string r;
    r.reserve(a.size() + b.size());
    r.append(a);
    r.append(b);
    return r;
}

string operator+(const string& a, const char* b) {
    string r(a);
    r.append(b);
    return r;
}

string operator+(const char* a, const string& b) {
    string r(a);
    r.append(b);
    return r;
}

bool operator==(const string& a, const string& b) {
    return a.compare(b) == 0;
}

bool operator!=(const string& a, const string& b) {
    return a.compare(b) != 0;
}

bool operator<(const string& a, const string& b) {
    return a.compare(b) < 0;
}

bool operator>(const string& a, const string& b) {
    return a.compare(b) > 0;
}

bool operator<=(const string& a, const string& b) {
    return a.compare(b) <= 0;
}

bool operator>=(const string& a, const string& b) {
    return a.compare(b) >= 0;
}

bool operator==(const string& a, const char* b) {
    return a.compare(b) == 0;
}

bool operator==(const char* a, const string& b) {
    return b.compare(a) == 0;
}

bool operator!=(const string& a, const char* b) {
    return a.compare(b) != 0;
}

bool operator!=(const char* a, const string& b) {
    return b.compare(a) != 0;
}

std::ostream& operator<<(std::ostream& os, const string& s) {
    os.write(s.c_str(), static_cast<std::streamsize>(s.size()));
    return os;
}

} // namespace mystl
