# Lab3 Prompt 汇总

> 本文件收集 lab3 各模块实际使用的提示词/问题，供报告和提交使用。

---

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

### 1.2 设计说明

- spec 写“做什么、什么不能动”，不写具体指令序列；AI 据此自行选择 `sp` 最后恢复、用 `s1~s4` 暂存 CSR 等实现。
- `[RELY]` 只给 `struct trapframe`、`SAVE_ALL`、`REGBYTES`、`trap()` 签名等最小可信信息，避免 AI 猜布局。
- 用 Test 1 验证：它同时覆盖寄存器恢复和 `sepc` 更新。

---

## 二、功能模块二：中断与异常处理【邵毅、王治中】

### 2.1 完整 prompt（四段式）

````text
[PROMPT]
**任务**：修改 code/kern/trap/trap.c，实现 lab3 功能模块二“中断与异常处理”，完成 interrupt_handler() 和 exception_handler() 两个函数。interrupt_handler() 负责对 Trap 入口送来的中断按原因分类处理；exception_handler() 负责对同步异常分类处理。两个函数处理结束后都必须返回到 trap_dispatch()/trap()，由已有的 Trap 返回流程恢复现场并执行 sret。

**操作要求**：必须实际修改项目文件，不要只展示代码。保留 trap.c 中已有代码和注释，只在标有 `//lab3 function2` 的位置补全两个函数；如需 `SHUTDOWN_AFTER`，可在 trap.c 顶部新增宏定义。不得修改两个函数的签名，不得修改 trap.c 中规定以外的其他函数（包括 trap_dispatch()、trap()、print_trapframe()、print_regs()、print_ticks()、idt_init()、trap_in_kernel() 等），不得改动 trapentry.S、trap.h 中的现有接口和数据结构。如需辅助函数，应声明为 static，仅供本模块内部使用。

**输出要求**：直接进行文件操作；实现必须使用下面 [RELY] 中列出的、工程中真实存在的宏、结构体、全局变量和函数，不要引入无关功能或自造接口。

[RELY]
// 结构体（code/kern/trap/trap.h）
struct trapframe {
    struct pushregs gpr;
    uintptr_t status;
    uintptr_t epc;       // 触发 Trap 的指令地址
    uintptr_t badvaddr;
    uintptr_t cause;     // scause
};
// tf->cause 最高位为 1 表示中断，为 0 表示同步异常。
// trap_dispatch() 已按 (intptr_t)tf->cause < 0 分流：
// 中断进入 interrupt_handler()，异常进入 exception_handler()。

// 中断原因（code/libs/riscv.h）
IRQ_U_SOFT=0，IRQ_S_SOFT=1，IRQ_H_SOFT=2，IRQ_M_SOFT=3，
IRQ_U_TIMER=4，IRQ_S_TIMER=5，IRQ_H_TIMER=6，IRQ_M_TIMER=7，
IRQ_U_EXT=8，IRQ_S_EXT=9，IRQ_H_EXT=10，IRQ_M_EXT=11。

// 异常原因（code/libs/riscv.h）
CAUSE_ILLEGAL_INSTRUCTION=0x2，CAUSE_BREAKPOINT=0x3。

// 时钟与统计（code/kern/trap/trap.c、code/kern/driver/clock.h）
TICK_NUM=100（已有）；SHUTDOWN_AFTER 需新增（本实现取 10）； 
extern volatile size_t ticks;
static void print_ticks(void);        // 已有，输出 "%d ticks\n"（TICK_NUM）
void clock_set_next_event(void);      // 通过 SBI 设定下一次时钟事件；必须调用才能持续收到时钟中断
void sbi_shutdown(void);              // code/libs/sbi.h，关机后不返回

// 输出与调试
void print_trapframe(struct trapframe *tf);  // 已有，打印完整 Trap frame
int cprintf(const char *fmt, ...);           // 已有

// Trap 返回机制
// trapentry.S 的 RESTORE_ALL 会把 tf->status、tf->epc、tf->badvaddr、tf->cause
// 写回对应 CSR，然后执行 sret。因此异常处理应更新 trapframe 中的 tf->epc，
// 不能直接写 sepc 后假定返回流程会跳过当前指令。

// RISC-V 指令长度
// 16 位压缩指令：指令低两位 != 0b11；
// 32 位指令：指令低两位 == 0b11。
// ebreak 为 32 位，c.ebreak 为 16 位。

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
- 由 trap_dispatch() 在 tf->cause 最高位为 1（中断）时调用，tf 指向有效的 trapframe；
- Trap 入口已保存现场，处理函数返回后由统一的 Trap 返回流程恢复现场并执行 sret。

**Post-Condition**:
- 按 tf->cause 去掉最高位后的中断原因完成对应处理，函数返回后 trap() 能正常返回；
- 每收到一次 Supervisor 时钟中断，ticks 恰好增加 1，并且下一次时钟事件已被重新设定；
- 非时钟中断不改变 ticks、不重新设定时钟事件、不推进 tf->epc；
- 无法识别的中断已输出完整 Trap frame 供调试。

