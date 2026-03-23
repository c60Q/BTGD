/************************************************************************************//**
* \file         Demo/TRICORE_TC3_TC375_Lite_Kit_Master_ADS/Prog/App/net.c
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

/****************************************************************************************
* Include files
****************************************************************************************/
//#include "header.h"                                    /* generic header               */
#include "netdev.h"
#include "uip.h"
#include "uip_arp.h"
#include "net.h"
#include "timer.h"
#include "boot_transport.h"
#include "boot_types.h"
#include <string.h>


/****************************************************************************************
* Macro definitions
****************************************************************************************/
/** \brief Delta time for the uIP periodic timer. */
#define NET_UIP_PERIODIC_TIMER_MS   (500)
/** \brief Delta time for the uIP ARP timer. */
#define NET_UIP_ARP_TIMER_MS        (10000)
/** \brief Macro for accessing the Ethernet header information in the buffer */
#define NET_UIP_HEADER_BUF          ((struct uip_eth_hdr *)&uip_buf[0])


/****************************************************************************************
* Local data declarations
****************************************************************************************/
/** \brief Holds the time out value of the uIP periodic timer. */
static unsigned long periodicTimerTimeOut;
/** \brief Holds the time out value of the uIP ARP timer. */
static unsigned long ARPTimerTimeOut;

#if (BOOT_COM_NET_DHCP_ENABLE > 0) // now set it as false
/** \brief Holds the MAC address which is used by the DHCP client. */
static struct uip_eth_addr macAddress;
#endif


/************************************************************************************//**
** \brief     Initializes the TCP/IP network communication interface.
** \return    none.
**
****************************************************************************************/
unsigned char NetInit(void)
{
	uip_ipaddr_t ipaddr;

	/* initialize the network device */
	if(netdev_init() == boot_FALSE){
		return boot_FALSE; // 
	}
	/* initialize the timer variables */
	periodicTimerTimeOut = TimerGet() + NET_UIP_PERIODIC_TIMER_MS;
	ARPTimerTimeOut = TimerGet() + NET_UIP_ARP_TIMER_MS;
	/* initialize the uIP TCP/IP stack. */
	uip_init();
	uip_arp_init();
#if (BOOT_COM_NET_DHCP_ENABLE == 0)
	/* set the IP address */
	uip_ipaddr(ipaddr, BOOT_COM_NET_IPADDR0, BOOT_COM_NET_IPADDR1, BOOT_COM_NET_IPADDR2,
	         bootComNetIpaddress3);
	uip_sethostaddr(ipaddr);
	/* set the network mask */
	uip_ipaddr(ipaddr, BOOT_COM_NET_NETMASK0, BOOT_COM_NET_NETMASK1, BOOT_COM_NET_NETMASK2,
	         BOOT_COM_NET_NETMASK3);
	uip_setnetmask(ipaddr);
	/* set the gateway address */
	uip_ipaddr(ipaddr, BOOT_COM_NET_GATEWAY0, BOOT_COM_NET_GATEWAY1, BOOT_COM_NET_GATEWAY2,
	         BOOT_COM_NET_GATEWAY3);
	uip_setdraddr(ipaddr);
#else
	/* set the IP address */
	uip_ipaddr(ipaddr, 0, 0, 0, 0);
	uip_sethostaddr(ipaddr);
	/* set the network mask */
	uip_ipaddr(ipaddr, 0, 0, 0, 0);
	uip_setnetmask(ipaddr);
	/* set the gateway address */
	uip_ipaddr(ipaddr, 0, 0, 0, 0);
	uip_setdraddr(ipaddr);
#endif
	/* start listening on the configured port on TCP/IP */
	uip_listen(HTONS(BOOT_COM_NET_OPEN_PORT));
	/* initialize the MAC and set the MAC address */
	netdev_init_mac();  

#if (BOOT_COM_NET_DHCP_ENABLE > 0)
    /* initialize the DHCP client application and send the initial request. */
    netdev_get_mac(&macAddress.addr[0]);
    dhcpc_init(&macAddress.addr[0], 6);
    dhcpc_request();
#endif
//	Debug_Print_Out("ETH create done, IP=192.168.1.", (uint32_t)bootComNetIpaddress3, 0, 0u, dbug_num_type_U32);
	return boot_TRUE;
} /*** end of NetInit ***/


