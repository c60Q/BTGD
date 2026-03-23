#ifndef BOOT_FLASH_H
#define BOOT_FLASH_H

#include <stdint.h>
#include <stdbool.h>
#include <boot_types.h>

/* ----------------------- Infineon TC3xx UCB Address ----------------------- */
/* UCB05 (UCB_USER) base on DFLASH address space */
#define UCB_START       (0xAF400000u)
#define UCB_END         (0xAF405FFFu)  /* 24k - 1 */
#define UCB_USER_BASE        (0xAF400A00u) /* UCB05 start address */

/* UID 4 words (32-bit each) inside UCB05 */
#define UCID_ADDR_WORD0      (UCB_USER_BASE + 0x00u)  /* 0xAF400A00 */
#define UCID_ADDR_WORD1      (UCB_USER_BASE + 0x04u)  /* 0xAF400A04 */
#define UCID_ADDR_WORD2      (UCB_USER_BASE + 0x08u)  /* 0xAF400A08 */
#define UCID_ADDR_WORD3      (UCB_USER_BASE + 0x0Cu)  /* 0xAF400A0C */

/* ----------------------- Flash Parameters ----------------------- */
#define TC3XX_DFLASH_PAGE_SIZE   (8u)
/* Minimum write unit (Burst Write is usually 256B, but here it is defined as Page) */
#define TC3XX_PFLASH_PAGE_SIZE   (32u)
/* The recommended write alignment size is to save RAM 
** by using a small buffer for each write instead of moving 3MB at once. */
#define TC3XX_PFLASH_BURST_SIZE  (256u)   
/* 定義 TC399 的 Sector 大小 (依據 User Manual) */
#define PFLASH_SECTOR_SIZE  (0x4000U)  /* 16KB for PFlash Logical Sector */
#define DFLASH_SECTOR_SIZE  (0x1000U)  /* 4KB for DFlash */

/* ----------------------- my PFLASH Memory Layout ----------------------- */
/* *total 16MB (TC399)
 * * [Bootloader]  0xA000_0000 (512 KB)
 * [Application] 0xA008_0000 (3 MB)
 * [Staging A]   0xA038_0000 (3 MB)
 * [Staging B]   0xA068_0000 (3 MB)
 */

/* ----------------------- PFLASH Bank Ranges (TC39x/TC399 16MB) -----------------------
 * Segment10 (0xA...) is non-cached; Segment8 (0x8...) is cached.
 * For flash operations, prefer Segment10 address.
 */

#define PFLASH_START_ADDR      (0xA0000000u)
#define PFLASH_END_ADDR        (0xA0FFFFFFu)

/* Each PF bank is 3MB except PF5 is last 1MB (for 16MB total) */
#define PF0_START              (0xA0000000u)
#define PF0_END                (0xA02FFFFFu)
#define PF1_START              (0xA0300000u)
#define PF1_END                (0xA05FFFFFu)
#define PF2_START              (0xA0600000u)
#define PF2_END                (0xA08FFFFFu)
#define PF3_START              (0xA0900000u)
#define PF3_END                (0xA0BFFFFFu)
#define PF4_START              (0xA0C00000u)
#define PF4_END                (0xA0EFFFFFu)
#define PF5_START              (0xA0F00000u)
#define PF5_END                (0xA0FFFFFFu)

/* If someone passes cached Segment8 (0x8...) address, convert to Segment10 (0xA...) */
#define PFLASH_NORM_ADDR(a)    ((((a) & 0xF0000000u) == 0x80000000u) ? ((a) + 0x20000000u) : (a))

/* Align helpers */
#define ALIGN_DOWN(x,a)        ((uint32_t)((x) & ~((uint32_t)((a)-1u))))
#define ALIGN_UP(x,a)          ((uint32_t)(((x) + ((a)-1u)) & ~((uint32_t)((a)-1u))))

/* Actual image-header struct size, aligned up to PFLASH page program granularity */
#define PFLASH_IMG_HDR_SIZE_RAW    ((uint32_t)sizeof(app_image_header_t))
#define PFLASH_IMG_HDR_SIZE        (ALIGN_UP(PFLASH_IMG_HDR_SIZE_RAW, TC3XX_PFLASH_PAGE_SIZE))

/*
 * Reserve the first full 16KB PFLASH logical sector for the application header area.
 * The header struct itself remains 64 bytes at the beginning of the image, followed by
 * padding up to +0x4000 so runtime header updates can erase/program only the dedicated header sector.
 */
#define APP_IMAGE_HEADER_STRUCT_SIZE   (0x40u)
#define APP_HEADER_RESERVED_SIZE       (0x4000u) //16KB
#define APP_IMAGE_BODY_OFFSET          (APP_HEADER_RESERVED_SIZE)

/* Compile-time guards for the new linker layout */
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert((PFLASH_IMG_HDR_SIZE % TC3XX_PFLASH_PAGE_SIZE) == 0u,
               "PFLASH image header must remain page aligned.");
