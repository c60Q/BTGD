


#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "IfxAsclin_Asc.h"
#include "IfxStdIf_DPipe.h"
#include "Ifx_Console.h"
#include "aurix_pin_mappings.h"
#include "boot_flow.h"
#include "boot_transport_parameter.h"
#include "boot_transport.h"
#include "net.h"
#include "boot_types.h"
#include "boot_flash.h"
#include "boot_timer.h"

#define BOOT_UART_TX_BUFFER_SIZE   (256)
#define BOOT_UART_RX_BUFFER_SIZE   (256)
#define BOOT_UART_BAUDRATE         (115200)
#define BOOT_UART_ISR_PRIORITY_TX  (25)
#define BOOT_UART_ISR_PRIORITY_RX  (26)
#define BOOT_UART_ISR_PRIORITY_ER  (27)

static IfxStdIf_DPipe g_ascStandardInterface;
static IfxAsclin_Asc  g_asc3;
static uint8 g_uartTxBuffer[BOOT_UART_TX_BUFFER_SIZE + sizeof(Ifx_Fifo) + 8];
static uint8 g_uartRxBuffer[BOOT_UART_RX_BUFFER_SIZE + sizeof(Ifx_Fifo) + 8];
static boolean g_debugUartInitialized = FALSE;

IFX_INTERRUPT(bootAsc3TxISR, 0, BOOT_UART_ISR_PRIORITY_TX);
IFX_INTERRUPT(bootAsc3RxISR, 0, BOOT_UART_ISR_PRIORITY_RX);
IFX_INTERRUPT(bootAsc3ErISR, 0, BOOT_UART_ISR_PRIORITY_ER);

volatile active_send_pkg_state_t active_send_pkg_state = ACTIVE_SEND_NONE;
uint8_t ethPkgRetryTimes = 0;
uint8_t debugPrintOnce = false;

/* UPDATE_IMAGE session state: fixed target slot, sequential 512-byte writes.
 * The target slot is selected from current ACTIVE identity:
 *   ACTIVE=A -> update STAGING_B
 *   ACTIVE=B -> update STAGING_A
 */
static uint32_t g_updateWriteAddr = 0u;
static uint32_t g_updateTargetBase = 0u;
static uint32_t g_updateTargetSize = 0u;
static uint8_t  g_updateEraseDone = boot_FALSE;

void bootAsc3TxISR(void)
{
	IfxStdIf_DPipe_onTransmit(&g_ascStandardInterface);
}

void bootAsc3RxISR(void)
{
	IfxStdIf_DPipe_onReceive(&g_ascStandardInterface);
}

void bootAsc3ErISR(void)
{
	IfxStdIf_DPipe_onError(&g_ascStandardInterface);
}

void Boot_Debug_Uart_Counter_Test(void)
{
    static uint64_t s_lastPrintSecond = UINT64_MAX;
    static uint32_t s_counter = 0u;
    systemTime now = getTime();

    if((s_lastPrintSecond != UINT64_MAX) && ((now.totalSeconds - s_lastPrintSecond) < 5u)){
        return;
    }

    s_lastPrintSecond = now.totalSeconds;
	s_counter++;
	Debug_Print_Force_Out("heartbeat(5s) = ", s_counter, 0, 0u, dbug_num_type_U32);
}


static void UpdateImageSession_Reset(void)
{
    g_updateWriteAddr = 0u;
    g_updateTargetBase = 0u;
    g_updateTargetSize = 0u;
    g_updateEraseDone = boot_FALSE;
}

bool ETH_Create(void)
{
    /* MAC/PHY/TCP stack initial */
	IfxPort_setPinState(IFXCFG_PORT_MCU_EPHY_RST.port, IFXCFG_PORT_MCU_EPHY_RST.pinIndex, IfxPort_State_high);//PHY enable pins
	return NetInit();
}


/* feed dog */
//void wdt_kick(void) { }// qqq none use now, keep it


