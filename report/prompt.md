# Lab4 Prompt 汇总

## 一、功能模块一：虚拟内存页操作【王治中】

~~~text
[PROMPT]
**任务**：修改 kern/mm/pmm.c，在 `//LAB4 function1 YOUR CODE` 标记下方实现虚拟内存页映射维护功能。

**操作要求**：在项目中修改实际文件，保留现有代码，仅在标记位置补全。

**输出要求**：直接进行文件操作，依据 [RELY]、[GUARANTEE]、[SPECIFICATION] 实现，不引入无关功能。

[RELY]
可用接口：`get_pte(pgdir, la, create)`（需分配中间页表时可能返回 NULL）、`pte_create`、`pte2page`、`page_ref_inc/dec`、`set_page_ref`、`alloc_page`、`free_page`、`tlb_invalidate`；权限宏 `PTE_V`、`PTE_W`、`PTE_R`、`PTE_U`。

[GUARANTEE]

```c
int page_insert(pde_t *pgdir, struct Page *page, uintptr_t la, uint32_t perm);
void page_remove(pde_t *pgdir, uintptr_t la);
```

[SPECIFICATION]

## page_insert

**Pre-Condition**: page 指向一个已分配的有效物理页，la 为待映射的线性地址。
**Post-Condition**: 成功时返回 0，la 稳定映射到 page，权限位设置为 PTE_V|perm，页表项内容、页引用计数、TLB 状态三者保持同步；当 get_pte 需要分配中间级页表页而分配失败时，返回 -E_NO_MEM，且不能留下半完成的映射。
  **Case 1**: la 原本已映射到同一物理页：不重复增减引用计数，仅将页表项权限更新为 perm 并失效对应 TLB。
  **Case 2**: la 原本已映射到其他物理页：不能简单覆盖旧表项，须先撤销旧映射（旧页引用计数减一，计数为 0 时释放该页），再建立新映射并增加引用计数。

## page_remove

**Pre-Condition**: la 为任意线性地址，函数需自行判断映射是否存在。
**Post-Condition**: 作为幂等的删除接口：la 存在有效映射时，清除该页表项，对应物理页引用计数减一，计数为 0 时释放该页，并失效对应 TLB；la 未映射时安静返回，不把未映射地址当作异常。
~~~

---

## 二、功能模块二：进程创建及初始化【邵毅】

~~~text
[PROMPT]
在 ucore lab4 中实现功能模块二：补全 `alloc_proc`、`proc_init`、`kernel_thread`、`do_fork`、`copy_thread`、`copy_mm`，把当前执行现场包装为 `idleproc`，并创建可被调度的 `initproc`；只改 `kern/process/proc.c` 的 `//lab4 function2 2412195` 标记处，不改其他文件、数据结构及 `do_exit` 占位实现。
[RELY]
已有 PCB、上下文、trapframe、进程链表与哈希表、`idleproc/initproc/current`、`nr_process`；可用 `kmalloc/kfree`、`find_proc/get_pid`、`hash_proc/setup_kstack/put_kstack/wakeup_proc`、`list_init/list_add`、`local_intr_save/restore`、`read_csr`、`assert/panic`；`boot_pgdir_pa` 为页表根物理地址，`bootstack` 为启动栈；常量、错误码和首次运行入口链路均已具备。
[GUARANTEE]
实现上述六个接口，其中 `alloc_proc`、`copy_thread`、`copy_mm` 为 `static`；签名不变。
[SPECIFICATION]
- alloc_proc：分配并初始化 PCB；页表根用 `boot_pgdir_pa`，链表节点自环；失败返回 `NULL`。不分配内核栈/PID、不挂链。
- proc_init：初始化进程链表与哈希表；建 `idleproc`（pid=0、复用启动栈、标记需要重新调度）并令其为 `current`，再创建 `initproc`（pid=1）。`idleproc` 不挂链，必须先设 `current` 再创建。
- kernel_thread：构造 trapframe，设置入口、参数和返回状态（S 模式、开中断）；以 `esp=0` 调 `do_fork`。
- do_fork：检查上限；分配 PCB 与内核栈、处理 mm、复制现场；在关中断临界区分配 PID、挂链并计数；最后唤醒。失败先释放内核栈再释放 PCB。
- copy_thread：在新内核栈顶安放 trapframe 副本，返回现场指向 `forkret`，栈指针指向 trapframe。
- copy_mm：只建内核线程，断言当前无用户 mm 后返回 0；保留接口供 lab5 扩展。
- 验收：`make` 通过；`make grade` 的 `[Test Process Create]` PASS。
~~~

---

## 三、功能模块三：进程调度【李行阳】

~~~text
[PROMPT]
在 ucore lab4 中实现功能模块三：补全 cpu_idle、schedule、proc_run、switch_to，不修改框架其他文件、数据结构、宏和测试。

[RELY]
已有 proc_struct、context、trapframe、proc_list、idleproc、current、PROC_RUNNABLE；已有 local_intr_save/restore、list_next、le2proc、write_csr、SATP_MODE_SV39、RISCV_PGSHIFT、flush_tlb；模块二已完成线程创建，并设置 context.ra=forkret、context.sp=tf、trapframe 的 s0/s1/epc。

[GUARANTEE]
可用上述接口；context 字段顺序必须与 switch.S 的偏移一致；不要使用 Sv32 遗留的 lsatp()。

[SPECIFICATION]
- cpu_idle：循环检查 need_resched，必要时调用 schedule。
- schedule：关中断；清 need_resched；按 RR 扫描 proc_list，跳过 &proc_list，选第一个 PROC_RUNNABLE；找不到回退 idleproc；next != current 时调用 proc_run。
- proc_run：切换 current、satp 和上下文，三者保持原子；写 satp 后调用 flush_tlb。
- switch_to：按 context 顺序保存/恢复 ra、sp、s0～s11，ret 跳到 to->context.ra。
- 边界：链表为空不崩溃；找不到就绪线程回退 idle。
- 验收：make 通过；make grade 中 [Test Schedule] PASS，30/30。
~~~
