#pragma once

// 手写智能指针：shared_ptr / weak_ptr / enable_shared_from_this
//
// 设计要点：
//   - 控制块（control block）承载引用计数与删除器：shared_ptr 只持有
//     「对象指针 + 控制块指针」两个成员，天然支持多态与自定义删除器；
//   - 分裂计数：shared_count 管「对象生命周期」，weak_count（含 1 个 shared
//     侧的隐式持有）管「控制块生命周期」——所以弱引用可以让控制块比对象活得久；
//   - 计数全部用 std::atomic<long>：拷贝/析构是原子的 ±1；
//   - weak_ptr::lock() 用 CAS 抢占提升，和"最后一个 shared_ptr 析构"之间不会
//     出现"对象已销毁却被提升成功"的竞态；
//   - make_shared：对象内嵌在控制块里（一次分配），减少一次内存分配；
//   - enable_shared_from_this：由裸指针构造 shared_ptr 时（或 make_shared 时）
//     自动回填内置 weak_this_，使对象能安全地拿到指向自己的 shared_ptr。
//   - 简化：不支持别名构造/数组与 const 版本；enable_shared_from_this 仅识别
//     恰好继承 enable_shared_from_this<自身> 的情况。
#include <atomic>
#include <cstddef>
#include <type_traits>
#include <utility>

#include "mystl/unique_ptr.h" // default_delete

namespace mystl {

template <typename T>
class shared_ptr;
template <typename T>
class weak_ptr;

// ---------------------------------------------------------------------------
// enable_shared_from_this：让「已由 shared_ptr 管理的对象」能安全地产生
// 指向自己的 shared_ptr（内部持一个 weak_ptr，避免自引用造成的循环）。
// ---------------------------------------------------------------------------
template <typename T>
class enable_shared_from_this {
public:
    shared_ptr<T> shared_from_this() {
        return weak_this_.lock();
    }
    weak_ptr<T> weak_from_this() noexcept {
        return weak_this_;
    }

protected:
    enable_shared_from_this() noexcept {}
    enable_shared_from_this(const enable_shared_from_this&) noexcept {}
    enable_shared_from_this& operator=(const enable_shared_from_this&) noexcept {
        return *this;
    }
    ~enable_shared_from_this() {}

private:
    template <typename U>
    friend class shared_ptr;
    mutable weak_ptr<T> weak_this_; // 由 shared_ptr 在接管对象时回填
};

// 探测 T 是否继承自 enable_shared_from_this<T>（SFINAE）
template <typename T>
class has_esft {
private:
    template <typename U>
    static char test(enable_shared_from_this<U>*);
    template <typename U>
    static long test(...);

public:
    typedef typename std::conditional<
        sizeof(test<T>(static_cast<T*>(nullptr))) == sizeof(char),
        std::true_type, std::false_type>::type type;
};

// ---------------------------------------------------------------------------
// 控制块
// ---------------------------------------------------------------------------

// 基类：屏蔽删除器类型，让 shared_ptr / weak_ptr 只依赖统一的接口
class control_block_base {
public:
    control_block_base() noexcept : shared_count(0), weak_count(0) {}
    virtual ~control_block_base() {}

    virtual void destroy() = 0;     // 对象生命周期结束：执行删除器 / 析构对象
    virtual void delete_this() = 0; // 控制块生命周期结束：释放自身

    std::atomic<long> shared_count; // 强引用计数
    std::atomic<long> weak_count;   // 弱引用计数（另含 1 个 shared 侧隐式持有）
};

// 普通版本：持有「对象指针 + 删除器」（shared_ptr(T*) / shared_ptr(T*, d) 用）
template <typename T, typename Deleter>
class control_block_ptr : public control_block_base {
public:
    control_block_ptr(T* p, const Deleter& d) : ptr_(p), deleter_(d) {}

    void destroy() override {
        deleter_(ptr_);
        ptr_ = nullptr;
    }
    void delete_this() override {
        delete this;
    }

private:
    T* ptr_;
    Deleter deleter_;
};

// make_shared 版本：对象直接构造在控制块内部（一次分配搞定控制块 + 对象）
template <typename T>
class control_block_make : public control_block_base {
public:
    template <typename... Args>
    explicit control_block_make(Args&&... args) {
        ::new (static_cast<void*>(&storage_)) T(std::forward<Args>(args)...);
    }

    void destroy() override {
        get()->~T();
    }
    void delete_this() override {
        delete this;
    }

