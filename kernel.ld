OUTPUT_ARCH( "riscv" )
ENTRY( _entry )

PHDRS
{
  text PT_LOAD FLAGS(5); /* read + execute */
  data PT_LOAD FLAGS(6); /* read + write */
}

SECTIONS
{
  /* OpenSBI occupies the machine-mode firmware area from 0x80000000. */
  #ifdef VISIONFIVE2
  . = 0x40200000;
#else
  . = 0x80200000;
#endif

  .text : {
    KEEP(*(.text.entry))
    *(.text .text.*)
    . = ALIGN(0x1000);
    _trampoline = .;
    KEEP(*(trampsec))
    . = ALIGN(0x1000);
    ASSERT(. - _trampoline == 0x1000, "error: trampoline larger than one page");
  } :text

  .rodata : {
    . = ALIGN(16);
    *(.srodata .srodata.*) /* do not need to distinguish this from .rodata */
    . = ALIGN(16);
    *(.rodata .rodata.*)
    . = ALIGN(4096);
    PROVIDE(KERNEL_DATA = .);
  } :text

  .data : {
    . = ALIGN(16);
    *(.sdata .sdata.*) /* do not need to distinguish this from .data */
    . = ALIGN(16);
    *(.data .data.*)
  } :data

  .bss : {
    BSS_BEGIN = .;
    . = ALIGN(16);
    *(.sbss .sbss.*) /* do not need to distinguish this from .bss */
    . = ALIGN(16);
    *(.bss .bss.*)
    . = ALIGN(8);
    BSS_END = .;
  } :data

  . = ALIGN(4096);
  ALLOC_BEGIN = .;
  KERNEL_IMAGE_SIZE = ALLOC_BEGIN - ADDR(.text);
  /* QEMU RAM is 130 MiB from 0x80000000; leave the first 2 MiB for OpenSBI. */
  ALLOC_END = ADDR(.text) + 128M;
  ASSERT(ALLOC_BEGIN < ALLOC_END, "kernel exceeds managed memory")
}