void DebugUart3_init(void)
{
	IfxAsclin_Asc_Config ascConf;
	const IfxAsclin_Asc_Pins pins = {
		.cts       = NULL_PTR,
		.ctsMode   = IfxPort_InputMode_pullUp,
		.rx        = &IfxAsclin3_RXD_P32_2_IN,
		.rxMode    = IfxPort_InputMode_pullUp,
		.rts       = NULL_PTR,
		.rtsMode   = IfxPort_OutputMode_pushPull,
		.tx        = &IfxAsclin3_TX_P32_3_OUT,
		.txMode    = IfxPort_OutputMode_pushPull,
		.pinDriver = IfxPort_PadDriver_cmosAutomotiveSpeed1
	};

	IfxAsclin_Asc_initModuleConfig(&ascConf, &MODULE_ASCLIN3);

	ascConf.baudrate.baudrate = BOOT_UART_BAUDRATE;
	ascConf.baudrate.oversampling = IfxAsclin_OversamplingFactor_16;
	ascConf.bitTiming.medianFilter = IfxAsclin_SamplesPerBit_three;
	ascConf.bitTiming.samplePointPosition = IfxAsclin_SamplePointPosition_8;

	ascConf.interrupt.txPriority = BOOT_UART_ISR_PRIORITY_TX;
	ascConf.interrupt.rxPriority = BOOT_UART_ISR_PRIORITY_RX;
	ascConf.interrupt.erPriority = BOOT_UART_ISR_PRIORITY_ER;
	ascConf.interrupt.typeOfService = IfxSrc_Tos_cpu0;

	ascConf.pins = &pins;
	ascConf.txBuffer = g_uartTxBuffer;
	ascConf.txBufferSize = BOOT_UART_TX_BUFFER_SIZE;
	ascConf.rxBuffer = g_uartRxBuffer;
	ascConf.rxBufferSize = BOOT_UART_RX_BUFFER_SIZE;

	IfxAsclin_Asc_initModule(&g_asc3, &ascConf);
	IfxAsclin_Asc_stdIfDPipeInit(&g_ascStandardInterface, &g_asc3);
	Ifx_Console_init(&g_ascStandardInterface);
	g_debugUartInitialized = TRUE;
	Debug_Print_Force_Out("{Debug UART ready}", 0u, 0, 0u, dbug_num_type_str);
	Debug_Print_Force_Out("BOOT_UART_BAUD = ", BOOT_UART_BAUDRATE, 0, 0u, dbug_num_type_U32);
}

static void Boot_Debug_Uart_AppendChar(char *buf, size_t bufSize, size_t *idx, char ch)
{
	if((buf == NULL) || (idx == NULL) || (bufSize == 0u)){
		return;
	}

	if((*idx) < (bufSize - 1u)){
		buf[*idx] = ch;
		(*idx)++;
		buf[*idx] = '\0';
	}
}

static void Boot_Debug_Uart_AppendStr(char *buf, size_t bufSize, size_t *idx, const char *str)
{
	if((buf == NULL) || (idx == NULL) || (bufSize == 0u) || (str == NULL)){
		return;
	}

	while((*str != '\0') && ((*idx) < (bufSize - 1u))){
		buf[*idx] = *str;
		(*idx)++;
		str++;
	}

	buf[*idx] = '\0';
}

static void Boot_Debug_Uart_AppendU32(char *buf, size_t bufSize, size_t *idx, uint32_t value)
{
	char temp[10];
	size_t pos = 0u;

	if((buf == NULL) || (idx == NULL) || (bufSize == 0u)){
		return;
	}

	if(value == 0u){
		Boot_Debug_Uart_AppendChar(buf, bufSize, idx, '0');
		return;
	}

	while((value > 0u) && (pos < sizeof(temp))){
		temp[pos++] = (char)('0' + (value % 10u));
		value /= 10u;
	}

	while(pos > 0u){
		Boot_Debug_Uart_AppendChar(buf, bufSize, idx, temp[--pos]);
	}
}

static void Boot_Debug_Uart_AppendI32(char *buf, size_t bufSize, size_t *idx, int32_t value)
{
	uint32_t absValue;

	if((buf == NULL) || (idx == NULL) || (bufSize == 0u)){
		return;
	}

	if(value < 0){
		Boot_Debug_Uart_AppendChar(buf, bufSize, idx, '-');
		absValue = (uint32_t)(-(value + 1)) + 1u;
	}
	else{
		absValue = (uint32_t)value;
	}

	Boot_Debug_Uart_AppendU32(buf, bufSize, idx, absValue);
}

static void Boot_Debug_Uart_AppendHex8(char *buf, size_t bufSize, size_t *idx, uint8_t value)
{
	static const char hexTable[] = "0123456789ABCDEF";

	Boot_Debug_Uart_AppendChar(buf, bufSize, idx, hexTable[(value >> 4) & 0x0Fu]);
	Boot_Debug_Uart_AppendChar(buf, bufSize, idx, hexTable[value & 0x0Fu]);
}


