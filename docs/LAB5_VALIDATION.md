# Lab 5 开发与验收记录

日期：2026-10-22。分支：`lab5`。助教基线：`course/lab5` 的 `4a0f4b7`；个人基线：Lab 4 的 `928fa8e`。

## 结论

用户内存与系统调用代码已实现，Ubuntu QEMU 全部业务验收通过；继承的 Lab 4 用户中断现场、panic 和 Lab 3 内核态基线通过。VF2 参考配置可构建，Image/布局及宿主 MMIO 探针通过。**VF2 实板尚未验收**：用户确认当前不能接线，先完成代码和可重复验收。

这些结果不代表实现了 fork、调度器或完整的进程生命周期；本实验只提供单用户地址空间及其独立复制/销毁能力。

## 规格与实施

1. `0d52228`：先补全用户内存与 syscall 契约并记录模板冲突，再接实现。
2. `7f56b56`：平台构建隔离、Image 头、首次/次级入口及实际启动核归属。
3. `4578672`：临时页表事务、多页堆栈管理、mmap 区间链与节点池、统一 syscall、页表独立复制及故障资源回收，配套自动验收和本文记录。

多页分配采用临时树：在本次操作完全取得用户页和页表页后，无分配地合并到目标页表；失败直接销毁临时树。测试逐个注入分配失败并核对 PTE 树摘要、mmap 元数据、内核/用户空闲页和节点数。复制服务允许已完成的目标前缀，其失败语义与映射事务不同。

## 已执行的验证

Ubuntu 是 SSH 别名 `ubuntu` 对应的 Parallels 虚拟机。此次初始连接超时，检查发现虚拟机关闭；启动原虚拟机后继续验证，未修改系统工具链或固件。

环境准备：

```bash
source ../env.sh
export OPENSBI="$OSLAB_ROOT/toolchain/usr/lib/riscv64-linux-gnu/opensbi/generic/fw_dynamic.bin"
```

| 检查 | 结果与证据 |
| --- | --- |
| `make test-lab5` | PASS：QEMU/宿主平台探针、全部内核自检、真实用户态演示及权限故障场景 |
| `make test-lab4` | PASS：配置增量构建、三种 panic、默认入口、整数现场跨 timer/UART 中断、未知调用、四类权限故障 |
| `make test-lab3` | PASS：关闭用户入口后的启动、内存、定时与 UART 基线 |
| QEMU/VF2/QEMU 不 clean 交替构建 | PASS：产物隔离，未变动的 QEMU 镜像及对象时间戳保持不变 |
| VF2 默认 ELF/bin | PASS：0x40200000 入口、0x48200000 管理窗口终点、Image magic/version/跳转/text_offset/image_size、BSS 和栈位置 |
| `make build PLATFORM=vf2 LAB5_TEST=1 SCENARIO=14 TARGET=target/tests/vf2-suite` | 测试镜像可编译；仅构建，未实板运行 |
| Ubuntu 实际 UART/PLIC 驱动探针 | PASS：QEMU hart0/1 与 VF2 hart1..4 的 context、IRQ 跨 32 位边界、邻近寄存器保护、访问宽度、RX 延后使能、收发与辅助核路由 |
| 平台常量探针 | PASS：有效 hart、加载地址、时间间隔、PTE A/D、UART RX/TX 中断位与 PLIC 计算结果 |
| 非法平台/空平台/多平台值/CPUNUM=1、vf2 run/debug | 全部按预期非零拒绝；vf2 提示使用 U-Boot booti |
| macOS 原生构建与第一轮 QEMU | PASS；最终业务结果以上述 Ubuntu 验收为准 |
| YAML 契约解析、`git diff --check` | PASS；新增 uvm/syscall 函数没有空白契约条目 |

主验收入口：`tests/test_lab5.py`。内核资源与权限探针：`src/kernel/mem/lab5_test.c`；用户场景：`tests/lab5_user.c`；真实设备驱动的 Linux 宿主探针：`tests/platform_mmio.c` 与 `tests/platform_constants.c`。

日志位于 `target/tests/lab5/`、`target/tests/`，这些是忽略提交的可再生产物。每场景保留 build.log 及串口/宿主探针日志。默认镜像不包含测试宏启用的探针。

## 2026-10-23：缩堆与 munmap 的空页表回收

此前用户数据页已归还，但解除映射后空的中间页表会保留至进程销毁。通用 `vm_unmappages` 的保留语义继续适用于底层调用者；用户内存层新增解除后的局部清理，只遍历本次范围覆盖的分支。空表先从父项移除、执行本地 `sfence.vma`，再归还内核页池。根页表、非空分支、trapframe/trampoline 均保留，清理不申请资源。

