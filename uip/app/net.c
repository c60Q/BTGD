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

#define NET_GARP_INITIAL_DELAY_MS    (1000u)
#define NET_GARP_REPEAT_DELAY_MS     (3000u)
#define NET_GARP_PACKET_LEN          (60u)
#define NET_ARP_REQUEST              (1u)
#define NET_ARP_REPLY                (2u)
#define NET_RELEVANT_RX_LOG_PORT     (BOOT_COM_NET_OPEN_PORT)
/* Open port is aligned to the current PC tool target port for this debug round. */
/* #define NET_LEGACY_PC_TOOL_PORT        (5000u) */
#define NET_GARP_KEEPALIVE_MS          (15000u)


/****************************************************************************************
* Local data declarations
****************************************************************************************/
/** \brief Holds the time out value of the uIP periodic timer. */
static unsigned long periodicTimerTimeOut;
/** \brief Holds the time out value of the uIP ARP timer. */
static unsigned long ARPTimerTimeOut;
/** \brief One-shot timer for delayed gratuitous ARP transmit. */
static unsigned long garpTimerTimeOut;
/** \brief Number of remaining gratuitous ARP announcements to send. */
static unsigned char garpAnnouncementsPending;
/** \brief Periodic keepalive timer for extra gratuitous ARP while waiting for a directed host packet. */
static unsigned long garpKeepAliveTimeOut;
/** \brief Flag indicating whether a directed/relevant packet for the bootloader was seen. */
static unsigned char netRelevantRxSeen;
/** \brief Number of directed/relevant packets seen for the bootloader. */
static unsigned long netRelevantRxCount;

#if defined(__TASKING__)
#pragma pack 2
#endif
struct net_arp_hdr
{
  struct uip_eth_hdr ethhdr;
  u16_t hwtype;
  u16_t protocol;
  u8_t hwlen;
  u8_t protolen;
  u16_t opcode;
  struct uip_eth_addr shwaddr;
  u16_t sipaddr[2];
  struct uip_eth_addr dhwaddr;
  u16_t dipaddr[2];
};
#if defined(__TASKING__)
#pragma pack default
#endif

static void NetSendGratuitousArpFrame(unsigned short opcode);
static void NetHandleGratuitousArpAnnouncement(void);
static unsigned char NetArpTargetsHost(const struct net_arp_hdr *arp);
static unsigned char NetIpv4DestIsHost(const unsigned char *frame);
static unsigned short NetReadBe16(const unsigned char *data);
static unsigned char NetIsRelevantTcpPort(unsigned short dstPort);
static void NetDebugPrintTcpCheckpoint(const char *prefix, const unsigned char *frame, unsigned long packetLen);

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
	garpAnnouncementsPending = 2u;
	garpTimerTimeOut = TimerGet() + NET_GARP_INITIAL_DELAY_MS;
	garpKeepAliveTimeOut = 0u;
	netRelevantRxSeen = 0u;
	netRelevantRxCount = 0u;
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
	/* uip_listen(HTONS(NET_LEGACY_PC_TOOL_PORT)); */
	debugPrintOnce = true;
	Debug_Print_Out("TCP listen port = ", (uint32_t)BOOT_COM_NET_OPEN_PORT, 0, 0u, dbug_num_type_U32);
	/* debugPrintOnce = true; */
	/* Debug_Print_Out("TCP listen legacy port = ", (uint32_t)NET_LEGACY_PC_TOOL_PORT, 0, 0u, dbug_num_type_U32); */
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


static unsigned short NetReadBe16(const unsigned char *data)
{
  return (unsigned short)(((unsigned short)data[0] << 8) | (unsigned short)data[1]);
}

static unsigned char NetArpTargetsHost(const struct net_arp_hdr *arp)
{
  if (arp == 0)
  {
    return 0u;
  }

  if ((arp->dipaddr[0] == uip_hostaddr[0]) && (arp->dipaddr[1] == uip_hostaddr[1]))
  {
    return 1u;
  }

  return 0u;
}

static unsigned char NetIpv4DestIsHost(const unsigned char *frame)
{
  if (frame == 0)
  {
    return 0u;
  }

  if ((frame[30] == (unsigned char)((uip_hostaddr[0] >> 8) & 0xFFu)) &&
      (frame[31] == (unsigned char)(uip_hostaddr[0] & 0xFFu)) &&
      (frame[32] == (unsigned char)((uip_hostaddr[1] >> 8) & 0xFFu)) &&
      (frame[33] == (unsigned char)(uip_hostaddr[1] & 0xFFu)))
  {
    return 1u;
  }

  return 0u;
}

