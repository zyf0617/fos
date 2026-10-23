# 用户态时钟抢占与RR基线验收

日期：2026-10-23。基于生命周期提交`e23d311`增加可选抢占，课程默认路径不变。实现入口为`trap/trap_user.c`、`proc/lifecycle.c`，契约为`spec/modules/lifecycle.yaml`。

## 实现和边界

`PROC_PREEMPT=1`要求`PROC_LIFECYCLE=1`。只有用户态timer中断在保存全部整数现场/epc、完成时钟更新并释放锁后调用`proc_timer_tick`。若其他槽RUNNABLE，当前槽计一次实际抢占并转回RUNNABLE，经CPU栈按槽循环选择下一个任务；没有其他就绪任务时直接返回用户态。每次切换检查实际启动hart、SIE关闭、noff=0和合法槽状态。

恢复时重新安装sepc/stvec/sstatus/satp，timer不推进用户PC。内核trap、系统调用和锁临界区不抢占，辅助hart不运行用户任务。沿用100ms名义时钟周期，内核处理会延后中断交付；yield后的任务继承当前周期余量，不在每次dispatch重启完整时间片。没有SMP、浮点/向量现场、硬实时保证或组调度。

退出日志的dispatches为进入RUNNING次数，ticks为该任务在用户态收到的timer样本数，preemptions为发现其他就绪任务后实际让出次数。它们不是精确CPU运行时间、组配额或公平性指标；当前仅供功能验收。

## 复现

```sh
make test-rr
python3 tests/test_process.py --preempt
make test-process
make test-lab5
make test-agent
make test-lab4
make test-lab3
make build PLATFORM=vf2 PROC_LIFECYCLE=1 PROC_PREEMPT=1 AEGIS=1 TARGET=target/tests/rr-vf2
```

RR专用镜像为`RR_TEST=1`，与PROC_TEST、课程测试及微基准互斥；它允许关闭PROC_PREEMPT作为负对照。两种机制开关均默认0。`make test-rr`还检查非法组合和同目录开关重建，关闭后不残留`proc_timer_tick`符号，协作式课程demo仍通过。

## 负对照与寄存器验证

同一用户镜像fork两个子进程，父wait。子分别打印START，设置任务独有的整数寄存器模式，在没有ecall/yield的汇编循环中读取guest time并运行约1秒，检查现场后打印DONE并exit。检查28个固定模式寄存器（ra/gp/tp、s0–s11、t0–t5、a1–a7）和sp；a0/t6用于计时与循环控制。两个任务的模式不同，可发现跨任务现场混用。

验收同时要求：

1. 开启抢占时，两个START都早于任意一个DONE，各子任务实际抢占至少2次；现场与退出状态正确。
2. 关闭抢占时，同一用户程序串行完成，不满足上述重叠条件；这证明验收能区分协作运行与timer抢占。
3. 五种配置的用户二进制SHA-256完全相同，避免换负载构造差异。
4. wait后全部动态页、trapframe、节点及组预算/成员恢复，清理完成后仍出现新的timer输出，UART回显正常。

循环的约1秒按guest time截止，包含任务未运行的时间，不是固定计算量。抢占模式更早完成两个时间窗口，不能解释为吞吐提升。这里只验证调度前进与现场恢复。

## 本轮结果与原始证据

[已保存的摘要](validation/rr/2026-10-23/rr-summary.json)包含源码指纹、用户/内核镜像摘要、每配置样本及日志校验和；同目录保存原始串口日志、构建配置及工具版本。原始target路径仍保留，`archived_log`指向已提交的副本。

| 配置 | 两者都启动后才有任务完成 | 子1/子2实际抢占 | 现场与退出 |
| --- | --- | --- | --- |
| 协作式负对照 | 否，串行 | 未启用 | PASS |
| RR，无组保护 | 是 | 5 / 5 | PASS |
| RR，完整审计 | 是 | 5 / 5 | PASS |
| RR，过滤成功事件 | 是 | 5 / 5 | PASS |
| RR，关闭审计 | 是 | 5 / 5 | PASS |

各抢占配置子任务dispatch均为6，用户timer样本均为5；这是本轮运行的实际计数，不作为未来测试的精确期望或CPU份额结论。测试只要求至少2次实际抢占。

抢占模式的完整生命周期回归通过：父子隔离、wait阻塞/坏指针重试、子故障、未支持FP指令、槽位耗尽、逐物理分配失败、重复复用、孤儿及三类真实fork策略拒绝。原课程与Agent回归也通过。VF2仅构建通过，未运行实板；macOS跳过Linux宿主MMIO探针。

下一阶段先定义组权重、可信成员管理和测量窗口，扩展不等成员数组，比较进程RR与组间选择。该阶段需采集CPU时间、等待与吞吐，当前timer样本和双任务前进结果不能代替组公平性实验。
