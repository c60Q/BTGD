

#include <string.h>
#include <stdint.h>
#include "IfxFlash.h"
#include "IfxCpu.h"
#include "IfxSrc.h"
#include "IfxGpt12.h"
#include "IfxAsclin.h"
#include "IfxGeth.h"
#include "IfxScuRcu.h"
#include "IfxScuWdt.h"

#include "boot_flash.h"
#include "boot_flow.h"
#include "boot_types.h"
#include "boot_transport.h"
#include "boot_transport_parameter.h"


/* ---------------------------------------------------------------------------
 * Jump-to-APP context setup (vectors / stacks)
 * Vector locations follow Lcf_Tasking_Tricore_Tc_Bootloader.lsl
 * APP executes from ACTIVE area; STAGING A/B are image storage only
 * --------------------------------------------------------------------------- */
#ifndef CPU_BIV
#define CPU_BIV  (0xFE20u)
#endif
#ifndef CPU_BTV
#define CPU_BTV  (0xFE24u)
#endif
#ifndef CPU_ISP
#define CPU_ISP  (0xFE28u)
#endif

/* CPU0 Trap/Interrupt vector table base (Segment8/cached) */
#define APP_CPU0_BTV_CACHED   (0x80000100u)
#define APP_CPU0_BIV_CACHED   (0x802FE000u)

#if defined(__TASKING__)
/* Linker symbols (stack ends) - value is the address */
extern unsigned int __USTACK0[];
extern unsigned int __ISTACK0[];
#endif

int Flash_Read(uint32_t addr, void *dst, uint32_t len)
{
    memcpy(dst, (const void*)(uintptr_t)addr, len);
    return 0;
}

volatile const boot_bcb_t* BCB_Read_Out(void)
{
	return (const boot_bcb_t*)(uintptr_t)BCB_ADDR;
}

bool Meta_Read_Out(app_image_header_t *m, uint32_t target_addr)
{
    if(!m) return false;
    return Flash_Read(target_addr, m, sizeof(*m)) == 0; 
}

/* ---------- Internal helpers ---------- */
#if defined(__GNUC__)
#define FLASH_INLINE static inline __attribute__((always_inline))
#else
#define FLASH_INLINE static inline
#endif

FLASH_INLINE uint32_t pflash_norm(uint32 addr)
{
	return (uint32)PFLASH_NORM_ADDR(addr);
}

FLASH_INLINE int pflash_getFlashType(uint32_t addr, IfxFlash_FlashType *type)
{
	uint32_t a = pflash_norm(addr);

	if((a >= PF0_START) && (a <= PF0_END)) { *type = IfxFlash_FlashType_P0; return 0; }
	if((a >= PF1_START) && (a <= PF1_END)) { *type = IfxFlash_FlashType_P1; return 0; }
	if((a >= PF2_START) && (a <= PF2_END)) { *type = IfxFlash_FlashType_P2; return 0; }
	if((a >= PF3_START) && (a <= PF3_END)) { *type = IfxFlash_FlashType_P3; return 0; }
	if((a >= PF4_START) && (a <= PF4_END)) { *type = IfxFlash_FlashType_P4; return 0; }
	if((a >= PF5_START) && (a <= PF5_END)) { *type = IfxFlash_FlashType_P5; return 0; }

	return -1;
}

FLASH_INLINE uint32_t pflash_bankEndPlus1(IfxFlash_FlashType t)
{
	switch(t){
		case IfxFlash_FlashType_P0: return (PF0_END + 1u);
		case IfxFlash_FlashType_P1: return (PF1_END + 1u);
		case IfxFlash_FlashType_P2: return (PF2_END + 1u);
		case IfxFlash_FlashType_P3: return (PF3_END + 1u);
		case IfxFlash_FlashType_P4: return (PF4_END + 1u);
		case IfxFlash_FlashType_P5: return (PF5_END + 1u);
		default: return 0u;
	}
}

