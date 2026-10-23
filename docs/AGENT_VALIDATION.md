# AegisOS Agent 保护原型验收

日期：2026-10-23。基础为 `e303668` 的 Lab 5；本轮增加 `src/kernel/agent/` 及实际内存、syscall、进程清理钩子。架构要求和当前边界见 [功能实现说明](功能实现说明.md)，可执行契约见 `spec/modules/agent.yaml`。

提交锚点：`3eb4844` 定义保护与审计契约；`3ca7f97` 接入实现、测试和构建开关。本文及配套研究/说明文档另行提交，以便汇报时定位代码版本。

## 1. 当前结论

QEMU 上已验证组归属、共享用户页预算、成员数上限、syscall允许集合、有界审计以及真实用户态拒绝/释放/复用/故障清理。关闭扩展时保留课程路径；开启扩展且使用足够预算和完整权限时，Lab 5 原有用户负载通过。

这是 Lab 5 保护阶段的实现。多个真实用户进程、组级调度、CPU配额、fork继承、文件/工具权限、agentctl 和真实 LLM 工作负载尚未实现。后续细化已增加只收紧策略、资源快照、双hart预算竞争、独立审计开关和QEMU微基准；实板及真实Agent性能尚未测量。

## 2. 测试入口与环境

```bash
make test-agent
make test-lab5
make test-lab4
make test-lab3
make build PLATFORM=vf2 AEGIS=1 TARGET=target/tests/aegis-vf2
make build PLATFORM=vf2 AEGIS=1 AEGIS_AUDIT=0 TARGET=target/tests/aegis-vf2-noaudit
make bench-agent
```

本轮宿主为 macOS，使用本机 `riscv64-elf-` 工具链和 QEMU，自带 OpenSBI，QEMU 双 hart、130 MiB RAM。各测试按独立 TARGET 保存构建和串口日志；专用扩展镜像定义 `AGENT_TEST` 及 Lab 5 故障注入宏，默认镜像不含这些测试代码。

## 3. 验证结果

| 检查 | 本轮结果与断言 |
| --- | --- |
| 两个内核成员共享预算 | PASS：组总额4页，成员A预留3页后成员B申请2页失败、1页成功；组计数汇总而非逐进程放大 |
| 成员数与重复入组 | PASS：2名额用满后第三成员失败；重复attach失败；detach及重复detach正确 |
| 双hart并发预留 | PASS：屏障下唯一1页预算恰好一个申请成功；每hart竞争1000轮，峰值为1、无双重持有、资源全部恢复 |
| 只收紧策略与快照 | PASS：当前占用、预留峰值、拒绝计数一致；低于当前占用、增加限额或扩大权限均拒绝 |
| heap/mmap/stack配额 | PASS：三条真实内存管理入口共用预算；超限不改变PTE/原有元数据 |
| 三类分配的OOM | PASS：逐分配点失败注入，预算返还；页表摘要、两个物理页池计数恢复；mmap节点不泄漏 |
| 实际syscall门禁 | PASS：禁止copyout，目标缓冲区保持原值；编号高位非零不能绕过；拒绝mmap保持返回0 ABI |
| 审计环 | PASS：连续序号、固定64容量、覆盖丢失数、零容量及未来游标边界；序号读取不清空环 |
| 独立关闭审计 | PASS：关闭后同一权限/配额/并发测试仍通过，读取无事件且lost=0，ELF不含events环符号 |
| 真实ecall演示 | PASS：权限拒绝，满额拒绝额外mmap/brk，解除后预算复用，重新分配页清零 |
| 真实栈缺页失败与清理 | PASS：满额扩栈停止用户程序，成员/预算/页池/节点全部归还，内核继续ticks |
| AEGIS=1 + 原Lab5用户负载 | PASS：原内核自检与用户程序完整通过，故障清理后组计数为0 |
| 同目录1→0→1切换 | PASS：开关重编译；关闭时ELF无agent_charge符号；开启/关闭的默认演示均通过 |
| 非法AEGIS值、缺少AEGIS的测试模式 | PASS：空值、2、多值以及独立AGENT_TEST=1均非零拒绝 |
| AEGIS=0课程Lab5/4/3 | PASS：平台构建、内存与用户服务、权限、panic、中断现场及内核基线 |
| VF2 + AEGIS=1 | 构建通过；没有实板运行 |
| VF2 + AEGIS=1 + AEGIS_AUDIT=0 | 构建通过；没有实板运行 |
| 三配置微基准 | 3种模式各3次独立启动，每类每次31批，共558批原始样本；同一用户镜像，QEMU数据及限制另行保存 |
| YAML与补丁格式 | 19个YAML解析通过；git diff --check通过 |

功能入口 `tests/test_agent.py` 生成 `target/tests/lab5/aegis-summary.json`，包含检查状态与日志相对路径。该功能测试本身不测量性能，因此其性能项为 `NOT_MEASURED`；性能由独立的 `tests/bench_agent.py` 运行并保存。功能原始日志包括：

- `target/tests/lab5/aegis-protection-audit1/aegis-protection-audit1.log`
- `target/tests/lab5/aegis-protection-audit0/aegis-protection-audit0.log`
- `target/tests/lab5/aegis-lab5/aegis-lab5.log`
- `target/tests/lab5/aegis-toggle/aegis-baseline.log`
- `target/tests/lab5/aegis-toggle/aegis-default.log`

这些功能日志是忽略提交的可再生产物。专用测试日志包含组42/43/44的内核边界测试事件和组1的真实用户事件，演示时应区分组号。审计关闭时不输出事件。

已保存的[QEMU微基准表格](benchmarks/agent/2026-10-23/README.md)、`results.json`和相邻9份串口日志随文档提交，包含构建配置、源码/镜像摘要、环境、执行顺序及原始批次。3次启动仍不足以推断所有宿主状态下的稳定性能，报告只作描述性比较。

## 4. 从结果可以推出什么

能推出：用户不能通过当前已暴露的堆、栈、mmap增长接口绕过所附属组的用户页预算；被拒绝的服务不会执行；已覆盖的资源不足路径不泄漏预算；故障退出能够返还已收费资源。

不能推出：完整资源DoS隔离、对象能力安全、永久审计、真实多进程组隔离、公平性改善、低开销或AI任务性能提升。页表页/内核元数据没有每组限额，复制API尚未纳入fork事务；当前只有一个运行中的用户地址空间。

本轮 macOS 按测试器设计跳过 Linux MMIO 宿主探针；历史 Ubuntu 驱动探针见 Lab 5 验收记录。VF2实板、真实工作负载对照和竞赛平台最终提交仍是后续任务。
