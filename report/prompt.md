# Lab2 Prompt 汇总

> 本文件收集 lab2 各练习实际使用的提示词/问题，供报告和提交使用。

---

## 一、功能模块一：`page_init`【李行阳】

### 1.1 完整 prompt（四段式）

```text
[PROMPT]
任务：直接修改工程文件，完成 lab2 功能模块一任务一：物理内存探测与页描述符初始化。
目标文件：kern/mm/pmm.c。
实现位置：标有 `//lab2 function1 YOUR CODE` 的位置。
要求：
- 必须实际修改项目文件，不要只在回答里展示代码；
- 保留已有代码和注释，只补全 page_init；
- 不得修改框架测试/断言函数、已有数据结构、宏、头文件接口和结构体；
- 实现后必须能通过 check_alloc_page() 和官方 test_page_init() 的检查。

[RELY]
以下上下文均来自当前工程，不要改名或臆造：
- PGSIZE = 4096，PPN(x) = (uintptr_t)(x) >> PGSHIFT；
- struct Page 包含 ref、flags、property、page_link；
- PHYSICAL_MEMORY_OFFSET = 0xFFFFFFFF40000000，DRAM_BASE = 0x80000000，nbase = DRAM_BASE / PGSIZE；
- 全局变量：struct Page *pages; size_t npage; uint64_t va_pa_offset;；
- uint64_t get_memory_base(void); uint64_t get_memory_size(void); 返回 DTB 报告的物理内存范围；
- extern char end[]; 是内核镜像末尾的虚拟地址；
- PADDR(kva) 把内核虚拟地址转为物理地址；pa2page(pa) 把物理地址转为 Page 描述符指针；
- init_memmap(struct Page *base, size_t n) 把 n 个连续空闲页交给当前 pmm_manager；
- SetPageReserved(page) 设置保留标志；可用 panic()、cprintf()。

[GUARANTEE]
- 必须实现 static void page_init(void);；
- 如需要辅助函数，只能声明为 static，不得改动任何公开接口；
- 成功后必须正确设置 va_pa_offset、npage、pages。

[SPECIFICATION]
Pre-Condition:
- init_pmm_manager() 已执行，DTB 信息已就绪。

Post-Condition:
- va_pa_offset == PHYSICAL_MEMORY_OFFSET；
- npage == PPN(get_memory_base() + get_memory_size())，且 npage > nbase；
- pages == ROUNDUP((uintptr_t)end, PGSIZE)，描述符数组覆盖 [nbase, npage)；
- 注册空闲区前，所有描述符先置为 reserved；
- 只把完整空闲页区间注册给 init_memmap()：
  起点 = ROUNDUP(PADDR(pages) + (npage - nbase) * sizeof(struct Page), PGSIZE)，
  终点 = ROUNDDOWN(mem_base + mem_size, PGSIZE)；
- 输出探测到的物理内存范围。

Cases:
1. DTB 返回的 base 或 size 为 0：panic() 终止；
2. npage <= nbase：panic() 终止；
3. 空闲区起点 >= 终点：不调用 init_memmap()；
4. 内存末端非页对齐：向下取整，尾部不足一页不注册；
5. 正常情况：按 Post-Condition 注册空闲页。

