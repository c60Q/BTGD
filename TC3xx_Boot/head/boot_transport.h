#ifndef BOOT_TRANSPORT_H
#define BOOT_TRANSPORT_H

#include <stdint.h>
#include <stdbool.h>
#include "boot_types.h"
#include "boot_transport_parameter.h"

typedef struct
{
  uint8_t  ctoData[TCP_BUF_SIZE];       			 /**< cto packet data buffer        */
  uint8_t  connected;                             /**< connection established        */
  uint8_t  protection;                            /**< protection state              */
  uint8_t  s_n_k_resource;                        /**< for seed/key sequence         */
  uint8_t  ctoPending;                            /**< cto transmission pending flag */
  boot_int16u ctoLen;                                /**< cto current packet length     */
//  boot_int32u mta;                                   /**< memory transfer address       */
}transceiver_info_t;

typedef enum 
{
    UPDATE_CMD_BOOT_VERSION = 0x01,
    UPDATE_CMD_APP_VERSION,
    UPDATE_CMD_UPDATE_IMAGE,
    UPDATE_CMD_IMAGE_CRC,
    UPDATE_CMD_RSTART_UPDATE,
    UPDATE_CMD_ENTRY_NEW_APP_REQUEST,
    UPDATE_CMD_HEART_BEATS,
    UPDATE_CMD_ERROR
} update_command_list_t;

typedef enum 
{
    UPDATE_CMD_PKG_SUCCESS = 0x01,
    UPDATE_CMD_PKG_FAULT
} update_command_pkg_state_t;

typedef enum 
{
    UPDATE_CMD_ERROR_CMD_ERROR = 0x01,
    UPDATE_CMD_ERROR_PAYLOAD_ERROR,
    UPDATE_CMD_ERROR_DATA_ERROR, 
    UPDATE_CMD_ERROR_CRC_ERROR,
    UPDATE_CMD_ERROR_INTERNAL_ERROR_RETRY,
    UPDATE_CMD_ERROR_INTERNAL_ERROR_NO_RETRY,
    UPDATE_CMD_ERROR_COPY_ACTIVE_FAIL,
    UPDATE_CMD_ERROR_INTO_IDEL_LOOP,
    UPDATE_CMD_ERROR_BUSY
} update_command_error_data_t;

/* forward declaration to avoid header include cycle */
typedef struct net_state uip_tcp_appstate_t;

typedef enum
{
    ACTIVE_SEND_NONE = 0,
    ACTIVE_SEND_READY_BOOT_UP,
    ACTIVE_SEND_COPY_BANK_FAIL,
    ACTIVE_SEND_INTO_IDLE_LOOP
} active_send_pkg_state_t;

typedef enum
{
	dbug_num_type_str = 0,
	dbug_num_type_U32,
	dbug_num_type_HEX32,
	dbug_num_type_I32
}debuf_num_type_t;

//typedef struct net_state uip_tcp_app_state;  // forward declare only
extern volatile active_send_pkg_state_t active_send_pkg_state;
extern uint8_t debugPrintOnce;

bool ETH_Create(void);
void Bootloader_Transmit_Data_Hanlder(uip_tcp_appstate_t *s);
void Bootloader_Receive_Data_Handler(uip_tcp_appstate_t *s, boot_int8u *data, boot_int16u len);
void Active_Send_Request_Set(active_send_pkg_state_t request);
void DebugUart3_init(void);
void Debug_Print_Out(const char *s, uint32_t num_U32, int32_t num_I32, uint32_t num_HEX32, debuf_num_type_t d_type);
void Debug_Print_Force_Out(const char *s, uint32_t num_U32, int32_t num_I32, uint32_t num_HEX32, debuf_num_type_t d_type);
void Debug_Print_Data_Array(const char *prefix, const uint8_t *data, uint32_t len);
void Boot_Debug_Uart_Counter_Test(void);

/* feed dog */
//void wdt_kick(void); // qqq none use now, keep it


#ifdef __cplusplus
extern "C" {
#endif

/* Returns:
 *   0  -> no staged update found
 *   >0 -> staged image was programmed and BCB updated (e.g., 1)
 *   <0 -> error code (CRC mismatch, erase/program failure, etc.)
 */

#ifdef __cplusplus
} /* extern "C" */
#endif


#endif /* BOOT_TRANSPORT_H */
