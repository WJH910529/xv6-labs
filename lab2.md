# Lab 2: System calls

課程頁面：[Lab: System calls](https://pdos.csail.mit.edu/6.S081/2022/labs/syscall.html)

這份文件是 Lab 2 的總筆記。目前只完成第一部分 **Using gdb (easy)**；之後實作 `trace` 與 `sysinfo` 時，再分別補進第二、第三部分。

```text
Lab 2: System calls
├── 1. Using GDB (easy)       ← 目前進度
│   ├── 觀察 syscall call stack
│   ├── 查看 proc、trapframe 與 a7
│   ├── 判斷 trap 前的 CPU mode
│   └── 用 GDB 與 kernel.asm 分析 kernel panic
├── 2. System call tracing    ← 尚未開始
└── 3. Sysinfo                ← 尚未開始
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

課程難度：**moderate**。狀態：**尚未開始，以下為預定筆記架構**。

### 3.1 題目需求與預期行為

#### `sysinfo(struct sysinfo *)` 的功能

#### `freemem` 與 `nproc` 的定義

#### `sysinfotest` 的通過條件

### 3.2 實作前：理解資料結構與資料流

#### `kernel/sysinfo.h` 的 `struct sysinfo`

#### kernel 如何將資料寫回 user space

#### 本功能需要修改的檔案

### 3.3 建立 `sysinfo` 的 system call 介面

#### 3.3.1 將 `_sysinfotest` 加入 `Makefile` 的 `UPROGS`

#### 3.3.2 在 `user/user.h` 預先宣告 struct 與函式

#### 3.3.3 在 `user/usys.pl` 產生 system call stub

#### 3.3.4 在 `kernel/syscall.h` 分配 `SYS_sysinfo`

#### 3.3.5 將 `sys_sysinfo()` 接到 syscall dispatch table

### 3.4 計算 free memory

#### 閱讀 `kernel/kalloc.c` 的 free list

#### 實作 free page 計數函式

#### 將 page 數換算成 byte 數

#### lock 與 concurrency 注意事項

### 3.5 計算 process 數量

#### 閱讀 `kernel/proc.c` 的 process table

#### 計算 state 不等於 `UNUSED` 的 process

#### process lock 注意事項

### 3.6 實作 `sys_sysinfo()`

#### 使用 `argaddr()` 取得 user virtual address

#### 在 kernel 填入 `struct sysinfo`

#### 使用 `copyout()` 複製到 user space

#### success 與 error return value

### 3.7 完整執行流程整理

#### user pointer 如何經過 trapframe 傳入 kernel

#### `sys_sysinfo()` 如何蒐集兩項資料

#### `copyout()` 如何跨越 kernel/user address space

### 3.8 測試與結果

#### 編譯與啟動 xv6

#### 執行 `sysinfotest`

#### 檢查 `sysinfotest: OK`

#### 執行 `make grade`

### 3.9 遇到的問題與除錯紀錄

#### struct 宣告或編譯問題

#### free memory 計算錯誤

#### process 數量計算錯誤

#### `copyout()` 失敗或 user address 錯誤

### 3.10 Sysinfo 小結

#### 最終修改檔案一覽

#### 核心觀念整理
