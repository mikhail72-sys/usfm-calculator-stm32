# usfmcalcv0 — USFM flow-meter "calculator" firmware (STM32 side)

FIRST THING each session: check `inbox/` for files with `status: open` —
those are asks from neighbour projects (lead / analyzer / device / cheater
of the USFM effort in C:\Piezus\TI\usfm, BlueUsbSerial, hartcalibr, ...),
see `inbox/README.md` for the convention. Answer in the same file.

## What this is

The USFM ultrasonic flow meter keeps the MSP430 (C:\Piezus\TI\usfm) as the
measuring front end and moves signal processing ("restore + zeros"
algorithm, frequency output) to a neighbouring STM32 reached over I2C
(SPI/UART as fall-backs). This repo is that STM32 firmware. Background:
`C:\Piezus\TI\usfm\inbox\2026-09-02_lead_task-embedded-algo.md` and
`..._analyzer_f411-fit.md`.

Stage 1 "transit" (fw 0.10, 2026-09-03) is live: STM32 is I2C master to
the MSP430 (protocol frames over I2C, F104 GET_FRAME on DRDY), frames go
to the PC as a USB record stream (`inbox/AGREED_usb-stream.md`), the PC
talks back over the same VCP (addr 2 = calculator registers, addr 1 =
bridge to the MSP430).  `pc/calctest.ps1` is the reference receiver and
bench test.  Stage 2 (dtof_emb on board, result into MSP block 3000) is
next.

Slave quirks (MSP fw 1.60) already handled — do not "simplify" them away:
2 ms pause between request STOP and reply read (`I2C_REPLY_DELAY_MS`),
body read expects the length prefix repeated, 400 kHz because at 100 kHz
the slave overwrites its RAW buffers under a 1 KB read.

## Layout — same scheme as uslm5lp0v3

The production MCU is not chosen yet, so the tree is built for several
MCU targets sharing one application core:

- root: MCU-neutral code — `usfmcalc.c` (main), `usb_vcp.c/.h` (CDC-ACM
  over libopencm3 usbd), `kernel.h` (the hardware API every target must
  implement), `protocol.h` — a COPY of the canonical
  `C:\Piezus\TI\usfm\protocol.h` (owned by device; never hand-edit, re-copy
  when its USFM_PROTO_VER moves);
- `F411_main/` — STM32F411CEU6 on a WeAct "black pill": `Makefile`,
  `MY_FLASH.ld`, `startup.c`, `config.h`, `pins_f411.h`, `kernel_f411.c`.

To add a second target: copy `F411_main/` to `<MCU>_main/`, rewrite
`kernel_<mcu>.c` / `pins_<mcu>.h` / `config.h` / `MY_FLASH.ld`, and point
the Makefile at the matching `master*.mk`. Shared code must only ever
reach hardware through `kernel.h`.

## Build

`make` (or `mk.bat`) inside the target dir. Master makefiles live one level
up in `armprojects/` (their own git repo): `masterF4.mk` was created for
this project — hard-float ABI (`-mfloat-abi=hard -mfpu=fpv4-sp-d16`, the
F4 library is built that way), `--gc-sections`. The only HAL is
`../libopencm3.my` — vanilla; library gaps are patched with local defines
in `kernel_*.c`, never in the library itself.

Flashing v0 (no bootloader, image at 0x08000000): `make swdflash`
(ST-LINK) or `make dfuflash` (ROM DFU: hold BOOT0, tap NRST; dfu-util on
PATH). `MY_FLASH.ld` documents the F411 sector map and the intended
bootloader/config layout for later.

## Hardware notes (black pill)

- 25 MHz HSE → PLL 96 MHz SYSCLK, 48 MHz USB (PLLQ), APB1 48 / APB2 96.
- LED PC13 active low, KEY PA0 active low, USB on PA11/PA12 (OTG FS).
- PA9 is NOT wired to VBUS: libopencm3's OTG init enables VBUS sensing,
  which would keep the device invisible — `KernelUsbPostInit()` sets
  NOVBUSSENS. Do not remove it.
- USB is polled (`usb_vcp_poll()` in the main loop), no USB interrupts.

## Conventions

- Sources are UTF-8 with ASCII-only comments (the parent project carries
  Windows-1251 Russian comments — do not import that habit here).
- USB VID/PID 0483:5740 (stock ST VCP) so Windows binds usbser.sys with no
  driver; change `USB_PID` in `config.h` before this becomes a product.
