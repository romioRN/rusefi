/*
    ChibiOS - Copyright (C) 2006..2016 Giovanni Di Sirio

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

/*
 * STM32F7 memory setup.
 *
 * RAM0 - Data, Heap.
 * RAM3 - Main Stack, Process Stack, BSS, NOCACHE, ETH.
 *
 * Notes:
 * BSS is placed in DTCM RAM in order to simplify DMA buffers management.
 * 'bl' is related to rusefi bootloader
 */

/* change address & length if bootloader */
/* WARNING same bootloader_size is hard-coded in CHECKSUM_ADDRESS variable in bundle.mk */
bootloader_size = 32k;
flash_start = 0x08000000 + (DEFINED(HAS_BOOTLOADER) ? bootloader_size : 0);
/* see also getFlashAddrFirstCopy() method */
flash_size = DEFINED(IS_BOOTLOADER) ? bootloader_size : (DEFINED(EFI_FLASH_SIZE) ? EFI_FLASH_SIZE : 768k);
image_size = DEFINED(HAS_BOOTLOADER) ? (flash_size - bootloader_size) : flash_size;

/* OpenBLT <-> main FW shared area */
_OpenBLT_Shared_Params_Size = DEFINED(HAS_BOOTLOADER) || DEFINED(IS_BOOTLOADER) ? 16 : 0;

MEMORY
{
    bl          : org = 0x08000000, len = bootloader_size       /* bootloader section */
    flash0 (rx) : org = flash_start, len = image_size
    flash1 (rx) : org = flash_start - 0x07e00000, len = image_size /* Flash as ITCM */
    flash2 (rx) : org = 0x00000000, len = 0
    flash3 (rx) : org = 0x00000000, len = 0
    flash4 (rx) : org = 0x00000000, len = 0
    flash5 (rx) : org = 0x00000000, len = 0
    flash6 (rx) : org = 0x00000000, len = 0
    flash7 (rx) : org = 0x00000000, len = 0
    shared      : org = 0x20020000, len = _OpenBLT_Shared_Params_Size
    ram0   (wx) : org = 0x20020000 + _OpenBLT_Shared_Params_Size, len = 368k - _OpenBLT_Shared_Params_Size      /* SRAM1 only, SRAM2 is used as no-cached area */
    ram1   (wx) : org = 0x20020000 + _OpenBLT_Shared_Params_Size, len = 368k - _OpenBLT_Shared_Params_Size      /* SRAM1 */
    ram2   (wx) : org = 0x2007C000, len = 16k       /* SRAM2 */
    ram3   (wx) : org = 0x20000000, len = 128k      /* DTCM-RAM */
    ram4   (wx) : org = 0x00000000, len = 16k       /* ITCM-RAM */
    ram5   (wx) : org = 0x40024000, len = 4k        /* BCKP SRAM */
    ram6   (wx) : org = 0x00000000, len = 0
    ram7   (wx) : org = 0x00000000, len = 0
}

__backup_sram_addr__ = ORIGIN(ram5);

/* For each data/text section two region are defined, a virtual region
   and a load region (_LMA suffix).*/

/* Flash region to be used for exception vectors.*/
REGION_ALIAS("VECTORS_FLASH", flash1);
REGION_ALIAS("VECTORS_FLASH_LMA", flash0);

/* Flash region to be used for constructors and destructors.*/
REGION_ALIAS("XTORS_FLASH", flash1);
REGION_ALIAS("XTORS_FLASH_LMA", flash0);

/* Flash region to be used for code text.*/
REGION_ALIAS("TEXT_FLASH", flash1);
REGION_ALIAS("TEXT_FLASH_LMA", flash0);

/* Flash region to be used for read only data.*/
REGION_ALIAS("RODATA_FLASH", flash0);
REGION_ALIAS("RODATA_FLASH_LMA", flash0);

/* Flash region to be used for various.*/
REGION_ALIAS("VARIOUS_FLASH", flash1);
REGION_ALIAS("VARIOUS_FLASH_LMA", flash0);

/* Flash region to be used for RAM(n) initialization data.*/
REGION_ALIAS("RAM_INIT_FLASH_LMA", flash0);

/* RAM region to be used for Main stack. This stack accommodates the processing
   of all exceptions and interrupts.*/
REGION_ALIAS("MAIN_STACK_RAM", ram0);

/* RAM region to be used for the process stack. This is the stack used by
   the main() function.*/
REGION_ALIAS("PROCESS_STACK_RAM", ram0);

/* RAM region to be used for data segment.*/
REGION_ALIAS("DATA_RAM", ram0);
REGION_ALIAS("DATA_RAM_LMA", flash0);

/* RAM region to be used for BSS segment.*/
REGION_ALIAS("BSS_RAM", ram0);

/* RAM region to be used for the default heap.*/
REGION_ALIAS("HEAP_RAM", ram0);

/* Stack rules inclusion.*/
INCLUDE rules_stacks.ld

/*===========================================================================*/
/* Custom sections for STM32F7xx.                                            */
/*===========================================================================*/

/* RAM region to be used for nocache segment.*/
REGION_ALIAS("NOCACHE_RAM", ram3);

/* RAM region to be used for eth segment.*/
REGION_ALIAS("ETH_RAM", ram3);

/* RAM region to be used for battery backuped data */
REGION_ALIAS("BKUP_RAM", ram5);

SECTIONS
{
    /* Special section for non cache-able areas.*/
    .nocache (NOLOAD) : ALIGN(4)
    {
        __nocache_base__ = .;
        *(.nocache)
        *(.nocache.*)
        *(.bss.__nocache_*)
        . = ALIGN(4);
        __nocache_end__ = .;
    } > NOCACHE_RAM

    /* Special section for Ethernet DMA non cache-able areas.*/
    .eth (NOLOAD) : ALIGN(4)
    {
        __eth_base__ = .;
        *(.eth)
        *(.eth.*)
        *(.bss.__eth_*)
        . = ALIGN(4);
        __eth_end__ = .;
    } > ETH_RAM

  /* shared between main FW and OpenBLT */
  .shared (NOLOAD) :
  {
    . = ALIGN(4);
    _sshared = .;
    __shared_start__ = _sshared;
    *(.shared)
    *(.shared.*)
    KEEP(*(.shared))
    . = ALIGN(4);
    _eshared = .;
    __shared_end__ = _eshared;
  } > shared

  .bkup_ram_noinit (NOLOAD):
  {
    *(.bkup_ram_noinit)
  } > BKUP_RAM
}

/* Code rules inclusion.*/
INCLUDE rules_code.ld

/* Data rules inclusion.*/
INCLUDE rules_data.ld

/* Memory rules inclusion.*/
INCLUDE rules_memory.ld
