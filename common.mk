# 这个文件负责公共的配置

TOOLPREFIX = riscv64-linux-gnu-
CC = ${TOOLPREFIX}gcc
LD = ${TOOLPREFIX}ld
OBJCOPY = ${TOOLPREFIX}objcopy
OBJDUMP = ${TOOLPREFIX}objdump

# 编译相关配置
CFLAGS = -Wall -Werror -O -fno-omit-frame-pointer -ggdb -gdwarf-2
CFLAGS += -std=gnu17
CFLAGS += -MMD -MP
# 不依赖工具链默认ISA；较新GCC将CSR与指令缓存同步拆成独立扩展。
CFLAGS += -march=rv64gc_zicsr_zifencei -mabi=lp64d
CFLAGS += -mcmodel=medany
CFLAGS += -ffreestanding -fno-common -nostdlib -mno-relax
CFLAGS += -I.
CFLAGS += $(shell $(CC) -fno-stack-protector -E -x c /dev/null >/dev/null 2>&1 && echo -fno-stack-protector)

# Disable PIE when possible (for Ubuntu 16.10 toolchain)
ifneq ($(shell $(CC) -dumpspecs 2>/dev/null | grep -e '[^f]no-pie'),)
CFLAGS += -fno-pie -no-pie
endif
ifneq ($(shell $(CC) -dumpspecs 2>/dev/null | grep -e '[^f]nopie'),)
CFLAGS += -fno-pie -nopie
endif

# 链接相关配置 
LDFLAGS = -z max-page-size=4096
