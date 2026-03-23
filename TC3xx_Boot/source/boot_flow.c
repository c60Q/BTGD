


/* ========================================================================== */
/*                iLLD-backed Flash ops for TC399 (guarded)                   */
/* ========================================================================== */
/*
 * Define USE_AURIX_ILLD when building with AURIX iLLD.
 * This implementation uses the IfxFlash API for sector erase and page program.
 * Notes (please adjust to your exact device variant):
 *  - PFLASH programming requires page-aligned writes (256 bytes) on TC3xx.
 *  - Sector erase works on logical sector boundaries (commonly 16 KB or larger).
 *  - Endinit/Safety Endinit must be handled during flash ops.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>


#include "IfxScuWdt.h"
#include "IfxFlash.h"
#include "IfxCpu.h"
#include "netdev.h"
#include "boot_flow.h"
#include "boot_transport_parameter.h"
#include "boot_transport.h"
#include "boot_flash.h"
#include "boot_types.h"
#include "boot_timer.h"

/* NOTE:
 * BCB storage is no longer occupied by a linker-placed .bcb_block section.
 * The current .lsl reserves the whole 4KB DFLASH sector at BCB_ADDR using
 * BCB_RESERVED_GROUP / reserved "BCB_RESERVED".
 * Therefore boot code must access and maintain BCB only through the fixed
 * memory-mapped address BCB_ADDR and the flash erase/program helpers below.
 */

boot_state_t flowStepStatus = boot_power_on_init;
boot_flow_Flag_t flowCtrlFlag = {0x00};
static bool g_bcbWriteFail = false;
static uint8_t retry_eth = 0;

uint32_t crc32_calc_buf(const void *data, size_t len) //CRC-32/ISO-HDLC or CRC-32/ADCCP or CRC-32/V.42
{
    const uint8_t *p = (const uint8_t*)data;
    uint32_t crc = ~0u;
    for (size_t i = 0; i < len; ++i)
    {
        crc ^= p[i];
        for (int k = 0; k < 8; ++k)
            crc = (crc >> 1) ^ (0xEDB88320u & (-(int)(crc & 1)));
    }
    return ~crc;
}

uint32_t crc32_region(uint32_t addr, uint32_t size)
{
    const uint8_t *p = (const uint8_t*)(uintptr_t)addr;
    return crc32_calc_buf(p, size);
}

#define BCB_IMAGE_VERSION_LEN (sizeof(((bcb_image_info_t *)0)->image_version))

static int memcmp_volatile_bytes(const volatile void *lhs, const volatile void *rhs, size_t len)
{
    const volatile uint8_t *a = (const volatile uint8_t *)lhs;
    const volatile uint8_t *b = (const volatile uint8_t *)rhs;

    for(size_t i = 0; i < len; ++i){
        if(a[i] != b[i]){
            return (a[i] < b[i]) ? -1 : 1;
        }
    }
    return 0;
}

static void memcpy_from_volatile_bytes(void *dst, const volatile void *src, size_t len)
{
    uint8_t *d = (uint8_t *)dst;
    const volatile uint8_t *s = (const volatile uint8_t *)src;

    for(size_t i = 0; i < len; ++i){
        d[i] = s[i];
    }
}

static void copy_bcb_image_info_from_volatile(bcb_image_info_t *dst, const volatile bcb_image_info_t *src)
{
    dst->image_magic = src->image_magic;
    dst->reserved_for_magic = src->reserved_for_magic;
    memcpy_from_volatile_bytes(dst->image_version, src->image_version, BCB_IMAGE_VERSION_LEN);
    dst->image_base = src->image_base;
    dst->image_size = src->image_size;
    dst->image_crc = src->image_crc;
    dst->reserved = src->reserved;
}

/* BCB DFLASH write helper (check return code + readback verify) */
static int BCB_Write_Checked(const boot_bcb_t *src)
{
    int rc;

    rc = D_Flash_Erase_Range(BCB_ADDR, BCB_LENGTH_4KB);
    if (rc != 0) {
        return rc;
    }

    rc = D_Flash_Program(BCB_ADDR, src, (uint32_t)sizeof(*src));
    if (rc != 0) {
        return rc;
    }

    /* Readback verify (DFLASH is memory-mapped) */
	volatile const boot_bcb_t *rb = (volatile const boot_bcb_t *)BCB_ADDR;
	boot_bcb_t rb_copy;
	memcpy(&rb_copy, (const void *)rb, sizeof(rb_copy));

	/* Core-field sanity check first: catches extreme partial writes without
	 * depending on raw whole-struct memcmp semantics. */
	if(rb_copy.magic != MAGIC_BCB){
		return -1;
	}
	if((rb_copy.action_cmd > (uint32_t)BCB_ACTION_STAY_LOOP) ||
		(rb_copy.active_app > (uint32_t)BCB_ACTIVE_ALL_FAIL) ||
		(rb_copy.staging_a_invalid > (uint32_t)BCB_BANK_INVALID) ||
		(rb_copy.staging_b_invalid > (uint32_t)BCB_BANK_INVALID)){
		return -2;
	}
	if((rb_copy.app_bank.image_magic != 0u) && (rb_copy.app_bank.image_magic != MAGIC_APP)){
		return -3;
	}
	if((rb_copy.staging_a.image_magic != 0u) && (rb_copy.staging_a.image_magic != MAGIC_APP)){
		return -4;
	}
	if((rb_copy.staging_b.image_magic != 0u) && (rb_copy.staging_b.image_magic != MAGIC_APP)){
		return -5;
	}

	/* Compare only the fixed fields that must match after a successful write */
	if((rb_copy.magic != src->magic) ||
		(rb_copy.action_cmd != src->action_cmd) ||
		(rb_copy.active_app != src->active_app) ||
		(rb_copy.staging_a_invalid != src->staging_a_invalid) ||
		(rb_copy.staging_b_invalid != src->staging_b_invalid)){
		return -6;
	}

	if((rb_copy.app_bank.image_magic != src->app_bank.image_magic) ||
		(rb_copy.app_bank.image_crc != src->app_bank.image_crc) ||
		(rb_copy.app_bank.image_base != src->app_bank.image_base) ||
		(rb_copy.app_bank.image_size != src->app_bank.image_size) ||
		(memcmp(rb_copy.app_bank.image_version, src->app_bank.image_version, sizeof(rb_copy.app_bank.image_version)) != 0)){
		return -7;
	}

	if((rb_copy.staging_a.image_magic != src->staging_a.image_magic) ||
		(rb_copy.staging_a.image_crc != src->staging_a.image_crc) ||
		(rb_copy.staging_a.image_base != src->staging_a.image_base) ||
		(rb_copy.staging_a.image_size != src->staging_a.image_size) ||
		(memcmp(rb_copy.staging_a.image_version, src->staging_a.image_version, sizeof(rb_copy.staging_a.image_version)) != 0)){
		return -8;
	}

	if((rb_copy.staging_b.image_magic != src->staging_b.image_magic) ||
		(rb_copy.staging_b.image_crc != src->staging_b.image_crc) ||
		(rb_copy.staging_b.image_base != src->staging_b.image_base) ||
		(rb_copy.staging_b.image_size != src->staging_b.image_size) ||
		(memcmp(rb_copy.staging_b.image_version, src->staging_b.image_version, sizeof(rb_copy.staging_b.image_version)) != 0)){
		return -9;
	}

    return 0;
}


