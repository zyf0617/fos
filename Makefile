PLATFORM ?= qemu
ifneq ($(PLATFORM),qemu)
ifneq ($(PLATFORM),vf2)
$(error unsupported PLATFORM: $(PLATFORM))
endif
endif
CPUNUM ?= 2
ifneq ($(CPUNUM),2)
$(error Lab 5 QEMU requires CPUNUM=2)
endif
# 引入通用配置文件
include common.mk
.DEFAULT_GOAL := build

PROC_LIFECYCLE ?= 0
PROC_TEST ?= 0
PROC_PREEMPT ?= 0
RR_TEST ?= 0
ifneq ($(PROC_LIFECYCLE),0)
ifneq ($(PROC_LIFECYCLE),1)
$(error PROC_LIFECYCLE must be 0 or 1)
endif
endif
ifneq ($(PROC_TEST),0)
ifneq ($(PROC_TEST),1)
$(error PROC_TEST must be 0 or 1)
endif
ifneq ($(PROC_LIFECYCLE),1)
$(error PROC_TEST requires PROC_LIFECYCLE=1)
endif
CFLAGS += -DPROC_TEST -DLAB5_TEST
endif
CFLAGS += -DPROC_LIFECYCLE=$(PROC_LIFECYCLE)
ifneq ($(PROC_PREEMPT),0)
ifneq ($(PROC_PREEMPT),1)
$(error PROC_PREEMPT must be 0 or 1)
endif
ifneq ($(PROC_LIFECYCLE),1)
$(error PROC_PREEMPT requires PROC_LIFECYCLE=1)
endif
endif
CFLAGS += -DPROC_PREEMPT=$(PROC_PREEMPT)
ifneq ($(RR_TEST),0)
ifneq ($(RR_TEST),1)
$(error RR_TEST must be 0 or 1)
endif
ifneq ($(PROC_LIFECYCLE),1)
$(error RR_TEST requires PROC_LIFECYCLE=1)
endif
ifneq ($(PROC_TEST),0)
$(error RR_TEST cannot be combined with PROC_TEST)
endif
CFLAGS += -DRR_TEST
endif
BOOT_USER ?= 1
AEGIS ?= 0
AEGIS_AUDIT ?= $(AEGIS)
AEGIS_AUDIT_SUCCESS ?= 1
AGENT_TEST ?= 0
AGENT_BENCH ?= 0
ifneq ($(AGENT_TEST),0)
ifneq ($(AGENT_TEST),1)
$(error AGENT_TEST must be 0 or 1)
endif
endif
ifneq ($(AGENT_BENCH),0)
ifneq ($(AGENT_BENCH),1)
$(error AGENT_BENCH must be 0 or 1)
endif
endif
ifneq ($(AEGIS),0)
ifneq ($(AEGIS),1)
$(error AEGIS must be 0 or 1)
endif
endif
ifneq ($(AEGIS_AUDIT),0)
ifneq ($(AEGIS_AUDIT),1)
$(error AEGIS_AUDIT must be 0 or 1)
endif
ifneq ($(AEGIS),1)
$(error AEGIS_AUDIT=1 requires AEGIS=1)
endif
endif
ifneq ($(AEGIS_AUDIT_SUCCESS),0)
ifneq ($(AEGIS_AUDIT_SUCCESS),1)
$(error AEGIS_AUDIT_SUCCESS must be 0 or 1)
endif
endif
CFLAGS += -DAEGIS=$(AEGIS) -DAEGIS_AUDIT=$(AEGIS_AUDIT) -DAEGIS_AUDIT_SUCCESS=$(AEGIS_AUDIT_SUCCESS)
ifeq ($(AGENT_TEST),1)
ifneq ($(AEGIS),1)
$(error AGENT_TEST=1 requires AEGIS=1)
endif
ifneq ($(PROC_LIFECYCLE),0)
$(error AGENT_TEST requires the course process path)
endif
CFLAGS += -DAGENT_TEST -DLAB5_TEST
endif
SCENARIO ?= 0
ifeq ($(PLATFORM),vf2)
CFLAGS += -DVISIONFIVE2
endif
LAB5_TEST ?= 0
ifeq ($(PROC_LIFECYCLE),1)
ifneq ($(filter 1,$(LAB5_TEST) $(TESTING)),)
$(error PROC_LIFECYCLE cannot use course ABI test modes)
endif
endif
ifeq ($(AGENT_BENCH),1)
ifneq ($(filter 1,$(AGENT_TEST) $(LAB5_TEST) $(TESTING) $(PROC_TEST) $(PROC_LIFECYCLE)),)
$(error AGENT_BENCH cannot be combined with other test modes)
endif
CFLAGS += -DAGENT_BENCH
endif
ifeq ($(LAB5_TEST),1)
CFLAGS += -DLAB5_TEST
endif
CFLAGS += -DLAB4_BOOT_USER=$(BOOT_USER) -DLAB4_SCENARIO=$(SCENARIO)
CFLAGS += -I$(TARGET)/user
ifeq ($(TESTING),1)
CFLAGS += -DLAB4_TEST
endif

