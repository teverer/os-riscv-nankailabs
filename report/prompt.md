# Lab3 Prompt 汇总

## 一、功能模块一：Trap 上下文恢复与入口【李行阳】

### 1.1 完整 prompt（四段式）

```text
[PROMPT]
在 ucore-riscv 的 lab3 中实现 Trap 入口与上下文恢复功能：
1. 补全 kern/trap/trapentry.S 中的 RESTORE_ALL 宏；
2. 补全 __alltraps 中 SAVE_ALL 之后、__trapret 之前的入口代码。
直接修改源文件，给出完整实现，并说明如何用 make grade 的 Test 1 验证。

[RELY]
- struct trapframe 定义在 kern/trap/trap.h：共 36 个 uintptr_t，前 32 个依次是 x0~x31，后 4 个是 sstatus、sepc、sbadaddr、scause。
- SAVE_ALL 已实现：csrw sscratch, sp；sp -= 36 * REGBYTES；保存 x0、x1、x3~x31；csrrw s0, sscratch, x0；csrr s1~s4 保存 4 个 CSR；STORE s0, 2*REGBYTES(sp)；STORE s1~s4, 32~35*REGBYTES(sp)。
- __trapret 已实现：RESTORE_ALL；sret。
- REGBYTES = 8；LOAD/STORE 分别展开为 ld/sd。
- void trap(struct trapframe *tf) 已实现，不需要修改。

[GUARANTEE]
- 只修改 RESTORE_ALL 宏体和 __alltraps 中 SAVE_ALL 之后的位置。
- 不修改 SAVE_ALL、struct trapframe、trap() 的签名。
- 不新增全局符号。

[SPECIFICATION]
RESTORE_ALL:
- Pre-Condition：sp 指向 SAVE_ALL 建立的完整 trapframe；当前为 S 模式；SIE = 0。
- Post-Condition：x1、x3~x31 和 sstatus/sepc/sbadaddr/scause 等于 trapframe 中对应槽位的值；sp 等于槽位 2 保存的原 sp；不修改 trapframe 以外的内存。
- Case：任意寄存器值（包括 0、全 1、等于 sp）都必须正确恢复。
- Requirements：x2(sp) 必须特殊处理；不得依赖任何寄存器进入前的值；CSR 写入借助的临时寄存器，其原值最终必须恢复；恢复 sstatus 不得在 sret 前意外打开中断。
__alltraps:
- Pre-Condition：SAVE_ALL 已执行，sp 指向 trapframe。
- Post-Condition：以 trapframe 地址为第一个参数调用 trap()；返回后 sp 不变，控制流进入 __trapret。
- Requirements：参数按 RISC-V ABI 放在 a0；不得破坏恢复所需的信息。
```

---

## 二、功能模块二：中断与异常处理【邵毅、王治中】

### 2.1 完整 prompt（四段式）

~~~text
[PROMPT]
**任务**：修改 code/kern/trap/trap.c，实现 lab3 功能模块二“中断与异常处理”，完成 interrupt_handler() 和 exception_handler() 两个函数。interrupt_handler() 负责对 Trap 入口送来的中断按原因分类处理；exception_handler() 负责对同步异常分类处理。两个函数处理结束后都必须返回到 trap_dispatch()/trap()，由已有的 Trap 返回流程恢复现场并执行 sret。

**操作要求**：必须实际修改项目文件，不要只展示代码。保留 trap.c 中已有代码和注释，只在标有 `//lab3 function2` 的位置补全两个函数；累计输出达到 SHUTDOWN_AFTER 后需要调用框架提供的关机接口，`SHUTDOWN_AFTER`在 trap.c 顶部新增宏定义。不得修改两个函数的签名，不得修改 trap.c 中规定以外的其他函数（包括 trap_dispatch()、trap()、print_trapframe()、print_regs()、print_ticks()、idt_init()、trap_in_kernel() 等），不得改动 trapentry.S、trap.h 中的现有接口和数据结构。如需辅助函数，应声明为 static，仅供本模块内部使用。

**输出要求**：直接进行文件操作；实现必须使用下面 [RELY] 中列出的、工程中真实存在的宏、结构体、全局变量和函数，不要引入无关功能或自造接口。