    T* get() noexcept {
        return reinterpret_cast<T*>(&storage_);
    }

private:
    typename std::aligned_storage<sizeof(T), alignof(T)>::type storage_;
};

// ---------------------------------------------------------------------------
// weak_ptr：不增加强计数，可观察/提升为 shared_ptr
// ---------------------------------------------------------------------------
template <typename T>
class weak_ptr {
public:
    weak_ptr() noexcept : ptr_(nullptr), cb_(nullptr) {}

    // 由 shared_ptr 构造/赋值：只增加弱计数
    template <typename U, typename = typename std::enable_if<std::is_convertible<U*, T*>::value>::type>
    weak_ptr(const shared_ptr<U>& sp) noexcept : ptr_(sp.ptr_), cb_(sp.cb_) {
        if (cb_) {
            cb_->weak_count.fetch_add(1);
        }
    }

    weak_ptr(const weak_ptr& other) noexcept : ptr_(other.ptr_), cb_(other.cb_) {
        if (cb_) {
            cb_->weak_count.fetch_add(1);
        }
    }

    weak_ptr(weak_ptr&& other) noexcept : ptr_(other.ptr_), cb_(other.cb_) {
        other.ptr_ = nullptr;
        other.cb_ = nullptr;
    }

    ~weak_ptr() {
        release();
    }

    weak_ptr& operator=(const weak_ptr& other) noexcept {
        if (this != &other) {
            weak_ptr tmp(other); // 先 +1 再释放旧的，自赋值/异常都安全
            swap(tmp);
        }
        return *this;
    }

    weak_ptr& operator=(weak_ptr&& other) noexcept {
        if (this != &other) {
            release();
            ptr_ = other.ptr_;
            cb_ = other.cb_;
            other.ptr_ = nullptr;
            other.cb_ = nullptr;
        }
        return *this;
    }

    template <typename U, typename = typename std::enable_if<std::is_convertible<U*, T*>::value>::type>
    weak_ptr& operator=(const shared_ptr<U>& sp) noexcept {
        weak_ptr tmp(sp);
        swap(tmp);
        return *this;
    }

    void reset() noexcept {
        release();
    }

    long use_count() const noexcept {
        return cb_ ? cb_->shared_count.load() : 0;
    }

    bool expired() const noexcept {
        return use_count() == 0;
    }

    // 提升为 shared_ptr：用 CAS 把强计数从 count 抢到 count+1，
    // 与"最后一个 shared_ptr 正在析构"竞争时，要么抢到（对象保活），
    // 要么观察到 0 失败（返回空）——不会提升出悬垂指针。
    shared_ptr<T> lock() const noexcept {
        if (!cb_) {
            return shared_ptr<T>();
        }
        long count = cb_->shared_count.load();
        while (count != 0) {
            if (cb_->shared_count.compare_exchange_weak(count, count + 1)) {
                return shared_ptr<T>(ptr_, cb_); // 已抢占 +1，直接装配
            }
            // CAS 失败时 count 已被更新为最新值，循环重新判断
        }
        return shared_ptr<T>();
    }

    void swap(weak_ptr& other) noexcept {
        std::swap(ptr_, other.ptr_);
        std::swap(cb_, other.cb_);
    }

private:
    void release() noexcept {
        if (cb_ && cb_->weak_count.fetch_sub(1) == 1) {
            cb_->delete_this(); // 我是最后一个弱引用（且强引用已归零）→ 释放控制块
        }
        ptr_ = nullptr;
        cb_ = nullptr;
    }

    template <typename U>
    friend class shared_ptr; // 让 shared_ptr 构造时回填 weak_this_ 等

    T* ptr_;
    control_block_base* cb_;
};

// ---------------------------------------------------------------------------
// shared_ptr
// ---------------------------------------------------------------------------
template <typename T>
class shared_ptr {
public:
    typedef T element_type;

    shared_ptr() noexcept : ptr_(nullptr), cb_(nullptr) {}

    // 由裸指针构造：新建控制块，计数初始化为 1（强）/ 1（弱，shared 侧隐式持有）
    explicit shared_ptr(T* p) : ptr_(nullptr), cb_(nullptr) {
        if (p) {
            control_block_ptr<T, default_delete<T>>* cb =
                new control_block_ptr<T, default_delete<T>>(p, default_delete<T>());
            cb->shared_count.store(1);
            cb->weak_count.store(1);
            ptr_ = p;
            cb_ = cb;
            enable_esft(ptr_, typename has_esft<T>::type());
        }
    }