**Case 1**：tf->cause 去掉最高位后为 IRQ_S_TIMER。
- 必须先调用 clock_set_next_event() 设定下一次时钟事件，再增加 ticks；
- ticks 每收到一次该中断只能增加一次；
- 当 ticks 是 TICK_NUM 的整数倍时，调用 print_ticks() 输出一次统计信息；
- 当 ticks 达到 SHUTDOWN_AFTER * TICK_NUM 时，输出关机提示（例如 `Shutdown after %d ticks`）并调用 sbi_shutdown()；
- 重新设定下一次时钟事件必须发生在可能耗时或关机的处理之前，减少延迟并保证重装不被跳过；
- sbi_shutdown() 不返回，因此达到关机条件后不再回到普通返回路径。

**Case 2**：tf->cause 去掉最高位后为其他已定义中断原因（用户/监管者/虚拟机/机器模式的软件中断、定时器中断、外部中断）。
- 输出对应的中断类型提示，例如 Supervisor software interrupt；
- 不增加 ticks，不调用 clock_set_next_event()，不修改 tf->epc；
- 处理完成后返回统一 Trap 返回流程。

**Case 3**：tf->cause 去掉最高位后不属于任何已定义中断原因。
- 按 `Unknown interrupt: cause %d` 的格式输出未知中断原因编号；
- 调用 print_trapframe(tf) 输出完整 Trap frame，辅助定位问题；
- 处理完成后返回统一 Trap 返回流程。

**Requirements**:
- 必须清除 scause 的最高位后再匹配 IRQ_* 宏，不能把带符号的负 cause 直接用于分支判断；
- ticks 每收到一次 IRQ_S_TIMER 必须且只能增加一次，不能在非时钟中断路径增加；
- 不能只进行 ticks 计数而不重新设定下一次时钟事件，否则系统只能收到一次时钟中断；
- 达到 SHUTDOWN_AFTER * TICK_NUM 后必须调用框架提供的关机接口，不能只输出信息而继续运行；
- 中断处理不得推进 tf->epc，也不得修改 trapframe 中与中断处理无关的字段。

## exception_handler

**Pre-Condition**:
- 由 trap_dispatch() 在 tf->cause 最高位为 0（同步异常）时调用，tf 指向有效的 trapframe；
- tf->epc 为触发异常的指令地址，且该指令在内存中可读，用于判断指令长度。

**Post-Condition**:
- 非法指令异常和断点异常已按实验规定格式输出信息；
- 两类异常的 tf->epc 已推进到下一条指令的地址；
- 其他异常已通过 print_trapframe(tf) 输出完整 Trap frame，且 tf->epc 保持不变；
- 返回后 __trapret 使用更新后的 tf->epc 写回 sepc，sret 不会重复执行已经触发异常的指令。

**Case 1**：tf->cause == CAUSE_ILLEGAL_INSTRUCTION。
- 按 `Illegal instruction at 0x%08x` 的格式输出非法指令信息，参数为触发异常的地址 tf->epc；
- 读取 tf->epc 处指令的前 16 位，若低两位为 0b11，则指令长度为 4 字节；否则为 2 字节；
- 将 tf->epc 增加实际指令长度，使返回后从下一条指令继续执行。

**Case 2**：tf->cause == CAUSE_BREAKPOINT。
- 按 `Breakpoint at 0x%08x` 的格式输出断点信息，参数为触发异常的地址 tf->epc；
- 同样根据指令编码判断长度：c.ebreak 为 2 字节，ebreak 为 4 字节；
- 将 tf->epc 增加实际指令长度，使返回后从下一条指令继续执行。

**Case 3**：tf->cause 为其他异常原因。
- 调用 print_trapframe(tf) 输出完整 Trap frame，作为兜底处理；
- 不修改 tf->epc，不尝试实现实验未要求的异常恢复逻辑；
- 处理完成后返回统一 Trap 返回流程。

**Requirements**:
- 不得对所有异常使用固定的 +4 偏移，必须结合 RISC-V 指令编码判断 16/32 位长度；
- 必须更新 trapframe 中的 tf->epc，不能只写 sepc 或假定返回时会自动跳过当前指令；
- 非法指令和断点异常处理后，tf->epc 必须落在下一条合法指令边界，避免再次执行同一条异常指令形成异常循环；
- 兜底路径不得改变 tf->epc，避免在未实现恢复逻辑时产生错误跳转；
- 不得修改 tf 中其他寄存器或 CSR 字段。
````

### 2.2 设计说明

- 中断处理必须先清 `scause` 最高位，再匹配 `IRQ_*`；时钟事件是一次性的，每次 `IRQ_S_TIMER` 都要重装。
- 异常处理改的是 `tf->epc`，不是 `sepc`；指令长度按低两位判断 2/4 字节，不能固定 +4。
- spec 按两个入口分别写 Pre/Post、Case 1/2/3 和 Requirements；重点约束时钟重装、`ticks` 只加一次、`epc` 按指令长度推进、未知类型用 `print_trapframe` 兜底。
- `SHUTDOWN_AFTER` 不是框架自带宏，本次实现新增（`#define SHUTDOWN_AFTER 10`）；prompt 的 `[RELY]` 已注明。
- 输出格式以 `tools/grade.sh` 的匹配串为准；指导书只写“规定格式”。
