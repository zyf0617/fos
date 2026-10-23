# 协作式进程生命周期：实现与验收

日期：2026-10-23。基于M1资源复制提交`7df56c5`，由本次机制提交增加真实父子任务运行。阶段计划见[后续开发计划](后续开发计划.md)，接口契约见`spec/modules/lifecycle.yaml`。

## 使用与范围

- 默认`PROC_LIFECYCLE=0`：原课程Lab5的单用户路径和0–7号ABI。
- `make build PROC_LIFECYCLE=1 AEGIS=1`：启用4槽、私有内核栈、单启动hart协作式生命周期和组保护；用户代码可使用8–12号调用。
- `make test-process`：隔离测试镜像，在普通、完整审计、过滤成功事件、关闭审计四种配置运行真实fork/exit/wait/yield/getpid，另外运行三种fork策略拒绝镜像。
- `PROC_TEST=1`仅用于验收：替换用户程序、启用分配故障注入。测试调用200不属于产品ABI，非测试镜像中未知号仍返回-1。与课程ABI测试模式或Agent微基准混用会被拒绝。

内核只在实际启动hart调度。4个栈页和相邻空洞在启动时准备，运行期保留，不属于每次fork申请/退出释放的动态资源。动态用户页、页表、trapframe和mmap节点在任务退出后回到调度栈上释放。仅yield、wait、exit或故障切换，timer不抢占；CPU密集任务尚可能阻止其他任务前进。没有exec、信号、POSIX完整等待语义、浮点/向量现场保存、SMP迁移或真实LLM工作负载。

## ABI

| 号 | 调用 | 结果 |
| --- | --- | --- |
| 8 | fork() | 父返回正PID，子返回0，失败-1 |
| 9 | exit(status64) | 完整64位退出状态，不返回 |
| 10 | wait(status64指针) | 返回任意直接子进程PID；0忽略状态；无子进程/复制失败-1；等待中子退出会唤醒 |
| 11 | yield() | 主动让出，将来恢复后返回0 |
| 12 | getpid() | 当前PID，首进程为0 |

坏状态指针不回收退出记录，允许重试；跨页失败可能已写前缀。用户故障用退出状态-1。父退出后活跃孤儿继续执行并自动回收，已有僵尸自动释放槽位；孤儿可继续fork/wait。组成员与用户页预算在实际退出后即退还，僵尸仅保留PID/状态及槽位。PID单调增长，到有符号int上限拒绝fork而不回绕。

新增服务沿用统一组权限分派。生命周期构建支持低13位允许集合，默认课程构建仍为低8位；用户无修改策略接口。默认扩展启动组为64页/4成员；fork整批预留继承同一个组。事件8表示退出，detail/result为0，不记录用户提供的退出状态；exit不返回，因此没有普通调用完成事件3。

## QEMU验收证据

宿主macOS、本机RISC-V交叉工具链、QEMU virt双hart/130MiB、自带OpenSBI；只由识别出的实际启动hart运行任务。自动保存构建与串口日志至`target/tests/lab5/process-*`，汇总为`target/tests/lab5/process-summary.json`。测试不是仅调用内核接口，而是执行真实用户ecall、切入子地址空间并经wait恢复父上下文。

| 检查 | 断言和日志标记 |
| --- | --- |
| fork与独立地址空间 | 子得到0、父得到正PID；代码/堆/栈/mmap独立，子修改heap/mmap后父仍见原值；`process fork isolation and blocking wait PASS` |
| 内核上下文恢复 | 父wait阻塞，子yield并恢复后退出，父恢复并读取完整64位状态 |
| 状态复制失败重试 | 零页、只读代码页、无U位trapframe、跨页到空洞均失败；随后合法指针仍读到同一PID和-7状态；`process wait retry PASS` |
| 子故障 | 子访问零页产生cause13/stval0，以-1被wait回收；父仍可继续fork；`process child fault reap PASS` |
| 未支持的浮点指令 | 用户态FS/VS关闭，浮点指令产生cause2并只结束子；`process unsupported FP fault PASS` |
| 槽位耗尽 | 父和3子占满4槽，下一次fork失败；逐个回收得到不重复的PID及正确状态；`process slot exhaustion PASS` |
| 真实fork OOM | 从第0次物理分配开始逐点注入直到成功，失败后核对两类页池、节点池和源收费；成功子真实退出并wait；`process actual fork OOM rollback PASS` |
| 重复复用 | 12轮fork/wait，PID单调增加，组预算/节点/页可复用；`process repeated reuse PASS` |
| 父退出/孤儿 | 父保留未wait僵尸并先退出，活跃子继续yield、fork/wait，再退出；父PASS先于`process orphan survived PASS` |
| 策略实际拒绝 | 场景31禁止fork权限、32组预算7页而每任务4页、33组成员上限1；fork均失败且父数据不变；`process fork policy rejection PASS` |
| 完整退出与兼容 | 全部动态资源和组预算/成员归还后输出`aegis process lifecycle cleaned up`，之后ticks10且UART回显；同目录关闭扩展后无proc_fork符号、课程demo通过 |

上述四配置、三策略拒绝及开关切换均通过。父子任务为单hart协作运行；这些结果不能作为抢占公平性、组级调度收益或跨hart地址空间并发的证据。

## 回归与限制

原课程与保护回归`make test-lab5`、`make test-agent`、`make test-lab4`、`make test-lab3`均通过。非测试生命周期镜像确认不含`pmem_test_fail_after`，同一输出目录切换回课程路径后原demo通过。生命周期VF2构建使用：

```sh
make build PLATFORM=vf2 PROC_LIFECYCLE=1 AEGIS=1 TARGET=target/tests/process-vf2
```

VF2实板未运行；macOS不运行Linux宿主MMIO探针。既有微基准保留原始源码指纹，本次不重用其数据推导生命周期机制的开销。下一阶段先加入timer抢占RR及不主动yield任务的前进测试，再研究组级权重。