    template <typename Deleter>
    shared_ptr(T* p, Deleter d) : ptr_(nullptr), cb_(nullptr) {
        if (p) {
            control_block_ptr<T, Deleter>* cb = new control_block_ptr<T, Deleter>(p, d);
            cb->shared_count.store(1);
            cb->weak_count.store(1);
            ptr_ = p;
            cb_ = cb;
            enable_esft(ptr_, typename has_esft<T>::type());
        }
    }

    // 拷贝：共享控制块，强计数 +1
    shared_ptr(const shared_ptr& other) noexcept : ptr_(other.ptr_), cb_(other.cb_) {
        if (cb_) {
            cb_->shared_count.fetch_add(1);
        }
    }

    template <typename U, typename = typename std::enable_if<std::is_convertible<U*, T*>::value>::type>
    shared_ptr(const shared_ptr<U>& other) noexcept : ptr_(other.ptr_), cb_(other.cb_) {
        if (cb_) {
            cb_->shared_count.fetch_add(1);
        }
    }

    // 移动：窃取，源置空（引用计数不变）
    shared_ptr(shared_ptr&& other) noexcept : ptr_(other.ptr_), cb_(other.cb_) {
        other.ptr_ = nullptr;
        other.cb_ = nullptr;
    }

    template <typename U, typename = typename std::enable_if<std::is_convertible<U*, T*>::value>::type>
    shared_ptr(shared_ptr<U>&& other) noexcept : ptr_(other.ptr_), cb_(other.cb_) {
        other.ptr_ = nullptr;
        other.cb_ = nullptr;
    }

    ~shared_ptr() {
        release();
    }

    // 拷贝赋值：copy-and-swap（自赋值安全）
    shared_ptr& operator=(const shared_ptr& other) noexcept {
        if (this != &other) {
            shared_ptr tmp(other);
            swap(tmp);
        }
        return *this;
    }

    // 移动赋值
    shared_ptr& operator=(shared_ptr&& other) noexcept {
        if (this != &other) {
            shared_ptr tmp(std::move(other));
            swap(tmp);
        }
        return *this;
    }

    void reset() noexcept {
        release();
    }

    void reset(T* p) {
        shared_ptr tmp(p);
        swap(tmp);
    }

    template <typename Deleter>
    void reset(T* p, Deleter d) {
        shared_ptr tmp(p, d);
        swap(tmp);
    }

    void swap(shared_ptr& other) noexcept {
        std::swap(ptr_, other.ptr_);
        std::swap(cb_, other.cb_);
    }

    // ---------------- 观察 ----------------
    T* get() const noexcept { return ptr_; }
    T& operator*() const noexcept { return *ptr_; }
    T* operator->() const noexcept { return ptr_; }
    explicit operator bool() const noexcept { return ptr_ != nullptr; }
    long use_count() const noexcept { return cb_ ? cb_->shared_count.load() : 0; }
    bool unique() const noexcept { return use_count() == 1; }

private:
    // 供 weak_ptr::lock() 抢占成功后直接装配（计数已 +1 完毕）
    shared_ptr(T* p, control_block_base* cb) noexcept : ptr_(p), cb_(cb) {}

    // 释放一次强引用；归零则销毁对象，并归还 shared 侧的隐式弱持有
    void release() noexcept {
        if (!cb_) {
            return;
        }
        if (cb_->shared_count.fetch_sub(1) == 1) {
            cb_->destroy();
            if (cb_->weak_count.fetch_sub(1) == 1) {
                cb_->delete_this();
            }
        }
        ptr_ = nullptr;
        cb_ = nullptr;
    }

    // 由裸指针接管对象时，若 T 继承 enable_shared_from_this<T>，回填 weak_this_
    void enable_esft(T* p, std::true_type) noexcept {
        static_cast<enable_shared_from_this<T>*>(p)->weak_this_ = weak_ptr<T>(*this);
    }
    void enable_esft(T*, std::false_type) noexcept {}

    template <typename U>
    friend class shared_ptr;
    template <typename U>
    friend class weak_ptr;
    template <typename U, typename... Args>
    friend shared_ptr<U> make_shared(Args&&... args);

    T* ptr_;
    control_block_base* cb_;
};

// make_shared：控制块与对象一次分配
template <typename U, typename... Args>
shared_ptr<U> make_shared(Args&&... args) {
    control_block_make<U>* cb = new control_block_make<U>(std::forward<Args>(args)...);
    cb->shared_count.store(1);
    cb->weak_count.store(1);
    shared_ptr<U> sp;
    sp.ptr_ = cb->get();
    sp.cb_ = cb;
    sp.enable_esft(sp.ptr_, typename has_esft<U>::type());
    return sp;
}

} // namespace mystl