static void NetSendGratuitousArpFrame(unsigned short opcode)
{
	struct net_arp_hdr *arp;
	unsigned long i;
	const char *msg;

	memset(uip_buf, 0, UIP_BUFSIZE + 2);
	arp = (struct net_arp_hdr *)&uip_buf[0];

	for (i = 0; i < 6u; i++)
	{
		arp->ethhdr.dest.addr[i] = 0xFFu;
		arp->dhwaddr.addr[i] = 0x00u;
		arp->shwaddr.addr[i] = uip_ethaddr.addr[i];
		arp->ethhdr.src.addr[i] = uip_ethaddr.addr[i];
	}

	arp->ethhdr.type = htons(UIP_ETHTYPE_ARP);
	arp->hwtype      = HTONS(1u);
	arp->protocol    = HTONS(UIP_ETHTYPE_IP);
	arp->hwlen       = 6u;
	arp->protolen    = 4u;
	arp->opcode      = HTONS(opcode);
	arp->sipaddr[0]  = uip_hostaddr[0];
	arp->sipaddr[1]  = uip_hostaddr[1];
	arp->dipaddr[0]  = uip_hostaddr[0];
	arp->dipaddr[1]  = uip_hostaddr[1];

	uip_len = NET_GARP_PACKET_LEN;
	msg = (opcode == NET_ARP_REPLY) ? "GARP reply send" : "GARP request send";
	debugPrintOnce = true;
	Debug_Print_Out(msg, 0u, 0, 0u, dbug_num_type_str);
	/* GARP MAC/IP detail is already validated and is temporarily silenced to reduce log noise. */
	/* debugPrintOnce = true; */
	/* Debug_Print_Data_Array("GARP src mac = ", &uip_ethaddr.addr[0], 6u); */
	/* debugPrintOnce = true; */
	/* Debug_Print_Out("GARP opcode = ", (uint32_t)opcode, 0, 0u, dbug_num_type_U32); */
	/* debugPrintOnce = true; */
	/* Debug_Print_Out("GARP ip = ", 0u, 0, 0u, dbug_num_type_str); */
	/* Debug_Print_Out("  ip[0] = ", (uint32_t)((uip_hostaddr[0] >> 8) & 0xFFu), 0, 0u, dbug_num_type_U32); */
	/* Debug_Print_Out("  ip[1] = ", (uint32_t)(uip_hostaddr[0] & 0xFFu), 0, 0u, dbug_num_type_U32); */
	/* Debug_Print_Out("  ip[2] = ", (uint32_t)((uip_hostaddr[1] >> 8) & 0xFFu), 0, 0u, dbug_num_type_U32); */
	/* Debug_Print_Out("  ip[3] = ", (uint32_t)(uip_hostaddr[1] & 0xFFu), 0, 0u, dbug_num_type_U32); */
	netdev_send();
	uip_len = 0u;
}