/* ----------------------- BCB sanity & recovery ----------------------- */
static bool BCB_IsSane(volatile const boot_bcb_t *b)
{
	if(b == NULL) return false;
	if(b->magic != MAGIC_BCB) return false;

	/* check BCB data over spec or not */
	if(b->action_cmd > (uint32_t)BCB_ACTION_STAY_LOOP){
		return false;
	}
	if(b->active_app > (uint32_t)BCB_ACTIVE_ALL_FAIL){
		return false;
	}
	if(b->staging_a_invalid > (uint32_t)BCB_BANK_INVALID){
		return false;
	}
	if(b->staging_b_invalid > (uint32_t)BCB_BANK_INVALID){
		return false;
	}

	/* For NORMAL/UPDATE/SWAP we must have a concrete active slot */
	if(b->action_cmd != (uint32_t)BCB_ACTION_STAY_LOOP){
		if(b->active_app != (uint32_t)BCB_ACTIVE_STAGING_A &&
			b->active_app != (uint32_t)BCB_ACTIVE_STAGING_B){
			return false;
		}
	}

	/* Active slot must not contradict its validity / image header state */
	if(b->active_app == (uint32_t)BCB_ACTIVE_STAGING_A){
		if(b->staging_a_invalid == (uint32_t)BCB_BANK_INVALID || b->staging_a.image_magic != MAGIC_APP){
			return false;
		}

		/* app_bank must describe the currently active staging image */
		if(b->app_bank.image_magic != b->staging_a.image_magic ||
			b->app_bank.image_crc  != b->staging_a.image_crc  ||
			memcmp_volatile_bytes(b->app_bank.image_version, b->staging_a.image_version, BCB_IMAGE_VERSION_LEN) != 0){
			return false;
		}
	}
	else if(b->active_app == (uint32_t)BCB_ACTIVE_STAGING_B){
		if(b->staging_b_invalid == (uint32_t)BCB_BANK_INVALID || b->staging_b.image_magic != MAGIC_APP){
			return false;
		}

		/* app_bank must describe the currently active staging image */
		if(b->app_bank.image_magic != b->staging_b.image_magic ||
			b->app_bank.image_crc  != b->staging_b.image_crc  ||
			memcmp_volatile_bytes(b->app_bank.image_version, b->staging_b.image_version, BCB_IMAGE_VERSION_LEN) != 0){
			return false;
		}
	}

	return true;
}

static void BCB_FillImageInfoFromMeta(bcb_image_info_t *dst,
                                     const app_image_header_t *meta,
                                     uint32_t image_base,
                                     uint32_t image_size,
                                     uint32_t payload_crc)
{
    memset(dst, 0, sizeof(*dst));
    if (meta != NULL) {
        dst->image_magic = meta->magic;
        memcpy(dst->image_version, meta->version, sizeof(dst->image_version));
    }
    dst->image_base = image_base;
    dst->image_size = image_size;
    dst->image_crc  = payload_crc;
}

/* Rebuild BCB based on what is already in flash.
 * Rebuild only records an "image present" view for staging slots from header magic,
 * not a "CRC-verified ready for swap/boot" state.
 * If ACTIVE is valid but neither staging slot matches ACTIVE, do not guess which
 * staging copy is trustworthy: mark both staging slots INVALID, switch BCB to
 * STAY_LOOP, and leave recovery to external reflashing.
 * Rewrites DFLASH BCB to recover a sane boot decision state from current flash contents.
 */
