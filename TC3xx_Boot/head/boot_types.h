#ifndef MEMORY_ADDRESS_H
#define MEMORY_ADDRESS_H

#include <stdint.h>
#include <string.h> /* For memcpy if needed in inline functions */

#define BOOT_FW_VERSION "00.00.01" //official_release_big_ver_num.official_release_small_ver_num.inside_test_num

/* ----------------------- Magic Numbers ----------------------- */
#define MAGIC_BCB		0xDEADB007u  /* DFLASH BCB magic word */
#define MAGIC_APP		0x1D107EA3u   /* PFLASH magic word */

/** \brief Boolean true value. */
#define boot_TRUE       (1)
/** \brief Boolean false value. */
#define boot_FALSE      (0)
/** \brief NULL pointer value. */
#define boot_NULL       ((void *)0)

typedef unsigned char   boot_bool;                     /**<  boolean type               */
typedef char            boot_char;                     /**<  character type             */
typedef unsigned long   boot_addr;                     /**<  memory address type        */
typedef unsigned char   boot_int8u;                    /**<  8-bit unsigned integer     */
typedef signed char     boot_int8s;                    /**<  8-bit   signed integer     */
typedef unsigned short  boot_int16u;                   /**< 16-bit unsigned integer     */
typedef signed short    boot_int16s;                   /**< 16-bit   signed integer     */
typedef unsigned int    boot_int32u;                   /**< 32-bit unsigned integer     */
typedef signed int      boot_int32s;                   /**< 32-bit   signed integer     */

/* ----------------------- Linker Symbols ----------------------- */
/* This corresponds to the definition in the 
   .lsl file and is used in the program to obtain boundaries. */

extern uint8_t __APP_IMAGE_START[];    /* 0xA0080000 */
extern uint8_t __APP_IMAGE_END[];      /* 0xA0380000 */

extern uint8_t __STAGING_A_START[];    /* 0xA0380000 */
extern uint8_t __STAGING_B_START[];    /* 0xA0680000 */

/* Weak definitions prevent compilation errors */
#pragma weak __APP_IMAGE_START
#pragma weak __APP_IMAGE_END
#pragma weak __STAGING_A_START
#pragma weak __STAGING_B_START

/* ----------------------- Data Structures ----------------------- */
/* 1. Image Header (useing PFLASH)
 * struct = 64 bytes, page size is 32 bytes
 */
typedef struct
{
    uint32_t magic;
    char version[24]; /* 24-byte version string field, fits 23 ASCII chars + '\0' */ /* "C-TestBoard-FACTORY-5.4" */
    uint32_t size;
    uint32_t crc;
    uint32_t reserved[7];
} app_image_header_t;

/* 2. BCB (Boot Control Block, using DFLASH)
 * struct = 168 bytes, page size is 8 bytes
 */
typedef struct
{
	uint32_t image_magic;
	uint32_t reserved_for_magic;
    char image_version[24];
	uint32_t image_base;
	uint32_t image_size;
    uint32_t image_crc;
	uint32_t reserved;
}bcb_image_info_t;

typedef struct
{
    uint32_t magic;
	uint32_t reserved_for_magic;
	uint32_t action_cmd;		/* 0=normal boot up app, 1=update, 2=AB swap, 3=stay loop */
	uint32_t active_app;     /* Logical owner identity of current ACTIVE image: 0=init, 1=Staging A, 2=Staging B, 3=all fail */
	/* NOTE:
	 * staging_x_invalid is an image-presence state stored in BCB.
	 * BCB_BANK_VALID here means the staging slot contains an image header with MAGIC_APP,
	 * i.e. an image is present in flash and has basic metadata recorded.
	 * It does NOT guarantee the full payload CRC has been re-verified for swap/recovery/boot use.
	 * Any path that really wants to use a staging image must do a strong verification again
	 * (header + payload CRC match against the recorded BCB metadata) before copy/swap/boot.
	 */
	uint32_t staging_a_invalid;
	uint32_t staging_b_invalid;

	bcb_image_info_t app_bank;
	bcb_image_info_t staging_a;
	bcb_image_info_t staging_b;
} boot_bcb_t;

typedef char app_image_header_t_size_check[(sizeof(app_image_header_t) == 64) ? 1 : -1];
typedef char bcb_image_info_t_size_check[(sizeof(bcb_image_info_t) == 48) ? 1 : -1];
typedef char boot_bcb_t_size_check[(sizeof(boot_bcb_t) == 168) ? 1 : -1];

#define BCB_DATA_SIZE   (sizeof(boot_bcb_t))

typedef enum {
	BCB_ACTIVE_INIT = 0x00,
    BCB_ACTIVE_STAGING_A,
    BCB_ACTIVE_STAGING_B,
    BCB_ACTIVE_ALL_FAIL
} bcb_active_t;

typedef enum {
	BCB_ACTION_NORMAL = 0x00,
    BCB_ACTION_UPDATE,
    BCB_ACTION_SWAP,
    BCB_ACTION_STAY_LOOP
} bcb_action_cmd_t;

typedef enum {
	/* VALID means "image present in staging" according to header magic / recorded metadata.
	 * It is NOT equivalent to "CRC-verified runnable image".
	 * Before a staging image is really used for swap/recovery/boot, code must perform strong verification again.
	 */
	BCB_BANK_VALID = 0x00,
    BCB_BANK_INVALID
} bcb_bank_valid_t;

#if 0 // may be kill
typedef enum
{
  ResetParamsNoFlags    = 0x00,               /**< \brief No flags utility value.      */
  ResetParamsReqTcpInit = 0x01                /**< \brief Request TCP/IP stack init.   */
} tResetParamsFlagType;
#endif






#endif /* MEMORY_ADDRESS_H */
