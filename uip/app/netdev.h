/************************************************************************************//**
* \file         Demo/TRICORE_TC3_TC375_Lite_Kit_Master_ADS/Prog/Libraries/uIP/netdev.h
* \brief        uIP network device port header file.
* \ingroup      Prog_TRICORE_TC3_TC375_Lite_Kit_Master_ADS
* \internal
*----------------------------------------------------------------------------------------
*                          C O P Y R I G H T
*----------------------------------------------------------------------------------------
*   Copyright (c) 2001, Swedish Institute of Computer Science. All rights reserved.
*
*----------------------------------------------------------------------------------------
*                            L I C E N S E
*----------------------------------------------------------------------------------------
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions
* are met:
*
* 1. Redistributions of source code must retain the above copyright
*    notice, this list of conditions and the following disclaimer.
*
* 2. Redistributions in binary form must reproduce the above copyright
*    notice, this list of conditions and the following disclaimer in the
*    documentation and/or other materials provided with the distribution.
*
* 3. Neither the name of the Institute nor the names of its contributors
*    may be used to endorse or promote products derived from this software
*    without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
* ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
* ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
* OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
* HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
* LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
* OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
* SUCH DAMAGE.
*
* Author: Adam Dunkels <adam@sics.se>
*
* This file is part of the uIP TCP/IP stack.
*
* \endinternal
****************************************************************************************/
#ifndef __NETDEV_H__
#define __NETDEV_H__

#include <stdint.h>
#include "uip.h"

/****************************************************************************************
* Macro definitions
****************************************************************************************/
/* The MAC address for the network interface. */
#define NETDEV_DEFAULT_MACADDR0           (0x08)
#define NETDEV_DEFAULT_MACADDR1           (0x00)
#define NETDEV_DEFAULT_MACADDR2           (0x27)
#define NETDEV_DEFAULT_MACADDR3           (0x69)
#define NETDEV_DEFAULT_MACADDR4           (0x5B)
#define NETDEV_DEFAULT_MACADDR5           (0x45)

/* The IP address and MASK and GATEWAY for the network interface. */
#define BOOT_COM_SET_UNIQUE_MAC_ENABLE (0)

/* if enable DHCP, need also enable uip-conf.h UIP_CONF_UDP and UIP_CONF_UDP_CHECKSUMS */
#define BOOT_COM_NET_DHCP_ENABLE	(0)
/** \brief Configure the 1st byte of the IP address */
#define BOOT_COM_NET_IPADDR0              (192)
/** \brief Configure the 2nd byte of the IP address */
#define BOOT_COM_NET_IPADDR1              (168)
/** \brief Configure the 3rd byte of the IP address */
#define BOOT_COM_NET_IPADDR2              (1)	//178
/** \brief Configure the 4th byte of the IP address */
#define BOOT_COM_NET_IPADDR3              (50)
/** \brief Configure the 1st byte of the network mask */
#define BOOT_COM_NET_NETMASK0             (255)
/** \brief Configure the 2nd byte of the network mask */
#define BOOT_COM_NET_NETMASK1             (255)
/** \brief Configure the 3rd byte of the network mask */
#define BOOT_COM_NET_NETMASK2             (255)
/** \brief Configure the 4th byte of the network mask */
#define BOOT_COM_NET_NETMASK3             (0)
/** \brief Configure the 1st byte of the gateway address */
#define BOOT_COM_NET_GATEWAY0             (192)
/** \brief Configure the 2nd byte of the gateway address */
#define BOOT_COM_NET_GATEWAY1             (168)
/** \brief Configure the 3rd byte of the gateway address */
#define BOOT_COM_NET_GATEWAY2             (1)  //178
/** \brief Configure the 4th byte of the gateway address */
#define BOOT_COM_NET_GATEWAY3             (1)

#define BOOT_COM_NET_OPEN_PORT	(5000)


/* Timeout time for transmitting a packet. */
#define NETDEV_TX_PACKET_TIMEOUT_MS       (250U)

/* Timeout time for allocating a new tranmsit buffer. */
#define NETDEV_TX_BUFF_ALLOC_TIMEOUT_MS   (10U)

/* GETH Tx/Rx buffer size definitions. */
#define IFXGETH_HEADER_LENGTH             (14)                                /* words */
#define IFXGETH_MAX_TX_BUFFER_SIZE        (2560+IFXGETH_HEADER_LENGTH+2)      /* bytes */
#define IFXGETH_MAX_RX_BUFFER_SIZE        (2560+IFXGETH_HEADER_LENGTH+2)      /* bytes */

/**
 * @ingroup iana
 * Port numbers
 * https://www.iana.org/assignments/service-names-port-numbers/service-names-port-numbers.txt
 */
enum lwip_iana_port_number {
  /** SMTP */
  LWIP_IANA_PORT_SMTP        = 25,
  /** DHCP server */
  LWIP_IANA_PORT_DHCP_SERVER = 67,
  /** DHCP client */
  LWIP_IANA_PORT_DHCP_CLIENT = 68,
  /** TFTP */
  LWIP_IANA_PORT_TFTP        = 69,
  /** HTTP */
  LWIP_IANA_PORT_HTTP        = 80,
  /** SNTP */
  LWIP_IANA_PORT_SNTP        = 123,
  /** NETBIOS */
  LWIP_IANA_PORT_NETBIOS     = 137,
  /** SNMP */
  LWIP_IANA_PORT_SNMP        = 161,
  /** SNMP traps */
  LWIP_IANA_PORT_SNMP_TRAP   = 162,
  /** HTTPS */
  LWIP_IANA_PORT_HTTPS       = 443,
  /** SMTPS */
  LWIP_IANA_PORT_SMTPS       = 465,
  /** MQTT */
  LWIP_IANA_PORT_MQTT        = 1883,
  /** MDNS */
  LWIP_IANA_PORT_MDNS        = 5353,
  /** Secure MQTT */
  LWIP_IANA_PORT_SECURE_MQTT = 8883
};

/* MAC adress buffer. */
extern struct uip_eth_addr macAddress;
extern uint8_t bootComNetIpaddress3;

/****************************************************************************************
* Function prototypes
****************************************************************************************/
unsigned char netdev_init(void);
void netdev_init_mac(void);
void netdev_get_mac(unsigned char * mac_addr);
unsigned int netdev_read(void);
void netdev_send(void);
void Net_Mac_Ip_Config(void);


#ifdef __cplusplus
extern "C" {
#endif
int UCID_Read16(uint8_t out16[16]);
void UCID_Hash6_From16(const uint8_t ucid16[16], struct uip_eth_addr *out6);
int UCID_Hash6_Get(struct uip_eth_addr *out6);
#ifdef __cplusplus
}
#endif



#endif /* __NETDEV_H__ */
/*********************************** end of netdev.h ***********************************/