static int BCB_RebuildFromFlash(void)
{
	app_image_header_t meta_app = {0};
	app_image_header_t meta_sa = {0};
	app_image_header_t meta_sb = {0};

	(void)Meta_Read_Out(&meta_app, PFLASH_APP_BASE);
	(void)Meta_Read_Out(&meta_sa,  PFLASH_STAGING_A_BASE);
	(void)Meta_Read_Out(&meta_sb,  PFLASH_STAGING_B_BASE);

	uint32_t act_crc = 0u;
	uint32_t sa_crc  = 0u;
	uint32_t sb_crc  = 0u;

	if(meta_app.magic == MAGIC_APP){
		act_crc = crc32_region(PFLASH_APP_PAYLOAD_BASE, PFLASH_APP_PAYLOAD_SIZE);
	}
	if(meta_sa.magic == MAGIC_APP){
		sa_crc = crc32_region(PFLASH_STAGING_A_PAYLOAD_BASE, PFLASH_STAGING_A_PAYLOAD_SIZE);
	}
	if(meta_sb.magic == MAGIC_APP){
		sb_crc = crc32_region(PFLASH_STAGING_B_PAYLOAD_BASE, PFLASH_STAGING_B_PAYLOAD_SIZE);
	}

	boot_bcb_t bcb_tmp;
	memset(&bcb_tmp, 0, sizeof(bcb_tmp));
	bcb_tmp.magic = MAGIC_BCB;

	/* Default to NORMAL after recovery (avoid endless UPDATE/SWAP loops due to corrupted action_cmd) */
	bcb_tmp.action_cmd = BCB_ACTION_NORMAL;

	/* Populate image info from flash.
	 * For staging slots, rebuild only reconstructs an "image present" view from flash:
	 * if the header magic is MAGIC_APP, the slot is marked BCB_BANK_VALID and its metadata is recorded.
	 * This VALID state is intentionally weak: it does NOT mean the staging image is already approved
	 * for swap/recovery/boot use. Any actual use path must still perform strong verification again
	 * (re-read header + recalculate payload CRC + compare against BCB-recorded metadata).
	 */
	BCB_FillImageInfoFromMeta(&bcb_tmp.app_bank, &meta_app, PFLASH_APP_BASE, PFLASH_APP_SIZE, act_crc);

	BCB_FillImageInfoFromMeta(&bcb_tmp.staging_a, &meta_sa, PFLASH_STAGING_A_BASE, PFLASH_APP_SIZE, sa_crc);
	bcb_tmp.staging_a_invalid = (meta_sa.magic == MAGIC_APP) ? BCB_BANK_VALID : BCB_BANK_INVALID;

	BCB_FillImageInfoFromMeta(&bcb_tmp.staging_b, &meta_sb, PFLASH_STAGING_B_BASE, PFLASH_APP_SIZE, sb_crc);
	bcb_tmp.staging_b_invalid = (meta_sb.magic == MAGIC_APP) ? BCB_BANK_VALID : BCB_BANK_INVALID;

	if(meta_app.magic != MAGIC_APP){
		/* No valid active image -> stay loop */
		bcb_tmp.active_app = BCB_ACTIVE_ALL_FAIL;
		bcb_tmp.action_cmd = BCB_ACTION_STAY_LOOP;
		memset(&bcb_tmp.app_bank, 0, sizeof(bcb_tmp.app_bank));
		return BCB_Write_Checked(&bcb_tmp);
	}

	/* Decide which staging slot represents current ACTIVE image.
	 * Matching is strict here: a staging slot only becomes the logical ACTIVE owner if its recorded image
	 * metadata still matches the current ACTIVE payload CRC. If neither slot matches ACTIVE, rebuild does not
	 * guess which staging copy is correct; the design enters STAY_LOOP and invalidates both staging slots.
	 */
	if(bcb_tmp.staging_a_invalid == BCB_BANK_VALID &&
		bcb_tmp.staging_a.image_magic == MAGIC_APP &&
		bcb_tmp.staging_a.image_crc == act_crc){
		bcb_tmp.active_app = BCB_ACTIVE_STAGING_A;
	}
	else if(bcb_tmp.staging_b_invalid == BCB_BANK_VALID &&
				bcb_tmp.staging_b.image_magic == MAGIC_APP &&
				bcb_tmp.staging_b.image_crc == act_crc){
		bcb_tmp.active_app = BCB_ACTIVE_STAGING_B;
    }
	else{
		/* ACTIVE exists, but neither staging slot matches ACTIVE.
		 * Enter STAY_LOOP and invalidate both staging slots, so user must recover by external reflashing. */
		bcb_tmp.active_app = BCB_ACTIVE_ALL_FAIL;
		bcb_tmp.action_cmd = BCB_ACTION_STAY_LOOP;

		bcb_tmp.app_bank.image_magic = 0u;
		memset(bcb_tmp.app_bank.image_version, 0, sizeof(bcb_tmp.app_bank.image_version));
		bcb_tmp.app_bank.image_crc = 0u;
		bcb_tmp.app_bank.image_base = 0u;
		bcb_tmp.app_bank.image_size = 0u;

		bcb_tmp.staging_a.image_magic = 0u;
		memset(bcb_tmp.staging_a.image_version, 0, sizeof(bcb_tmp.staging_a.image_version));
		bcb_tmp.staging_a.image_crc = 0u;
		bcb_tmp.staging_a_invalid = BCB_BANK_INVALID;

		bcb_tmp.staging_b.image_magic = 0u;
		memset(bcb_tmp.staging_b.image_version, 0, sizeof(bcb_tmp.staging_b.image_version));
		bcb_tmp.staging_b.image_crc = 0u;
		bcb_tmp.staging_b_invalid = BCB_BANK_INVALID;
	}

	return BCB_Write_Checked(&bcb_tmp);
}

bool Image_And_Meta_InIt(app_image_header_t *meta_app, app_image_header_t *meta_stagingA)
{
	if((meta_app == NULL) || (meta_stagingA == NULL)){
		return false;
	}

	if(!Meta_Read_Out(meta_app, PFLASH_APP_BASE)){
		return false;
	}

	if(meta_app->magic != MAGIC_APP){
		return false;
	}

	if(Copy_Bank_to_Bank(PFLASH_APP_BASE, PFLASH_STAGING_A_BASE) != 0){
	    return false;
	}

	if(!Meta_Read_Out(meta_stagingA, PFLASH_STAGING_A_BASE)){
		return false;
	}
	if(meta_stagingA->magic != MAGIC_APP){
		return false;
	}

	meta_app->crc = crc32_region(PFLASH_APP_PAYLOAD_BASE, PFLASH_APP_PAYLOAD_SIZE);
	meta_stagingA->crc = crc32_region(PFLASH_STAGING_A_PAYLOAD_BASE, PFLASH_STAGING_A_PAYLOAD_SIZE);

	if(meta_stagingA->crc != meta_app->crc){
		return false;
	}
	
	return true;
}

static int BCB_Init_Default(app_image_header_t *m_app, app_image_header_t *m_stag_a)
{
    boot_bcb_t bcb_tmp;
    memset(&bcb_tmp, 0, sizeof(boot_bcb_t));

    bcb_tmp.magic = MAGIC_BCB;
	bcb_tmp.active_app = BCB_ACTIVE_STAGING_A;
    bcb_tmp.action_cmd = BCB_ACTION_NORMAL;
	
    bcb_tmp.app_bank.image_magic = m_app->magic;
    memcpy(bcb_tmp.app_bank.image_version, m_app->version, sizeof(bcb_tmp.app_bank.image_version));
    bcb_tmp.app_bank.image_crc = m_app->crc;
    bcb_tmp.app_bank.image_base = PFLASH_APP_BASE;
	bcb_tmp.app_bank.image_size   = PFLASH_APP_SIZE;
	    
    bcb_tmp.staging_a.image_magic = m_stag_a->magic;
    memcpy(bcb_tmp.staging_a.image_version, m_stag_a->version, sizeof(bcb_tmp.staging_a.image_version));
    bcb_tmp.staging_a.image_crc = m_stag_a->crc;
    bcb_tmp.staging_a.image_base = PFLASH_STAGING_A_BASE;
	bcb_tmp.staging_a_invalid = BCB_BANK_VALID;
	bcb_tmp.staging_a.image_size  = PFLASH_APP_SIZE;

    bcb_tmp.staging_b.image_base = PFLASH_STAGING_B_BASE;
	bcb_tmp.staging_b_invalid = BCB_BANK_INVALID;
	bcb_tmp.staging_b.image_size  = PFLASH_APP_SIZE;

	return BCB_Write_Checked(&bcb_tmp);
}