void NetTransmitPacket(boot_int8u *data, boot_int16u len)
{
	uip_tcp_appstate_t *s;

	if (len + 4 > TCP_BUF_SIZE){
        return; 
    }
	
	/* get pointer to application state */
	s = &(uip_conn->appstate);
#if 0
	/* add the dto counter by little end */
	s->dto_data[0] = (boot_int8u)(s->dto_counter & 0xFF);
    s->dto_data[1] = (boot_int8u)((s->dto_counter >> 8) & 0xFF);
    s->dto_data[2] = (boot_int8u)((s->dto_counter >> 16) & 0xFF);
    s->dto_data[3] = (boot_int8u)((s->dto_counter >> 24) & 0xFF);
#else
	/* add the dto counter by big end */
	s->dto_data[0] = (boot_int8u)((s->dto_counter >> 24) & 0xFF);
	s->dto_data[1] = (boot_int8u)((s->dto_counter >> 16) & 0xFF);
	s->dto_data[2] = (boot_int8u)((s->dto_counter >>  8) & 0xFF);
	s->dto_data[3] = (boot_int8u)( s->dto_counter        & 0xFF);
#endif
	/* copy the actual response */
	memcpy(&s->dto_data[4], data, len);
	
	/* set the length of the TCP/IP packet */
	s->dto_len = len + 4;
	/* set the flag to request the transmission of this packet. */
	s->dto_tx_req = boot_TRUE;
	/* update dto counter for the next transmission */
	s->dto_counter++;
} /*** end of NetTransmitPacket ***/


