#ifndef BOOT_FLOW_H
#define BOOT_FLOW_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <boot_types.h>

#define BOOTUP_ACCEPT_TIMEOUT_S (10u) // 10 sec
#define ETH_RE_CREATE_TIMEOUT_S (5u) //5 sec


typedef enum
{
	BCB_UPDATE_BOOT_UP_FAIL = 0x00,
	BCB_UPDATE_SWAP,
	BCB_UPDATE_SWAP_FAIL,
	BCB_UPDATE_SWAP_IO_FAIL, /* swap copy/program/erase fail (do NOT invalidate staging) */
	BCB_UPDATE_STAGING_NEW_IMAGE_CHECK,
	BCB_UPDATE_STAGING_FAIL,
	BCB_UPDATE_ACTIVE_SUCCESS
}bcb_update_state_t;

typedef enum
{
    boot_power_on_init = 0,
    boot_image_data_check,  /* read meta_app + BCB */
    boot_command_judge,  /* check magic, create ethernet */
    boot_enable_eth_transceiver,  /* tcp server enable */
    boot_receive_image_end_crc,
    boot_new_image_copy,
    boot_wait_reply_to_boot_up,
    boot_loop_stay,
    boot_try_another_app,
    boot_eth_create_retry,
    boot_reset,
    boot_prev_state_init_value
}boot_state_t;

typedef union
{
    uint8_t flowCtrlByte;
    struct
	{
        uint8_t netTaskEnable : 1;     // Bit 0 (LSB)
        uint8_t imageReceiveDone : 1;
        uint8_t imageEndCrcCorrect : 1;
        uint8_t bootUpAccept : 1;
        uint8_t b4 : 1;
        uint8_t b5 : 1;
        uint8_t b6 : 1;
        uint8_t ethPkgFault : 1;     // Bit 7 (MSB)
    }bits;
}boot_flow_Flag_t;

extern boot_flow_Flag_t flowCtrlFlag;

uint32_t crc32_calc_buf(const void *data, size_t len);
uint32_t crc32_region(uint32_t addr, uint32_t size);
void Boot_Flow_Handler(void);

#endif /* BOOT_FLOW_H */