static int BCB_Record_Active_Info(const app_image_header_t *meta, uint32_t act_crc)
{
	volatile const boot_bcb_t *bcb = BCB_Read_Out();
	boot_bcb_t b_tmp;
	memcpy(&b_tmp, (const void *)bcb, sizeof(b_tmp));

	if((meta == NULL) || (meta->magic != MAGIC_APP)){
		return -11;
	}

	/* Fast path: skip DFlash rewrite only when BCB is already in a sane, normalized state. */
	if((b_tmp.magic == MAGIC_BCB) &&
		(b_tmp.action_cmd == BCB_ACTION_NORMAL) &&
		((b_tmp.active_app == BCB_ACTIVE_STAGING_A) || (b_tmp.active_app == BCB_ACTIVE_STAGING_B)) &&
		(b_tmp.app_bank.image_magic == meta->magic) &&
		(memcmp(b_tmp.app_bank.image_version, meta->version, sizeof(b_tmp.app_bank.image_version)) == 0) &&
		(b_tmp.app_bank.image_crc == act_crc) &&
		(b_tmp.app_bank.image_base == PFLASH_APP_BASE) &&
		(b_tmp.app_bank.image_size == PFLASH_APP_SIZE)){
		return 0;
	}

	b_tmp.magic = MAGIC_BCB;
	b_tmp.action_cmd = BCB_ACTION_NORMAL;
	/* Normalize active_app if it drifted into an invalid state. */
	if((b_tmp.active_app != BCB_ACTIVE_STAGING_A) && (b_tmp.active_app != BCB_ACTIVE_STAGING_B)){
		if(b_tmp.staging_a_invalid == BCB_BANK_VALID){
			b_tmp.active_app = BCB_ACTIVE_STAGING_A;
		}
		else if(b_tmp.staging_b_invalid == BCB_BANK_VALID){
			b_tmp.active_app = BCB_ACTIVE_STAGING_B;
		}
		else{
			b_tmp.active_app = BCB_ACTIVE_STAGING_A;
		}
	}
	memset(&b_tmp.app_bank, 0, sizeof(b_tmp.app_bank));
	b_tmp.app_bank.image_magic = meta->magic;
	memcpy(b_tmp.app_bank.image_version, meta->version, sizeof(b_tmp.app_bank.image_version));
	b_tmp.app_bank.image_crc  = act_crc;
	b_tmp.app_bank.image_base = PFLASH_APP_BASE;
	b_tmp.app_bank.image_size = PFLASH_APP_SIZE;

	return BCB_Write_Checked(&b_tmp);
}

static bool Try_Boot_Target(uint32_t start, uint32_t size)
{
	/* Normalize to Segment10 (0xA...) */
	uint32_t start_n = (uint32_t)PFLASH_NORM_ADDR(start);
	if(size <= APP_IMAGE_BODY_OFFSET){
		return false;
	}

	uint32_t end_n = start_n + size;
	if(end_n < start_n){
		return false; /* overflow */
	}

	/* Entry is fixed by .lsl: image body begins after the reserved 16KB header sector, and .text.start is first */
	uint32_t entry = start_n + APP_IMAGE_BODY_OFFSET;

	/* Basic range check */
	if(entry >= end_n){
		return false;
	}
	jump_to_entry(entry);

	/* should never return */
	return false;
}