# 自动探测本机可用的RISC-V工具链前缀
# common.mk中默认是riscv64-linux-gnu-, 但有些环境(例如macOS上的brew)装的是riscv64-unknown-elf-
# 探测不到任何工具链时保留common.mk中的默认值, 命令行传入的TOOLPREFIX优先级最高:
#   make TOOLPREFIX=riscv64-linux-gnu-
DETECTED_TOOLPREFIX := $(shell \
	for p in riscv64-linux-gnu- riscv64-unknown-elf- riscv64-elf- riscv64-none-elf-; do \
		if command -v $${p}gcc >/dev/null 2>&1; then echo $$p; break; fi; \
	done)
ifneq ($(DETECTED_TOOLPREFIX),)
TOOLPREFIX = $(DETECTED_TOOLPREFIX)
endif

# QEMU核数已在文件开头校验；NCPU由平台头文件定义。
# 定义目标文件输出目录
TARGET ?= target/$(PLATFORM)
# 定义各模块路径
KernelPath = src/kernel
UserPath = src/user
# 内核链接脚本
KERNEL_LD = $(TARGET)/kernel/kernel.ld
# 用户程序链接脚本 (用户程序从USER_BASE = 0x1000开始执行)
USER_LD    = user.ld
# 定义内核目标文件路径
ELFKernel = $(TARGET)/kernel/kernel-$(PLATFORM).elf
NakedKernel = $(TARGET)/kernel/kernel-$(PLATFORM).bin
# 定义用户程序的目标文件路径
ELFUser = $(TARGET)/user/initcode.elf
NakedUser = $(TARGET)/user/initcode.bin
# initcode数组的头文件 (由proc.c直接#include)
InitcodeHeader = $(TARGET)/user/initcode.h