[RELY]
// 结构体与分流（code/kern/trap/trap.h、trap.c）
// struct trapframe （含 gpr、status、epc、badvaddr、cause 字段）；
// tf->cause 最高位为 1 表示中断、为 0 表示同步异常，trap_dispatch() 已按此分流到两个处理函数。

// 中断原因（code/libs/riscv.h）：IRQ_* 宏共 12 个（IRQ_U_SOFT=0 ~ IRQ_M_EXT=11），需特判 IRQ_S_TIMER=5。
// 异常原因（code/libs/riscv.h）：CAUSE_ILLEGAL_INSTRUCTION=0x2、CAUSE_BREAKPOINT=0x3。

// 时钟与统计：TICK_NUM=100（已有）、SHUTDOWN_AFTER 需新增、extern volatile size_t ticks（时钟中断计数）；
// void print_ticks(void)：输出一次统计信息；
// void clock_set_next_event(void)：设定下一次时钟事件，不调用则只能收到一次时钟中断（clock.h）；
// void sbi_shutdown(void)：关机，不返回（sbi.h）。

// 输出与调试（已有）：int cprintf(const char *fmt, ...)、void print_trapframe(struct trapframe *tf)（打印完整 Trap frame）。

// Trap 返回与指令长度：RESTORE_ALL 会把 tf->epc 等写回对应 CSR 后执行 sret，
// 因此改变返回地址应修改 tf->epc，不能直接写 sepc；
// 指令低两位为 0b11 时长度为 4 字节，否则为 2 字节（ebreak 4 字节，c.ebreak 2 字节）。

[GUARANTEE]
必须实现或修改的接口：
```c
void interrupt_handler(struct trapframe *tf);
void exception_handler(struct trapframe *tf);
```

可以根据需要添加：
- 静态辅助函数（建议声明为 static，例如用于判断异常指令长度）；
- 本模块内部使用的宏定义或静态变量。

不得改变上述公开接口，不得新增与实验无关的全局接口。

[SPECIFICATION]
## interrupt_handler

**Pre-Condition**:
- 由 trap_dispatch() 在 tf->cause 为中断（最高位为 1）时调用，tf 指向有效的 trapframe；返回后由统一的 Trap 返回流程恢复现场并执行 sret。

**Post-Condition**:
- 按去掉最高位后的中断原因完成对应处理并正常返回；每次时钟中断 ticks 恰好加 1，且下一次时钟事件已被重新设定；其他中断不改变 ticks、不重设时钟事件、不推进 tf->epc。

**Case 1**：去掉最高位后为 IRQ_S_TIMER。
- 先重新设定下一次时钟事件，再将 ticks 加 1；ticks 为 TICK_NUM 整数倍时输出一次统计信息；达到 SHUTDOWN_AFTER * TICK_NUM 时输出关机提示并关机（关机后不返回）。

**Case 2**：其他中断。
- 已定义的中断输出对应的中断类型提示；无法识别的按 `Unknown interrupt: cause %d` 的格式输出原因编号，并打印完整 Trap frame 辅助调试。

**Requirements**:
- 匹配 IRQ_* 宏前必须清除 cause 的最高位，不能把带符号的负 cause 直接用于分支；不修改 trapframe 中与中断处理无关的字段。

## exception_handler

**Pre-Condition**:
- 由 trap_dispatch() 在 tf->cause 为同步异常（最高位为 0）时调用；tf->epc 为触发异常的指令地址，且该指令在内存中可读。

**Post-Condition**:
- 非法指令和断点异常已按实验规定格式输出，且 tf->epc 已推进到下一条指令地址（必须结合指令编码判断长度，不得固定 +4）；其他异常已打印完整 Trap frame，且 tf->epc 保持不变；返回后不会重复执行触发异常的指令。

**Case 1**：CAUSE_ILLEGAL_INSTRUCTION 或 CAUSE_BREAKPOINT。
- 分别按 `Illegal instruction at 0x%08x` / `Breakpoint at 0x%08x` 的格式输出（参数为 tf->epc），按指令编码判断实际长度（c.ebreak 2 字节、ebreak 4 字节）并推进 tf->epc。

**Case 2**：其他异常原因。
- 打印完整 Trap frame 兜底；不修改 tf->epc 及其他字段，不实现实验未要求的恢复逻辑。
~~~