int BCB_Data_Update(bcb_update_state_t action)
{
	bool activeWrite = true;
	volatile const boot_bcb_t *bcb = BCB_Read_Out();
	boot_bcb_t b_tmp;
	memcpy(&b_tmp, (const void *)bcb, sizeof(b_tmp));
	b_tmp.magic = MAGIC_BCB;
	
	switch(action){
		case BCB_UPDATE_BOOT_UP_FAIL:
			memset(&b_tmp.app_bank, 0, sizeof(b_tmp.app_bank));
			if(bcb->active_app == BCB_ACTIVE_STAGING_A){
				b_tmp.staging_a_invalid = BCB_BANK_INVALID;
				memset(&b_tmp.staging_a, 0, sizeof(b_tmp.staging_a));
			}
			else if(bcb->active_app == BCB_ACTIVE_STAGING_B){
				b_tmp.staging_b_invalid = BCB_BANK_INVALID;
				memset(&b_tmp.staging_b, 0, sizeof(b_tmp.staging_a));
			}
			else{
				activeWrite = false;
				break;
			}

			if((b_tmp.staging_a_invalid == (uint32_t)BCB_BANK_INVALID) && (b_tmp.staging_b_invalid == (uint32_t)BCB_BANK_INVALID)){
				b_tmp.active_app = (uint32_t)BCB_ACTIVE_ALL_FAIL;
				b_tmp.action_cmd = (uint32_t)BCB_ACTION_STAY_LOOP;
			}
			else{
			/* keep fallback opportunity for next decision */
				b_tmp.action_cmd = (uint32_t)BCB_ACTION_NORMAL;
			}
			activeWrite = true;
			break;
		case BCB_UPDATE_SWAP:
			b_tmp.action_cmd = BCB_ACTION_NORMAL;
			if(bcb->active_app == BCB_ACTIVE_STAGING_A){
				b_tmp.active_app = BCB_ACTIVE_STAGING_B;
				b_tmp.app_bank.image_magic = bcb->staging_b.image_magic;
	            memcpy_from_volatile_bytes(b_tmp.app_bank.image_version, bcb->staging_b.image_version, BCB_IMAGE_VERSION_LEN);
				b_tmp.app_bank.image_crc = bcb->staging_b.image_crc;
				b_tmp.staging_b_invalid = BCB_BANK_VALID;
			}
			else if(bcb->active_app == BCB_ACTIVE_STAGING_B){
				b_tmp.active_app = BCB_ACTIVE_STAGING_A;
				b_tmp.app_bank.image_magic = bcb->staging_a.image_magic;
	            memcpy_from_volatile_bytes(b_tmp.app_bank.image_version, bcb->staging_a.image_version, BCB_IMAGE_VERSION_LEN);
				b_tmp.app_bank.image_crc = bcb->staging_a.image_crc;
				b_tmp.staging_a_invalid = BCB_BANK_VALID;
			}
			else{
				activeWrite = false;
			}
			break;
		case BCB_UPDATE_SWAP_FAIL:
			/* verify fail: invalidate target staging slot */
			b_tmp.action_cmd = BCB_ACTION_NORMAL;
			if(bcb->active_app == BCB_ACTIVE_STAGING_A){
				b_tmp.staging_b.image_magic = 0u;
				memset(b_tmp.staging_b.image_version, 0, sizeof(b_tmp.staging_b.image_version));
				b_tmp.staging_b.image_crc = 0u;
				b_tmp.staging_b_invalid = BCB_BANK_INVALID;
			}
			else if(bcb->active_app == BCB_ACTIVE_STAGING_B){
				b_tmp.staging_a.image_magic = 0u;
				memset(b_tmp.staging_a.image_version, 0, sizeof(b_tmp.staging_a.image_version));
				b_tmp.staging_a.image_crc = 0u;
				b_tmp.staging_a_invalid = BCB_BANK_INVALID;
			}
			else{
				activeWrite = false;
			}
			
			if(b_tmp.staging_a_invalid == BCB_BANK_INVALID && b_tmp.staging_b_invalid == BCB_BANK_INVALID){
				b_tmp.active_app = BCB_ACTIVE_ALL_FAIL;
				b_tmp.action_cmd = BCB_ACTION_STAY_LOOP;
			}
			break;
		case BCB_UPDATE_SWAP_IO_FAIL:{
			    /* Swap copy/erase/program I/O failed may happen in:
			     * 1. explicit SWAP flow (action_cmd == SWAP)
			     * 2. fallback swap path from boot_try_another_app()
			     *
			     * In both cases, keep current active selection unchanged and
			     * return action_cmd to NORMAL so next boot decision can continue
			     * from a sane baseline.
			     */
			    if((bcb->active_app == BCB_ACTIVE_STAGING_A) ||  (bcb->active_app == BCB_ACTIVE_STAGING_B)){
			        b_tmp.action_cmd = BCB_ACTION_NORMAL;
			        activeWrite = true;
			    }
			    break;
			}
			break;
		case BCB_UPDATE_STAGING_NEW_IMAGE_CHECK:{
			if((bcb->active_app != BCB_ACTIVE_STAGING_A) && (bcb->active_app != BCB_ACTIVE_STAGING_B)){
				activeWrite = false;
				break;
			}
			
			app_image_header_t m_data ={0};
			uint32_t crcTemp;
			const bool updateB = (bcb->active_app == BCB_ACTIVE_STAGING_A);
			const uint32_t stag_base = updateB ? PFLASH_STAGING_B_BASE : PFLASH_STAGING_A_BASE;
			const uint32_t payload_size = updateB ? PFLASH_STAGING_B_PAYLOAD_SIZE : PFLASH_STAGING_A_PAYLOAD_SIZE;
			const uint32_t payload_base = updateB ? PFLASH_STAGING_B_PAYLOAD_BASE : PFLASH_STAGING_A_PAYLOAD_BASE;
			
			if(!Meta_Read_Out(&m_data, stag_base) || m_data.magic != MAGIC_APP){
				if(updateB){
					memset(&b_tmp.staging_b, 0, sizeof(b_tmp.staging_b));
					b_tmp.staging_b_invalid = BCB_BANK_INVALID;
				}
				else{
					memset(&b_tmp.staging_a, 0, sizeof(b_tmp.staging_a));
					b_tmp.staging_a_invalid = BCB_BANK_INVALID;
				}
				activeWrite = true;
				b_tmp.action_cmd = BCB_ACTION_NORMAL;
				break;
			}

		    crcTemp = crc32_region(payload_base, payload_size);

		    if(updateB){
				BCB_FillImageInfoFromMeta(&b_tmp.staging_b, &m_data, PFLASH_STAGING_B_BASE, PFLASH_APP_SIZE, crcTemp);
		        b_tmp.staging_b_invalid = BCB_BANK_VALID;
		    }
			else{
				BCB_FillImageInfoFromMeta(&b_tmp.staging_a, &m_data, PFLASH_STAGING_A_BASE, PFLASH_APP_SIZE, crcTemp);
		        b_tmp.staging_a_invalid = BCB_BANK_VALID;
		    }
			b_tmp.action_cmd = BCB_ACTION_NORMAL;
			activeWrite = true;
		    break;
		}

		case BCB_UPDATE_STAGING_FAIL:{
			if(bcb->active_app != BCB_ACTIVE_STAGING_A && bcb->active_app != BCB_ACTIVE_STAGING_B){
				activeWrite = false;
				break;
			}
			
			const bool updateB = (bcb->active_app == BCB_ACTIVE_STAGING_A);
			if(updateB){
				b_tmp.staging_b_invalid = BCB_BANK_INVALID;
				b_tmp.staging_b.image_magic = 0u;
				memset(b_tmp.staging_b.image_version, 0, sizeof(b_tmp.staging_b.image_version));
				b_tmp.staging_b.image_crc = 0u;
				b_tmp.staging_b_invalid = BCB_BANK_INVALID;
			}
			else{
				b_tmp.staging_a_invalid = BCB_BANK_INVALID;
				b_tmp.staging_a.image_magic = 0u;
				memset(b_tmp.staging_a.image_version, 0, sizeof(b_tmp.staging_a.image_version));
				b_tmp.staging_a.image_crc = 0u;
				b_tmp.staging_a_invalid = BCB_BANK_INVALID;
			}
			b_tmp.action_cmd = BCB_ACTION_NORMAL;
			break;
		}
		case BCB_UPDATE_ACTIVE_SUCCESS:{
			if(bcb->active_app != BCB_ACTIVE_STAGING_A && bcb->active_app != BCB_ACTIVE_STAGING_B){
				activeWrite = false;
				break;
			}
		    const bool srcIsB = (bcb->active_app == BCB_ACTIVE_STAGING_A);
		    bcb_image_info_t src;
		    copy_bcb_image_info_from_volatile(&src, srcIsB ? &bcb->staging_b : &bcb->staging_a);

		    b_tmp.action_cmd = BCB_ACTION_NORMAL;
		    b_tmp.active_app = srcIsB ? BCB_ACTIVE_STAGING_B : BCB_ACTIVE_STAGING_A;
		    b_tmp.app_bank.image_magic = src.image_magic;
		    memcpy(b_tmp.app_bank.image_version, src.image_version, sizeof(b_tmp.app_bank.image_version));
		    b_tmp.app_bank.image_crc = src.image_crc;

			if(srcIsB){
				b_tmp.staging_b_invalid = BCB_BANK_VALID;
			}
			else{
				b_tmp.staging_a_invalid = BCB_BANK_VALID;
			}

			
			break;
		}
		default:
			activeWrite = false;
			break;
	}
    if(activeWrite){
		int wCheck;
		wCheck = BCB_Write_Checked(&b_tmp);
		if(wCheck != 0){
			g_bcbWriteFail = true;
		}
		return wCheck;
	}
	else{
		g_bcbWriteFail = true;
		return -10;
	}
}