# 收集内核源代码文件（.c和.S汇编文件）
KernelSourceFile = $(wildcard $(KernelPath)/*.c) $(wildcard $(KernelPath)/*.S)
KernelSourceFile += $(wildcard $(KernelPath)/*/*.c) $(wildcard $(KernelPath)/*/*.S)
# 收集用户态源代码文件
UserSourceFile = $(wildcard $(UserPath)/*.c)
ifeq ($(LAB5_TEST),1)
UserSourceFile = tests/lab5_user.c
endif
ifeq ($(TESTING),1)
UserSourceFile += tests/user_context.S
endif
ifeq ($(AGENT_TEST),1)
UserSourceFile = tests/agent_user.c
endif
ifeq ($(AGENT_BENCH),1)
UserSourceFile = tests/agent_bench.c
endif

ifeq ($(PROC_TEST),1)
UserSourceFile = tests/process_user.c
endif
ifeq ($(RR_TEST),1)
UserSourceFile = tests/rr_user.c tests/rr_context.S
endif

# 生成目标文件（.o）路径列表
KernelOBJ = $(patsubst $(KernelPath)/%.S, $(TARGET)/kernel/%.o, $(filter %.S, $(KernelSourceFile)))
KernelOBJ += $(patsubst $(KernelPath)/%.c, $(TARGET)/kernel/%.o, $(filter %.c, $(KernelSourceFile)))
UserOBJ = $(patsubst $(UserPath)/%.c, $(TARGET)/user/%.o, $(filter %.c, $(UserSourceFile)))
ifeq ($(LAB5_TEST),1)
UserOBJ = $(TARGET)/user/lab5_user.o
endif
ifeq ($(TESTING),1)
UserOBJ += $(TARGET)/user/user_context.o
endif
ifeq ($(AGENT_TEST),1)
UserOBJ = $(TARGET)/user/agent_user.o
endif
ifeq ($(AGENT_BENCH),1)
UserOBJ = $(TARGET)/user/agent_bench.o
endif

ifeq ($(PROC_TEST),1)
UserOBJ = $(TARGET)/user/process_user.o
endif
ifeq ($(RR_TEST),1)
UserOBJ = $(TARGET)/user/rr_user.o $(TARGET)/user/rr_context.o
endif

# QEMU模拟器配置
QEMU     = qemu-system-riscv64  # 指定QEMU程序
OPENSBI ?= default
QEMUOPTS = -machine virt -bios $(OPENSBI) -kernel $(ELFKernel)  # 使用OpenSBI启动S-mode内核
QEMUOPTS += -m 130M -smp $(CPUNUM) -nographic  # 物理内存从0x80000000到0x88200000

# 调试相关配置
GDBPORT = $(shell expr `id -u` % 5000 + 25000)  # 动态计算GDB端口号
# 根据QEMU版本选择合适的GDB调试参数
QEMUGDB = $(shell if $(QEMU) -help | grep -q '^-gdb'; \
	then echo "-gdb tcp::$(GDBPORT)"; \
	else echo "-s -p $(GDBPORT)"; fi)

# 引入编译器生成的依赖文件(.d)
# CFLAGS中的-MD会为每个.o生成同名的.d文件, 里面记录了该目标文件依赖的所有头文件
# 不引入它们的话, 修改头文件(例如timer/type.h中的INTERVAL)不会触发重新编译
-include $(KernelOBJ:.o=.d) $(UserOBJ:.o=.d)

# 生成GDB初始化文件
.gdbinit: .gdbinit.tmpl-riscv
	sed "s/:1234/:$(GDBPORT)/" < $^ > $@

# 运行目标：先构建再启动QEMU
ifeq ($(PLATFORM),qemu)
run: build
	$(QEMU) $(QEMUOPTS)

# 调试目标：启动带GDB调试的QEMU
debug: $(ELFKernel) .gdbinit
	$(QEMU) $(QEMUOPTS) -S $(QEMUGDB)

else
run debug:
	@echo "PLATFORM=vf2: use U-Boot booti"
	@exit 1
endif

# 构建目标：创建输出目录并编译内核
ifeq ($(PLATFORM),vf2)
build: $(NakedKernel)
endif
build: $(TARGET) $(ELFKernel) 

# 创建输出目录结构（如果不存在）
.PHONY: $(TARGET)
$(TARGET):
	@mkdir -p $(TARGET)/kernel
	@mkdir -p $(TARGET)/kernel/arch
	@mkdir -p $(TARGET)/kernel/boot
	@mkdir -p $(TARGET)/kernel/lock
	@mkdir -p $(TARGET)/kernel/lib
	@mkdir -p $(TARGET)/kernel/mem
	@mkdir -p $(TARGET)/kernel/trap
	@mkdir -p $(TARGET)/kernel/proc
	@mkdir -p $(TARGET)/kernel/syscall
	@mkdir -p $(TARGET)/kernel/agent
	@mkdir -p $(TARGET)/user

# 编译规则：将汇编文件(.S)编译为目标文件(.o)
$(TARGET)/kernel/%.o: $(KernelPath)/%.S | $(TARGET)
	$(CC) $(CFLAGS) -c -o $@ $<

# 编译规则：将C文件(.c)编译为目标文件(.o)
$(TARGET)/kernel/%.o: $(KernelPath)/%.c | $(TARGET)
	$(CC) $(CFLAGS) -c -o $@ $<

# 编译用户态程序
$(TARGET)/user/%.o: $(UserPath)/%.c | $(TARGET)
	$(CC) $(CFLAGS) -c -o $@ $<

$(TARGET)/user/user_context.o: tests/user_context.S | $(TARGET)
	$(CC) $(CFLAGS) -c -o $@ $<

# 链接生成用户程序ELF (main位于USER_BASE, 由user.ld保证)
$(ELFUser): $(UserOBJ) $(USER_LD)
	$(LD) $(LDFLAGS) -T $(USER_LD) $(UserOBJ) -o $@

# 转换成平坦二进制映像: 第一个字节就是USER_BASE处的指令
$(NakedUser): $(ELFUser)
	$(OBJCOPY) -O binary $< $@

# 转换成内核可以直接使用的数组
$(InitcodeHeader): $(NakedUser)
	xxd -i -n target_user_initcode $< > $@

# proc.c会#include initcode.h, 因此它必须等待initcode生成完毕
$(TARGET)/kernel/proc/proc.o: $(InitcodeHeader)

$(TARGET)/user/lab5_user.o: tests/lab5_user.c | $(TARGET)
	$(CC) $(CFLAGS) -c -o $@ $<

$(TARGET)/user/agent_user.o: tests/agent_user.c | $(TARGET)
	$(CC) $(CFLAGS) -c -o $@ $<

$(TARGET)/user/rr_user.o: tests/rr_user.c | $(TARGET)
	$(CC) $(CFLAGS) -c -o $@ $<

$(TARGET)/user/rr_context.o: tests/rr_context.S | $(TARGET)
	$(CC) $(CFLAGS) -c -o $@ $<

$(TARGET)/user/process_user.o: tests/process_user.c | $(TARGET)
	$(CC) $(CFLAGS) -c -o $@ $<

$(TARGET)/user/agent_bench.o: tests/agent_bench.c | $(TARGET)
	$(CC) $(CFLAGS) -c -o $@ $<

$(KERNEL_LD): kernel.ld $(TARGET)/build-config | $(TARGET)
	$(CC) -E -P -x c $(if $(filter vf2,$(PLATFORM)),-DVISIONFIVE2) $< -o $@

$(NakedKernel): $(ELFKernel)
	$(OBJCOPY) -O binary $< $@

# 链接生成内核ELF文件
$(ELFKernel): $(KernelOBJ) $(KERNEL_LD)
	$(LD) $(LDFLAGS) -T $(KERNEL_LD) $(KernelOBJ) -o $@

# 清理目标：删除输出目录和生成的头文件
.PHONY: clean
clean:
	rm -rf $(TARGET)
	rm -f $(InitcodeHeader)

# Lab4完整验收与保留的Lab3内核态基线。
test: test-lab5

test-lab5:
	python3 tests/test_lab5.py

test-lab4:
	python3 tests/test_lab4.py

test-lab3:
	python3 tests/test_lab4.py --lab3

test-agent:
	python3 tests/test_agent.py

test-rr:
	python3 tests/test_rr.py

test-process:
	python3 tests/test_process.py

bench-agent:
	python3 tests/bench_agent.py

.PHONY: build run clean debug test test-lab3 test-lab4 test-lab5 test-agent test-process test-rr bench-agent

# 配置变化也必须重编译，避免测试宏或用户入口混入另一种镜像。
$(TARGET)/build-config: FORCE | $(TARGET)
	@printf '%s\n' '$(CC)' '$(LD)' '$(OBJCOPY)' '$(CFLAGS)' '$(LDFLAGS)' '$(CPUNUM)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp

$(KernelOBJ) $(UserOBJ): $(TARGET)/build-config
FORCE:
.PHONY: FORCE

# GNU make 3.81按秒比较文件时间；配置快速切换时不能仅依赖stamp的mtime。
CONFIG_CHANGED := $(shell printf '%s\n' '$(CC)' '$(LD)' '$(OBJCOPY)' '$(CFLAGS)' '$(LDFLAGS)' '$(CPUNUM)' | cmp -s - $(TARGET)/build-config || echo yes)
ifeq ($(CONFIG_CHANGED),yes)
$(KernelOBJ) $(UserOBJ) $(ELFUser) $(NakedUser) $(InitcodeHeader) $(ELFKernel) $(KERNEL_LD): FORCE
endif
