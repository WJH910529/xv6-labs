# [Lab 2] System Call Tracing

課程題目：[MIT 6.S081 2022 - Lab: System calls](https://pdos.csail.mit.edu/6.S081/2022/labs/syscall.html)

難度：`moderate`

完成狀態：System call tracing 的四個 grader tests 全部通過。

這份筆記從零開始記錄如何在 xv6 新增 `trace(mask)` system call。除了列出要修改的程式，也會解釋每個檔案負責什麼、資料如何從 user space 進入 kernel，以及為什麼要把 trace mask 放進 `struct proc`。

---

## 1. 題目要做什麼？

目標是新增：

```c
int trace(int mask);
```

process 呼叫 `trace(mask)` 後，kernel 要記住這個 mask。此 process 後續每次執行 system call，kernel 都會檢查對應的 bit；如果該 bit 是 `1`，就在 system call 即將返回 user space 前印出：

```text
process-id: syscall syscall-name -> return-value
```

例如：

```sh
trace 32 grep hello README
```

輸出：

```text
3: syscall read -> 1023
3: syscall read -> 961
3: syscall read -> 321
3: syscall read -> 0
```

題目還有兩個重要條件：

1. trace 設定只影響呼叫它的 process，不能影響其他 processes。
2. 這個 process 後來 `fork()` 出來的 children 必須繼承相同設定。

這題不是讓 `trace()` 主動呼叫或包住其他 system call。實際分工是：

```text
trace(mask)
    └── 只負責把 mask 記錄到目前 process

syscall()
    └── 每次 system call 完成後，負責檢查 mask 並輸出紀錄
```

---

## 2. 先理解 trace mask

### 2.1 Syscall number 與 mask 不一樣

`kernel/syscall.h` 為每個 system call 分配一個編號：

```c
#define SYS_fork   1
#define SYS_read   5
#define SYS_write 16
```

這些數字是 syscall number，也是 mask 中對應的 bit 位置。

要追蹤 `read`，不是直接把 mask 設為 `5`，而是把第 5 bit 設為 `1`：

```c
1 << SYS_read
= 1 << 5
= 32
```

因此：

```sh
trace 32 grep hello README
```

表示只追蹤 `SYS_read`。

### 2.2 為什麼 `trace 5` 不是追蹤 read？

十進位 `5` 的二進位是：

```text
5 = 0000 0101
         │ │
         │ └── bit 0 = 1
         └──── bit 2 = 1
```

所以 mask `5` 表示選擇 bit 0 和 bit 2，不是選擇 syscall number 5。

要選擇 syscall number 5，必須使用：

```text
1 << 5 = 32
```

### 2.3 同時追蹤多個 system calls

bit mask 可以同時選擇多個項目。例如同時追蹤 `read` 和 `write`：

```c
(1 << SYS_read) | (1 << SYS_write)
= (1 << 5) | (1 << 16)
= 32 | 65536
= 65568
```

檢查某個 syscall 是否被選取：

```c
if(mask & (1 << num)) {
  // num 對應的 bit 是 1
}
```

### 2.4 為什麼全部追蹤使用 2147483647？

```text
2147483647 = 2^31 - 1 = 0x7fffffff
```

它的低 31 bits 全部都是 `1`：

```text
01111111111111111111111111111111
```

目前 xv6 的 syscall numbers 都在這個範圍，因此：

```sh
trace 2147483647 grep hello README
```

就會追蹤所有 system calls。

---

## 3. 完整架構與資料流

### 3.1 新增 system call 的完整路徑

```text
user/trace.c
    │ 呼叫 trace(mask)
    ▼
user/user.h
    │ 提供 C 函式宣告
    ▼
user/usys.S
    │ li a7, SYS_trace
    │ ecall
    ▼
kernel/trap.c:usertrap()
    │
    ▼
kernel/syscall.c:syscall()
    │ syscalls[SYS_trace]
    ▼
kernel/sysproc.c:sys_trace()
    │ argint(0, &mask)
    ▼
myproc()->trace_mask = mask
```

### 3.2 之後追蹤其他 system calls 的流程

```text
被追蹤的 process 呼叫 read()
    │
    ▼
user read stub
    │ a7 = SYS_read
    │ ecall
    ▼
kernel syscall()
    │ num = trapframe->a7
    │ 呼叫 sys_read()
    │ 將 return value 放進 trapframe->a0
    │ 檢查 trace_mask 的第 num bit
    ▼
符合時印出 pid、名稱、return value
```

### 3.3 這次修改的檔案

| 檔案 | 作用 |
|---|---|
| `Makefile` | 將課程提供的 `trace` user program 放入 xv6 file system |
| `user/user.h` | 宣告 user 可呼叫的 `trace(int)` |
| `user/usys.pl` | 產生 `trace` 的 assembly syscall stub |
| `kernel/syscall.h` | 分配 `SYS_trace` syscall number |
| `kernel/proc.h` | 在每個 process 保存自己的 trace mask |
| `kernel/proc.c` | 初始化 mask，並在 `fork()` 時複製給 child |
| `kernel/sysproc.c` | 實作 kernel handler `sys_trace()` |
| `kernel/syscall.c` | 註冊 handler、保存名稱並輸出 tracing 結果 |

`user/trace.c` 是課程提供的測試程式，本次不需要自行重寫。

---

## 4. 讀懂課程提供的 `user/trace.c`

核心程式：

```c
int
main(int argc, char *argv[])
{
  int i;
  char *nargv[MAXARG];

  if(argc < 3 || (argv[1][0] < '0' || argv[1][0] > '9')){
    fprintf(2, "Usage: %s mask command\n", argv[0]);
    exit(1);
  }

  if (trace(atoi(argv[1])) < 0) {
    fprintf(2, "%s: trace failed\n", argv[0]);
    exit(1);
  }

  for(i = 2; i < argc && i < MAXARG; i++){
    nargv[i-2] = argv[i];
  }
  exec(nargv[0], nargv);
  exit(0);
}
```

假設輸入：

```sh
trace 32 grep hello README
```

shell 建立的 arguments 是：

```text
argv[0] = "trace"
argv[1] = "32"
argv[2] = "grep"
argv[3] = "hello"
argv[4] = "README"
```

先執行：

```c
trace(atoi(argv[1]));
```

`atoi("32")` 會得到整數 `32`，因此目前 process 的 trace mask 被設為 32。

接著把 command 部分整理為：

```text
nargv[0] = "grep"
nargv[1] = "hello"
nargv[2] = "README"
```

最後：

```c
exec(nargv[0], nargv);
```

把目前 process 執行的程式換成 `grep`。

`exec()` 不會建立新 process，也不會更換 `struct proc`，所以先前保存在 `trace_mask` 的 `32` 仍然存在：

```text
執行 trace 時：pid = 3, trace_mask = 32
        │
        ▼ exec("grep", ...)
執行 grep 時： pid = 3, trace_mask = 32
```

---

## 5. Step 1：將 trace program 加入 xv6

### 5.1 修改 `Makefile`

在 `UPROGS` 加入：

```makefile
UPROGS=\
	$U/_grind\
	$U/_wc\
	$U/_zombie\
	$U/_trace\
```

意義：

- `$U` 是 `user` 目錄。
- `_trace` 是要產生的 user executable。
- `mkfs` 最後會把它放進 xv6 的 `fs.img`。
- 行尾 `\` 是 Makefile 的續行符號。

### 5.2 第一次編譯

執行：

```sh
make qemu
```

當時看到：

```text
user/trace.c:17:7: error: implicit declaration of function 'trace'
   17 |   if (trace(atoi(argv[1])) < 0) {
      |       ^~~~~
cc1: all warnings being treated as errors
```

這表示 `Makefile` 已成功要求 compiler 編譯 `user/trace.c`，但 compiler 尚未看過 `trace()` 的函式宣告。

此時的狀態：

```text
trace.c ──compiler──> 失敗

原因：不知道 trace() 的參數與回傳型別
```

---

## 6. Step 2：宣告 user-space `trace()`

在 `user/user.h` 的 system call declarations 加入：

```c
int trace(int);
```

這個宣告告訴 compiler：

```text
函式名稱：trace
參數：一個 int mask
回傳值：int
```

但宣告不等於實作。它只能讓 compiler 完成 `trace.c -> trace.o`，還沒有任何 `.o` 真正提供 `trace` symbol。

再次執行：

```sh
make qemu
```

`trace.c` 成功編譯：

```text
-c -o user/trace.o user/trace.c
```

接著 linker 失敗：

```text
user/trace.o: in function `main':
user/trace.c:17:(.text+0x52): undefined reference to `trace'
```

這個錯誤清楚區分 compiler 和 linker：

```text
user/user.h 有宣告
        │
        ▼
compiler 知道如何編譯 trace(...) 呼叫
        │
        ▼
產生 user/trace.o
        │
        ▼
linker 找不到提供 trace symbol 的 .o
```

下一步必須建立真正的 assembly stub。

---

## 7. Step 3：建立 syscall number 與 assembly stub

### 7.1 分配 `SYS_trace`

在 `kernel/syscall.h` 加入：

```c
#define SYS_trace 22
```

目前原本最後一個編號是：

```c
#define SYS_close 21
```

因此使用下一個未使用的 `22`。

這裡要分清楚兩種數字：

```text
SYS_trace = 22
    用途：告訴 kernel 本次 ecall 要呼叫 trace system call

trace mask = 32
    用途：告訴 trace 之後要追蹤 SYS_read
```

### 7.2 在 `user/usys.pl` 加入 entry

```perl
entry("trace");
```

`usys.pl` 是 generator。執行 `make` 時，它會產生 `user/usys.S`，內容相當於：

```asm
.global trace
trace:
  li a7, SYS_trace
  ecall
  ret
```

每一行的作用：

```text
.global trace
    將 trace symbol 公開，讓 linker 可以找到

li a7, SYS_trace
    把 syscall number 22 放進 a7

ecall
    從 user mode trap 進 kernel

ret
    system call 完成後返回 trace.c
```

`trace()` 的第一個 C argument 會依 RISC-V calling convention 放在 `a0`。因此進入 kernel 時：

```text
a0 = mask
a7 = SYS_trace
```

### 7.3 第二個階段性結果

重新編譯後，linker 已經可以從 `user/usys.o` 找到 `trace`，所以 xv6 能正常啟動。

執行：

```sh
trace 32 grep hello README
```

當時得到：

```text
3 trace: unknown sys call 22
trace: trace failed
```

這個結果反而證明 user-to-kernel 路徑已接通：

```text
trace.c
  └── 成功呼叫 trace assembly stub
        └── a7 = 22
              └── ecall
                    └── kernel syscall() 收到 num = 22
```

問題只剩 kernel 的 dispatch table 還不認識 `22`。

---

## 8. Step 4：在 process 保存 trace mask

### 8.1 為什麼要放在 `struct proc`？

題目要求：

- 每個 process 可以有自己的 tracing 設定。
- 設定不能影響其他 processes。
- children 要繼承 parent 的設定。

因此 mask 是 per-process state，最合理的位置就是 `struct proc`。

如果使用單一 global variable：

```c
int global_trace_mask;
```

那麼任一 process 呼叫 `trace()` 都會改變整個系統的設定，違反題目要求。

### 8.2 修改 `kernel/proc.h`

在 `struct proc` 加入：

```c
int trace_mask;              // System calls selected for tracing
```

現在每個 process 都各自擁有：

```text
process A: trace_mask = 32
process B: trace_mask = 0
process C: trace_mask = 2147483647
```

### 8.3 初始化 mask

在 `kernel/proc.c:allocproc()` 初始化：

```c
found:
  p->pid = allocpid();
  p->state = USED;
  p->trace_mask = 0;
```

`struct proc` slots 會被重複使用。新 process 的 mask 應預設為 `0`，代表不追蹤任何 system call，也避免拿到上一個 process 留下的設定。

---

## 9. Step 5：實作 kernel handler `sys_trace()`

在 `kernel/sysproc.c` 加入：

```c
uint64
sys_trace(void)
{
  int mask;

  argint(0, &mask);
  myproc()->trace_mask = mask;
  return 0;
}
```

### 9.1 `argint(0, &mask)` 在做什麼？

user 呼叫：

```c
trace(32);
```

依 RISC-V calling convention，第一個 argument 在 register `a0`。發生 trap 時，xv6 將 user registers 保存到目前 process 的 trapframe。

`argint()` 的內部路徑是：

```text
argint(0, &mask)
    │
    ▼
argraw(0)
    │
    ▼
myproc()->trapframe->a0
    │
    ▼
mask = 32
```

此 xv6 版本的 `argint()` 回傳型別是 `void`，因此不需要寫：

```c
if(argint(0, &mask) < 0)
```

### 9.2 為什麼回傳 0？

`trace()` 成功設定 mask 後回傳 `0`。`user/trace.c` 會檢查：

```c
if(trace(...) < 0) {
  fprintf(2, "trace failed\n");
}
```

回傳 `0` 表示設定成功，不會進入 error branch。

---

## 10. Step 6：將 `sys_trace()` 註冊到 dispatcher

### 10.1 宣告 kernel handler

在 `kernel/syscall.c` 加入：

```c
extern uint64 sys_trace(void);
```

這讓 `syscall.c` 知道 `sys_trace()` 實作在其他 `.c` 檔案。

### 10.2 加入 function-pointer array

```c
static uint64 (*syscalls[])(void) = {
  // ...
  [SYS_close]   sys_close,
  [SYS_trace]   sys_trace,
};
```

這個 array 的型別是：

```text
array of pointers to functions
  taking no C arguments
  returning uint64
```

`syscall()` 先取得：

```c
num = p->trapframe->a7;
```

若 `num = SYS_trace = 22`：

```c
syscalls[num]()
```

就等於呼叫：

```c
sys_trace()
```

### 10.3 第三個階段性結果

此時執行：

```sh
trace 32 grep hello README
```

得到：

```text
$ trace 32 grep hello README
$
```

這不是失敗，而是代表：

1. `trace(32)` 已成功回傳 `0`。
2. `exec("grep", ...)` 已成功。
3. `trace_mask` 已經保存。
4. 目前還沒有加入輸出 tracing result 的程式。

---

## 11. Step 7：建立 syscall 名稱表

`syscall()` 目前只有 syscall number，但題目要求印出名稱，因此加入：

```c
static char *syscall_names[] = {
  [SYS_fork]    = "fork",
  [SYS_exit]    = "exit",
  [SYS_wait]    = "wait",
  [SYS_pipe]    = "pipe",
  [SYS_read]    = "read",
  [SYS_kill]    = "kill",
  [SYS_exec]    = "exec",
  [SYS_fstat]   = "fstat",
  [SYS_chdir]   = "chdir",
  [SYS_dup]     = "dup",
  [SYS_getpid]  = "getpid",
  [SYS_sbrk]    = "sbrk",
  [SYS_sleep]   = "sleep",
  [SYS_uptime]  = "uptime",
  [SYS_open]    = "open",
  [SYS_write]   = "write",
  [SYS_mknod]   = "mknod",
  [SYS_unlink]  = "unlink",
  [SYS_link]    = "link",
  [SYS_mkdir]   = "mkdir",
  [SYS_close]   = "close",
  [SYS_trace]   = "trace",
};
```

使用 designated initializer 的好處是 index 直接等於 syscall number：

```text
syscall_names[SYS_read]
= syscall_names[5]
= "read"
```

不要使用依賴連續編號的 `syscall_names[num - 1]`。如果未來 syscall numbers 中間有空洞，名稱就可能錯位。

---

## 12. Step 8：在 syscall 完成後輸出紀錄

原本的核心程式：

```c
p->trapframe->a0 = syscalls[num]();
```

它完成兩件事：

1. 呼叫真正的 kernel handler。
2. 將 handler return value 保存到 trapframe 的 `a0`。

在後面加入：

```c
if(p->trace_mask & (1 << num)) {
  printf("%d: syscall %s -> %d\n",
         p->pid, syscall_names[num], (int)p->trapframe->a0);
}
```

完整區塊：

```c
if(num > 0 && num < NELEM(syscalls) && syscalls[num]) {
  p->trapframe->a0 = syscalls[num]();

  if(p->trace_mask & (1 << num)) {
    printf("%d: syscall %s -> %d\n",
           p->pid, syscall_names[num], (int)p->trapframe->a0);
  }
} else {
  printf("%d %s: unknown sys call %d\n",
         p->pid, p->name, num);
  p->trapframe->a0 = -1;
}
```

### 12.1 為什麼一定要先呼叫 handler？

題目要求印出 return value。在 handler 尚未執行前，還不知道結果。

正確順序：

```text
呼叫 syscalls[num]()
        │
        ▼
取得 return value
        │
        ▼
存入 trapframe->a0
        │
        ▼
檢查 mask 並印出
        │
        ▼
返回 user space
```

如果先印出再呼叫 handler，`a0` 仍可能是原本的第一個 argument，而不是 return value。

### 12.2 為什麼 `trace` 本身也可能被印出？

執行：

```sh
trace 2147483647 grep hello README
```

`sys_trace()` 先把 mask 設為全部開啟，再回到 `syscall()` 檢查 mask。因此這次 `trace` system call 自己的 bit 已經是 `1`，會印出：

```text
4: syscall trace -> 0
```

---

## 13. Step 9：讓 child process 繼承 mask

### 13.1 為什麼 `exec()` 不用複製，但 `fork()` 要？

`exec()` 替換目前 process 的 user address space，但保留同一個 `struct proc`：

```text
trace program                  grep program
pid = 3            exec       pid = 3
trace_mask = 32  ────────>     trace_mask = 32
```

`fork()` 則會呼叫 `allocproc()` 建立新的 child `struct proc`：

```text
parent                         child
trace_mask = 2     fork       trace_mask = 0（初始化值）
                 ────────>
```

若不手動複製，child 不會被追蹤，違反題目要求。

### 13.2 修改 `fork()`

在複製 trapframe 後加入：

```c
// copy saved user registers.
*(np->trapframe) = *(p->trapframe);

// Copy trace settings from parent to child.
np->trace_mask = p->trace_mask;

// Cause fork to return 0 in the child.
np->trapframe->a0 = 0;
```

此時：

```text
parent p->trace_mask = 2
          │
          ▼ fork
child np->trace_mask = 2
```

children 後續再 `fork()` 時，也會把相同 mask 傳給下一代。

---

## 14. 完整執行範例

### 14.1 `trace 32 grep hello README`

```text
shell 啟動 trace process，假設 pid = 3
    │
    ▼
trace(32)
    │ a0 = 32
    │ a7 = SYS_trace
    ▼
sys_trace()
    │ p->trace_mask = 32
    │ return 0
    ▼
exec("grep", ...)
    │ 同一個 pid、同一個 struct proc
    ▼
grep 呼叫 open()
    │ 32 & (1 << SYS_open) = 0
    │ 不輸出
    ▼
grep 呼叫 read()
    │ 32 & (1 << SYS_read) = 32
    │ 輸出 read 與 return value
    ▼
read 回傳 0
    │ 代表 EOF，仍然是一次 read syscall
    ▼
輸出 read -> 0
```

實際結果：

```text
$ trace 32 grep hello README
3: syscall read -> 1023
3: syscall read -> 961
3: syscall read -> 321
3: syscall read -> 0
$
```

### 14.2 `trace 2147483647 grep hello README`

實際結果：

```text
$ trace 2147483647 grep hello README
4: syscall trace -> 0
4: syscall exec -> 3
4: syscall open -> 3
4: syscall read -> 1023
4: syscall read -> 961
4: syscall read -> 321
4: syscall read -> 0
4: syscall close -> 0
$
```

其中：

- `trace -> 0`：mask 設定成功。
- `exec -> 3`：此 xv6 版本的 `exec` handler 回傳 argument count。
- `open -> 3`：取得 file descriptor 3。
- `read -> 正數`：成功讀取的 byte 數。
- `read -> 0`：已到 EOF。
- `close -> 0`：關閉成功。

---

## 15. 測試與驗證

### 15.1 只追蹤 read

```sh
trace 32 grep hello README
```

只應看到 `read`，不能看到 `open`、`exec` 或 `close`。

### 15.2 追蹤所有 system calls

```sh
trace 2147483647 grep hello README
```

應看到 `trace`、`exec`、`open`、`read`、`close`。

### 15.3 未啟用 tracing 的對照測試

```sh
grep hello README
```

預期：

```text
$ grep hello README
$
```

不能出現任何 `syscall` tracing line。這可確認 mask 是 per-process state，沒有錯誤地影響其他 processes。

### 15.4 驗證 children inheritance

```sh
trace 2 usertests forkforkfork
```

`2 = 1 << SYS_fork`，所以只追蹤 `fork`。parent 與 descendants 都應輸出：

```text
3: syscall fork -> 4
5: syscall fork -> 6
6: syscall fork -> 7
...
```

最後應看到：

```text
ALL TESTS PASSED
```

### 15.5 執行 trace 專用 grader

```sh
make grade GRADEFLAGS=trace
```

本次實際驗證結果：

```text
trace 32 grep: OK
trace all grep: OK
trace nothing: OK
trace children: OK
```

代表 System call tracing 的四個 tests 全部通過。

這只驗證 tracing 部分。整個 Lab 2 還要完成 `Sysinfo`、`answers-syscall.txt` 和 `time.txt`，最後再執行完整的：

```sh
make grade
```

---

## 16. 常見錯誤與原因

### 16.1 `implicit declaration of function 'trace'`

原因：`user/user.h` 沒有：

```c
int trace(int);
```

這是 compiler 階段的錯誤。

### 16.2 `undefined reference to 'trace'`

原因：雖然有 C declaration，但 `user/usys.o` 尚未提供 `trace` symbol。

檢查：

```perl
entry("trace");
```

這是 linker 階段的錯誤。

### 16.3 `unknown sys call 22`

原因：user stub 已成功進入 kernel，但 `syscalls[]` 尚未加入：

```c
[SYS_trace] sys_trace,
```

這是 runtime kernel dispatch 的問題。

### 16.4 `trace()` 成功，但完全沒有 tracing output

原因：`sys_trace()` 只保存了 mask，尚未在 `syscall()` 加入：

```c
if(p->trace_mask & (1 << num))
```

### 16.5 第一組測試成功，但 `trace children` 失敗

原因：`fork()` 沒有複製：

```c
np->trace_mask = p->trace_mask;
```

### 16.6 新 process 無故開始 tracing

原因：重用 `struct proc` 時沒有初始化 mask。

在 `allocproc()` 設定：

```c
p->trace_mask = 0;
```

### 16.7 return value 印出亂碼

常見錯誤：

```c
printf("%d: syscall %s -> %d\n", p->pid, syscall_names[num]);
```

格式字串有三個欄位，卻只提供兩個 arguments。必須加入：

```c
(int)p->trapframe->a0
```

### 16.8 名稱與 syscall 對錯位置

不要依賴：

```c
syscall_names[num - 1]
```

使用 designated initializer，讓 index 直接等於 syscall number：

```c
[SYS_read] = "read"
```

然後：

```c
syscall_names[num]
```

### 16.9 把 mask 32 誤解成 syscall number

```text
SYS_read = 5
read 的 mask = 1 << 5 = 32
```

`5` 是 bit 位置，`32` 才是只設定該 bit 的 mask。

### 16.10 Linker 顯示 RWX warning

編譯過程可能看到：

```text
warning: user/_trace has a LOAD segment with RWX permissions
```

這是 linker warning，不是本題程式失敗的原因。判斷 build 是否失敗時，應繼續往後找 `error`、`undefined reference` 或 `make: ***` 等訊息。

---

## 17. 最終修改摘要

### `Makefile`

```makefile
$U/_trace\
```

### `user/user.h`

```c
int trace(int);
```

### `user/usys.pl`

```perl
entry("trace");
```

### `kernel/syscall.h`

```c
#define SYS_trace 22
```

### `kernel/proc.h`

```c
int trace_mask;
```

### `kernel/proc.c`

```c
p->trace_mask = 0;
```

以及：

```c
np->trace_mask = p->trace_mask;
```

### `kernel/sysproc.c`

```c
uint64
sys_trace(void)
{
  int mask;

  argint(0, &mask);
  myproc()->trace_mask = mask;
  return 0;
}
```

### `kernel/syscall.c`

需要完成：

1. `extern uint64 sys_trace(void);`
2. `[SYS_trace] sys_trace,`
3. `syscall_names[]`
4. handler 執行後的 mask 檢查與 `printf()`

---

## 18. 這題真正學到的觀念

### 18.1 一個 system call 不只修改一個函式

新增 system call 必須同時接通：

```text
user declaration
    + user assembly stub
    + syscall number
    + kernel handler
    + kernel dispatch table
```

缺少任一層，就會分別出現 compiler、linker 或 runtime error。

### 18.2 System call arguments 來自 trapframe

user 將 arguments 放在 `a0` 到 `a5`，syscall number 放在 `a7`。trap 後，kernel 透過目前 process 的 trapframe 取回它們。

### 18.3 System call return value 也經過 `a0`

kernel 執行完 handler 後：

```c
p->trapframe->a0 = syscalls[num]();
```

回到 user mode 時，user program 就會從 `a0` 取得 return value。

### 18.4 Per-process state 應放在 `struct proc`

trace mask 屬於個別 process。放在 `struct proc` 才能做到彼此隔離，並在 `fork()` 時定義清楚的繼承規則。

### 18.5 `exec()` 與 `fork()` 的差異

```text
exec：替換目前 process 的程式，保留同一個 struct proc
fork：建立新的 child struct proc，需要複製要繼承的狀態
```

這就是 `exec()` 不必特別處理 trace mask，但 `fork()` 必須複製的原因。

### 18.6 Bit mask 適合表示多個開關

一個 `int` 可以用不同 bits 表示多個 system calls，檢查快速，也能自由組合。這種設計也常用於 permissions、flags、CPU status registers 和 device control fields。

---

## 19. 完成檢查表

- [x] `_trace` 已加入 `UPROGS`
- [x] `trace(int)` 已在 `user/user.h` 宣告
- [x] `user/usys.pl` 已加入 `entry("trace")`
- [x] `SYS_trace` 已分配 syscall number
- [x] `struct proc` 已加入 `trace_mask`
- [x] 新 process 的 mask 已初始化為 0
- [x] `sys_trace()` 已取得並保存 user argument
- [x] `sys_trace()` 已加入 syscall dispatch table
- [x] syscall number 已能對應名稱
- [x] tracing line 包含 pid、名稱與 return value
- [x] 未啟用 trace 的 process 不受影響
- [x] `fork()` 會讓 child 繼承 mask
- [x] `trace 32 grep` 測試通過
- [x] `trace all grep` 測試通過
- [x] `trace nothing` 測試通過
- [x] `trace children` 測試通過

System call tracing 至此完成。