Requirements:
- 区分虚拟地址、物理地址、物理页号、Page 描述符地址；
- 确保 OpenSBI 占用区、内核镜像、描述符数组都不进入空闲区；
- 写功能与状态，不要输出逐步伪代码。
```

### 1.2 设计说明

- 提示词写“完成物理内存探测与页描述符初始化”，而不是“实现 `page_init` 函数”；
- `[RELY]` 只列当前工程真实存在的宏、结构体、函数和全局变量；
- `[SPECIFICATION]` 用前置/后置/分支描述行为，不写逐步伪代码；
- 每条要求都能对应 `test_page_init()` 的 8 项检查或 `check_alloc_page()`。

---

## 三、功能模块二（王治中：非 Challenge；邵毅：Challenge）

> **【王治中填写 First-Fit / Best-Fit / Buddy；邵毅填写 SLUB】**

### 3.1 First-Fit（王治中）

### 3.2 Best-Fit（王治中）

````text
[PROMPT]
**任务**：修改 kern/mm/best_fit_pmm.c 的 lab2、YOUR CODE 处，实现 Best-Fit 管理器并定义 best_fit_pmm_manager；在 kern/mm/pmm.c 的 init_pmm_manager() 中将当前物理内存管理器切换为best_fit_pmm_manager

**操作要求**：你必须在项目中修改实际文件，保留现有代码，不得改动两个测试函数。

**输出要求**：直接进行文件操作。

[RELY]
// 宏与结构
PGSIZE；SetPageProperty / ClearPageProperty / PageProperty；struct Page；free_area_t
// 可用函数
list_add_before / list_del / list_next；pages、npage、nbase
// 测试函数直接用 free_list、nr_free，须在本文件自行提供

[GUARANTEE]
```c
static void best_fit_init(void);
static void best_fit_init_memmap(struct Page *base, size_t n);
static struct Page *best_fit_alloc_pages(size_t n);
static void best_fit_free_pages(struct Page *base, size_t n);
static size_t best_fit_nr_free_pages(void);
const struct pmm_manager best_fit_pmm_manager;
```

[SPECIFICATION]
## best_fit_init_memmap
**Post-Condition**: 头页记录块页数并置标志，计数加 n，按页地址升序入链。

## best_fit_alloc_pages
**Post-Condition**: 取页数不小于 n 的最小块，同大小取低地址；余量切分成新块，无块可满足返回 NULL。

## best_fit_free_pages
**Post-Condition**: 按页地址升序插入，再与前、后相邻空闲块合并。
````

### 3.3 Buddy System（王治中）

实现 prompt：

````text
[PROMPT]
**任务**：从零生成伙伴系统分配模块，实现 2 的幂分块、分裂与伙伴合并并定义 buddy_pmm_manager；在 pmm.c 的 init_pmm_manager() 选它。

**操作要求**：你必须在项目中创建并修改实际文件，不得改动已有分配模块及其测试函数。

**输出要求**：直接进行文件操作。

[RELY]
// 宏与结构
PG_property；struct Page；free_area_t
// 可用函数
list_add_before / list_del / list_next；pages、nbase；alloc_pages

[GUARANTEE]
```c
static void buddy_init(void);
static void buddy_init_memmap(struct Page *base, size_t n);
static struct Page *buddy_alloc_pages(size_t n);
static void buddy_free_pages(struct Page *base, size_t n);
static size_t buddy_nr_free_pages(void);
static void buddy_check(void);
const struct pmm_manager buddy_pmm_manager;
```

[SPECIFICATION]
## buddy_init_memmap
**Post-Condition**: 按"起点对齐"贪心切成若干 2^k 块，按地址升序挂入对应阶。

## buddy_alloc_pages
**Post-Condition**: 取不小于 n 的最小阶块，不足则从更高阶逐级分裂、低位交出；无可用块返回 NULL。

## buddy_free_pages
**Post-Condition**: 逐级与同阶伙伴合并，摘链时扣减总空闲页数并清旧块头标志。
````

测试 prompt：

````text
[PROMPT]
**任务**：在伙伴系统模块的实现文件内写测试，入口 buddy_check

**操作要求**：你必须在项目中修改实际文件，不改被测函数与其它模块。

**输出要求**：直接进行文件操作。每个用例通过时打印一行 [Buddy] Case N: PASS，全部结束时打印 [Buddy] all tests passed, free pages = %lu

[RELY]
// 可访问状态
各阶链表与块数、总空闲页数等
// 可用函数
alloc_pages / free_pages / nr_free_pages；assert；cprintf

[GUARANTEE]
```c
static void buddy_check(void);
```

[SPECIFICATION]
## buddy_check
**Post-Condition**: 逐阶校验地址升序、按阶对齐、块页数之和等于总空闲页数；各阶分配释放往返后总数复原。

  **Case 1**: 分配到耗尽后无块可分配，非 2 的幂请求按向上取整处理。

  **Case 2**: 无空闲页时释放一段按阶对齐的连续页应合成更大阶的块，互不为伙伴的两页不得合并。

  **Case 3**: 逐页释放全部后，总数与各阶块数回到初始快照。
````

### 3.4 SLUB（邵毅）

实现 prompt（第一轮）：

```text
为一个小型 RISC-V 教学内核（uCore 风格）从零实现一个 SLUB 风格的内存对象分配模块。内核已具备第一层按页分配的物理内存管理，请先自行阅读其中的代码和可用工具（页分配接口、断言、打印、Page 结构等），在 kern/mm/ 下新增 slub.h / slub.c，实现第二层：基于 SLUB 主体思想的任意大小对象分配，并提供 kmalloc(size) / kfree(ptr) 两层接口；释放时只传对象指针，不允许调用方提供额外信息。初始化接口在我完成页分配初始化之后调用，请给出确切的调用语句。

