# xv6 + GDB 快速複習手冊

這份是給「下次回來可以直接重現」的最小流程。

## 0. 你要先知道的觀念

- `make CPUS=1 qemu-gdb`：啟動 QEMU + xv6，並在 GDB 連線前先暫停 CPU。
- GDB 是另一個終端連上去控制執行。
- 兩個視窗分工：
  - 視窗 A（QEMU/xv6）：輸入 xv6 shell 指令（例如 `sleep 10`）
  - 視窗 B（GDB）：輸入除錯指令（例如 `b`, `c`, `p`）

---

## 1. 啟動流程（固定模板）

### 視窗 A

```bash
cd ~/xv6-labs-2022
make CPUS=1 qemu-gdb
```

### 視窗 B

```bash
cd ~/xv6-labs-2022
gdb-multiarch kernel/kernel
```

進入 gdb 後：

```gdb
target remote :26000
```

> 若你看到 `Remote debugging using :26000`，代表連線成功。

---

## 2. 第一次建議斷點（看開機 + sleep syscall）

在 GDB 輸入：

```gdb
b _entry
b main
b sys_sleep
c
```

然後重複 `c` 幾次，直到 xv6 開機完成出現 shell 提示字元 `$`。

---

## 3. 用 `sleep 10` trace syscall

1. 去視窗 A（QEMU）輸入：

```sh
sleep 10
```

2. 回視窗 B（GDB），應該會停在 `sys_sleep`。

3. 看關鍵值：

```gdb
p/x $a7
p *myproc()->trapframe
```

預期：
- `$a7 = 0xd`（十進位 13，對應 `SYS_sleep`）
- `trapframe.a0` 通常是 `sleep` 的參數（例如 `10`）

---

## 4. 常用 GDB 指令（xv6 最常用）

- `b <symbol>`：設斷點，例如 `b syscall`
- `b file:line`：設檔案行號斷點，例如 `b kernel/trap.c:53`
- `c`：繼續執行到下一個斷點
- `n`：執行下一行（不進入函式）
- `s`：單步進入函式
- `p expr`：印變數/表達式
- `p/x expr`：16 進位輸出
- `info break`：列出斷點與編號
- `disable <id>`：暫停某斷點（例如 `disable 3 4`）
- `enable <id>`：重新啟用斷點
- `delete <id>`：刪除斷點

---

## 5. 你之前遇過的常見問題

### 問題 A：`unknown architecture "riscv:rv64"` 或 `Truncated register ...`

原因：用錯 GDB（一般 `gdb`）  
解法：改用 `gdb-multiarch`（或 `riscv64-linux-gnu-gdb`）。

### 問題 B：QEMU 卡在 `xv6 kernel is booting`

原因：CPU 停在斷點，屬正常現象。  
解法：在 GDB 視窗輸入 `c` 繼續。

### 問題 C：在 gdb 打 `sleep 10` 出現 `Undefined command`

原因：`sleep 10` 是 xv6 shell 指令，不是 GDB 指令。  
解法：切回視窗 A 輸入。

### 問題 D：`.gdbinit auto-loading has been declined`

通常可先忽略，不影響手動 `target remote :26000`。  
若想允許自動載入，可依提示把路徑加到 gdb safe-path。

---

## 6. 進階：完整 syscall 路徑斷點

若你要看 user -> kernel 全路徑，可加：

```gdb
b usertrap
b syscall
b sys_sleep
```

說明：
- `usertrap`：trap 進 kernel 的 C handler（`kernel/trap.c`）
- `syscall`：syscall 分派點（`kernel/syscall.c`）
- `sys_sleep`：目標 handler（`kernel/sysproc.c`）

---

## 7. 每次複習建議順序（60 秒版）

1. `make CPUS=1 qemu-gdb`
2. `gdb-multiarch kernel/kernel`
3. `target remote :26000`
4. `b main`、`b sys_sleep`
5. `c` 到 shell
6. 視窗 A 打 `sleep 10`
7. 視窗 B 看 `$a7` 與 `trapframe`

完成這套，就代表你可以重現 Lecture 3 的核心 syscall trace。
