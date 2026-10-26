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

实现 prompt：

```text
[PROMPT]

新建 code/kern/mm/slub.h 和 slub.c，在页分配器之上实现 SLUB 风格的第二层对象分配，提供 kmalloc(size) / kfree(ptr)（后者只传对象指针）。必须修改实际文件，只新增这两个文件和入口一行调用 slub_init();（位于 pmm_init(); 之后）；单核不加锁，不实现 per-CPU、RCU、复杂 partial 排序。大小等级、对齐与错误语义写入 slub.h 注释并与实现一致；make 通过，QEMU 启动初始化无 panic。

[RELY]

  类别   	关键内容                                    
  宏    	PGSIZE、PGSHIFT、PPN()、KERNBASE、KERNTOP、ROUNDUP()、ROUNDDOWN()
  结构   	struct Page（ref、flags、property、page_link）、list_entry_t
  页层接口 	alloc_page()、free_page()、nr_free_pages()
  地址关系 	DRAM 直映射到 [KERNBASE, KERNTOP)，VA = PA + va_pa_offset；page2pa()、pa2page()、PADDR()、pages、nbase、va_pa_offset
  调试/顺序	assert()、panic()（不返回）、cprintf()；先 pmm_init() 后 slub_init()

[GUARANTEE]

    void slub_init(void);
    void *kmalloc(size_t size);
    void kfree(void *ptr);

可添加 static 辅助函数与静态变量（如缓存表），不得改变公开接口。

[SPECIFICATION]

slub_init：首次调用建立各等级缓存，重复调用直接返回；不申请页，nr_free_pages() 不变；元数据必须静态获得，禁止用对象分配器申请元数据（无递归）。

kmalloc：未初始化则 panic。size==0 或超单页返回 NULL；优先从部分占用 slab 分配（不申请新页），其无空闲对象时才向页层申请一个整页作新 slab，页层耗尽返回 NULL。每 slab 恰好一页，freelist 指针存于空闲对象自身空间；返回指针满足等级对齐、空间不小于请求、与存活对象不重叠。

kfree：只传指针，由指针自行定位 slab/cache。合法对象回到其 slab 空闲链表；slab 全空则当次归还整页且不保留（全部释放后 nr_free_pages() 恢复原值），归还前清除页描述符上的本模块状态。以下 panic：ptr==NULL、不在 [KERNBASE, KERNTOP)、页非 slab、偏移非对象边界、元数据非法、双重释放。
```

测试 prompt：

```text
[PROMPT]

先读 slub.h 确认接口与已声明语义，新建 code/kern/mm/slub_check.c（可配 slub_check.h），实现统一入口 check_slub()，在 slub_init(); 之后调用。禁止修改 slub.c/slub.h，禁止读写分配器内部结构（缓存表、partial 链表、freelist、struct Page 字段），只用 kmalloc/kfree 与页分配器查询接口黑盒测试；断言失败即停机，控制资源用量避免耗尽内存。每项通过打印"检查项 + PASS"，最后打印总结；合法路径不 panic，非法使用必须 panic 且作为独立最小运行阶段；make 通过，QEMU 自动运行全部检查通过。

[RELY]

- 被测接口：slub_init(void)、kmalloc(size_t size)、kfree(void *ptr)，以 slub.h 实际声明为准。
- 已声明语义（须按此验证）：等级 8..4096 共 10 级、向上归并、同级共用缓存、按等级对齐；kmalloc(0)、超单页、页层耗尽返回 NULL；未初始化即用、kfree(NULL)、非法/非边界/双重释放 panic；不保留空 slab，测试前后空闲页数应一致。
- 可用接口与资源：nr_free_pages()、PGSIZE、assert()、panic()、cprintf()；128 MiB DRAM 空闲页约 3.2 万，随机阶段存活对象至多数百个。

[GUARANTEE]

    void check_slub(void);
    void check_slub_bad_use(int which);

后者为非法使用的独立 panic 阶段（which 取 0..3，每 case 单独启动一次）。可添加 static 辅助函数、测试静态数组、模式校验函数、固定种子随机生成器。

[SPECIFICATION]

check_slub：入口记录 nr_free_pages() 基准，全部通过后对象全部释放、页数与基准一致并打印总结；任一失败立即 panic。

  检查          	方法                                      
  1 分配有效性     	1B/小/中/近最大尺寸逐个分配，写满模式读回；验证 size==0 与超单页返回 NULL
  2 不重叠与对齐    	多种尺寸对象交叉读写，地址满足等级对齐                     
  3 释放复用      	分配 A、邻居 B → 释放 A → 再分配须复用 A，B 不破坏       
  4 slab 增长   	分配远超单页容量（如 640 个），中途穿插释放再补，全部存活且内容正确    
  5 slab 收缩无泄漏	多尺寸对象全部释放，nr_free_pages() 精确恢复          
  6 交错分配/释放   	分批释放部分后继续分配，存活对象不破坏、释放空间可复用             
  8 随机压力      	固定种子多尺寸交替数百次分配/释放，随机抽验，结束全部释放并校验页数      

check_slub_bad_use：必须由被测实现 panic 且不返回，若返回则测试自行 panic 报"非法操作未被检测"。case 0 同地址连续释放两次（双重释放）；case 1 释放栈上地址（非 slab 页）；case 2 kfree(NULL)；case 3 释放对象内部 +1 指针（非对象边界）。
```