【必须体现的 SLUB 语义（每条都可验证）】
1. 对象按大小向上归并到有限个大小等级，同等级对象来自同一种缓存，等级划分方案写明；
2. slab 向第一层申请整页（如每 slab 一页），页内切分成多个对象；
3. 空闲对象内嵌 freelist（对象空间里直接存下一个空闲对象信息）；
4. 分配优先取当前/部分占用 slab 的空闲对象，不够时才向页层申请新页；
5. slab 全空时归还页层，保证对象全释放后系统占用的页数恢复到初始值（无泄漏）；
6. kfree 能可靠定位对象所属 slab 与缓存；
7. 非法指针与双重释放必须能检出（assert/panic）；
8. 缓存元数据内存不允许"元数据又请求元数据"的递归。
错误语义（如 size==0、超过单页）明确声明并保持一致。

【边界】
不实现 Linux SLUB 的 per-CPU、RCU、复杂 partial 排序等，只要体现主体思想；不修改既有代码的现有语义；单核无并发，不需要加锁；不要求性能与时空复杂度。

【交付与验收】
kern/mm/slub.h、slub.c 完整代码 + 简要说明（大小等级、freelist 组织、kfree 定位方法、slab 归还时机）。代码放入 code/kern/mm/ 后 make 通过，QEMU 启动运行初始化无 panic。
```

测试 prompt（第二轮）：

```text
我已得到 SLUB 风格对象分配模块（kern/mm/ 下 slub.h 提供 kmalloc/kfree，kfree 只传指针），先阅读该头文件确认实际接口签名，然后为其设计测试函数，接入内核单独运行。

【约束】
- 测试放在新文件（建议 kern/mm/slub_check.c），提供统一入口 check_slub()，由我在对象分配器初始化之后调用；
- 只允许通过 kmalloc/kfree 和页分配器可查询接口做黑盒测试，禁止读写待测实现的内部数据结构，禁止修改 slub.c/slub.h；
- 每个检查点断言失败即停，通过时打印"检查项 + 通过"，最后打印总结；控制测试资源用量，避免耗尽内存。

【必须覆盖的检查（每项注明拦截哪类回归）】
1. 分配有效性：覆盖 1 字节、小对象、中对象、接近对象最大尺寸，逐个分配、对整个对象写满线性模式后读回比对；
2. 不重叠与对齐：同时持有多个不同尺寸对象，交叉写 A 读 B 验证互不踩踏；多次分配地址满足实现声明对齐规则；
3. 释放复用：分配 A → kfree(A) → 再分配同大小，验证其地址被回收复用；
4. slab 增长：一种小尺寸循环分配数量远超单页对象容量（中途穿插释放若干），全部对象存活且内容正确；
5. slab 收缩无泄漏：全部 kfree 后系统空闲页数恢复到序列开始前的值（若实现声明有保留结构，断言差异不超过其声明值）；
6. 交错分配/释放：分批释放部分对象后继续分配，验证存活对象内容不被破坏，未存活空间不被错误复用；
7. 非法使用：kfree 同一地址两次、kfree 栈上/NULL 指针，必须触发断言/panic（可单独一个最小运行阶段做）；
8. 固定种子随机压力：多种尺寸间随机 kmalloc/kfree 至数百个存活对象，随机抽验存活对象内容，结束全部释放并校验页数恢复。
若实现声明的边界语义与常规不同（如 size==0），按其声明验证一致性并记录在注释中。

【验收】
代码放入 code/kern/mm/ 后 make 通过；QEMU 中自动运行，全部检查通过打印总结。
```