先新增缩堆后的页表摘要与两类空闲页计数断言，在旧实现上运行 `python3 tests/test_lab5.py --group heap-stack`，得到预期的事务/资源断言失败；接入清理后同一测试通过。新增覆盖跨 2MiB 和 1GiB 边界后缩回，以及 mmap 完整解除后 PTE 树、两类页池、节点池和进程元数据恢复原状。已有跨权限裁剪、拆分失败及页内缩堆测试继续验证相邻映射与失败原子性。

本轮在 macOS 原生 RISC-V GCC/QEMU 上重新执行：

| 检查 | 结果 |
| --- | --- |
| `python3 tests/test_lab5.py --group heap-stack` | PASS：新增资源回收断言及原有 OOM/栈增长场景 |
| `make test-lab5`（等价执行 `python3 tests/test_lab5.py`） | PASS：全部 Lab 5 内核/用户场景、权限故障与清理、默认演示、QEMU/VF2 交替构建及 Image 检查 |
| `make test-lab4` | PASS：增量构建、三种 panic、timer/UART 现场、未知调用和权限故障 |
| `make test-lab3` | PASS：内核态内存/中断基线 |
| 18 个 YAML 文件解析、`git diff --check` | PASS |

Linux MMIO 宿主探针本轮在 macOS 按测试器设计跳过；上文 2026-10-22 的 Ubuntu 结果是历史证据，本轮没有重新运行 Linux 探针或连接 VF2 实板。

## 本轮发现并修正的继承问题

- UART 原 IER_RX_ENABLE 与 IER_TX_ENABLE 的位定义相反。旧串口回显检查不足以判断中断源配置；宿主探针检查真实寄存器值后暴露该问题。现改为 RX=bit0、TX=bit1，正常只开启 RX。修正后重新通过 Ubuntu 的 Lab 5/4/3 回归。
- VF2 的 PLIC S-context 应由设备树顺序确定，不能直接套用 QEMU。对官方源码核对并用实际驱动探针复现错误后，修正为 `2 * hart`。
- 助教模板的 mmap 返回值、未知系统调用 panic、参数截断、trapframe 销毁归属及缺少 perm 元数据，均按本地完整规格统一；没有直接覆盖个人已完成的安全实现。

## VF2 配置来源及推导

- [Linux v6.12 JH7110 设备树](https://github.com/torvalds/linux/blob/v6.12/arch/riscv/boot/dts/starfive/jh7110.dtsi)：S7 hart0 禁用；U74 hart1..4 提供 Sv39。PLIC context 列表先列 hart0 的 M-mode，再对 hart1..4 按 M/S 成对排列。因此 hart1 的 S-context 为2，后续为4/6/8，即 `2*hart`。UART0 地址为0x10000000，IRQ32，32位访问、寄存器步长4。
- [Linux v6.12 JH7110 公共设备树](https://github.com/torvalds/linux/blob/v6.12/arch/riscv/boot/dts/starfive/jh7110-common.dtsi)：RAM 从0x40000000起、timebase=4MHz；每约100ms事件采用400000 ticks。
- [StarFive UART 文档](https://doc-en.rvspace.org/VisionFive2/DG_UART/JH7110_SDK/general_uart_controller.html)：交叉核对UART的IRQ、reg-shift及reg-io-width。
- [U-Boot VisionFive2 文档](https://docs.u-boot.org/en/v2024.04/board/starfive/visionfive2.html)：交接依赖已有M-mode固件/S-mode运行环境。当前按课程 Image 头及 text_offset=0x200000 约定选择0x40200000作为参考加载地址，管理窗口为其后128MiB。

以上属于源码和参考平台依据，不是当前板卡 DTB 的现场测量。实板仍须确认时钟、有效 hart、UART 时钟、PLIC、固件 TIME/HSM、内存保留区与 DTB 位置；不满足前提时不加载运行镜像。

## 待实板连接后完成

1. 仅读取现有串口、固件、DTB 和内存信息，确认板卡与参考参数一致；先检查现有串口会话，避免两个接收程序抢日志。
2. 确认 DTB、固件与运行中的 U-Boot 不在内核管理窗口，文件加载成功后再 booti，失败不使用旧内存内容。
3. 保存所有有效 hart 启动、UART 输入、每核 timer、全局 ticks、Sv39、用户示例及任务1–5日志。
4. 分别报告“构建/静态镜像检查通过”和“真实硬件运行通过”；宿主 MMIO 内存不能验证真实中断投递、时钟或缓存行为。

实板步骤沿用已有固件，不自动 saveenv、不更新 OpenSBI/U-Boot、不写裸存储。本轮没有发送任何开发板串口命令。
