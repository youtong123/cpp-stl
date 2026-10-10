# cpp-stl

从零手写的 C++11 迷你 STL 子集：动态数组、字符串、两级内存池、智能指针，配套断言测试与实测基准。
不使用任何第三方库。

## 组件

| 组件 | 位置 | 要点 |
|---|---|---|
| `vector<T, Alloc>` | `include/mystl/vector.h` | 动态数组；倍增扩容（首块 1）；`move_if_noexcept` 搬运策略（noexcept 移动才移动，否则退回复制）；分配器参数化 |
| `string` | `include/mystl/string.h` | 深拷贝 / 移动语义；`c_str()` 永不为空指针、结尾 `'\0'` 不变量；`at()` 越界抛异常；`find` 家族；`append(c_str())` 内部别名安全 |
| `MemoryPool` | `include/mystl/memory_pool.h` | SGI 风格两级分配器：≤128B 按 8B 对齐走 16 条自由链表，>128B 直接 malloc；批量 refill；chunk 记账，`release_all()` 显式归还；互斥锁保护 |
| `pool_allocator<T>` | 同上 | 符合容器要求的池式分配器（rebind / construct / destroy） |
| `unique_ptr<T, Deleter>` | `include/mystl/unique_ptr.h` | 独占所有权、move-only；自定义删除器（0 开销，删除器是成员）；派生→基类转换构造 |
| `shared_ptr` / `weak_ptr` | `include/mystl/shared_ptr.h` | 原子引用计数控制块；分裂计数（强/弱分离）；`make_shared` 对象内嵌控制块单次分配；`weak_ptr::lock()` CAS 提升防"最后强引用析构"竞态；`enable_shared_from_this` |
| `move_if_noexcept` | `include/mystl/type_util.h` | 手写实现，理解扩容搬运逻辑的根 |

## 构建与测试

```bash
cmake -S . -B build
cmake --build build -j
cd build && ctest --output-on-failure
```

- C++11（`CMAKE_CXX_STANDARD 11`），`-Wall -Wextra -Werror` 零告警（GCC 13.3.0 验证）
- 断言测试：4 个独立可执行文件、共 **40716 项断言**，包括：
  - 计数类型验证 `emplace_back` 就地构造与 `move_if_noexcept` 搬运路径（noexcept 移动类型 zero-copy、可能抛的移动类型退回复制，构造/拷贝/移动次数精确断言）
  - string 自身缓冲区别名追加（`s.append(s.c_str())` 触发扩容重分配）与 1 万字符扩容路径
  - 内存池 20000 次混合分配/释放压力测试（LCG 确定性随机序列、内容哨兵校验、对齐校验）
  - 智能指针生命周期计数、弱引用提升、`enable_shared_from_this` 两条接管路径、循环引用断环
- 内存安全验证：
  - Valgrind `--leak-check=full --error-exitcode=1`：4 个测试全部 **0 错误、无泄漏**
  - ASan + UBSan 重新构建：4 个测试全部通过、无未定义行为

## 基准（实测）

环境：Intel Core i9-14900HX / WSL2 Ubuntu 24.04 / GCC 13.3.0 / `-O2`。
每项内部 best of 3，复测两次取较优值，两次波动 ≤15%。

| 场景 | mystl | std |
|---|---|---|
| `vector` push_back 1000 万 int（无 reserve） | 28.9 ms | 32.9 ms |
| `vector` push_back 1000 万 int（先 reserve） | 11.5 ms | 14.1 ms |
| 100 万次随机小块 alloc/free（1–200B，64 块同时存活） | 11.1 ms（`MemoryPool`） | 8.7 ms（`operator new/delete`） |
| `vector` 增长 100 万 int | 2.4 ms（`pool_allocator`） | 2.4 ms（默认分配器） |

如实解读（数据为哪来就说哪来）：

- **vector**：无 reserve 时与 libstdc++ 同量级略优（扩容策略相近）；reserve 路径双方都是纯搬运填装，mystl 略快。
- **小块池不敌 glibc**：现代 glibc（tcache）对单线程 1–200B 分配已高度优化，而本实现是加锁单池、无 thread-local 缓存。池的价值在于：消除逐块分配器元数据、以整 chunk 提升局部性、给碎片一个上界，且行为简单可审计。**thread-local 缓存 / 多池分片**是明确的后续优化方向。
- **vector 增长打平**：百万级 int 的缓冲区很快超过 128B 阈值，池自动回落到 malloc，符合设计预期。

## 目录结构

```
include/mystl/   # 全部组件（vector/string/memory_pool/allocator/unique_ptr/shared_ptr/type_util）
src/             # string.cpp、memory_pool.cpp（其余为 header-only）
tests/           # 4 个断言测试 + 自研极简测试框架 test_util.h
benchs/          # bench_vector、bench_pool（-O2 单独编译）
```

## 已知局限

- 内存池只回收不归还 OS（`release_all()` 供进程退出前显式清理），长生命周期进程里属"以空间换时间"。
- `shared_ptr` 不支持别名构造、数组与 `const` 版本（README 层面如实标注）。
- `string` 无 SSO 优化，全部走堆缓冲。