/* ---------- DFLASH helpers (bank-aware) ---------- */
FLASH_INLINE int dflash_getFlashType(uint32_t addr, IfxFlash_FlashType *type)
{
	/* Block UCB region: never allow erase/program there */
	if((addr >= UCB_START) && (addr <= UCB_END)){
		return -2; /* forbidden */
	}

	if((addr >= DFLASH0_START) && (addr <= DFLASH0_END)){
		*type = IfxFlash_FlashType_D0;
		return 0;
	}

	/* If your iLLD doesn't have D1, you can map it to D0 or handle separately */
	if((addr >= DFLASH1_START) && (addr <= DFLASH1_END)){
#ifdef IfxFlash_FlashType_D1
		*type = IfxFlash_FlashType_D1;
		return 0;
#else
		/* fallback: treat as D1 (not ideal, but prevents compile break) */
		return -1;
#endif
	}
	return -1; /* out of DFLASH */
}

FLASH_INLINE uint32_t dflash_bankEndPlus1(IfxFlash_FlashType t)
{
	switch(t){
		case IfxFlash_FlashType_D0: return (DFLASH0_END + 1u);
#ifdef IfxFlash_FlashType_D1
		case IfxFlash_FlashType_D1: return (DFLASH1_END + 1u);
#endif
		default: return 0u;
	}
}


/* ---------- Put flash functions into RAM (PSPR) ---------- */
#if defined(__TASKING__)
#pragma section code "cpu0_psram"
#define FLASH_RAMFUNC
#elif defined(__GNUC__)
#define FLASH_RAMFUNC __attribute__((section(".text.cpu0_psram")))
#else
#define FLASH_RAMFUNC
#endif
FLASH_RAMFUNC int P_Flash_Erase_Range(uint32_t dst_addr, uint32_t len)
{
	if (len == 0u){
		return 0;
	}

	uint32_t start = pflash_norm(dst_addr);
	uint32_t end = start + len;

	if(end < start){
		return -1; /* overflow */
	}

	/* Basic range check */
	if((start < PFLASH_START_ADDR) || (end > (PFLASH_END_ADDR + 1u))){
		return -2; /* out of PFLASH */
	}

	/* Ensure Flash in read mode / clean status */
	IfxFlash_resetToRead(0);
	/* Erase uses sector boundary; erase the sectors that overlap [start, end) */
	uint32_t sectorAddr = ALIGN_DOWN(start, PFLASH_SECTOR_SIZE);

	while(sectorAddr < end){
		IfxFlash_FlashType ft;
		if(pflash_getFlashType(sectorAddr, &ft) != 0){
			IfxFlash_resetToRead(0);
			return -2;
		}

		/* Do not cross bank boundary in a single eraseMultipleSectors() call */
		uint32_t bankLimit = pflash_bankEndPlus1(ft);
		uint32_t localEnd = (end < bankLimit) ? end : bankLimit;
		uint32_t bytesToCover = localEnd - sectorAddr;
		uint32_t numSectors = (bytesToCover + (PFLASH_SECTOR_SIZE - 1u)) / PFLASH_SECTOR_SIZE;

		if ((bankLimit == 0u) || (localEnd <= sectorAddr)){
			return -6;
		}
		
		if(numSectors == 0u){
			numSectors = 1u;
		}

		/* Protected command */
		uint16_t pw = IfxScuWdt_getSafetyWatchdogPassword();
		IfxScuWdt_clearSafetyEndinit(pw);

		if(numSectors == 1u){
			IfxFlash_eraseSector(sectorAddr);
		}
		else{
			IfxFlash_eraseMultipleSectors(sectorAddr, numSectors);
		}

		IfxScuWdt_setSafetyEndinit(pw);

		/* Wait until done on correct PF bank */
		if (IfxFlash_waitUnbusy(0, ft) != 0u){
			IfxFlash_resetToRead(0);
			return -3; /* timeout / error */
		}

		IfxFlash_resetToRead(0);
		sectorAddr += (numSectors * PFLASH_SECTOR_SIZE);
	}
	return 0;
}

