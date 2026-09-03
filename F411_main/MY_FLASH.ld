/* libopencm3-style linker script for STM32F411CEU6 (WeAct "black pill")
 *
 * F411CE flash = 512K in 8 sectors (erase granularity is the SECTOR):
 *   sector 0  0x08000000  16K
 *   sector 1  0x08004000  16K
 *   sector 2  0x08008000  16K
 *   sector 3  0x0800C000  16K
 *   sector 4  0x08010000  64K
 *   sector 5  0x08020000 128K
 *   sector 6  0x08040000 128K
 *   sector 7  0x08060000 128K
 *
 * v0 layout -- no bootloader yet, the app boots straight from 0x08000000
 * (st-flash / ROM DFU).  Code gets sectors 0..5 (256K, far more than
 * needed), sectors 6 and 7 are reserved as the two config "EEPROM" areas
 * (same CONFIG1/CONFIG2 idea as uslm5lp0v3; 128K sectors give a huge
 * wear-levelling budget at the price of ~1-2 s erase time).
 *
 * When a bootloader appears, the natural F4 map is: bootloader = sector 0,
 * CONFIG1/2 = sectors 1/2, app from sector 4 (0x08010000) -- just move
 * _FLASH_ORIGIN and the MEMORY lines, startup.c already programs VTOR.
 *
 * .firmware_metadata / _METADATA_* are kept from the F103 script so a
 * future bootloader finds the tag at the same offset; nothing emits
 * .fw_metadata in v0 and the empty section is simply dropped.
 */

EXTERN(vector_table)
ENTRY(reset_handler)

_FLASH_ORIGIN    = 0x08000000;
_METADATA_OFFSET = 0x1E0;
_METADATA_SIZE   = 0x20;
_METADATA_ADDR   = _FLASH_ORIGIN + _METADATA_OFFSET;
_CODE_START      = _METADATA_ADDR + _METADATA_SIZE;

_Min_Heap_Size  = 0x400;
_Min_Stack_Size = 0x800;

MEMORY
{
    FLASH   (rx)  : ORIGIN = _FLASH_ORIGIN, LENGTH = 256K
    CONFIG1 (rx)  : ORIGIN = 0x08040000,    LENGTH = 128K
    CONFIG2 (rx)  : ORIGIN = 0x08060000,    LENGTH = 128K
    RAM     (rwx) : ORIGIN = 0x20000000,    LENGTH = 128K
}

SECTIONS
{
    /* Vector table at start of app flash (startup.c points VTOR here).
     * Cortex-M4 with 86 external IRQs needs a 512-byte aligned table --
     * _FLASH_ORIGIN is, and .vectors is the first thing placed. */
    .vectors : {
        KEEP(*(.vectors))
        . = ALIGN(4);
    } >FLASH

    /* Firmware metadata at fixed offset for bootloader identification */
    .firmware_metadata _METADATA_ADDR : {
        KEEP(*(.fw_metadata))
    } >FLASH

    /* Program code and constants */
    .text _CODE_START : {
        . = ALIGN(4);
        *(.text*)
        . = ALIGN(4);
        *(.rodata*)
        . = ALIGN(4);
        *(.glue_7)
        *(.glue_7t)
        KEEP(*(.init))
        KEEP(*(.fini))
        . = ALIGN(4);
        _etext = .;
    } >FLASH

    .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } >FLASH
    .ARM : {
        __exidx_start = .;
        *(.ARM.exidx*)
        __exidx_end = .;
    } >FLASH

    .preinit_array : {
        . = ALIGN(4);
        __preinit_array_start = .;
        KEEP(*(.preinit_array))
        __preinit_array_end = .;
    } >FLASH
    .init_array : {
        . = ALIGN(4);
        __init_array_start = .;
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        __init_array_end = .;
    } >FLASH
    .fini_array : {
        . = ALIGN(4);
        __fini_array_start = .;
        KEEP(*(.fini_array))
        KEEP(*(SORT(.fini_array.*)))
        __fini_array_end = .;
    } >FLASH

    /* Not cleared on reset - boot/app communication */
    .noinit (NOLOAD) : { *(.noinit*) } >RAM

    .data : {
        _data = .;
        *(.data*)
        *(.ramtext*)
        . = ALIGN(4);
        _edata = .;
    } >RAM AT >FLASH
    _data_loadaddr = LOADADDR(.data);

    /* Config EEPROM sectors */
    .e1rom (NOLOAD) : {
        KEEP(*(.e1rom))
    } >CONFIG1

    .e2rom (NOLOAD) : {
        KEEP(*(.e2rom))
    } >CONFIG2

    /* BSS cleared by reset_handler from _edata to _ebss */
    .bss : {
        __bss_start__ = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
        __bss_end__ = .;
    } >RAM

    ._user_heap_stack : {
        . = ALIGN(8);
        PROVIDE(end = .);
        PROVIDE(_end = .);
        PROVIDE(__end__ = .);
        . = . + _Min_Heap_Size;
        . = . + _Min_Stack_Size;
        . = ALIGN(8);
    } >RAM

    /DISCARD/ : {
        *(.eh_frame)
    }

    .ARM.attributes 0 : { *(.ARM.attributes) }
}

PROVIDE(_stack = ORIGIN(RAM) + LENGTH(RAM));
