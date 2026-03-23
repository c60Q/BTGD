/************************************************************************************//**
* \file         Demo/TRICORE_TC3_TC375_Lite_Kit_Master_ADS/Prog/App/net.h
* \brief        Network application for the uIP TCP/IP stack.
* \ingroup      Prog_TRICORE_TC3_TC375_Lite_Kit_Master_ADS
* \internal
*----------------------------------------------------------------------------------------
*                          C O P Y R I G H T
*----------------------------------------------------------------------------------------
*   Copyright (c) 2021  by Feaser    http://www.feaser.com    All rights reserved
*
*----------------------------------------------------------------------------------------
*                            L I C E N S E
*----------------------------------------------------------------------------------------
* This file is part of OpenBLT. OpenBLT is free software: you can redistribute it and/or
* modify it under the terms of the GNU General Public License as published by the Free
* Software Foundation, either version 3 of the License, or (at your option) any later
* version.
*
* OpenBLT is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
* without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
* PURPOSE. See the GNU General Public License for more details.
*
* You have received a copy of the GNU General Public License along with OpenBLT. It
* should be located in ".\Doc\license.html". If not, contact Feaser to obtain a copy.
*
* \endinternal
****************************************************************************************/
#ifndef NET_H
#define NET_H

/****************************************************************************************
* Macro definitions
****************************************************************************************/
#ifndef UIP_APPCALL
#define UIP_APPCALL NetApp
#endif /* UIP_APPCALL */

#include "boot_types.h"
#include "boot_transport_parameter.h"
#include "boot_transport.h"
#include <stdint.h>


/****************************************************************************************
* Type definitions
****************************************************************************************/
/** \brief Define the uip_tcp_appstate_t datatype. This is the state of our tcp/ip
 *         application, and the memory required for this state is allocated together
 *         with each TCP connection. One application state for each TCP connection.
 */
typedef struct net_state 
{
	boot_int32u dto_counter;
	boot_int8u  dto_data[TCP_BUF_SIZE];
	boot_int16u dto_len;
	boot_bool   dto_tx_req;
	boot_bool   dto_tx_pending;
	/* RX/TX transport state is per TCP connection */
	transceiver_info_t receiveInfo;
	transceiver_info_t transmitInfo;
	/* RX accumulation buffer to handle TCP sticky/fragmented frames */
	uint8_t  rx_accum[TCP_BUF_SIZE * 2u];
	uint16_t rx_accum_len;
};//uip_tcp_appstate_t;

/****************************************************************************************
* Function prototypes
****************************************************************************************/
unsigned char NetInit(void);
void NetTransmitPacket(boot_int8u *data, boot_int16u len);
void NetApp(void);
void NetTask(void);


#endif /* NET_H */
/*********************************** end of net.h **************************************/