static bool Staging_Verify_For_Use(bool useB, app_image_header_t *meta_out, uint32_t *crc_out)
{
	volatile const boot_bcb_t *b = BCB_Read_Out();
	bcb_image_info_t slot;
	copy_bcb_image_info_from_volatile(&slot, useB ? &b->staging_b : &b->staging_a);
	const uint32_t slot_invalid = useB ? b->staging_b_invalid : b->staging_a_invalid;
	const uint32_t slot_base = useB ? PFLASH_STAGING_B_BASE : PFLASH_STAGING_A_BASE;
	const uint32_t payload_base = useB ? PFLASH_STAGING_B_PAYLOAD_BASE : PFLASH_STAGING_A_PAYLOAD_BASE;
	const uint32_t payload_size = useB ? PFLASH_STAGING_B_PAYLOAD_SIZE : PFLASH_STAGING_A_PAYLOAD_SIZE;
	app_image_header_t meta = {0};
	uint32_t crcTemp = 0u;

	/* "VALID" in BCB only means an image is present. Before swap/recovery/copy-to-active,
	 * do a strong local verification here: header magic + payload CRC vs BCB record. */
	if(slot_invalid != BCB_BANK_VALID){
		return false;
	}
	if(slot.image_magic != MAGIC_APP){
		return false;
	}
	if(!Meta_Read_Out(&meta, slot_base) || meta.magic != MAGIC_APP){
		return false;
	}

	crcTemp = crc32_region(payload_base, payload_size);
	if(slot.image_crc != crcTemp){
		return false;
	}
	if(memcmp(slot.image_version, meta.version, sizeof(slot.image_version)) != 0){
		return false;
	}

	if(meta_out != NULL){
		*meta_out = meta;
	}
	if(crc_out != NULL){
		*crc_out = crcTemp;
	}
	return true;
}

static bool Normal_Boot_Up(void)
{
	volatile const boot_bcb_t *bcb = BCB_Read_Out();
	app_image_header_t meta = {0};
	uint32_t act_app_crc = 0u;
	uint32_t expect_crc = 0u;
	bool state = true;

	/* Basic BCB presence check */
	if(bcb->magic != MAGIC_BCB){
		state = false;
		(void)BCB_Data_Update(BCB_UPDATE_BOOT_UP_FAIL);
		return state;
	}

	/* ACTIVE header must exist (staging is optional per system rule) */
	if(!Meta_Read_Out(&meta, PFLASH_APP_BASE) || (meta.magic != MAGIC_APP)){
		state = false;
		(void)BCB_Data_Update(BCB_UPDATE_BOOT_UP_FAIL);
		return state;
	}

	/* Verify ACTIVE payload CRC: prefer header CRC if present, else fall back to BCB app_bank CRC */
	act_app_crc = crc32_region(PFLASH_APP_PAYLOAD_BASE, PFLASH_APP_PAYLOAD_SIZE);

	if(meta.crc != 0u){
		expect_crc = meta.crc;
	}
	else if(bcb->app_bank.image_magic == MAGIC_APP){
		expect_crc = bcb->app_bank.image_crc;
	}
	else{
		expect_crc = 0u; /* no reference available */
	}

	if(expect_crc != 0u){
		state = (act_app_crc == expect_crc);
	}
	else{
		/* No reference CRC available; deny boot (fail-safe) */
		state = false;
	}

	if(!state){
		(void)BCB_Data_Update(BCB_UPDATE_BOOT_UP_FAIL);
		return state;
	}

	/* Record ACTIVE info to BCB without re-computing CRC */
	if(BCB_Record_Active_Info(&meta, act_app_crc) != 0){
		g_bcbWriteFail = true;
		return false;
	}

	/* Jump to ACTIVE app image */
	if(!Try_Boot_Target(PFLASH_APP_BASE, PFLASH_APP_SIZE)){
		state = false;
		(void)BCB_Data_Update(BCB_UPDATE_BOOT_UP_FAIL);
	}
    return state;
}