static void Boot_Debug_Uart_AppendHex32(char *buf, size_t bufSize, size_t *idx, uint32_t value)
{
	static const char hexTable[] = "0123456789ABCDEF";
	int nibbleIdx;
	bool started = false;

	if((buf == NULL) || (idx == NULL) || (bufSize == 0u)){
		return;
	}

	for(nibbleIdx = 7; nibbleIdx >= 0; nibbleIdx--){
		uint32_t nibble = (value >> ((uint32_t)nibbleIdx * 4u)) & 0x0Fu;

		if((nibble != 0u) || started){
			Boot_Debug_Uart_AppendChar(buf, bufSize, idx, hexTable[nibble]);
			started = true;
		}
	}

	if(started == false){
		Boot_Debug_Uart_AppendChar(buf, bufSize, idx, '0');
	}
}

void Debug_Print_Data_Array(const char *prefix, const uint8_t *data, uint32_t len)
{
	Ifx_SizeT count;
	char debugBuffer[128];
	size_t idx = 0u;
	uint32_t i;

	if(g_debugUartInitialized == FALSE){
		return;
	}

	if(debugPrintOnce == false){
		return;
	}

	if((prefix == NULL) || (data == NULL)){
		debugPrintOnce = false;
		return;
	}

	debugBuffer[0] = '\0';

	/* prefix */
	Boot_Debug_Uart_AppendStr(debugBuffer, sizeof(debugBuffer), &idx, prefix);

	/* data bytes: XX XX XX ... */
	for(i = 0u; i < len; i++){
		/* avoid buffer overflow, flush current line first */
		if(idx > (sizeof(debugBuffer) - 6u)){
			Boot_Debug_Uart_AppendStr(debugBuffer, sizeof(debugBuffer), &idx, "\r\n");
			count = (Ifx_SizeT)strlen(debugBuffer);
			if(count > 0u){
				(void)IfxStdIf_DPipe_write(&g_ascStandardInterface, (void *)debugBuffer, &count, TIME_INFINITE);
			}

			idx = 0u;
			debugBuffer[0] = '\0';

			/* continue line with indent */
			Boot_Debug_Uart_AppendStr(debugBuffer, sizeof(debugBuffer), &idx, "  ");
		}

		Boot_Debug_Uart_AppendHex8(debugBuffer, sizeof(debugBuffer), &idx, data[i]);

		if(i < (len - 1u)){
			Boot_Debug_Uart_AppendChar(debugBuffer, sizeof(debugBuffer), &idx, ' ');
		}
	}

	/* append CRLF */
	Boot_Debug_Uart_AppendStr(debugBuffer, sizeof(debugBuffer), &idx, "\r\n");

	count = (Ifx_SizeT)strlen(debugBuffer);
	if(count > 0u){
		(void)IfxStdIf_DPipe_write(&g_ascStandardInterface, (void *)debugBuffer, &count, TIME_INFINITE);
	}

	(void)IfxStdIf_DPipe_flushTx(&g_ascStandardInterface, TIME_INFINITE);
	debugPrintOnce = false;
}

static void Boot_Debug_Uart_PrintLine(const char *s, uint32_t num_U32, int32_t num_I32, uint32_t num_HEX32,
		debuf_num_type_t d_type, boolean requireGate)
{
	Ifx_SizeT count;
	char debugBuffer[128];
	size_t idx = 0u;

	if(g_debugUartInitialized == FALSE){
		return;
	}

	if((requireGate != FALSE) && (debugPrintOnce == false)){
		return;
	}

	debugBuffer[0] = '\0';
	Boot_Debug_Uart_AppendStr(debugBuffer, sizeof(debugBuffer), &idx, s);

	switch(d_type){
		case dbug_num_type_U32:
			Boot_Debug_Uart_AppendU32(debugBuffer, sizeof(debugBuffer), &idx, num_U32);
			break;

		case dbug_num_type_HEX32:
			Boot_Debug_Uart_AppendHex32(debugBuffer, sizeof(debugBuffer), &idx, num_HEX32);
			break;

		case dbug_num_type_I32:
			Boot_Debug_Uart_AppendI32(debugBuffer, sizeof(debugBuffer), &idx, num_I32);
			break;
		case dbug_num_type_str:
		default:
			break;
	}
	/* append CRLF */
	Boot_Debug_Uart_AppendStr(debugBuffer, sizeof(debugBuffer), &idx, "\r\n");

	count = (Ifx_SizeT)strlen(debugBuffer);
	if(count > 0u){
		(void)IfxStdIf_DPipe_write(&g_ascStandardInterface, (void *)debugBuffer, &count, TIME_INFINITE);
	}

	(void)IfxStdIf_DPipe_flushTx(&g_ascStandardInterface, TIME_INFINITE);
	if(requireGate != FALSE){
		debugPrintOnce = false;
	}
}