FLASH_RAMFUNC int P_Flash_Program(uint32_t dst_addr, const void *src_data, uint32_t len)
{
	const uint8_t *p = (const uint8_t *)src_data;
	uint8_t page[TC3XX_PFLASH_PAGE_SIZE];

	if((len == 0u) || (src_data == NULL)){
		return 0;
	}

	dst_addr = pflash_norm(dst_addr);

	/* Require 32-byte aligned destination */
	if((dst_addr & (TC3XX_PFLASH_PAGE_SIZE - 1u)) != 0u){
		return -1;
	}

    /* NOTE: 要用「對齊後」的 endWrite，避免最後一頁 padding 寫超界 */
    uint32_t endWrite = dst_addr + ALIGN_UP(len, TC3XX_PFLASH_PAGE_SIZE);
	if(endWrite < dst_addr){
		return -5; /* overflow */
	}

	if((dst_addr < PFLASH_START_ADDR) || (endWrite > (PFLASH_END_ADDR + 1u))){
		return -2; /* out of PFLASH */
	}

	IfxFlash_resetToRead(0);
	while(len > 0u){
		IfxFlash_FlashType ft;
		if(pflash_getFlashType(dst_addr, &ft) != 0){
			IfxFlash_resetToRead(0);
			return -2;
		}

		uint32_t chunk = (len >= TC3XX_PFLASH_PAGE_SIZE) ? TC3XX_PFLASH_PAGE_SIZE : len;
		memset(page, 0xFF, sizeof(page));
		memcpy(page, p, chunk);

		/* Enter page mode and make sure ready */
		IfxFlash_enterPageMode(dst_addr);
		if(IfxFlash_waitUnbusy(0, ft) != 0u){
			IfxFlash_resetToRead(0);
			return -3;
		}

		for(uint32_t i = 0u; i < TC3XX_PFLASH_PAGE_SIZE; i += 8u){
			uint32_t wordL = (uint32_t)page[i] |
							((uint32_t)page[i + 1u] << 8) |
							((uint32_t)page[i + 2u] << 16) |
							((uint32_t)page[i + 3u] << 24);

			uint32_t wordU = (uint32_t)page[i + 4u] |
							((uint32_t)page[i + 5u] << 8) |
							((uint32_t)page[i + 6u] << 16) |
							((uint32_t)page[i + 7u] << 24);

			/* iLLD 常用做法：loadPage2X32 載入 64-bit 到 assembly buffer */
			IfxFlash_loadPage2X32(dst_addr + i, wordL, wordU);
		}

		/* Issue write (protected) */
		uint16_t pw = IfxScuWdt_getSafetyWatchdogPassword();
		IfxScuWdt_clearSafetyEndinit(pw);
		IfxFlash_writePage(dst_addr);
		IfxScuWdt_setSafetyEndinit(pw);

		if(IfxFlash_waitUnbusy(0, ft) != 0u){
			IfxFlash_resetToRead(0);
			return -4;
		}

		IfxFlash_resetToRead(0);
		dst_addr += TC3XX_PFLASH_PAGE_SIZE;
		p += chunk;
		len -= chunk;
	}
	return 0;
}

FLASH_RAMFUNC int D_Flash_Erase_Range(uint32_t dst_addr, uint32_t len)
{
	if(len == 0u){
		return 0;
	}

	uint32_t start = dst_addr;
	uint32_t end = start + len;
	if(end < start){
		return -1; /* overflow */
	}

	/* range quick check (only allow within DFLASH0/1, and never UCB) */
	IfxFlash_FlashType tmp;
	if(dflash_getFlashType(start, &tmp) != 0){
		return -2;
	}
	if(dflash_getFlashType(end - 1u, &tmp) != 0){
		return -2;
	}

	IfxFlash_resetToRead(0);
	uint32_t sectorAddr = ALIGN_DOWN(start, DFLASH_SECTOR_SIZE);
	while(sectorAddr < end){
		IfxFlash_FlashType curType;
		if(dflash_getFlashType(sectorAddr, &curType) != 0){
			IfxFlash_resetToRead(0);
			return -2;
		}

		uint32_t bankLimit = dflash_bankEndPlus1(curType);
		uint32_t localEnd = (end < bankLimit) ? end : bankLimit;
		uint32_t bytesToCover = localEnd - sectorAddr;
		uint32_t numSectors = (bytesToCover + (DFLASH_SECTOR_SIZE - 1u)) / DFLASH_SECTOR_SIZE;

		if ((bankLimit == 0u) || (localEnd <= sectorAddr)){
			return -6;
		}
		
		if(numSectors == 0u){
			numSectors = 1u;
		}

		uint16_t pw = IfxScuWdt_getSafetyWatchdogPassword();
		IfxScuWdt_clearSafetyEndinit(pw);

		if(numSectors == 1u){
			IfxFlash_eraseSector(sectorAddr);
		}
		else{
			IfxFlash_eraseMultipleSectors(sectorAddr, numSectors);
		}

		IfxScuWdt_setSafetyEndinit(pw);

		if(IfxFlash_waitUnbusy(0, curType) != 0u){
			IfxFlash_resetToRead(0);
			return -3;
		}

		IfxFlash_resetToRead(0);
		sectorAddr += (numSectors * DFLASH_SECTOR_SIZE);
	}
	return 0;
}