/************************************************************************************//**
** \brief     The uIP network application that detects the tcp socket connect command on the
**            port used by the bootloader. This indicates that the bootloader should
**            be activated.
** \return    none.
**
****************************************************************************************/
void NetApp(void)
{
	uip_tcp_appstate_t *s;
	uint8_t *newDataPtr;
	uint16_t newDataLen;

	/* get pointer to application state */
	s = &(uip_conn->appstate);

	if(uip_connected()){
		/* init the dto counter and reset the pending dto data length and transmit related
		* flags.
		*/	
		s->dto_counter = 1;
		s->dto_len = 0;
		s->dto_tx_req = boot_FALSE;
		s->dto_tx_pending = boot_FALSE;
		memset(&s->receiveInfo, 0, sizeof(s->receiveInfo));
		memset(&s->transmitInfo, 0, sizeof(s->transmitInfo));
		s->rx_accum_len = 0u;
		debugPrintOnce = true; //qqqq
		Debug_Print_Out("netapp0", 0, 0, 0, dbug_num_type_str);
	}

	if(uip_closed() || uip_aborted() || uip_timedout()){
		/* clear per-connection buffered RX data on disconnect */
		s->dto_len = 0;
		s->dto_tx_req = boot_FALSE;
		s->dto_tx_pending = boot_FALSE;
		memset(&s->receiveInfo, 0, sizeof(s->receiveInfo));
		memset(&s->transmitInfo, 0, sizeof(s->transmitInfo));
		s->rx_accum_len = 0u;
		debugPrintOnce = true; //qqqq
		Debug_Print_Out("netapp1", 0, 0, 0, dbug_num_type_str);
		return;
	}

	if(uip_acked()){
		/* dto sent so reset the pending flag. */
		s->dto_tx_pending = boot_FALSE;
		debugPrintOnce = true; //qqqq
		Debug_Print_Out("netapp2", 0, 0, 0, dbug_num_type_str);
	}

	if(uip_rexmit()){
	/* is a dto transmission pending that should now be retransmitted? */
	/* retransmit the currently pending dto response */
		debugPrintOnce = true; //qqqq
		Debug_Print_Out("netapp3", 0, 0, 0, dbug_num_type_str);
		if(s->dto_tx_pending == boot_TRUE){
			debugPrintOnce = true; //qqqq
			Debug_Print_Out("netapp4", 0, 0, 0, dbug_num_type_str);
			/* resend the last pending dto response */
			uip_send(s->dto_data, s->dto_len);
			debugPrintOnce = true; //qqqq
			Debug_Print_Out("netapp5", 0, 0, 0, dbug_num_type_str);
		}
	}

	if(uip_poll()){
	/* check if there is a packet waiting to be transmitted. this is done via polling
	 * because then it is possible to asynchronously send data. otherwise data is
	 * only really send after a newly received packet was received.
	 */
	 	debugPrintOnce = true; //qqqq
		Debug_Print_Out("netapp6", 0, 0, 0, dbug_num_type_str);
		Bootloader_Transmit_Data_Hanlder(s);
		if(s->dto_tx_req == boot_TRUE){
			debugPrintOnce = true; //qqqq
			Debug_Print_Out("netapp7", 0, 0, 0, dbug_num_type_str);
			/* reset the transmit request flag. */
			s->dto_tx_req = boot_FALSE;
			if (s->dto_len > 0){
				debugPrintOnce = true; //qqqq
				Debug_Print_Out("netapp8", 0, 0, 0, dbug_num_type_str);
				/* set the transmit pending flag. */
				s->dto_tx_pending = boot_TRUE;
				/* submit the data for transmission. */
				uip_send(s->dto_data, s->dto_len);
			}
		}
	}
	if(uip_newdata()){
		debugPrintOnce = true; //qqqq
		Debug_Print_Out("netapp9", 0, 0, 0, dbug_num_type_str);
		/* Accumulate raw TCP payload (includes 4-byte index counter) */
		if(uip_datalen() > 0){
			debugPrintOnce = true; //qqqq
			Debug_Print_Out("netapp10", 0, 0, 0, dbug_num_type_str);
			uint16_t in_len = (uint16_t)uip_datalen();
			if(in_len > 0u){
				debugPrintOnce = true; //qqqq
				Debug_Print_Out("netapp11", 0, 0, 0, dbug_num_type_str);
				if(in_len > (uint16_t)sizeof(s->rx_accum)){
					/* oversize payload: drop */
					s->rx_accum_len = 0u;
					debugPrintOnce = true; //qqqq
					Debug_Print_Out("netapp12", 0, 0, 0, dbug_num_type_str);
				}
				else{
					debugPrintOnce = true; //qqqq
					Debug_Print_Out("netapp13", 0, 0, 0, dbug_num_type_str);
					if((s->rx_accum_len + in_len) > (uint16_t)sizeof(s->rx_accum)){
						/* overflow protection: drop accumulated data */
						s->rx_accum_len = 0u;
						debugPrintOnce = true; //qqqq
						Debug_Print_Out("netapp14", 0, 0, 0, dbug_num_type_str);
					}
					memcpy(&s->rx_accum[s->rx_accum_len], (const void *)uip_appdata, (size_t)in_len);
					s->rx_accum_len += in_len;
				}
			}
		}
	}

	/* request/response: process at most one complete frame when no response is pending.
	 * Keep decoding buffered data even when there is no new TCP payload this cycle,
	 * so frames left in rx_accum after a prior ACK can continue to advance. */
	if((s->dto_tx_pending == boot_FALSE) && (s->dto_tx_req == boot_FALSE) && (s->rx_accum_len > 0u)){
		uint16_t offset = 0u;
		debugPrintOnce = true; //qqqq
		Debug_Print_Out("netapp15", 0, 0, 0, dbug_num_type_str);
		while((s->rx_accum_len - offset) >= (uint16_t)(FIXED_INDEX_COUNT_SIZE + FIXED_LENGTH_SIZE)){
			uint8_t *p = &s->rx_accum[offset];

			/* LEN is at p[4..5] (big-endian), counts CMD+PKG+DATA */
			uint16_t dataLen = (uint16_t)(((uint16_t)p[4] << 8) | (uint16_t)p[5]);

			/* basic guard */
			if(dataLen < (uint16_t)(FIXED_CMD_SIZE + FIXED_PKG_STATE_SIZE)){
				/* ------------------------------------------------------------------
				 * If dataLen is too small, it indicates that this is not a valid frame;
				 * slide one byte to attempt realignment. 
				 * ------------------------------------------------------------------*/
				offset += 1u; 
				continue;
			}

			/* upper bound guard (prevent wrap/absurd LEN) */
			uint16_t maxDataLen = FIXED_CMD_SIZE + FIXED_PKG_STATE_SIZE + IMAGE_UPDATE_DATA_SIZE;
			if(dataLen > maxDataLen){
				/* ------------------------------------------------------------------
				 * maxDataLen: The maximum allowed dataLen in rx_accum after deducting fixed fields. 
				 * If dataLen is too large to fit into the buffer, slide one byte to attempt realignment.
				 * ------------------------------------------------------------------*/
				offset += 1u; /* resync */
				continue;
			}
				
			if ((s->rx_accum_len - offset) < (FIXED_INDEX_COUNT_SIZE + FIXED_LENGTH_SIZE + FIXED_CMD_SIZE)) {
				break; // wait more bytes
			}
				
			/* quick cmd guard: speeds up resync on noise/delimiter */
			uint8_t cmd = p[FIXED_INDEX_COUNT_SIZE + FIXED_LENGTH_SIZE];
			if((cmd < (uint8_t)UPDATE_CMD_BOOT_VERSION) || (cmd > (uint8_t)UPDATE_CMD_ERROR)){
				offset += 1u; /* resync */
				continue;
			}
			/* frame size without end-delim */
			uint16_t frame_no_delim = (uint16_t)(FIXED_INDEX_COUNT_SIZE + FIXED_LENGTH_SIZE + dataLen + FIXED_CRC_SIZE);

			if((s->rx_accum_len - offset) < frame_no_delim){
				break; /* wait for more bytes */
			}

				
			/* Optional end delimiter '\n' after CRC: eat it only if present */
			uint16_t frame_size = frame_no_delim;
			if((s->rx_accum_len - offset) > frame_no_delim){
				if(p[frame_no_delim] == (uint8_t)'\n'){
					frame_size = (uint16_t)(frame_no_delim + FIXED_END_DELIM_SIZE);
				}
			}

			/* Process exactly one frame (strip index) */
			newDataPtr = p;
			newDataLen = (uint16_t)(FIXED_LENGTH_SIZE + dataLen + FIXED_CRC_SIZE);
			debugPrintOnce = true;//qqqq
			Debug_Print_Data_Array("uIP RX= ", &newDataPtr[FIXED_INDEX_COUNT_SIZE], (uint32_t)newDataLen);//qqqq
			Bootloader_Receive_Data_Handler(s, &newDataPtr[FIXED_INDEX_COUNT_SIZE], (boot_int16u)newDataLen);

			offset += frame_size;
			break; /* one request per response */
		}
		debugPrintOnce = true; //qqqq
		Debug_Print_Out("netapp16", 0, 0, 0, dbug_num_type_str);
		if(offset > 0u){
			memmove(s->rx_accum, &s->rx_accum[offset], (size_t)(s->rx_accum_len - offset));
			s->rx_accum_len = (uint16_t)(s->rx_accum_len - offset);
			debugPrintOnce = true; //qqqq
			Debug_Print_Out("netapp17", 0, 0, 0, dbug_num_type_str);
		}
	}

	/* Immediate transmit for a response generated by the decode step above.
	 * This avoids waiting for the next poll cycle before sending the ACK/response. */
	if((s->dto_tx_pending == boot_FALSE) && (s->dto_tx_req == boot_TRUE)){
		s->dto_tx_req = boot_FALSE;
		debugPrintOnce = true; //qqqq
		Debug_Print_Out("netapp18", 0, 0, 0, dbug_num_type_str);
		if (s->dto_len > 0){
			debugPrintOnce = true; //qqqq
			Debug_Print_Out("netapp19", 0, 0, 0, dbug_num_type_str);
			s->dto_tx_pending = boot_TRUE;
			uip_send(s->dto_data, s->dto_len);
		}
	}
}