_Static_assert(PFLASH_IMG_HDR_SIZE == APP_IMAGE_HEADER_STRUCT_SIZE,
               "app_image_header_t size changed; update the .lsl header pad start offset as well.");
_Static_assert(PFLASH_IMG_HDR_SIZE <= APP_IMAGE_BODY_OFFSET,
               "app_image_header_t must fit inside the reserved 16KB header sector.");
#else
typedef char pflash_hdr_size_must_be_page_aligned[((PFLASH_IMG_HDR_SIZE % TC3XX_PFLASH_PAGE_SIZE) == 0u) ? 1 : -1];
typedef char pflash_hdr_size_must_match_lsl_pad_start[(PFLASH_IMG_HDR_SIZE == APP_IMAGE_HEADER_STRUCT_SIZE) ? 1 : -1];
typedef char pflash_hdr_must_fit_reserved_sector[(PFLASH_IMG_HDR_SIZE <= APP_IMAGE_BODY_OFFSET) ? 1 : -1];
#endif

/* Bootloader */
#define PFLASH_BOOT_BASE        0xA0000000u
#define PFLASH_BOOT_SIZE        0x00080000u   /* 512 KB */

/* Active Application */
#define PFLASH_APP_BASE         0xA0080000u
#define PFLASH_APP_SIZE         0x00300000u   /* 3 MB */

#define PFLASH_APP_PAYLOAD_BASE (PFLASH_APP_BASE + APP_IMAGE_BODY_OFFSET)
#define PFLASH_APP_PAYLOAD_SIZE (PFLASH_APP_SIZE - APP_IMAGE_BODY_OFFSET)

/* Staging A */
#define PFLASH_STAGING_A_BASE   (PFLASH_APP_BASE + PFLASH_APP_SIZE) /* 0xA0380000 */
#define PFLASH_STAGING_A_PAYLOAD_BASE (PFLASH_STAGING_A_BASE + APP_IMAGE_BODY_OFFSET)
#define PFLASH_STAGING_A_PAYLOAD_SIZE (PFLASH_APP_SIZE - APP_IMAGE_BODY_OFFSET)

/* Staging B */
#define PFLASH_STAGING_B_BASE   (PFLASH_STAGING_A_BASE + PFLASH_APP_SIZE) /* 0xA0680000 */
#define PFLASH_STAGING_B_PAYLOAD_BASE (PFLASH_STAGING_B_BASE + APP_IMAGE_BODY_OFFSET)
#define PFLASH_STAGING_B_PAYLOAD_SIZE (PFLASH_APP_SIZE - APP_IMAGE_BODY_OFFSET)

/* ----------------------- DFLASH Memory Layout ----------------------- */
#define DFLASH0_START   (0xAF000000u)
#define DFLASH0_END     (0xAF0FFFFFu)
#define DFLASH1_START   (0xAFC00000u)
#define DFLASH1_END     (0xAFC1FFFFu)

#define BCB_OFFSET      (0x000FE000u)   /* 8KB window at end of DF0 */
#define BCB_ADDR        (DFLASH0_START + BCB_OFFSET)  /* 0xAF0FE000 */
#define BCB_LENGTH_8KB  (0x2000u)       /* 8KB window */
#define BCB_LENGTH_4KB  (0x1000u)       /* 4KB window */

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert((BCB_ADDR % DFLASH_SECTOR_SIZE) == 0, "BCB_ADDR must be 4KB aligned");
_Static_assert((BCB_ADDR % TC3XX_DFLASH_PAGE_SIZE) == 0, "BCB_ADDR must be 8B aligned");
_Static_assert((BCB_DATA_SIZE % TC3XX_DFLASH_PAGE_SIZE) == 0, "BCB size must be 8B multiple");
_Static_assert(BCB_DATA_SIZE <= BCB_LENGTH_4KB, "BCB must fit into 4KB erase window");
#endif

int Flash_Read(uint32_t addr, void *dst, uint32_t len);
int P_Flash_Erase_Range(uint32_t dst_addr, uint32_t len);
int P_Flash_Program(uint32_t dst_addr, const void *src_data, uint32_t len);
int D_Flash_Erase_Range(uint32_t dst_addr, uint32_t len);
int D_Flash_Program(uint32_t dst_addr, const void *src_data, uint32_t len);
volatile const boot_bcb_t* BCB_Read_Out(void);
bool Meta_Read_Out(app_image_header_t *m, uint32_t target_addr);
int Copy_Data_to_Pflash(uint32_t *target_addr, const uint8_t *source_data);
int Copy_Bank_to_Bank(uint32_t source_addr, uint32_t target_addr);




__attribute__((noreturn)) void jump_to_entry(uint32_t entry_addr);
#if 0// qqq none use now, keep for temp
void soft_reset(void);
#endif

#endif /* BOOT_FLASH_H */