FLASH_RAMFUNC int D_Flash_Program(uint32_t dst_addr, const void *src_data, uint32_t len)
{
	const uint8_t *p = (const uint8_t *)src_data;
	uint8_t page[TC3XX_DFLASH_PAGE_SIZE];

	if((len == 0u) || (src_data == NULL)){
		return 0;
	}

    if((dst_addr & (TC3XX_DFLASH_PAGE_SIZE - 1u)) != 0u){
        return -1; /* 8B align */
    }

	/* NOTE: 用對齊後 endWrite，避免最後一頁 padding 寫超界 qqq */
	uint32 endWrite = dst_addr + ALIGN_UP(len, TC3XX_DFLASH_PAGE_SIZE);
	if(endWrite < dst_addr){
		return -5; /* overflow */
	}
	IfxFlash_FlashType tmp2;
	if(dflash_getFlashType(dst_addr, &tmp2) != 0){
		return -2;
	}
	if(dflash_getFlashType(endWrite - 1u, &tmp2) != 0){
		return -2;
	}

	IfxFlash_resetToRead(0);
	while (len > 0u){
		IfxFlash_FlashType curType;
		if (dflash_getFlashType(dst_addr, &curType) != 0){
			IfxFlash_resetToRead(0);
			return -2;
		}

		uint32_t chunk = (len >= TC3XX_DFLASH_PAGE_SIZE) ? TC3XX_DFLASH_PAGE_SIZE : len;
		memset(page, 0xFF, sizeof(page));
		memcpy(page, p, chunk);

		/* enter page mode */
		IfxFlash_enterPageMode(dst_addr);
		if(IfxFlash_waitUnbusy(0, curType) != 0u){
			IfxFlash_resetToRead(0);
			return -3;
		}

		/* DFLASH page = 8 bytes = 2 x 32-bit words */
		uint32_t w0 = (uint32_t)page[0] |
					((uint32_t)page[1] << 8) |
					((uint32_t)page[2] << 16) |
					((uint32_t)page[3] << 24);

		uint32_t w1 = (uint32_t)page[4] |
					((uint32_t)page[5] << 8) |
					((uint32_t)page[6] << 16) |
					((uint32_t)page[7] << 24);

		/* DFLASH: 一頁 8 bytes，直接 load 2x32 */
		IfxFlash_loadPage2X32(dst_addr, w0, w1);

		uint16_t pw = IfxScuWdt_getSafetyWatchdogPassword();
		IfxScuWdt_clearSafetyEndinit(pw);
		IfxFlash_writePage(dst_addr);
		IfxScuWdt_setSafetyEndinit(pw);

		if(IfxFlash_waitUnbusy(0, curType) != 0u){
			IfxFlash_resetToRead(0);
			return -4;
		}

        IfxFlash_resetToRead(0);

		dst_addr += TC3XX_DFLASH_PAGE_SIZE;
		p += chunk;
		len -= chunk;
	}
	return 0;
}
#if defined(__TASKING__)
#pragma section code restore
#endif

