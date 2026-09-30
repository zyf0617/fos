# 引入通用配置文件
include common.mk

.DEFAULT_GOAL := build

# 自动选择当前环境里已安装的RISC-V交叉工具链。
# 仍可在命令行中用TOOLPREFIX=...显式覆盖。
DETECTED_TOOLPREFIX := $(shell \
	for p in riscv64-linux-gnu- riscv64-unknown-elf- riscv64-elf- riscv64-none-elf-; do \
		if command -v $${p}gcc >/dev/null 2>&1; then echo $$p; break; fi; \
	done)
ifneq ($(DETECTED_TOOLPREFIX),)
TOOLPREFIX = $(DETECTED_TOOLPREFIX)
endif

# 配置CPU核心数量
CPUNUM = 2
# 定义目标文件输出目录
TARGET = target
# 定义各模块路径
KernelPath = src/kernel
UserPath = src/user
# 内核链接脚本
KERNEL_LD  = kernel.ld
# 定义内核目标文件路径
ELFKernel = $(TARGET)/kernel/kernel-qemu.elf
NakedKernel = $(TARGET)/kernel/kernel-qemu.bin

# 收集内核源代码文件（.c和.S汇编文件）
KernelSourceFile = $(wildcard $(KernelPath)/*.c) $(wildcard $(KernelPath)/*.S)
KernelSourceFile += $(wildcard $(KernelPath)/*/*.c) $(wildcard $(KernelPath)/*/*.S)
# 收集用户态源代码文件
UserSourceFile = $(wildcard $(UserPath)/*.c)

# 生成目标文件（.o）路径列表
KernelOBJ = $(patsubst $(KernelPath)/%.S, $(TARGET)/kernel/%.o, $(filter %.S, $(KernelSourceFile)))
KernelOBJ += $(patsubst $(KernelPath)/%.c, $(TARGET)/kernel/%.o, $(filter %.c, $(KernelSourceFile)))
UserOBJ = $(patsubst $(UserPath)/%.c, $(TARGET)/user/%.o, $(filter %.c, $(UserSourceFile)))

# QEMU模拟器配置
# 指定QEMU程序
QEMU = qemu-system-riscv64
LOCAL_OPENSBI = ../toolchain/usr/lib/riscv64-linux-gnu/opensbi/generic/fw_dynamic.bin
OPENSBI ?= $(if $(wildcard $(LOCAL_OPENSBI)),$(LOCAL_OPENSBI),default)
QEMUOPTS = -machine virt -bios $(OPENSBI) -kernel $(ELFKernel)  # 使用OpenSBI启动S-mode内核
QEMUOPTS += -m 130M -smp $(CPUNUM) -nographic  # 物理内存从0x80000000到0x88200000

# QEMU 中的内核会持续等待中断，因此自动测试使用有界超时，
# 再对串口日志中的启动、内存和时钟信号做断言。
TEST_TIMEOUT ?= 10
TEST_LOG = $(TARGET)/lab3-test.log
UART_TEST_TIMEOUT ?= 4
UART_TEST_LOG = $(TARGET)/lab3-uart-test.log

# 调试相关配置
# 动态计算GDB端口号
GDBPORT = $(shell expr `id -u` % 5000 + 25000)
# 根据QEMU版本选择合适的GDB调试参数
QEMUGDB = $(shell if $(QEMU) -help | grep -q '^-gdb'; \
	then echo "-gdb tcp::$(GDBPORT)"; \
	else echo "-s -p $(GDBPORT)"; fi)

# 引入编译器生成的头文件依赖，修改头文件后能正确触发重编译。
-include $(KernelOBJ:.o=.d) $(UserOBJ:.o=.d)

# 生成GDB初始化文件
.gdbinit: .gdbinit.tmpl-riscv
	sed "s/:1234/:$(GDBPORT)/" < $^ > $@

# 运行目标：先构建再启动QEMU
run: build
	$(QEMU) $(QEMUOPTS)

# 可重复的 Lab 3 启动、内存与时钟中断回归测试。
test-core: build
	@set -eu; \
	status=0; \
	timeout $(TEST_TIMEOUT)s $(QEMU) $(QEMUOPTS) > $(TEST_LOG) 2>&1 || status=$$?; \
	if [ $$status -ne 0 ] && [ $$status -ne 124 ]; then \
		cat $(TEST_LOG); \
		exit $$status; \
	fi; \
	grep -Fq "cpu 0 is booting with OpenSBI!" $(TEST_LOG); \
	grep -Fq "cpu 1 is booting with OpenSBI!" $(TEST_LOG); \
	grep -Fq "lab-2 memory self-test passed" $(TEST_LOG); \
	grep -Fq "lab-3 timer interrupt test passed" $(TEST_LOG); \
	if grep -Fq "panic!" $(TEST_LOG); then cat $(TEST_LOG); exit 1; fi; \
	echo "Lab 3 core QEMU test passed"

# 通过QEMU串口注入普通字符、Delete/Backspace和回车，验证PLIC外部中断链路。
test-uart: build
	@set -eu; \
	status=0; \
	{ sleep 1; printf 'uart-tesx\177t\r'; sleep 1; } | \
		timeout $(UART_TEST_TIMEOUT)s $(QEMU) $(QEMUOPTS) > $(UART_TEST_LOG) 2>&1 || status=$$?; \
	if [ $$status -ne 0 ] && [ $$status -ne 124 ]; then \
		cat $(UART_TEST_LOG); \
		exit $$status; \
	fi; \
	grep -Fq "$$(printf 'uart-tesx\b \bt')" $(UART_TEST_LOG); \
	if grep -Fq "panic!" $(UART_TEST_LOG); then cat $(UART_TEST_LOG); exit 1; fi; \
	echo "Lab 3 UART interrupt test passed"

test: test-core test-uart

# 调试目标：启动带GDB调试的QEMU
debug: $(ELFKernel) .gdbinit
	$(QEMU) $(QEMUOPTS) -S $(QEMUGDB)

# 构建目标：创建输出目录并编译内核
build: $(TARGET) $(ELFKernel) 

# 创建输出目录结构（如果不存在）
.PHONY: $(TARGET)
$(TARGET):
ifeq ($(wildcard $(TARGET)),)
	@mkdir -p $(TARGET)/kernel
	@mkdir -p $(TARGET)/kernel/arch
	@mkdir -p $(TARGET)/kernel/boot
	@mkdir -p $(TARGET)/kernel/lock
	@mkdir -p $(TARGET)/kernel/lib
	@mkdir -p $(TARGET)/kernel/mem
	@mkdir -p $(TARGET)/kernel/trap
endif

# 编译规则：将汇编文件(.S)编译为目标文件(.o)
$(TARGET)/kernel/%.o: $(KernelPath)/%.S
	$(CC) $(CFLAGS) -c -o $@ $<

# 编译规则：将C文件(.c)编译为目标文件(.o)
$(TARGET)/kernel/%.o: $(KernelPath)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

# 编译用户态程序
$(TARGET)/user/%.o: $(UserPath)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

# 链接生成内核ELF文件
$(ELFKernel): $(KernelOBJ) $(UserOBJ)
	$(LD) $(LDFLAGS) -T $(KERNEL_LD) $^ -o $@

# 清理目标：删除输出目录
.PHONY: clean
clean:
	rm -rf target

.PHONY: build run test test-core test-uart debug clean