static bool Swap_Image(void)
{
	/* Swap_Image() is a real-use path, so BCB "VALID" is only an image-present hint and is not sufficient by itself.
	 * Even if rebuild/update previously marked a staging slot as VALID, swap must still perform
	 * strong verification before using it: the slot must be logically available in BCB, its header
	 * must still contain MAGIC_APP, and its payload CRC must still match the metadata recorded in BCB.
	 * Only after that verification passes may the image be copied into ACTIVE.
	 */
	volatile const boot_bcb_t *b = BCB_Read_Out();
	bool changeToB = (b->active_app == BCB_ACTIVE_STAGING_A);
	uint32_t source_base = changeToB ? PFLASH_STAGING_B_BASE : PFLASH_STAGING_A_BASE;

	if(b->active_app != BCB_ACTIVE_STAGING_A && b->active_app != BCB_ACTIVE_STAGING_B) {
	    return boot_FALSE;
	}

	if(!Staging_Verify_For_Use(changeToB, NULL, NULL)){
		/* Strong verification failed.
		 * This is different from the weak rebuild-time "image present" meaning of BCB_BANK_VALID:
		 * here we already decided to use the staging image, so a CRC/header mismatch means the slot must
		 * be treated as unusable for swap and invalidated in BCB.
		 */
		(void)BCB_Data_Update(BCB_UPDATE_SWAP_FAIL);
		return boot_FALSE;
	}

	/* staging verified OK, now try to copy to active bank */
	int rc = Copy_Bank_to_Bank(source_base, PFLASH_APP_BASE);
	if(rc == 0){
		(void)BCB_Data_Update(BCB_UPDATE_SWAP);
		return boot_TRUE;
	}
	/* copy/program/erase fail: do NOT invalidate staging (treat as I/O failure) */
	(void)BCB_Data_Update(BCB_UPDATE_SWAP_IO_FAIL);
	return boot_FALSE;
}

static boot_state_t Boot_Action_Command_Check(void)
{
	volatile const boot_bcb_t *bcb = BCB_Read_Out();
	g_bcbWriteFail = false;

	if(bcb->action_cmd == BCB_ACTION_NORMAL){
		bool bootUpGood = Normal_Boot_Up();
		if(g_bcbWriteFail){
			Debug_Print_Out("{BCB DFlash Write Fail}0", 0u, 0, 0u, dbug_num_type_str);
			return boot_loop_stay;
		}
		if(!bootUpGood){
			return boot_try_another_app;
		}
		return boot_loop_stay;
	}
	else if(bcb->action_cmd == BCB_ACTION_UPDATE){
		retry_eth = 0;
		return ETH_Create() ? boot_enable_eth_transceiver : boot_eth_create_retry;
	}
	else if(bcb->action_cmd == BCB_ACTION_SWAP){
		bool swapped = Swap_Image();
		if(g_bcbWriteFail){
			return boot_loop_stay;
		}
		if(!swapped){
			return boot_try_another_app;
		}

		bool bootUpGood = Normal_Boot_Up();
		if(g_bcbWriteFail){
			return boot_loop_stay;
		}
		if(!bootUpGood){
			return boot_try_another_app;
		}
		return boot_loop_stay;
	}
	else{
		return boot_loop_stay;
	}
}