__attribute__((noreturn))
void jump_to_entry(uint32_t entry_addr)
{
	void (*entry)(void) = (void (*)(void))(uintptr_t)entry_addr;
	IfxCpu_disableInterrupts();

	/* Stop boot-time activities before handing over to APP */
	flowCtrlFlag.bits.netTaskEnable = 0u;

	/* Disable boot timer (GPT12 T3) interrupt to avoid pending ISR after APP enables interrupts */
	volatile Ifx_SRC_SRCR *src_t3 = IfxGpt12_T3_getSrc(&MODULE_GPT120);
	IfxSrc_disable(src_t3);
	IfxSrc_clearRequest(src_t3);

/* Disable boot UART (ASCLIN3) interrupts to avoid pending ISR after APP enables interrupts */
	volatile Ifx_SRC_SRCR *src;
	src = IfxAsclin_getSrcPointerRx(&MODULE_ASCLIN3);
	IfxSrc_disable(src);
	IfxSrc_clearRequest(src);

	src = IfxAsclin_getSrcPointerTx(&MODULE_ASCLIN3);
	IfxSrc_disable(src);
	IfxSrc_clearRequest(src);

	src = IfxAsclin_getSrcPointerEr(&MODULE_ASCLIN3);
	IfxSrc_disable(src);
	IfxSrc_clearRequest(src);
	/* Also disable/clear ASCLIN module-side interrupt enables and latched flags */
	IfxAsclin_disableAllFlags(&MODULE_ASCLIN3);
	IfxAsclin_clearAllFlags(&MODULE_ASCLIN3);

	/* Disable boot Ethernet (GETH DMA ch0) service requests (RX/TX) */
	src = IfxGeth_getSrcPointer(&MODULE_GETH, IfxGeth_ServiceRequest_2); /* TX ch0 */
	IfxSrc_disable(src);
	IfxSrc_clearRequest(src);

	src = IfxGeth_getSrcPointer(&MODULE_GETH, IfxGeth_ServiceRequest_6); /* RX ch0 */
	IfxSrc_disable(src);
	IfxSrc_clearRequest(src);
	/* Also disable DMA/MTL interrupt enables and clear latched GETH status */
	IfxGeth_dma_disableInterrupt(&MODULE_GETH, IfxGeth_DmaChannel_0, IfxGeth_DmaInterruptFlag_transmitInterrupt);
	IfxGeth_dma_disableInterrupt(&MODULE_GETH, IfxGeth_DmaChannel_0, IfxGeth_DmaInterruptFlag_receiveInterrupt);
	IfxGeth_dma_disableInterrupt(&MODULE_GETH, IfxGeth_DmaChannel_0, IfxGeth_DmaInterruptFlag_transmitBufferUnavailable);
	IfxGeth_dma_disableInterrupt(&MODULE_GETH, IfxGeth_DmaChannel_0, IfxGeth_DmaInterruptFlag_receiveBufferUnavailable);
	IfxGeth_dma_disableInterrupt(&MODULE_GETH, IfxGeth_DmaChannel_0, IfxGeth_DmaInterruptFlag_normalInterruptSummary);
	IfxGeth_dma_disableInterrupt(&MODULE_GETH, IfxGeth_DmaChannel_0, IfxGeth_DmaInterruptFlag_abnormalInterruptSummary);
	IfxGeth_dma_clearAllInterruptFlags(&MODULE_GETH, IfxGeth_DmaChannel_0);
	IfxGeth_mtl_disableInterrupt(&MODULE_GETH, IfxGeth_MtlQueue_0, IfxGeth_MtlInterruptFlag_txQueueUnderflow);
	IfxGeth_mtl_disableInterrupt(&MODULE_GETH, IfxGeth_MtlQueue_0, IfxGeth_MtlInterruptFlag_rxQueueOverflow);
	IfxGeth_mtl_clearAllInterruptFlags(&MODULE_GETH, IfxGeth_MtlQueue_0);
	/* Stop both DMA directions so no RX/TX DMA state is left armed for APP */
	MODULE_GETH.DMA_CH[IfxGeth_RxDmaChannel_0].RX_CONTROL.B.SR = FALSE;
	IfxGeth_dma_stopTransmitter(&MODULE_GETH, IfxGeth_TxDmaChannel_0);
	IfxGeth_mac_disableTransmitter(&MODULE_GETH);
	IfxGeth_mac_disableReceiver(&MODULE_GETH);

	/* Clear CCPN: avoid hard-coded address if possible */
#ifdef CPU_ICR
	__mtcr(CPU_ICR, 0);
#else
	__mtcr(0xFE2C, 0);
#endif
	/* Re-point vectors & stacks to APP layout (see Lcf_Tasking_Tricore_Tc_Bootloader.lsl) */
	__mtcr(CPU_BTV, (unsigned int)APP_CPU0_BTV_CACHED);
	__mtcr(CPU_BIV, (unsigned int)APP_CPU0_BIV_CACHED);
#if defined(__TASKING__)
	__mtcr(CPU_ISP, (unsigned int)__ISTACK0);
	/* Load user stack pointer (A10) */
	__asm("mov.aa a10, %0" :: "a"(__USTACK0) : "a10");
#endif
/* optional: clear SRNs for peripherals used in boot */
	/* watchdog policy: keep your current approach */
	{
		uint16_t pw = IfxScuWdt_getSafetyWatchdogPassword();
		IfxScuWdt_disableSafetyWatchdog(pw);
		pw = IfxScuWdt_getCpuWatchdogPassword();
		IfxScuWdt_disableCpuWatchdog(pw);
	}
	__dsync();
	__isync();
	entry();
	Debug_Print_Out("{jump to entry fail, go to fail loop}", 0u, 0, 0u, dbug_num_type_str);
	while (1){// qqqqqq if jump fail, need add something to do?
		__nop();
	}
}

