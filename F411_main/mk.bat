@echo off
PATH=D:\gcc-arm-11.2-2022.02\tools\bin\;%PATH%
@echo on
make clean
make all 2>err.txt
rem no bootloader / CRC wrapper yet -- flash bin\usfmcalc_f411.bin at 0x08000000
rem   ST-LINK : make swdflash
rem   ROM DFU : hold BOOT0, tap NRST, then  make dfuflash  (dfu-util on PATH)