static void NetHandleGratuitousArpAnnouncement(void)
{
	if ((garpAnnouncementsPending > 0u) && (TimerGet() >= garpTimerTimeOut))
	{
		if (garpAnnouncementsPending == 2u)
		{
			NetSendGratuitousArpFrame(NET_ARP_REQUEST);
			garpTimerTimeOut = TimerGet() + NET_GARP_REPEAT_DELAY_MS;
		}
		else
		{
			NetSendGratuitousArpFrame(NET_ARP_REPLY);
			garpTimerTimeOut = 0u;
			garpKeepAliveTimeOut = 0u;
		}
		garpAnnouncementsPending--;
	}
	/* Periodic GARP keepalive resend is intentionally disabled for this debug round. */
	else if ((garpAnnouncementsPending == 0u) &&
	         (netRelevantRxSeen == 0u) &&
	         (garpKeepAliveTimeOut != 0u) &&
	         (TimerGet() >= garpKeepAliveTimeOut))
	{
		/* garpKeepAlivePrintDiv++; */
		/* if ((garpKeepAlivePrintDiv == 1u) || ((garpKeepAlivePrintDiv & 0x3u) == 0u)) */
		/* { */
		/* 	debugPrintOnce = true; */
		/* 	Debug_Print_Out("GARP keepalive resend", netRelevantRxCount, 0, 0u, dbug_num_type_U32); */
		/* } */
		/* NetSendGratuitousArpFrame(NET_ARP_REPLY); */
		garpKeepAliveTimeOut = 0u;
	}
}

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
	}

	if(uip_closed() || uip_aborted() || uip_timedout()){
		/* clear per-connection buffered RX data on disconnect */
		s->dto_len = 0;
		s->dto_tx_req = boot_FALSE;
		s->dto_tx_pending = boot_FALSE;
		memset(&s->receiveInfo, 0, sizeof(s->receiveInfo));
		memset(&s->transmitInfo, 0, sizeof(s->transmitInfo));
		s->rx_accum_len = 0u;
		return;
	}

	if(uip_acked()){
		/* dto sent so reset the pending flag. */
		s->dto_tx_pending = boot_FALSE;
	}

	if(uip_rexmit()){
	/* is a dto transmission pending that should now be retransmitted? */
	/* retransmit the currently pending dto response */
		if(s->dto_tx_pending == boot_TRUE){
			/* resend the last pending dto response */
			uip_send(s->dto_data, s->dto_len);
		}
	}

	if(uip_poll()){
	/* check if there is a packet waiting to be transmitted. this is done via polling
	 * because then it is possible to asynchronously send data. otherwise data is
	 * only really send after a newly received packet was received.
	 */
		Bootloader_Transmit_Data_Hanlder(s);
		if(s->dto_tx_req == boot_TRUE){
			/* reset the transmit request flag. */
			s->dto_tx_req = boot_FALSE;
			if (s->dto_len > 0){
				/* set the transmit pending flag. */
				s->dto_tx_pending = boot_TRUE;
				/* submit the data for transmission. */
				uip_send(s->dto_data, s->dto_len);
			}
		}
	}
	if(uip_newdata()){
		/* Accumulate raw TCP payload (includes 4-byte index counter) */
		if(uip_datalen() > 0){
			uint16_t in_len = (uint16_t)uip_datalen();
			if(in_len > 0u){
				if(in_len > (uint16_t)sizeof(s->rx_accum)){
					/* oversize payload: drop */
					s->rx_accum_len = 0u;
				}
				else{
					if((s->rx_accum_len + in_len) > (uint16_t)sizeof(s->rx_accum)){
						/* overflow protection: drop accumulated data */
						s->rx_accum_len = 0u;
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
			/* uIP RX frame dump temporarily silenced to reduce log noise. */
			Bootloader_Receive_Data_Handler(s, &newDataPtr[FIXED_INDEX_COUNT_SIZE], (boot_int16u)newDataLen);

			offset += frame_size;
			break; /* one request per response */
		}
		if(offset > 0u){
			memmove(s->rx_accum, &s->rx_accum[offset], (size_t)(s->rx_accum_len - offset));
			s->rx_accum_len = (uint16_t)(s->rx_accum_len - offset);
		}
	}

	/* Immediate transmit for a response generated by the decode step above.
	 * This avoids waiting for the next poll cycle before sending the ACK/response. */
	if((s->dto_tx_pending == boot_FALSE) && (s->dto_tx_req == boot_TRUE)){
		s->dto_tx_req = boot_FALSE;
		if (s->dto_len > 0){
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
  
  NetHandleGratuitousArpAnnouncement();

  /* check for an RX packet and read it. */
  packetLen = netdev_read();
  if (packetLen > 0)
  {
    unsigned short ethType;
    unsigned char  relevantPacket = 0u;

    ethType = htons(NET_UIP_HEADER_BUF->type);
    /* set uip_len for uIP stack usage */
    uip_len = (unsigned short)packetLen;

    if (ethType == UIP_ETHTYPE_ARP)
    {
      struct net_arp_hdr *arp;
      unsigned short opcode;

      arp = (struct net_arp_hdr *)&uip_buf[0];
      opcode = htons(arp->opcode);
      if ((NetArpTargetsHost(arp) != 0u) && (opcode == NET_ARP_REQUEST))
      {
        relevantPacket = 1u;
        netRelevantRxSeen = 1u;
        netRelevantRxCount++;
        debugPrintOnce = true;
        Debug_Print_Out("RX relevant ARP request for host", netRelevantRxCount, 0, 0u, dbug_num_type_U32);
      }
      else if ((NetArpTargetsHost(arp) != 0u) && (opcode == NET_ARP_REPLY))
      {
        relevantPacket = 1u;
        netRelevantRxSeen = 1u;
        netRelevantRxCount++;
        debugPrintOnce = true;
        Debug_Print_Out("RX relevant ARP reply for host", netRelevantRxCount, 0, 0u, dbug_num_type_U32);
      }
    }
    else if ((ethType == UIP_ETHTYPE_IP) && (packetLen >= 38u) && (NetIpv4DestIsHost((const unsigned char *)&uip_buf[0]) != 0u))
    {
      unsigned char proto;
      proto = (unsigned char)uip_buf[23];
      if ((proto == UIP_PROTO_TCP) && (packetLen >= 54u))
      {
        unsigned short dstPort;
        unsigned char tcpFlags;

        dstPort = NetReadBe16((const unsigned char *)&uip_buf[36]);
        tcpFlags = (unsigned char)(uip_buf[47] & 0x3Fu);

        debugPrintOnce = true;
        Debug_Print_Out("RX host IPv4 TCP", (uint32_t)packetLen, 0, 0u, dbug_num_type_U32);
        NetDebugPrintTcpCheckpoint("RX TCP dst port = ", (const unsigned char *)&uip_buf[0], packetLen);

        if ((NetIsRelevantTcpPort(dstPort) != 0u) && ((tcpFlags & 0x02u) != 0u))
        {
          relevantPacket = 1u;
          netRelevantRxSeen = 1u;
          netRelevantRxCount++;
          debugPrintOnce = true;
          Debug_Print_Out("TCP SYN hit listen dst = ", (uint32_t)dstPort, 0, 0u, dbug_num_type_U32);
        }
        else if (NetIsRelevantTcpPort(dstPort) != 0u)
        {
          relevantPacket = 1u;
          netRelevantRxSeen = 1u;
          netRelevantRxCount++;
          debugPrintOnce = true;
          Debug_Print_Out("RX relevant TCP dst port", (uint32_t)dstPort, 0, 0u, dbug_num_type_U32);
        }
        else
        {
          debugPrintOnce = true;
          Debug_Print_Out("RX drop tcp port mismatch = ", (uint32_t)dstPort, 0, 0u, dbug_num_type_U32);
        }
      }
      else if (proto == UIP_PROTO_UDP)
      {
        relevantPacket = 1u;
        netRelevantRxSeen = 1u;
        netRelevantRxCount++;
        debugPrintOnce = true;
        Debug_Print_Out("RX relevant UDP for host", netRelevantRxCount, 0, 0u, dbug_num_type_U32);
      }
      else
      {
        relevantPacket = 1u;
        netRelevantRxSeen = 1u;
        netRelevantRxCount++;
        debugPrintOnce = true;
        Debug_Print_Out("RX relevant IPv4 for host", netRelevantRxCount, 0, 0u, dbug_num_type_U32);
      }
    }

    if (relevantPacket != 0u)
    {
      debugPrintOnce = true;
      Debug_Print_Out("packetLen = ", (uint32_t)packetLen, 0, 0u, dbug_num_type_U32);
      debugPrintOnce = true;
      Debug_Print_Out("ethType = 0x", 0u, 0, (uint32_t)ethType, dbug_num_type_HEX32);
    }

    /* process incoming IP packets here. */
    if (NET_UIP_HEADER_BUF->type == htons(UIP_ETHTYPE_IP))
    {
      if (relevantPacket != 0u)
      {
        debugPrintOnce = true;
        Debug_Print_Out("RX IP", 0u, 0, 0u, dbug_num_type_str);
        debugPrintOnce = true;
        Debug_Print_Out("RX deliver to uIP", (uint32_t)uip_len, 0, 0u, dbug_num_type_U32);
      }
      uip_arp_ipin();
      uip_input();
      /* if the above function invocation resulted in data that
       * should be sent out on the network, the global variable
       * uip_len is set to a value > 0.
       */
      if (uip_len > 0)
      {
        if (relevantPacket != 0u)
        {
          debugPrintOnce = true;
          Debug_Print_Out("uIP produced TX len = ", (uint32_t)uip_len, 0, 0u, dbug_num_type_U32);
        }
        uip_arp_out();
        netdev_send();
        uip_len = 0;
      }
      else if (relevantPacket != 0u)
      {
        debugPrintOnce = true;
        Debug_Print_Out("uIP no TX response", 0u, 0, 0u, dbug_num_type_str);
      }
    }
    /* process incoming ARP packets here. */
    else if (NET_UIP_HEADER_BUF->type == htons(UIP_ETHTYPE_ARP))
    {
      if (relevantPacket != 0u)
      {
        debugPrintOnce = true;
        Debug_Print_Out("RX ARP", 0u, 0, 0u, dbug_num_type_str);
      }
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
static unsigned char NetIsRelevantTcpPort(unsigned short dstPort)
{
  if (dstPort == NET_RELEVANT_RX_LOG_PORT)
  {
    return 1u;
  }

  return 0u;
}

static void NetDebugPrintTcpCheckpoint(const char *prefix, const unsigned char *frame, unsigned long packetLen)
{
  unsigned short srcPort;
  unsigned short dstPort;
  unsigned char flags;

  if ((frame == 0) || (packetLen < 54u))
  {
    return;
  }

  srcPort = NetReadBe16(&frame[34]);
  dstPort = NetReadBe16(&frame[36]);
  flags = (unsigned char)(frame[47] & 0x3Fu);

  debugPrintOnce = true;
  Debug_Print_Out(prefix, (uint32_t)dstPort, 0, 0u, dbug_num_type_U32);
  debugPrintOnce = true;
  Debug_Print_Out("TCP src port = ", (uint32_t)srcPort, 0, 0u, dbug_num_type_U32);
  debugPrintOnce = true;
  Debug_Print_Out("TCP flags = 0x", 0u, 0, (uint32_t)flags, dbug_num_type_HEX32);
}