void Boot_Flow_Handler(void)
{
	/* ---- state entry bookkeeping (print-once / timeout base) ---- */
	static boot_state_t s_prev_state = (boot_state_t)boot_prev_state_init_value;
	static uint64 s_wait_bootup_start_s = 0;
	static uint64 s_wait_eth_re_create_s = 0;
	__nop();//qqqq
	if(flowStepStatus != s_prev_state){
		if(flowStepStatus == boot_wait_reply_to_boot_up){
			s_wait_bootup_start_s = getTime().totalSeconds;  /* mark entry time */
		}
		else if(flowStepStatus == boot_eth_create_retry){
			s_wait_eth_re_create_s = getTime().totalSeconds;
		}
		s_prev_state = flowStepStatus;
		debugPrintOnce = true;
	}

    switch(flowStepStatus){
        case boot_power_on_init:
			Debug_Print_Out("{MCU power on init}", 0u, 0, 0u, dbug_num_type_str);
			/***** Can add something want to do before check image data *****/
#if 1	//qqqq
			flowStepStatus = ETH_Create() ? boot_enable_eth_transceiver : boot_eth_create_retry;
			debugPrintOnce = true;
			Debug_Print_Out("IP= 192.168.1.", bootComNetIpaddress3, 0, 0u, dbug_num_type_U32);
			__nop();//qqqq
#endif
			/****************************************************************/
			
//			flowStepStatus = boot_image_data_check;
            break;
        case boot_image_data_check:{
			Debug_Print_Out("{Image, Meta, BCB, check and update}", 0u, 0, 0u, dbug_num_type_str);
			app_image_header_t meta_app = {0}, meta_stagingA = {0};
			volatile const boot_bcb_t *b = BCB_Read_Out();
			if(b->magic != MAGIC_BCB){ // bread now MCU, first power up, only boot and active bank have image
				if(!Image_And_Meta_InIt(&meta_app, &meta_stagingA)){
					flowStepStatus = boot_loop_stay;
					break;
				}
				if(BCB_Init_Default(&meta_app, &meta_stagingA) != 0){
					Debug_Print_Out("{BCB DFlash Write Fail}1", 0u, 0, 0u, dbug_num_type_str);
					flowStepStatus = boot_loop_stay;
					break;
				}
			}
			else if(!BCB_IsSane(b)){
				Debug_Print_Out("{BCB Sanity Fail: Rebuild From Flash}", 0u, 0, 0u, dbug_num_type_str);
				int BCB_Re_state = BCB_RebuildFromFlash();
			
				if(BCB_Re_state != 0){
					Debug_Print_Out("{BCB DFlash Write Fail}2", 0u, 0, 0u, dbug_num_type_str);
					flowStepStatus = boot_loop_stay;
					break;
				}
			}
			flowStepStatus = boot_command_judge;
			break;
		}
		case boot_command_judge:{
			Debug_Print_Out("{BootUp, Update, Swap check}", 0u, 0, 0u, dbug_num_type_str);
			flowCtrlFlag.flowCtrlByte = 0x00;
			flowStepStatus = Boot_Action_Command_Check();
			break;
		}
		case boot_enable_eth_transceiver:
			Debug_Print_Out("{wait connect and transceive start}", 0u, 0, 0u, dbug_num_type_str);
			if(!flowCtrlFlag.bits.netTaskEnable){
				flowCtrlFlag.bits.netTaskEnable = boot_TRUE;
			}
			if(flowCtrlFlag.bits.imageReceiveDone){
				flowCtrlFlag.bits.imageReceiveDone = boot_FALSE;
				flowStepStatus = boot_receive_image_end_crc;
			}
			break;
		case boot_receive_image_end_crc:
			if(flowCtrlFlag.bits.imageEndCrcCorrect){
				if(BCB_Data_Update(BCB_UPDATE_STAGING_NEW_IMAGE_CHECK) != 0){
					Debug_Print_Out("{BCB DFlash Write Fail}3", 0u, 0, 0u, dbug_num_type_str);
					flowStepStatus = boot_loop_stay;
				}
				else{ /* Quick gate only: confirm BCB target slot still looks populated (VALID + MAGIC_APP) before copy-to-ACTIVE. Full source staging verification is deferred to boot_new_image_copy() via Staging_Verify_For_Use(). */
					volatile const boot_bcb_t *b2 = BCB_Read_Out();
					const bool updateB2 = (b2->active_app == BCB_ACTIVE_STAGING_A);
					bcb_image_info_t t;
					copy_bcb_image_info_from_volatile(&t, updateB2 ? &b2->staging_b : &b2->staging_a);
					const uint32_t t_inv = updateB2 ? b2->staging_b_invalid : b2->staging_a_invalid;

					if(t_inv != BCB_BANK_VALID || t.image_magic != MAGIC_APP){
						if(BCB_Data_Update(BCB_UPDATE_STAGING_FAIL) != 0){
							Debug_Print_Out("{BCB DFlash Write Fail}4", 0u, 0, 0u, dbug_num_type_str);
						}
						flowStepStatus = boot_loop_stay;
					}
					else{
						flowStepStatus = boot_new_image_copy;
					}
				}
			}
			else{
				if(BCB_Data_Update(BCB_UPDATE_STAGING_FAIL) != 0){
					Debug_Print_Out("{BCB DFlash Write Fail}5", 0u, 0, 0u, dbug_num_type_str);
				}
				flowStepStatus = boot_loop_stay;
			}
			flowCtrlFlag.bits.imageEndCrcCorrect = boot_FALSE;
			break;
		case boot_new_image_copy:{
			volatile const boot_bcb_t *b = BCB_Read_Out();
			bool copyB = (b->active_app == BCB_ACTIVE_STAGING_A);
			uint32_t source_base = copyB ? PFLASH_STAGING_B_BASE : PFLASH_STAGING_A_BASE;

			/* Before copying a staging image into ACTIVE, do the same strong local verification
			 * used by swap/recovery: header magic + payload CRC vs BCB record. */
			if(!Staging_Verify_For_Use(copyB, NULL, NULL)){
				if(BCB_Data_Update(BCB_UPDATE_STAGING_FAIL) != 0){
					Debug_Print_Out("{BCB DFlash Write Fail}6", 0u, 0, 0u, dbug_num_type_str);
				}
				flowStepStatus = boot_loop_stay;
				break;
			}

			if(!Copy_Bank_to_Bank(source_base, PFLASH_APP_BASE)){
				if(BCB_Data_Update(BCB_UPDATE_ACTIVE_SUCCESS) != 0){
					Debug_Print_Out("{BCB DFlash Write Fail}7", 0u, 0, 0u, dbug_num_type_str);
					flowStepStatus = boot_loop_stay;
					break;
				}
				Active_Send_Request_Set(ACTIVE_SEND_READY_BOOT_UP);
				flowStepStatus = boot_wait_reply_to_boot_up;
			}
			else{ // no need to update BCB because next step will SWAP, so keep the bcb.active_app stay
				Active_Send_Request_Set(ACTIVE_SEND_COPY_BANK_FAIL);
				flowStepStatus = boot_try_another_app;
			}
			break;
		}
		case boot_wait_reply_to_boot_up:
			if(flowCtrlFlag.bits.bootUpAccept){
				flowCtrlFlag.bits.bootUpAccept = boot_FALSE;
				if(!Normal_Boot_Up()){
					flowStepStatus = boot_try_another_app;
				}
			}
			else{
				/* timeout guard if PC never replies */
				if((getTime().totalSeconds - s_wait_bootup_start_s) >= (uint64)BOOTUP_ACCEPT_TIMEOUT_S){
					Debug_Print_Out("{wait boot up accept reply timeout}", 0u, 0, 0u, dbug_num_type_str);
					Active_Send_Request_Set(ACTIVE_SEND_READY_BOOT_UP);
					s_wait_bootup_start_s = getTime().totalSeconds;
					break;
				}
				if(flowCtrlFlag.bits.ethPkgFault == boot_TRUE){
					flowStepStatus = boot_loop_stay;
				}
			}
			break;
        case boot_loop_stay:
			Debug_Print_Out("{boot_loop_stay}", 0u, 0, 0u, dbug_num_type_str);
            break;
        case boot_try_another_app:
			Debug_Print_Out("{boot_try_another_app}", 0u, 0, 0u, dbug_num_type_str);
		    g_bcbWriteFail = false;
			if(!Swap_Image() || g_bcbWriteFail){
				flowStepStatus = boot_loop_stay;
				Active_Send_Request_Set(ACTIVE_SEND_INTO_IDLE_LOOP);
			}
			else{
				if(g_bcbWriteFail){
					flowStepStatus = boot_loop_stay;
					break;
				}
				if(!Normal_Boot_Up()){
					if(g_bcbWriteFail){
						flowStepStatus = boot_loop_stay;
						break;
					}
					flowStepStatus = boot_try_another_app;
				}
			}
			break;
        case boot_eth_create_retry:
			Debug_Print_Out("{boot_eth_create_retry}", 0u, 0, 0u, dbug_num_type_str);
			if((getTime().totalSeconds - s_wait_eth_re_create_s) >= (uint64)ETH_RE_CREATE_TIMEOUT_S){
				flowStepStatus = ETH_Create() ? boot_enable_eth_transceiver : boot_eth_create_retry;
				retry_eth++;
				s_wait_eth_re_create_s = getTime().totalSeconds;
				if (retry_eth >= RETRY_ETH_CREATE_MAX){
					flowStepStatus = boot_loop_stay;
				}
			}
            break;
#if 0 // qqq none use now, keep it
        case boot_reset:
			Debug_Print_Out("{boot_reset} soft reset", 0u, 0, 0u, dbug_num_type_str);
            soft_reset();
            break;
#endif
        default:
            flowStepStatus = boot_loop_stay;
            break;
    }
}