void Debug_Print_Out(const char *s, uint32_t num_U32, int32_t num_I32, uint32_t num_HEX32, debuf_num_type_t d_type)
{
	Boot_Debug_Uart_PrintLine(s, num_U32, num_I32, num_HEX32, d_type, TRUE);
}

void Debug_Print_Force_Out(const char *s, uint32_t num_U32, int32_t num_I32, uint32_t num_HEX32, debuf_num_type_t d_type)
{
	Boot_Debug_Uart_PrintLine(s, num_U32, num_I32, num_HEX32, d_type, FALSE);
}


void Cto_Len_Crc_Fill_In(uint16_t dLen, uint8_t *pData, uint16_t *pLength)
{
	uint32_t crcTemp = 0x00;
	uint16_t crcIndex = 0x00;

	/* trasmit length big end */
	pData[0] = (uint8_t)((dLen >> 8) & 0xFF);
	pData[1] = (uint8_t)(dLen & 0xFF);
	
	crcTemp = crc32_calc_buf(&pData[2], dLen);
	/* jump over data */
	crcIndex = 2 + dLen;

	/* trasmit CRC big end */
	pData[crcIndex++] = (uint8_t)((crcTemp >> 24) & 0xFF);
	pData[crcIndex++] = (uint8_t)((crcTemp >> 16) & 0xFF);
	pData[crcIndex++] = (uint8_t)((crcTemp >> 8) & 0xFF);
	pData[crcIndex++] = (uint8_t)(crcTemp & 0xFF);
	pData[crcIndex++] = '\n';

	*pLength = FIXED_LENGTH_SIZE + dLen + FIXED_CRC_SIZE + FIXED_END_DELIM_SIZE;
}

void Active_Send_Request_Set(active_send_pkg_state_t request)
{
    active_send_pkg_state = request;
}

void Bootloader_Transmit_Data_Hanlder(uip_tcp_appstate_t *s)
{
    uint16_t dataLength = 0;
	transceiver_info_t *transmitInfo = &s->transmitInfo;

    if ((s->dto_tx_pending == boot_TRUE) || (s->dto_tx_req == boot_TRUE))    {
        return;
    }

    switch (active_send_pkg_state){
        case ACTIVE_SEND_READY_BOOT_UP:
			dataLength = 2;
			transmitInfo->ctoData[2] = UPDATE_CMD_ENTRY_NEW_APP_REQUEST;// CMD
			transmitInfo->ctoData[3] = UPDATE_CMD_PKG_SUCCESS;
			Cto_Len_Crc_Fill_In(dataLength, transmitInfo->ctoData, (uint16_t*)&transmitInfo->ctoLen);
            break;
		case ACTIVE_SEND_COPY_BANK_FAIL:
			dataLength = 3;
			transmitInfo->ctoData[2] = UPDATE_CMD_ERROR;// CMD
			transmitInfo->ctoData[3] = UPDATE_CMD_PKG_SUCCESS;
			transmitInfo->ctoData[4] = UPDATE_CMD_ERROR_COPY_ACTIVE_FAIL;// Data
			Cto_Len_Crc_Fill_In(dataLength, transmitInfo->ctoData, (uint16_t*)&transmitInfo->ctoLen);
			break;
		case ACTIVE_SEND_INTO_IDLE_LOOP:
			dataLength = 3;
			transmitInfo->ctoData[2] = UPDATE_CMD_ERROR;// CMD
			transmitInfo->ctoData[3] = UPDATE_CMD_PKG_SUCCESS;
			transmitInfo->ctoData[4] = UPDATE_CMD_ERROR_INTO_IDEL_LOOP;// Data
			Cto_Len_Crc_Fill_In(dataLength, transmitInfo->ctoData, (uint16_t*)&transmitInfo->ctoLen);
			break;
		default:
			transmitInfo->ctoLen = 0;
			break;
    }

	/* send the response if it contains something */
	if (transmitInfo->ctoLen > 0){
	  /* set cto packet transmission pending flag */
	  transmitInfo->ctoPending = 1;
	
	  /* transmit the cto response packet */
	  NetTransmitPacket(transmitInfo->ctoData, transmitInfo->ctoLen);
	}
	transmitInfo->ctoPending = 0;
	transmitInfo->ctoLen = 0;     
	active_send_pkg_state = ACTIVE_SEND_NONE;
}


