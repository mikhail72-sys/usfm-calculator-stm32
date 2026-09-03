/* startup.c -- bare-metal reset handler for Cortex-M4F (STM32F411)
 *
 * Overrides libopencm3's weak reset_handler (vector.o still supplies the
 * vector table).  Compared with the library version:
 *   - resets MSP before any stack use (safe when jumped to from a
 *     bootloader),
 *   - points VTOR at our own vector table (again: bootloader-safe, and
 *     required once _FLASH_ORIGIN moves away from 0x08000000),
 *   - enables the FPU (CP10/CP11 full access) -- the whole build is
 *     hard-float, the first float op before this would UsageFault.
 */
#include <libopencm3/cm3/vector.h>
#include <libopencm3/cm3/scb.h>

typedef void (*funcp_t)(void);

extern unsigned _data_loadaddr, _data, _edata, _ebss;
extern funcp_t __preinit_array_start, __preinit_array_end;
extern funcp_t __init_array_start, __init_array_end;
extern int main(void);
extern unsigned _stack;

static void __attribute__((noinline, used)) startup_body(void)
{
    volatile unsigned *src, *dst;

    /* FPU on, before anything that might touch S-registers */
    SCB_CPACR |= SCB_CPACR_FULL * (SCB_CPACR_CP10 | SCB_CPACR_CP11);
    __asm__ volatile("dsb\n\tisb");

    SCB_VTOR = (uint32_t)&vector_table;

    for (src = &_data_loadaddr, dst = &_data; dst < &_edata; )
        *dst++ = *src++;
    while (dst < &_ebss)
        *dst++ = 0;
    for (funcp_t *fp = &__preinit_array_start; fp < &__preinit_array_end; fp++)
        (*fp)();
    for (funcp_t *fp = &__init_array_start; fp < &__init_array_end; fp++)
        (*fp)();
    (void)main();
    for (;;);
}

void __attribute__((naked)) reset_handler(void)
{
    __asm__ volatile(
        "ldr r0, =_stack  \n\t"
        "mov sp, r0       \n\t"
        "bl  startup_body"
        ::: "r0"
    );
}