#if 0// qqq none use now, keep for temp
void soft_reset(void)
{
    IfxScuRcu_performReset(IfxScuRcu_ResetType_system, IfxScuRcu_ResetCause_application);
}
#endif

#if defined(__TASKING__)
#  pragma section code "cpu0_psram"
#elif defined(__GNUC__)
__attribute__((section(".text.cpu0_psram")))
#endif
int Copy_Data_to_Pflash(uint32_t *target_addr, const uint8_t *source_data)
{
    if (!target_addr || !source_data) return -1;

    uint32_t total_len = IMAGE_UPDATE_DATA_SIZE;
    uint8_t  buffer[TC3XX_PFLASH_BURST_SIZE];
    uint32_t bytes_copied = 0;
    uint32_t dst = *target_addr; //take the value

    /* Alignment to 256B */
    if ((dst % TC3XX_PFLASH_BURST_SIZE) != 0u) {
        return -4; /* alignment error */
    }

    while (bytes_copied < total_len)
    {
        uint32_t chunk = total_len - bytes_copied;
        if (chunk > TC3XX_PFLASH_BURST_SIZE) {
            chunk = TC3XX_PFLASH_BURST_SIZE;
        }

        /* 再避免跨越單一 PFLASH 256B page */
        uint32_t page_room = TC3XX_PFLASH_BURST_SIZE - (dst % TC3XX_PFLASH_BURST_SIZE);
        if (chunk > page_room) {
            chunk = page_room;
        }

        memcpy(buffer, &source_data[bytes_copied], chunk);
        if (P_Flash_Program(dst, buffer, chunk) != 0) {
            return -2; /* Program failed */
        }

        if (memcmp((const void *)(uintptr_t)dst, buffer, chunk) != 0) {
            return -3; /* Verify failed */
        }

        dst          += chunk;
        bytes_copied += chunk;
    }

    *target_addr = dst;  /* 回寫下一個可寫位址 */
    return 0; /* Success */
}
#if defined(__TASKING__)
#  pragma section code restore
#endif

/* * --------------------------------------------------------------------------
 * Copy_Bank_to_Bank
 * use for copy staging app image to active app bank
 * --------------------------------------------------------------------------
 */
#if defined(__TASKING__)
#pragma section code "cpu0_psram"
#elif defined(__GNUC__)
__attribute__((section(".text.cpu0_psram")))
#endif
int Copy_Bank_to_Bank(uint32_t source_addr, uint32_t target_addr)
{
	uint32_t src_addr = (uint32_t)PFLASH_NORM_ADDR(source_addr);
	uint32_t dst_addr = target_addr;
	uint32_t total_len = PFLASH_APP_SIZE;
	uint8_t buffer[TC3XX_PFLASH_BURST_SIZE];
	uint32_t bytes_copied = 0;

	/* Erase target bank */
	if(P_Flash_Erase_Range(dst_addr, total_len) != 0){
		return -1; // Erase failed
	}

	/* start copy */
	while (bytes_copied < total_len){
		/* Calculate how much to move this time */
		uint32_t chunk = (total_len - bytes_copied);
		if(chunk > TC3XX_PFLASH_BURST_SIZE){
			chunk = TC3XX_PFLASH_BURST_SIZE;
		}

		/* Read source through normalized PFlash alias (Segment10) before memcpy */
		memcpy(buffer, (void*)(src_addr + bytes_copied), chunk);

		/* 32-byte page write */
		if(P_Flash_Program(dst_addr + bytes_copied, buffer, chunk) != 0){
			return -2; // Program failed
		}
        
		/* Verify RAM buffer and Flash */
		if(memcmp((void*)(dst_addr + bytes_copied), buffer, chunk) != 0){
			return -3; // Verify failed
		}

		/* Update progress */
		bytes_copied += chunk;
        
		/* feed dog */
//		wdt_kick(); // qqq none use now, keep it
	}

	return 0; // Success
}
#if defined(__TASKING__)
#pragma section code restore
#endif