void Bootloader_Receive_Data_Handler(uip_tcp_appstate_t *s, boot_int8u *data, boot_int16u len)
{
	transceiver_info_t *receiveInfo = &s->receiveInfo;
	uint32_t crcTemp = 0x00, crcRec = 0x00;
	uint16_t dataLength = 0x00, crcIndex = 0x00;

	/* default: payload fault */
	uint8_t rx_cmd = 0x98;

	/* parsed fields (safe defaults) */
	uint8_t rx_pkg_state = 0x00;
	const uint8_t *rx_data_payload = (const uint8_t *)0;
	uint16_t rx_payload_len = 0u;

	/* RX parse + guards
	* Protocol layout (index already stripped by net.c):
	* [0..1] : LEN (big-endian), counts CMD + PKG state + DATA bytes
	* [2]    : CMD
	* [3]    : PKG state
	* [4..]  : DATA
	* [..]   : CRC32 (big-endian), computed over CMD+PKG+DATA
	* '\n'   : optional end (ignored currently)
	*/

	/* len is full receive data - 4 bytes index, make sure len is > FIXED_LENGTH_SIZE that we can take dataLength */
	if((data != NULL) && (len >= FIXED_LENGTH_SIZE)){
		dataLength = ((uint16_t)data[0] << 8) | (uint16_t)data[1];
		/* need at least CMD + PKG */
		if(dataLength >= (FIXED_CMD_SIZE + FIXED_PKG_STATE_SIZE)){
			/* need full: LEN + (CMD+PKG+DATA) + CRC32 */
			if(len >= (FIXED_LENGTH_SIZE + dataLength + FIXED_CRC_SIZE)){
				/* safe to parse fields */
				rx_cmd = data[2];
				rx_pkg_state = data[3];
				rx_data_payload = &data[4];
				rx_payload_len = (uint16_t)(dataLength - (FIXED_CMD_SIZE + FIXED_PKG_STATE_SIZE));

				/* CRC32 over CMD + PKG + DATA */
				crcTemp = crc32_calc_buf(&data[2], dataLength);

				crcIndex = (uint16_t)(FIXED_LENGTH_SIZE + dataLength); /* 2 + dataLength */
				crcRec = ((uint32_t)data[crcIndex] << 24) |
						 ((uint32_t)data[crcIndex + 1] << 16) |
						 ((uint32_t)data[crcIndex + 2] << 8)  |
						 ((uint32_t)data[crcIndex + 3]);

				if(crcTemp != crcRec){
					rx_cmd = 0x99; /* crc fault */
				}
			}
		}
	}

	if((rx_pkg_state != UPDATE_CMD_PKG_SUCCESS) && (rx_cmd != UPDATE_CMD_ENTRY_NEW_APP_REQUEST)){
		rx_cmd = 0x98;
	}

	switch (rx_cmd){
		case UPDATE_CMD_BOOT_VERSION:{
			static const uint8_t ver[] = BOOT_FW_VERSION;
			receiveInfo->ctoData[2] = rx_cmd;
			receiveInfo->ctoData[3] = UPDATE_CMD_PKG_SUCCESS;

			dataLength = (uint16_t)strlen((const char *)ver);
			memcpy(&receiveInfo->ctoData[4], ver, dataLength);
			dataLength = FIXED_CMD_SIZE + FIXED_PKG_STATE_SIZE + dataLength;
			Cto_Len_Crc_Fill_In(dataLength, receiveInfo->ctoData, (uint16_t *)&receiveInfo->ctoLen);
			break;
		}
		case UPDATE_CMD_APP_VERSION:{
		    volatile const boot_bcb_t *b = BCB_Read_Out();
		    const volatile char *ver_str = b->app_bank.image_version;
		    size_t max_len = sizeof(((boot_bcb_t *)0)->app_bank.image_version);
		    size_t verLen = 0;

		    receiveInfo->ctoData[2] = rx_cmd;
		    receiveInfo->ctoData[3] = UPDATE_CMD_PKG_SUCCESS;

 		    while((verLen < max_len) && ((uint8_t)ver_str[verLen] >= 0x20u) && ((uint8_t)ver_str[verLen] <= 0x7Eu)){
		        verLen++;
		    }
		    memcpy(&receiveInfo->ctoData[4], (const void *)ver_str, verLen);
		    dataLength = (uint16_t)(FIXED_CMD_SIZE + FIXED_PKG_STATE_SIZE + verLen);
		    Cto_Len_Crc_Fill_In(dataLength, receiveInfo->ctoData, (uint16_t *)&receiveInfo->ctoLen);
		    break;
		}
		case UPDATE_CMD_UPDATE_IMAGE:{
			volatile const boot_bcb_t *b = BCB_Read_Out();
			const bool updateB = (b->active_app == BCB_ACTIVE_STAGING_A);
			const bool updateA = (b->active_app == BCB_ACTIVE_STAGING_B);
			const uint32_t target_base = updateB ? PFLASH_STAGING_B_BASE : PFLASH_STAGING_A_BASE;
			const uint32_t target_size = PFLASH_APP_SIZE;
			const bool newRound = ((g_updateEraseDone == boot_FALSE) || (g_updateTargetBase != target_base));

			/* Fixed-slot update only. Do not accept writes when:
			* 1. current action is not UPDATE
			* 2. ACTIVE identity is not A/B
			*/
			if((b->action_cmd != BCB_ACTION_UPDATE) || ((!updateB) && (!updateA))){
				dataLength = FIXED_CMD_SIZE + FIXED_PKG_STATE_SIZE + 1;
				receiveInfo->ctoData[2] = UPDATE_CMD_ERROR;
				receiveInfo->ctoData[3] = UPDATE_CMD_PKG_FAULT;
				receiveInfo->ctoData[4] = UPDATE_CMD_ERROR_INTERNAL_ERROR_NO_RETRY;
				Cto_Len_Crc_Fill_In(dataLength, receiveInfo->ctoData, (uint16_t *)&receiveInfo->ctoLen);
				UpdateImageSession_Reset();
				break;
			}

			/* Protocol payload must contain exactly one fixed 512-byte image chunk. */
			if((rx_data_payload == NULL) || (rx_payload_len != IMAGE_UPDATE_DATA_SIZE)){
				dataLength = FIXED_CMD_SIZE + FIXED_PKG_STATE_SIZE + 1;
				receiveInfo->ctoData[2] = UPDATE_CMD_ERROR;
				receiveInfo->ctoData[3] = UPDATE_CMD_PKG_FAULT;
				receiveInfo->ctoData[4] = UPDATE_CMD_ERROR_PAYLOAD_ERROR;
				Cto_Len_Crc_Fill_In(dataLength, receiveInfo->ctoData, (uint16_t *)&receiveInfo->ctoLen);
				break;
			}

			/* First packet of a round:
			* select target slot and erase full target image area once.
			* Erase failure is treated as RETRYABLE:
			* PC may retry the same current packet, and bootloader will restart the round cleanly.
			*/
			if(newRound){
				if(P_Flash_Erase_Range(target_base, target_size) != 0){
					dataLength = FIXED_CMD_SIZE + FIXED_PKG_STATE_SIZE + 1;
					receiveInfo->ctoData[2] = UPDATE_CMD_ERROR;
					receiveInfo->ctoData[3] = UPDATE_CMD_PKG_FAULT;
					receiveInfo->ctoData[4] = UPDATE_CMD_ERROR_INTERNAL_ERROR_RETRY;
					Cto_Len_Crc_Fill_In(dataLength, receiveInfo->ctoData, (uint16_t *)&receiveInfo->ctoLen);
					UpdateImageSession_Reset();
					break;
				}
				g_updateTargetBase = target_base;
				g_updateTargetSize = target_size;
				g_updateWriteAddr  = target_base;
				g_updateEraseDone  = boot_TRUE;
			}

			/* Do not allow writes beyond the fixed staging image region. */
			if((g_updateWriteAddr < g_updateTargetBase) ||
			(g_updateWriteAddr > (g_updateTargetBase + g_updateTargetSize)) ||
			((g_updateWriteAddr + IMAGE_UPDATE_DATA_SIZE) > (g_updateTargetBase + g_updateTargetSize))){
				dataLength = FIXED_CMD_SIZE + FIXED_PKG_STATE_SIZE + 1;
				receiveInfo->ctoData[2] = UPDATE_CMD_ERROR;
				receiveInfo->ctoData[3] = UPDATE_CMD_PKG_FAULT;
				receiveInfo->ctoData[4] = UPDATE_CMD_ERROR_INTERNAL_ERROR_NO_RETRY;
				Cto_Len_Crc_Fill_In(dataLength, receiveInfo->ctoData, (uint16_t *)&receiveInfo->ctoLen);
				UpdateImageSession_Reset();
				break;
			}

			/* Program current 512-byte chunk.
			* In the current sequential-write design, once a flash write/program/verify fails
			* in the middle of a round, retrying only the current packet is NOT safe unless
			* packet index/offset based resume is supported.
			* Therefore this is treated as fatal for the current session.
			*/
			if(Copy_Data_to_Pflash(&g_updateWriteAddr, &rx_data_payload[0]) != 0){
				dataLength = FIXED_CMD_SIZE + FIXED_PKG_STATE_SIZE + 1;
				receiveInfo->ctoData[2] = UPDATE_CMD_ERROR;
				receiveInfo->ctoData[3] = UPDATE_CMD_PKG_FAULT;
				receiveInfo->ctoData[4] = UPDATE_CMD_ERROR_INTERNAL_ERROR_NO_RETRY;
				Cto_Len_Crc_Fill_In(dataLength, receiveInfo->ctoData, (uint16_t *)&receiveInfo->ctoLen);
				UpdateImageSession_Reset();
				break;
			}
			dataLength = FIXED_CMD_SIZE + FIXED_PKG_STATE_SIZE;
			receiveInfo->ctoData[2] = rx_cmd;
			receiveInfo->ctoData[3] = UPDATE_CMD_PKG_SUCCESS;
			Cto_Len_Crc_Fill_In(dataLength, receiveInfo->ctoData, (uint16_t *)&receiveInfo->ctoLen);
			break;
		}
		case UPDATE_CMD_IMAGE_CRC:{
			volatile const boot_bcb_t *b = BCB_Read_Out();
			bool updateB = (b->active_app == BCB_ACTIVE_STAGING_A);

			uint32_t payload_size = updateB ? PFLASH_STAGING_B_PAYLOAD_SIZE : PFLASH_STAGING_A_PAYLOAD_SIZE;
			uint32_t payload_base = updateB ? PFLASH_STAGING_B_PAYLOAD_BASE : PFLASH_STAGING_A_PAYLOAD_BASE;
			(void)payload_base;
			(void)payload_size;
			
			/* Need 4 bytes CRC in DATA */
			if((rx_data_payload == NULL) || (rx_payload_len < FIXED_CRC_SIZE)){
				dataLength = FIXED_CMD_SIZE + FIXED_PKG_STATE_SIZE + 1; // 1 data
				receiveInfo->ctoData[2] = UPDATE_CMD_ERROR;
				receiveInfo->ctoData[3] = UPDATE_CMD_PKG_FAULT;
				receiveInfo->ctoData[4] = UPDATE_CMD_ERROR_PAYLOAD_ERROR;
				flowCtrlFlag.bits.imageEndCrcCorrect = boot_FALSE;
				Cto_Len_Crc_Fill_In(dataLength, receiveInfo->ctoData, (uint16_t *)&receiveInfo->ctoLen);
				flowCtrlFlag.bits.imageReceiveDone = boot_TRUE;
				UpdateImageSession_Reset();
				break;
			}

			crcRec = ((uint32_t)rx_data_payload[0] << 24) |
					 ((uint32_t)rx_data_payload[1] << 16) |
					 ((uint32_t)rx_data_payload[2] << 8)  |
					 ((uint32_t)rx_data_payload[3]);

			crcTemp = crc32_region(payload_base, payload_size);
			if(crcTemp != crcRec){
				dataLength = FIXED_CMD_SIZE + FIXED_PKG_STATE_SIZE + 1; // 1 data
				receiveInfo->ctoData[2] = UPDATE_CMD_ERROR;
				receiveInfo->ctoData[3] = UPDATE_CMD_PKG_FAULT;
				receiveInfo->ctoData[4] = UPDATE_CMD_ERROR_CRC_ERROR;
				flowCtrlFlag.bits.imageEndCrcCorrect = boot_FALSE;
            }
			else{
				dataLength = FIXED_CMD_SIZE + FIXED_PKG_STATE_SIZE;
				receiveInfo->ctoData[2] = rx_cmd;
				receiveInfo->ctoData[3] = UPDATE_CMD_PKG_SUCCESS;
				flowCtrlFlag.bits.imageEndCrcCorrect = boot_TRUE;
			}
			Cto_Len_Crc_Fill_In(dataLength, receiveInfo->ctoData, (uint16_t *)&receiveInfo->ctoLen);
			flowCtrlFlag.bits.imageReceiveDone = boot_TRUE;
			UpdateImageSession_Reset();
			break;
		}
#if 0
		case UPDATE_CMD_RSTART_UPDATE:	/* qqqqqq 此項暫時不作，後續再補 */

			break;
#endif
		case UPDATE_CMD_ENTRY_NEW_APP_REQUEST:
			if(rx_pkg_state == UPDATE_CMD_PKG_SUCCESS){ /* PC reply this CMD back */
				flowCtrlFlag.bits.bootUpAccept = boot_TRUE;
				flowCtrlFlag.bits.ethPkgFault = boot_FALSE;
				ethPkgRetryTimes = 0;
			}
			else{
				flowCtrlFlag.bits.bootUpAccept = boot_FALSE;
				ethPkgRetryTimes++;
				if(ethPkgRetryTimes >= RETRY_ETH_PKT_MAX){
					ethPkgRetryTimes = 0;
					flowCtrlFlag.bits.ethPkgFault = boot_TRUE;
				}
			}
			break;
		case UPDATE_CMD_HEART_BEATS:
			dataLength = FIXED_CMD_SIZE + FIXED_PKG_STATE_SIZE + 1; // 1 is data length
			/* Need 1 byte data: 0xAA */
			if((rx_data_payload == NULL) || (rx_payload_len < 1u)){
				receiveInfo->ctoData[2] = UPDATE_CMD_ERROR;
				receiveInfo->ctoData[3] = UPDATE_CMD_PKG_FAULT;
				receiveInfo->ctoData[4] = UPDATE_CMD_ERROR_PAYLOAD_ERROR;
            }
			else if((rx_data_payload[0] != 0xAA)){
				receiveInfo->ctoData[2] = UPDATE_CMD_ERROR;
				receiveInfo->ctoData[3] = UPDATE_CMD_PKG_FAULT;
				receiveInfo->ctoData[4] = UPDATE_CMD_ERROR_DATA_ERROR;
			}
			else{
				receiveInfo->ctoData[2] = rx_cmd;
				receiveInfo->ctoData[3] = UPDATE_CMD_PKG_SUCCESS;
				receiveInfo->ctoData[4] = 0x55;
			}
			Cto_Len_Crc_Fill_In(dataLength, receiveInfo->ctoData, (uint16_t *)&receiveInfo->ctoLen);
			break;
        default:{
			/* rx_cmd is either:
			 * - 0x98 payload fault
			 * - 0x99 crc fault
			 * - or an unrecognized CMD (CRC OK but not handled)
			 */
			dataLength = FIXED_CMD_SIZE + FIXED_PKG_STATE_SIZE + 1; // 1 is data
			receiveInfo->ctoData[2] = UPDATE_CMD_ERROR;
			receiveInfo->ctoData[3] = UPDATE_CMD_PKG_FAULT;
			if(rx_cmd == 0x99){ // CMD & payload corrent but pak crc error
				receiveInfo->ctoData[4] = UPDATE_CMD_ERROR_CRC_ERROR;
            }
			else if(rx_cmd == 0x98){ // payload error
				receiveInfo->ctoData[4] = UPDATE_CMD_ERROR_PAYLOAD_ERROR;
			}
			else{ // payload corrent but CMD error
				receiveInfo->ctoData[4] = UPDATE_CMD_ERROR_CMD_ERROR;
			}
			Cto_Len_Crc_Fill_In(dataLength, receiveInfo->ctoData, (uint16_t *)&receiveInfo->ctoLen);
			break;
		}
	}

	/* send the response if it contains something */
	if(receiveInfo->ctoLen > 0){
		/* set cto packet transmission pending flag */
		receiveInfo->ctoPending = 1;

		/* transmit the cto response packet */
		NetTransmitPacket(receiveInfo->ctoData, receiveInfo->ctoLen);
	}
	receiveInfo->ctoPending = 0;
	receiveInfo->ctoLen = 0;
}






