# Lab 2: System calls

課程頁面：[Lab: System calls](https://pdos.csail.mit.edu/6.S081/2022/labs/syscall.html)

這份文件是 Lab 2 的總筆記。目前已完成 **Using GDB**、**System call tracing** 與 **Sysinfo** 的實作；整份 lab 尚待補上 `answers-syscall.txt` 與 `time.txt`。

```text
Lab 2: System calls
├── 1. Using GDB (easy)       ← 已完成
│   ├── 觀察 syscall call stack
│   ├── 查看 proc、trapframe 與 a7
│   ├── 判斷 trap 前的 CPU mode
│   └── 用 GDB 與 kernel.asm 分析 kernel panic
├── 2. System call tracing    ← 已完成實作與測試
└── 3. Sysinfo                ← 已完成實作與測試
```

---

## 1. Using GDB (easy)

### 1.1 練習目標

這一部分不是要新增 system call，而是先熟悉 kernel 除錯方法：

- 用 GDB 連到 QEMU 中的 xv6。
- 在 `syscall()` 設定 breakpoint。
- 用 backtrace 確認 system call 的 kernel 呼叫路徑。
- 查看目前 process 的 `struct proc` 與 trapframe。
- 讀取 `a7` 中的 syscall number。
- 從 `sstatus` 判斷 trap 前的 CPU privilege mode。
- 故意製造 kernel page fault，再用 `scause`、`sepc`、`stval` 和 `kernel.asm` 找出原因。

### 1.2 本次觀察的 syscall 路徑

xv6 啟動後，第一個 user program `initcode` 會執行 `exec` system call：

```text
user/initcode.S
  li a7, SYS_exec
  ecall
        │
        ▼ trap 進入 kernel
kernel/trap.c
  usertrap()
        │
        ▼ 呼叫
kernel/syscall.c
  syscall()
```

`user/initcode.S` 的關鍵程式碼：

```asm
la a0, init
la a1, argv
li a7, SYS_exec
ecall
```

`a7` 放 syscall number；`a0`、`a1` 放 `exec` 的引數。`ecall` 使 CPU 從 user mode trap 進 kernel，最後由 `usertrap()` 呼叫 `syscall()`。

---

### 1.3 啟動 QEMU 與 GDB

這個練習需要兩個 terminal。

#### Terminal 1：啟動 QEMU GDB server

```sh
cd ~/xv6-labs-2022
make qemu-gdb
```

實際輸出：

```text
*** Now run 'gdb' in another window.
qemu-system-riscv64 -machine virt -bios none -kernel kernel/kernel -m 128M \
  -smp 3 -nographic -global virtio-mmio.force-legacy=false \
  -drive file=fs.img,if=none,format=raw,id=x0 \
  -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0 \
  -S -gdb tcp::26000

xv6 kernel is booting

hart 1 starting
hart 2 starting
```

其中：

- `-S`：QEMU 啟動時先暫停 CPU，等待 GDB。
- `-gdb tcp::26000`：在 TCP port `26000` 開啟 GDB server。

#### Terminal 2：啟動 GDB

```sh
cd ~/xv6-labs-2022
gdb-multiarch
```

啟動時看到：

```text
The target architecture is set to "riscv:rv64".
warning: No executable has been specified and target does not support
determining executable automatically.  Try using the "file" command.
0x0000000000001000 in ?? ()
```

repo 內的 `.gdbinit` 已自動完成主要設定：

```gdb
set architecture riscv:rv64
target remote 127.0.0.1:26000
symbol-file kernel/kernel
```

因此後續 `b syscall` 能找到 source line，代表 `kernel/kernel` 的 symbols 已成功載入。上面的 warning 是連線當下尚未從 target 自動取得 executable，不影響後續操作。

若 `.gdbinit` 沒有生效，可以手動輸入：

```gdb
file kernel/kernel
target remote 127.0.0.1:26000
```

---

### 1.4 題目一：誰呼叫了 `syscall()`？

設定 breakpoint 並繼續執行：

```gdb
(gdb) b syscall
Breakpoint 1 at 0x80002028: file kernel/syscall.c, line 133.
(gdb) c
Continuing.
[Switching to Thread 1.2]

Thread 2 hit Breakpoint 1, syscall () at kernel/syscall.c:133
133     {
```

可用 source layout 查看目前所在位置：

```gdb
(gdb) layout src
```

接著印出 call stack：

```gdb
(gdb) bt
#0  syscall () at kernel/syscall.c:133
#1  0x0000000080001d5c in usertrap () at kernel/trap.c:67
#2  0x0505050505050505 in ?? ()
```

backtrace 由上往下解讀：

- `#0` 是目前停住的 `syscall()`。
- `#1` 是呼叫 `syscall()` 的 `usertrap()`。
- `#2` 不是一般 C function call frame；user mode 是經由 trap 進入 kernel，因此這裡不必把它當成正常的 caller 解讀。

答案：

```text
usertrap() called syscall().
```

對應到 `kernel/trap.c:67`：

```c
syscall();
```

---

### 1.5 題目二：查看 `proc`、trapframe 與 `a7`

先用 `n` 越過 `struct proc *p = myproc();`：

```gdb
(gdb) n
135       struct proc *p = myproc();
(gdb) n
137       num = p->trapframe->a7;
(gdb) n
140       if(num > 0 && num < NELEM(syscalls) && syscalls[num]) {
```

此時 `p` 已經指向目前 process 的 `struct proc`，可以用十六進位印出內容：

```gdb
(gdb) p /x *p
$1 = {
  lock = {locked = 0x0, name = 0x80008178, cpu = 0x0},
  state = 0x4,
  chan = 0x0,
  killed = 0x0,
  xstate = 0x0,
  pid = 0x1,
  parent = 0x0,
  kstack = 0x3fffffd000,
  sz = 0x1000,
  pagetable = 0x87f73000,
  trapframe = 0x87f74000,
  context = {
    ra = 0x800014a4, sp = 0x3fffffde80, s0 = 0x3fffffdeb0,
    s1 = 0x8000b5e0, s2 = 0x8000b1b0, s3 = 0x1,
    s4 = 0x80011468, s5 = 0x3, s6 = 0x8001c280, s7 = 0x1,
    s8 = 0x8001c3a8, s9 = 0x4, s10 = 0x0, s11 = 0x0
  },
  ofile = {0x0 <repeats 16 times>},
  cwd = 0x800196f0,
  name = {0x69, 0x6e, 0x69, 0x74, 0x63, 0x6f, 0x64, 0x65, 0x0, ...}
}
```

重要欄位：

| 欄位 | 值 | 意義 |
|---|---:|---|
| `pid` | `0x1` | process id 是 1 |
| `trapframe` | `0x87f74000` | 保存 user registers 的 trapframe 位址 |
| `name` | `initcode` 的 ASCII | 目前 process 是 `initcode` |

接著查看 trapframe 中保存的 `a7`：

```gdb
(gdb) p /x p->trapframe->a7
$2 = 0x7
```

在 `kernel/syscall.h` 中：

```c
#define SYS_exec 7
```

因此答案是：

```text
p->trapframe->a7 = 0x7, which represents SYS_exec.
```

這正好對應 `user/initcode.S` 的：

```asm
li a7, SYS_exec
ecall
```

---

### 1.6 題目三：trap 前的 CPU mode

目前 CPU 正在 kernel 的 supervisor mode 執行。題目要判斷的是：發生 trap 前，CPU 在哪個 mode？

查看 `sstatus`：

```gdb
(gdb) p /x $sstatus
$3 = 0x200000022
```

`kernel/riscv.h` 定義：

```c
#define SSTATUS_SPP (1L << 8)  // Previous mode, 1=Supervisor, 0=User
```

所以檢查 bit 8，也就是 mask `0x100`：

```gdb
(gdb) p /x $sstatus & 0x100
$4 = 0x0
```

結果 `SPP = 0`，因此答案是：

```text
The previous mode was user mode because the SPP bit was 0.
```

注意「目前 mode」與「previous mode」不同：

| 時間點 | CPU mode |
|---|---|
| 執行 `ecall` 前 | User mode |
| GDB 停在 `syscall()` 時 | Supervisor mode |
| `sstatus.SPP` 記錄的 previous mode | User mode |

---

### 1.7 題目四至六：分析 kernel page fault

#### 1.7.1 故意製造錯誤

暫時把 `kernel/syscall.c` 的：

```c
num = p->trapframe->a7;
```

改成：

```c
num = *(int *)0;
```

這行會要求 kernel 從 virtual address `0` 讀取一個 `int`。

執行：

```sh
make qemu
```

實際輸出：

```text
xv6 kernel is booting

hart 2 starting
hart 1 starting
scause 0x000000000000000d
sepc=0x000000008000203c stval=0x0000000000000000
panic: kerneltrap
```

三個 register 的作用：

| Register | 本次值 | 意義 |
|---|---:|---|
| `scause` | `0xd` | exception 原因是 load page fault |
| `sepc` | `0x8000203c` | 發生 exception 的 instruction address |
| `stval` | `0x0` | load 所存取而發生錯誤的 virtual address |

#### 1.7.2 用 `sepc` 找 faulting instruction

`make` 會用 `objdump` 產生 `kernel/kernel.asm`。用 panic 的 `sepc` 搜尋：

```sh
rg -n "8000203c" kernel/kernel.asm
```

原本使用 `grep` 的實際結果：

```text
4484:    8000203c:      00002683    lw a3,0(zero) # 0 <_entry-0x80000000>
```

faulting instruction 是：

```asm
lw a3,0(zero)
```

RISC-V 的 `zero` register 永遠是 `0`，因此 `0(zero)` 就是 address `0`。`lw` 會從該位址讀取一個 32-bit word，並寫入 `a3`。

所以在這次編譯結果中：

```text
The register corresponding to num is a3.
```

#### 1.7.3 為什麼 kernel crash？

xv6 kernel page table 沒有 mapping virtual address `0`。`lw a3,0(zero)` 嘗試讀取未映射的位址，因此 CPU 產生 load page fault，接著 xv6 的 `kerneltrap()` 印出資訊並 panic。

panic 資訊彼此吻合：

```text
num = *(int *)0
       │
       ├── scause = 0xd  → load page fault
       ├── stval  = 0x0  → 錯誤存取 address 0
       └── sepc   = 0x8000203c
                         └── lw a3,0(zero)
```

#### 1.7.4 用 GDB 在 faulting instruction 前停下

這是目前紀錄中尚未實際執行的確認步驟。保持錯誤程式碼，重新開兩個 terminal。

Terminal 1：

```sh
make qemu-gdb
```

Terminal 2：

```sh
gdb-multiarch
```

在 GDB 使用本次 panic 的 `sepc`：

```gdb
(gdb) b *0x000000008000203c
(gdb) layout asm
(gdb) c
```

停住後可再次確認目前 instruction：

```gdb
(gdb) x/i $pc
```

預期看到：

```asm
lw a3,0(zero)
```

注意：修改程式或重新編譯後，instruction address 可能改變。應以該次 panic 印出的最新 `sepc` 為準。

#### 1.7.5 查看 panic 時的 process

停在 faulting instruction 後輸入：

```gdb
(gdb) p p->name
(gdb) p p->pid
```

根據前面第一次 syscall 的 `p /x *p`，本次預期是：

```text
name = "initcode"
pid = 1
```

這與啟動流程一致：xv6 的第一個 user process `initcode` 正在發出 `SYS_exec`。不過仍應完成上面的 GDB 指令，把「預期值」實際確認一次。

---

### 1.8 作業答案整理

以下內容可整理到課程要求的 `answers-syscall.txt`。

#### Question 1

Looking at the backtrace output, which function called `syscall`?

```text
usertrap() called syscall().
```

#### Question 2

What is the value of `p->trapframe->a7` and what does that value represent?

```text
p->trapframe->a7 is 0x7. It represents SYS_exec, the exec system call
issued by the initial user program in user/initcode.S.
```

#### Question 3

What was the previous mode that the CPU was in?

```text
The previous mode was user mode. Bit SPP (bit 8) of sstatus is 0.
```

#### Question 4

Write down the faulting assembly instruction. Which register corresponds to `num`?

```text
The faulting instruction is "lw a3,0(zero)". In this compiled kernel,
the register corresponding to num is a3.
```

#### Question 5

Why does the kernel crash?

```text
The kernel dereferences virtual address 0, which is not mapped in the
kernel address space. This causes a load page fault. scause is 0xd and
stval is 0x0, confirming both the fault type and the faulting address.
```

#### Question 6

What binary was running when the kernel panicked, and what was its pid?

```text
The expected binary is initcode and its pid is 1. Confirm this at the
faulting breakpoint with "p p->name" and "p p->pid".
```

---

### 1.9 GDB 指令速查

| 指令 | 用途 | 範例 |
|---|---|---|
| `b function` | 在函式設定 breakpoint | `b syscall` |
| `b *address` | 在 instruction address 設 breakpoint | `b *0x8000203c` |
| `c` | 繼續執行到 breakpoint、exception 或程式結束 | `c` |
| `n` | 執行下一行 C；不進入被呼叫的函式 | `n` |
| `s` | 執行下一行 C；會進入被呼叫的函式 | `s` |
| `bt` / `backtrace` | 顯示目前 call stack | `bt` |
| `p expr` | 印出 expression | `p p->pid` |
| `p /x expr` | 用十六進位印出 expression | `p /x $sstatus` |
| `p /x *p` | 印出 pointer 指向的 struct | `p /x *p` |
| `x/i address` | 查看一條 assembly instruction | `x/i $pc` |
| `layout src` | TUI 顯示 C source | `layout src` |
| `layout asm` | TUI 顯示 assembly | `layout asm` |
| `Ctrl-x a` | 開啟或關閉 TUI layout | |
| `q` | 離開 GDB | `q` |

常用 register 在 GDB expression 中要加 `$`：

```gdb
p /x $pc
p /x $sp
p /x $sstatus
```

### 1.10 實驗後恢復程式碼

完成 page-fault 練習後，務必把 `kernel/syscall.c` 恢復為：

```c
num = p->trapframe->a7;
```

否則 xv6 會在第一個 system call 時 panic，無法繼續後面的 Lab 2。

---

## 2. System call tracing

課程難度：**moderate**。狀態：**尚未開始，以下為預定筆記架構**。

### 2.1 題目需求與預期行為

#### `trace(mask)` 的功能

#### trace mask 的 bit 與 syscall number

#### trace 輸出格式

#### child process 繼承規則

### 2.2 實作前：理解現有程式與執行流程

#### `user/trace.c` 如何解析參數並執行目標程式

#### xv6 system call 的 user-to-kernel 路徑

#### 本功能需要修改的檔案

### 2.3 實作過程

#### 2.3.1 將 `_trace` 加入 `Makefile` 的 `UPROGS`

#### 2.3.2 建立 `trace` 的 user-space system call 介面

##### 在 `user/user.h` 宣告 `trace()`

##### 在 `user/usys.pl` 產生 system call stub

##### 在 `kernel/syscall.h` 分配 `SYS_trace`

#### 2.3.3 在 `struct proc` 保存 trace mask

##### 在 `kernel/proc.h` 新增欄位

##### process 建立與重用時的初始值

#### 2.3.4 實作 `sys_trace()`

##### 使用 `argint()` 取得 user argument

##### 將 mask 儲存到目前 process

#### 2.3.5 讓 `fork()` 複製 trace mask

#### 2.3.6 將 `sys_trace()` 接到 syscall dispatch table

##### 宣告 kernel handler

##### 加入 `syscalls[]` function-pointer array

#### 2.3.7 在 `syscall()` 輸出 tracing 結果

##### 建立 syscall number 到名稱的對照表

##### 用 bit mask 判斷是否需要輸出

##### 取得並印出 syscall return value

### 2.4 完整執行流程整理

#### `trace` 指令本身的流程

#### 被追蹤程式發出 system call 的流程

#### trace mask 經過 `fork()` 與 `exec()` 的變化

### 2.5 測試與結果

#### 只追蹤 `read`：`trace 32 grep hello README`

#### 追蹤所有 system calls：`trace 2147483647 grep hello README`

#### 未啟用 tracing 的對照測試：`grep hello README`

#### 驗證 child inheritance：`trace 2 usertests forkforkfork`

#### 執行 `make grade`

### 2.6 遇到的問題與除錯紀錄

#### 編譯或 linking 問題

#### syscall 無法 dispatch 的問題

#### mask 判斷或輸出格式問題

#### WSL 執行測試逾時問題

### 2.7 System call tracing 小結

#### 最終修改檔案一覽

#### 核心觀念整理

---

## 3. Sysinfo

課程難度：**moderate**。狀態：**已完成，`sysinfotest` 通過**。

### 3.1 題目需求與預期行為

這一部分要新增：

```c
int sysinfo(struct sysinfo *info);
```

呼叫者提供一個位於 user space 的 `struct sysinfo` 位址；kernel 蒐集系統資訊後，把結果寫回該結構。成功時回傳 `0`，user pointer 無效或複製失敗時回傳 `-1`。

#### `freemem` 與 `nproc` 的定義

題目指定兩個欄位：

```c
struct sysinfo {
  uint64 freemem;   // amount of free memory (bytes)
  uint64 nproc;     // number of process
};
```

- `freemem` 是目前 free physical pages 的總 byte 數，不是 page 數。
- `nproc` 是 process table 中 `state != UNUSED` 的 slot 數量。

`nproc` 因此會計入 `USED`、`SLEEPING`、`RUNNABLE`、`RUNNING` 與 `ZOMBIE`；只有 `UNUSED` 不計。

#### `sysinfotest` 的通過條件

提供的 `user/sysinfotest.c` 主要驗證：

1. 一般的 `sysinfo(&info)` 能成功。
2. 無效的 user address 會讓 syscall 回傳 `-1`，kernel 不會直接 crash。
3. 配置與釋放 page 後，`freemem` 會按照 `PGSIZE` 改變。
4. `fork()` 建立 child 後 `nproc` 增加一，child 被 `wait()` 回收後恢復。

成功時輸出：

```text
sysinfotest: start
sysinfotest: OK
```

### 3.2 實作前：理解資料結構與資料流

#### `kernel/sysinfo.h` 的 `struct sysinfo`

`kernel/sysinfo.h` 已由 lab 提供：

```c
struct sysinfo {
  uint64 freemem;
  uint64 nproc;
};
```

同一份 layout 會被 user test 與 kernel handler 使用，才能正確解讀複製的 16 bytes。

#### kernel 如何將資料寫回 user space

user program 呼叫：

```c
struct sysinfo info;
sysinfo(&info);
```

`&info` 是 user virtual address。進入 kernel 後，即使這個值以 `uint64` 保存，kernel 也不能把它當成普通 kernel pointer 直接 dereference；必須用目前 process 的 page table 翻譯並檢查該位址。

本題的資料流是：

```text
user/sysinfotest.c
  sysinfo(&info)
        │ a0 = &info，a7 = SYS_sysinfo
        ▼
user/usys.S
  ecall
        ▼
kernel/syscall.c
  syscalls[SYS_sysinfo]()
        ▼
kernel/sysproc.c
  sys_sysinfo()
        ├── freemem()
        ├── nproc()
        └── copyout(current process pagetable, user address, kernel info)
        ▼
user 的 info 得到結果
```

#### 本功能需要修改的檔案

| 檔案 | 用途 |
|---|---|
| `Makefile` | 編譯 `_sysinfotest` 並放進 `fs.img` |
| `user/user.h` | 宣告 user API 與 `struct sysinfo` tag |
| `user/usys.pl` | 產生執行 `ecall` 的 user stub |
| `kernel/syscall.h` | 分配 syscall number |
| `kernel/syscall.c` | 將 number 對應到 `sys_sysinfo()` |
| `kernel/sysproc.c` | 實作 kernel syscall handler |
| `kernel/kalloc.c` | 計算 free physical memory |
| `kernel/proc.c` | 計算使用中的 process slots |
| `kernel/defs.h` | 宣告 kernel helper functions |

### 3.3 建立 `sysinfo` 的 system call 介面

#### 3.3.1 將 `_sysinfotest` 加入 `Makefile` 的 `UPROGS`

```make
$U/_trace\
$U/_sysinfotest\
```

`user/sysinfotest.c` 存在於 host filesystem，不代表 xv6 shell 能直接執行它。加入 `UPROGS` 後，Makefile 才會編譯 `user/_sysinfotest`，並由 `mkfs` 把它放進 xv6 的 `fs.img`。

若漏掉這步，xv6 shell 會顯示：

```text
exec sysinfotest failed
```

#### 3.3.2 在 `user/user.h` 預先宣告 struct 與函式

```c
struct sysinfo;
int sysinfo(struct sysinfo *);
```

第一行是 forward declaration，表示 `struct sysinfo` 這個 tag 存在。函式參數只保存 pointer，所以這裡不需要知道 struct 的完整欄位。

真正需要宣告變數或讀取欄位的程式，仍要 include `kernel/sysinfo.h`：

```c
#include "kernel/sysinfo.h"
#include "user/user.h"
```

#### 3.3.3 在 `user/usys.pl` 產生 system call stub

```perl
entry("sysinfo");
```

`make` 會用 `usys.pl` 產生概念上如下的 assembly：

```asm
.global sysinfo
sysinfo:
  li a7, SYS_sysinfo
  ecall
  ret
```

呼叫 `sysinfo(&info)` 時，RISC-V calling convention 已把第一個參數放入 `a0`；stub 再把 syscall number 放入 `a7`，然後用 `ecall` 進入 kernel。回到 user space 時，`a0` 保存 syscall return value。

#### 3.3.4 在 `kernel/syscall.h` 分配 `SYS_sysinfo`

```c
#define SYS_sysinfo 23
```

這個數字必須唯一，並與 user stub 使用的名稱一致。

#### 3.3.5 將 `sys_sysinfo()` 接到 syscall dispatch table

先在 `kernel/syscall.c` 宣告外部函式：

```c
extern uint64 sys_sysinfo(void);
```

再加入 function-pointer table：

```c
[SYS_sysinfo] sys_sysinfo,
```

因為 trace 功能會把 syscall number 轉換成名稱，也同步加入：

```c
[SYS_sysinfo] = "sysinfo",
```

當 `syscall()` 從 trapframe 的 `a7` 取得 `23` 時：

```c
p->trapframe->a0 = syscalls[num]();
```

實際效果就是呼叫 `sys_sysinfo()`，並把回傳值寫回 `a0`。

### 3.4 計算 free memory

#### 閱讀 `kernel/kalloc.c` 的 free list

xv6 的 physical memory allocator 以 linked list 保存所有 free pages：

```c
struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;
```

每個 free page 的開頭被當成 `struct run` 使用，所以每個 node 就代表一個大小為 `PGSIZE` 的 free physical page：

```text
kmem.freelist
      │
      ▼
  free page ──> free page ──> free page ──> 0
```

#### 實作 free page 計數函式

在 `kernel/kalloc.c` 加入：

```c
uint64
freemem(void)
{
  uint64 npage = 0;
  struct run *r;

  acquire(&kmem.lock);
  for(r = kmem.freelist; r != 0; r = r->next)
    npage++;
  release(&kmem.lock);

  return npage * PGSIZE;
}
```

#### 將 page 數換算成 byte 數

題目要求的是 bytes，而 allocator 的一個 node 代表一個 page。因此最後必須乘上 `PGSIZE`：

```c
return npage * PGSIZE;
```

例如目前有 100 個 free pages：

```text
100 × 4096 = 409600 bytes
```

#### lock 與 concurrency 注意事項

`kalloc()` 會從 free list 移除 node，`kfree()` 會把 node 放回 free list。兩者都用 `kmem.lock` 保護 linked list。

`freemem()` 必須在整段 traversal 期間持有同一把 lock。若只讀取卻不加鎖，其他 CPU 可能同時改變 `r->next`，導致統計不一致，甚至沿著已改變的 linked list 繼續走訪。

這裡只在計數時持鎖；乘法和 return 不需要保護，所以先 release 再計算結果。

### 3.5 計算 process 數量

#### 閱讀 `kernel/proc.c` 的 process table

xv6 使用固定大小的陣列保存 process：

```c
struct proc proc[NPROC];
```

每個 slot 都有狀態：

```c
enum procstate { UNUSED, USED, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };
```

#### 計算 state 不等於 `UNUSED` 的 process

在 `kernel/proc.c` 加入：

```c
uint64
nproc(void)
{
  uint64 count = 0;
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state != UNUSED)
      count++;
    release(&p->lock);
  }

  return count;
}
```

迴圈會檢查 `proc[0]` 到 `proc[NPROC - 1]`。`p < &proc[NPROC]` 中的 `&proc[NPROC]` 是陣列結尾後一格，只用於比較，不會 dereference。

#### process lock 注意事項

`kernel/proc.h` 在 `state` 上方明確註明，使用它時必須持有 `p->lock`。process 可能同時由其他 CPU 建立、排程、睡眠、結束或回收，因此不能在沒有鎖的情況下讀取 `state`。

本實作每次只鎖一個 process slot，讀完立即釋放：

```c
acquire(&p->lock);
// read p->state
release(&p->lock);
```

不需要同時鎖住全部 `NPROC` 個 slots，也不需要額外建立一把全域 process-table lock。

最後在 `kernel/defs.h` 宣告兩個 helper：

```c
// kalloc.c
uint64          freemem(void);

// proc.c
uint64          nproc(void);
```

這讓 `sysproc.c` 在編譯時知道函式名稱、參數與回傳型別；linker 再把呼叫連到 `kalloc.o` 與 `proc.o` 中的實作。

### 3.6 實作 `sys_sysinfo()`

#### 使用 `argaddr()` 取得 user virtual address

先讓 `kernel/sysproc.c` 看得到完整 struct 定義：

```c
#include "sysinfo.h"
```

handler 的第一步是取得第 0 個 syscall argument：

```c
uint64 uaddr;
argaddr(0, &uaddr);
```

`argaddr(0, ...)` 最終會讀取 trapframe 的 `a0`。它只取出 address 數值，不會在此刻完整驗證 user memory；有效性由稍後的 `copyout()` 檢查。

#### 在 kernel 填入 `struct sysinfo`

```c
struct sysinfo info;

info.freemem = freemem();
info.nproc = nproc();
```

`info` 是真正配置在 kernel stack 上的區域物件。不能改成以下寫法：

```c
struct sysinfo *info;
info->freemem = freemem();
```

上面的 pointer 沒有指向已配置的物件，dereference 會寫入未知位址。另外，`sizeof(info)` 在 pointer 版本只會得到 pointer 大小，而不是 `struct sysinfo` 大小。

#### 使用 `copyout()` 複製到 user space

完整 handler：

```c
uint64
sys_sysinfo(void)
{
  uint64 uaddr;
  struct sysinfo info;
  struct proc *p = myproc();

  argaddr(0, &uaddr);
  info.freemem = freemem();
  info.nproc = nproc();

  if(copyout(p->pagetable, uaddr, (char *)&info, sizeof(info)) < 0)
    return -1;

  return 0;
}
```

四個 `copyout()` 參數分別是：

| 參數 | 意義 |
|---|---|
| `p->pagetable` | 目前 process 的 user page table |
| `uaddr` | user space 的目的 virtual address |
| `(char *)&info` | kernel space 的來源位址 |
| `sizeof(info)` | 要複製的完整 struct 大小 |

`copyout()` 會依 page table 將 user virtual address 翻譯成 physical address，也會檢查 mapping 與寫入權限。資料若跨越 page boundary，它會分段處理。

#### success 與 error return value

- `copyout()` 成功：`sys_sysinfo()` 回傳 `0`。
- user pointer 無效、沒有 mapping 或不可寫：`copyout()` 回傳負值，`sys_sysinfo()` 回傳 `-1`。

`syscall()` 會把這個值存進 trapframe 的 `a0`；返回 user mode 後，`sysinfo()` 的 C 呼叫者便取得該回傳值。

### 3.7 完整執行流程整理

#### user pointer 如何經過 trapframe 傳入 kernel

```text
sysinfo(&info)
   │
   ├── a0 = &info
   ├── a7 = SYS_sysinfo
   └── ecall
          ▼
      usertrap()
          ▼
      syscall()
          ├── num = trapframe->a7
          └── syscalls[num]()
                    ▼
               sys_sysinfo()
                    └── argaddr(0) 讀取 trapframe->a0
```

#### `sys_sysinfo()` 如何蒐集兩項資料

```text
freemem()
  lock kmem.lock
  count kmem.freelist nodes
  unlock
  return pages × PGSIZE

nproc()
  for each proc slot
    lock p->lock
    count if state != UNUSED
    unlock
```

兩項資訊分別在各自資料結構的 owner module 中計算：memory allocator 的資料留在 `kalloc.c`，process table 的資料留在 `proc.c`。`sysproc.c` 只負責組合結果與 syscall 邊界處理。

`freemem` 與 `nproc` 並非全系統的原子快照，因為兩者分開取得鎖；題目只要求回報呼叫期間觀察到的值，不要求兩個欄位在同一瞬間擷取。

#### `copyout()` 如何跨越 kernel/user address space

```text
kernel local struct info
        │ source: &info
        │
        │ copyout(p->pagetable, uaddr, ...)
        ▼
user virtual address uaddr
        │
        ▼
user local struct info
```

`argaddr()` 負責取出 address，`copyout()` 才負責依 user page table 安全地寫入。把兩者分開是 xv6 syscall handler 的常見模式。

### 3.8 測試與結果

#### 編譯與啟動 xv6

先做乾淨建置：

```sh
make clean
make qemu
```

本次完整編譯成功，包含：

```text
user/_sysinfotest
```

#### 執行 `sysinfotest`

在 xv6 shell 執行：

```sh
sysinfotest
```

#### 檢查 `sysinfotest: OK`

也可以只執行 grader 中的 sysinfo 測試：

```sh
make grade GRADEFLAGS=sysinfotest
```

本次實際結果：

```text
== Test sysinfotest == sysinfotest: OK
```

#### 執行 `make grade`

完成 `answers-syscall.txt` 與 `time.txt` 後，再執行：

```sh
make grade
```

`sysinfotest` 通過只代表 Sysinfo 實作正確；整份 lab 的總分仍包含 GDB answers、trace tests 與 time file。

### 3.9 遇到的問題與除錯紀錄

#### struct 宣告或編譯問題

`user/user.h` 只需要 forward declaration：

```c
struct sysinfo;
int sysinfo(struct sysinfo *);
```

`kernel/sysproc.c` 要建立真正的 `struct sysinfo info`，因此必須 include 完整定義：

```c
#include "sysinfo.h"
```

只宣告 `struct sysinfo *info` 不會配置 struct 本體；未初始化 pointer 不能直接用 `->` 寫欄位。

#### free memory 計算錯誤

常見原因：

- 回傳 free page 數，忘記乘 `PGSIZE`。
- 沒有取得 `kmem.lock` 就走訪 free list。
- 在 release lock 之後繼續讀取 `r->next`。

#### process 數量計算錯誤

常見原因：

- 只計算 `RUNNING`，但題目要求所有非 `UNUSED` 狀態。
- 把 `ZOMBIE` 排除，但 zombie slot 尚未回到 `UNUSED`，所以仍需計入。
- 沒有持有 `p->lock` 就讀取 `p->state`。
- child exit 後沒有理解 `wait()` 才會完成回收並讓 slot 回到 `UNUSED`。

#### `copyout()` 失敗或 user address 錯誤

常見原因：

- 把 user address 直接 cast 成 kernel pointer 並 dereference。
- `copyout()` 的 source 與 destination 寫反。
- 使用 `sizeof(pointer)`，只複製 8 bytes。
- 忽略 `copyout()` 的錯誤回傳值，導致無效 user pointer 看似成功。
- 忘記用目前 process 的 `p->pagetable`。

### 3.10 Sysinfo 小結

#### 最終修改檔案一覽

| 檔案 | 最終變更 |
|---|---|
| `Makefile` | 加入 `$U/_sysinfotest` |
| `user/user.h` | 宣告 `struct sysinfo` 與 `sysinfo()` |
| `user/usys.pl` | 加入 `entry("sysinfo")` |
| `kernel/syscall.h` | 定義 `SYS_sysinfo` |
| `kernel/syscall.c` | 宣告、dispatch 並命名 `sys_sysinfo` |
| `kernel/sysproc.c` | 蒐集資料並用 `copyout()` 回傳 struct |
| `kernel/kalloc.c` | 在 lock 保護下計算 free memory bytes |
| `kernel/proc.c` | 在每個 process lock 保護下計算 process 數量 |
| `kernel/defs.h` | 宣告 `freemem()` 與 `nproc()` |

#### 核心觀念整理

1. system call 的 user pointer 只是 user virtual address，kernel 必須透過 `copyin()`／`copyout()` 存取。
2. `argaddr()` 取出 pointer argument；真正的 address validation 由後續 copy function 完成。
3. 若要填入一個 struct，應建立真正的 struct 物件，不能只宣告未初始化 pointer。
4. `sizeof(info)` 是 struct 大小；`sizeof(pointer)` 只是 pointer 大小。
5. 即使函式只讀取共享資料，若資料可能被其他 CPU 同時修改，仍要遵守原資料結構的 locking discipline。
6. free-list node 數量乘上 `PGSIZE`，才是題目要求的 free memory bytes。
7. `nproc` 計算所有 `state != UNUSED` 的 process slots，包含 zombie。
8. helper function 放在擁有資料的 module，syscall handler 負責組合資料與跨 user/kernel 邊界。