/************************************************************************************//**
** \brief     Runs the TCP/IP server task.
** \return    none.
**
****************************************************************************************/
void NetTask(void)
{
  unsigned long connection;
  unsigned long packetLen;
  
  /* check for an RX packet and read it. */
  packetLen = netdev_read();
  if (packetLen > 0)
  {
  	debugPrintOnce = true;//qqqq
	Debug_Print_Out("packetLen = ", (uint32_t)packetLen, 0, 0u, dbug_num_type_U32);

	debugPrintOnce = true;//qqqq
	Debug_Print_Out("ethType = 0x", 0u, 0, (uint32_t)htons(NET_UIP_HEADER_BUF->type), dbug_num_type_HEX32);
    /* set uip_len for uIP stack usage */
    uip_len = (unsigned short)packetLen;

    /* process incoming IP packets here. */
    if (NET_UIP_HEADER_BUF->type == htons(UIP_ETHTYPE_IP))
    {
    	debugPrintOnce = true;//qqqq
   		 Debug_Print_Out("RX IP", 0u, 0, 0u, dbug_num_type_str);
      uip_arp_ipin();
      uip_input();
      /* if the above function invocation resulted in data that
       * should be sent out on the network, the global variable
       * uip_len is set to a value > 0.
       */
      if (uip_len > 0)
      {
        uip_arp_out();
        netdev_send();
        uip_len = 0;
      }
    }
    /* process incoming ARP packets here. */
    else if (NET_UIP_HEADER_BUF->type == htons(UIP_ETHTYPE_ARP))
    {
    	debugPrintOnce = true;//qqqq
    	Debug_Print_Out("RX ARP", 0u, 0, 0u, dbug_num_type_str);
      uip_arp_arpin();

      /* if the above function invocation resulted in data that
       * should be sent out on the network, the global variable
       * uip_len is set to a value > 0.
       */
      if (uip_len > 0)
      {
        netdev_send();
        uip_len = 0;
      }
    }
  }

  /* process TCP/IP Periodic Timer here. */
  if (TimerGet() >= periodicTimerTimeOut)
  {
    periodicTimerTimeOut += NET_UIP_PERIODIC_TIMER_MS;
    for (connection = 0; connection < UIP_CONNS; connection++)
    {
      uip_periodic(connection);
      /* If the above function invocation resulted in data that
       * should be sent out on the network, the global variable
       * uip_len is set to a value > 0.
       */
      if (uip_len > 0)
      {
        uip_arp_out();
        netdev_send();
        uip_len = 0;
      }
    }

#if UIP_UDP // qqq none use now, set to 0
    for (connection = 0; connection < UIP_UDP_CONNS; connection++)
    {
      uip_udp_periodic(connection);
      /* If the above function invocation resulted in data that
       * should be sent out on the network, the global variable
       * uip_len is set to a value > 0.
       */
      if(uip_len > 0)
      {
        uip_arp_out();
        netdev_send();
        uip_len = 0;
      }
    }
#endif
  }

  /* process ARP Timer here. */
  if (TimerGet() >= ARPTimerTimeOut)
  {
    ARPTimerTimeOut += NET_UIP_ARP_TIMER_MS;
    uip_arp_timer();
  }

  /* perform polling operations here. */
  /* see note "uip send perform polling operations" */
  for (connection = 0; connection < UIP_CONNS; connection++)
  {
    uip_poll_conn(&uip_conns[connection]);
    /* If the above function invocation resulted in data that
     * should be sent out on the network, the global variable
     * uip_len is set to a value > 0.
     */
    if (uip_len > 0)
    {
      uip_arp_out();
      netdev_send();
      uip_len = 0;
    }
  }
} /*** end of NetServerTask ***/


/*********************************** end of net.c **************************************/
