/************************************************************************************//**
* \file         Demo/TRICORE_TC3_TC375_Lite_Kit_Master_ADS/Prog/Libraries/uIP/netdev.c
* \brief        uIP network device port source file.
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

/****************************************************************************************
* Include files
****************************************************************************************/
#include <string.h>                              /* String utils (memcpy)              */
//#include "header.h"                              /* Generic header                     */
#include "uip.h"                                 /* uIP TCP/IP stack                   */
#include "uip_arp.h"                             /* uIP address resolution protocol    */
#include "IfxGeth_Eth.h"                         /* iLLD ethernet driver               */
//#include "IfxGeth_Phy_Dp83825i.h"                /* Ethernet PHY DP83825I driver       */
#include "IfxGeth_Phy_88E1512.h"                /* Ethernet PHY DP83825I driver       */
#include "Ifx_Types.h"
#include "IfxScuWdt.h"
#include "netdev.h"
#include "aurix_pin_mappings.h"
#include "timer.h"
#include "boot_flash.h"
#include "boot_transport.h"


/****************************************************************************************
* Local data declarations
****************************************************************************************/
uint8_t bootComNetIpaddress3;

/* MAC adress buffer. */
struct uip_eth_addr macAddress;

/* GETH driver handle. */
static IfxGeth_Eth g_IfxGeth;

/* Ethernet Tx/Rx data buffers. */
static uint8 channel0TxBuffer1[IFXGETH_MAX_TX_DESCRIPTORS][IFXGETH_MAX_TX_BUFFER_SIZE];
static uint8 channel0RxBuffer1[IFXGETH_MAX_RX_DESCRIPTORS][IFXGETH_MAX_RX_BUFFER_SIZE];

/* Boolean flag to keep track of the link status. */
static uint8 linkUpFlag;


/****************************************************************************************
* Local constant declarations
****************************************************************************************/
/* Pin configuration of the DP83825I PHY. */
#if 0// use for openBLT TC397 EV board PHY
static const IfxGeth_Eth_RmiiPins rmii_pins = {
   .crsDiv = &IfxGeth_CRSDVA_P11_11_IN,                                      /* CRSDIV */
   .refClk = &IfxGeth_REFCLKA_P11_12_IN,                                     /* REFCLK */
   .rxd0   = &IfxGeth_RXD0A_P11_10_IN,                                       /* RXD0   */
   .rxd1   = &IfxGeth_RXD1A_P11_9_IN,                                        /* RXD1   */
   .mdc    = &IfxGeth_MDC_P21_2_OUT,                                         /* MDC    */
   .mdio   = &IfxGeth_MDIO_P21_3_INOUT,                                      /* MDIO   */
   .txd0   = &IfxGeth_TXD0_P11_3_OUT,                                        /* TXD0   */
   .txd1   = &IfxGeth_TXD1_P11_2_OUT,                                        /* TXD1   */
   .txEn   = &IfxGeth_TXEN_P11_6_OUT                                         /* TXEN   */
};
#endif
/* pin configuration RTL8211F */
const IfxGeth_Eth_RgmiiPins Marvell_88E1512_pins = {
   .txClk	= &IfxGeth_TXCLK_P11_4_OUT,      /* TXCLK */
   .txd0	= &IfxGeth_TXD0_P11_3_OUT,       /* TXD0 */
   .txd1	= &IfxGeth_TXD1_P11_2_OUT,       /* TXD1 */
   .txd2	= &IfxGeth_TXD2_P11_1_OUT,       /* TXD2 */
   .txd3	= &IfxGeth_TXD3_P11_0_OUT,       /* TXD3 */
   .txCtl	= &IfxGeth_TXCTL_P11_6_OUT,      /* TXCTL */
   .rxClk	= &IfxGeth_RXCLKA_P11_12_IN,     /* RXCLK */
   .rxd0	= &IfxGeth_RXD0A_P11_10_IN,      /* RXD0 */
   .rxd1	= &IfxGeth_RXD1A_P11_9_IN,       /* RXD1 */
   .rxd2	= &IfxGeth_RXD2A_P11_8_IN,       /* RXD2 */
   .rxd3	= &IfxGeth_RXD3A_P11_7_IN,       /* RXD3 */
   .rxCtl	= &IfxGeth_RXCTLA_P11_11_IN,     /* RXCTL */
   .mdc		= &IfxGeth_MDC_P12_0_OUT,        /* MDC */
   .mdio	= &IfxGeth_MDIO_P12_1_INOUT,     /* MDIO */
   .grefClk = &IfxGeth_GREFCLK_P11_5_IN  /* GREFCLK */
};

/****************************************************************************************
* Function prototypes
****************************************************************************************/
static void netdev_link_refresh(void);


/****************************************************************************************
*            N E T W O R K   D E V I C E   R O U T I N E S
****************************************************************************************/
/************************************************************************************//**
** \brief     Initializes the network device.
**
****************************************************************************************/
unsigned char netdev_init(void)
{
	IfxGeth_Eth_Config GethConfig;

	/* Reset the link status flag. */
	linkUpFlag = 0;

	/* Store the default MAC and IP address. */
	Net_Mac_Ip_Config();

	/* Initialize the config structure with default values. */
	IfxGeth_Eth_initModuleConfig(&GethConfig, &MODULE_GETH);
	/* Store information about the RTL8211F PHY. */
	GethConfig.phyInterfaceMode = IfxGeth_PhyInterfaceMode_rgmii;
	GethConfig.pins.rgmiiPins = &Marvell_88E1512_pins;
	GethConfig.mac.lineSpeed = IfxGeth_LineSpeed_1000Mbps;
	/* MAC core configuration. */
	GethConfig.mac.loopbackMode = IfxGeth_LoopbackMode_disable;
	GethConfig.mac.macAddress[0] = macAddress.addr[0];
	GethConfig.mac.macAddress[1] = macAddress.addr[1];
	GethConfig.mac.macAddress[2] = macAddress.addr[2];
	GethConfig.mac.macAddress[3] = macAddress.addr[3];
	GethConfig.mac.macAddress[4] = macAddress.addr[4];
	GethConfig.mac.macAddress[5] = macAddress.addr[5];
	/* MTL configuration. */
	GethConfig.mtl.numOfTxQueues = 1;
	GethConfig.mtl.numOfRxQueues = 1;
	GethConfig.mtl.txQueue[0].txQueueSize = IfxGeth_QueueSize_2560Bytes;
	GethConfig.mtl.txQueue[0].storeAndForward = TRUE;
	GethConfig.mtl.rxQueue[0].rxQueueSize = IfxGeth_QueueSize_2560Bytes;
	GethConfig.mtl.rxQueue[0].rxDmaChannelMap = IfxGeth_RxDmaChannel_0;
	GethConfig.mtl.rxQueue[0].storeAndForward = TRUE;
	/* DMA configuration. */
	GethConfig.dma.numOfTxChannels = 1;
	GethConfig.dma.numOfRxChannels = 1;
	/* DMA Tx configuration. */
	GethConfig.dma.txChannel[0].channelId = IfxGeth_TxDmaChannel_0;
	GethConfig.dma.txChannel[0].txDescrList = (IfxGeth_TxDescrList *)&IfxGeth_Eth_txDescrList[0];
	GethConfig.dma.txChannel[0].txBuffer1StartAddress = (uint32 *)&channel0TxBuffer1[0][0];
	GethConfig.dma.txChannel[0].txBuffer1Size = IFXGETH_MAX_TX_BUFFER_SIZE;
	/* DMA Rx configuration. */
	GethConfig.dma.rxChannel[0].channelId = IfxGeth_RxDmaChannel_0;
	GethConfig.dma.rxChannel[0].rxDescrList = (IfxGeth_RxDescrList *)&IfxGeth_Eth_rxDescrList[0];
	GethConfig.dma.rxChannel[0].rxBuffer1StartAddress = (uint32 *)&channel0RxBuffer1[0][0];
	GethConfig.dma.rxChannel[0].rxBuffer1Size = IFXGETH_MAX_RX_BUFFER_SIZE;
	/* DMA event signaling. Note that the priority is set to 0, which bypassed the
	* interrupt from actually being enabled. This network interface works in polling
	* mode.
	*/
    // see note "set which cpu to service the interrupt"
	//if (CPU_WHICH_SERVICE_ETHERNET) gethIsrProvider = (IfxSrc_Tos)(CPU_WHICH_SERVICE_ETHERNET+1);
	//else  gethIsrProvider = (IfxSrc_Tos)CPU_WHICH_SERVICE_ETHERNET;
  
	GethConfig.dma.txInterrupt[0].channelId = IfxGeth_DmaChannel_0;
	GethConfig.dma.txInterrupt[0].priority = 0; //highest number first
	GethConfig.dma.txInterrupt[0].provider = IfxSrc_Tos_cpu0;
	GethConfig.dma.rxInterrupt[0].channelId = IfxGeth_DmaChannel_0;
	GethConfig.dma.rxInterrupt[0].priority = 0; //highest number firsts
	GethConfig.dma.rxInterrupt[0].provider = IfxSrc_Tos_cpu0;

	/* first we reset our phy manually, to make sure that the phy is ready when we init our module */
	{
		IfxGeth_enableModule(&MODULE_GETH);
		IfxPort_setPinModeOutput(IfxGeth_MDC_P12_0_OUT.pin.port, IfxGeth_MDC_P12_0_OUT.pin.pinIndex, IfxPort_OutputMode_pushPull, IfxGeth_MDC_P12_0_OUT.select);
		GETH_GPCTL.B.ALTI0  = IfxGeth_MDIO_P12_1_INOUT.inSelect;

		while (GETH_MAC_MDIO_ADDRESS.B.GB) {};
		// first we wait that we are able to communicate with the Phy
		do
		{
			GETH_MAC_MDIO_ADDRESS.U = (0 << 21) | (0 << 16) | (0 << 8) | (3 << 2) | (1 << 0);
			while (GETH_MAC_MDIO_ADDRESS.B.GB) {};
		} while (GETH_MAC_MDIO_DATA.U & 0x8000);                                                      // wait for reset to finish
		// reset PHY
		// put data
		GETH_MAC_MDIO_DATA.U = 0x8000;
		GETH_MAC_MDIO_ADDRESS.U = (0 << 21) | (0 << 16) | (0 << 8) |  (1 << 2) | (1 << 0);
		while (GETH_MAC_MDIO_ADDRESS.B.GB) {};

		do
		{
			GETH_MAC_MDIO_ADDRESS.U = (0 << 21) | (0 << 16) | (0 << 8) | (3 << 2) | (1 << 0);
			while (GETH_MAC_MDIO_ADDRESS.B.GB) {};
		} while (GETH_MAC_MDIO_DATA.U & 0x8000);                                                      // wait for reset to finish
	} // see note "infineon TC399 PHY ETH init setting"
  
	/* Initialize the module. */
	IfxGeth_Eth_initModule(&g_IfxGeth, &GethConfig);
	/*	 Initialize the PHY. */
	//IfxGeth_Eth_Phy_Dp83825i_init(); // use for openBLT TC397 EV board PHY
	if(IfxGeth_Eth_Phy_88E1512_init() != 0){  //IfxGeth_Eth_Phy_88E1512_init()!=0 is false,
		debugPrintOnce = true;//qqqq
		Debug_Print_Out("88E1512 init F", 0, 0, 0, dbug_num_type_str);
	    return boot_FALSE; // fail
	}

	/* Enable Ethernet transmitter and receiver. */
	IfxGeth_Eth_startTransmitters(&g_IfxGeth, 1);
	IfxGeth_Eth_startReceivers(&g_IfxGeth, 1);
	/* Refresh the link status now that the Ethernet module is up and running. */
	netdev_link_refresh();
	return boot_TRUE;
} /*** end of netdev_init ***/


/************************************************************************************//**
** \brief     Initializes the MAC address of the network device.
**
****************************************************************************************/
void netdev_init_mac(void)
{
  /* Configure the MAC address */
  uip_setethaddr(macAddress);
} /*** end of netdev_init_mac ***/


/************************************************************************************//**
** \brief     Obtains the MAC address of the network device.
** \param     mac_addr MAC address storage buffer as a byte array.
**
****************************************************************************************/
void netdev_get_mac(unsigned char * mac_addr)
{
  mac_addr[0] = macAddress.addr[0];
  mac_addr[1] = macAddress.addr[1];
  mac_addr[2] = macAddress.addr[2];
  mac_addr[3] = macAddress.addr[3];
  mac_addr[4] = macAddress.addr[4];
  mac_addr[5] = macAddress.addr[5];
} /*** end of netdev_get_mac ***/


/************************************************************************************//**
** \brief     Read newly received data from the network device.
** \details   Copy the newly received data to the uip_buf byte buffer. Keep in mind its
**            maximum size (UIP_CONF_BUFFER_SIZE).
** \return    Number of newly received bytes that were copied to uip_buf or 0 if no new
**            data was received.
**
****************************************************************************************/
unsigned int netdev_read(void)
{
  unsigned int               result = 0;
  IfxGeth_RxDescr volatile * rxDescriptor;
  uint16                     frameLen;
  uint8                    * rxData;

  /* Note this this function is called continuously to poll for new data. A good place
   * to also refresh the link status.
   */
  netdev_link_refresh();

  /* Only continue if the link is actually up. */
  if (linkUpFlag != 0)
  {
    /* Only continue if new data was received. */
    if (IfxGeth_Eth_isRxDataAvailable(&g_IfxGeth, IfxGeth_RxDmaChannel_0) != FALSE)
    {
	    debugPrintOnce = true;//qqqq
	    Debug_Print_Out("RX data available", 0u, 0, 0u, dbug_num_type_str);
      /* Get pointer to its descriptor. */
      rxDescriptor = IfxGeth_Eth_getActualRxDescriptor(&g_IfxGeth, IfxGeth_RxDmaChannel_0);
      /* Only continue with a valid descriptor. */
      if (rxDescriptor != NULL)
      {
        /* Determine the size of the newly received frame. */
        uint32 rdes3 = rxDescriptor->RDES3.U;
        uint32 rdes1 = rxDescriptor->RDES1.U;

        if (((rdes3 & (1UL << 15)) != 0U) ||
            ((rdes1 & (1UL << 7)) != 0U) ||
            ((rdes3 & (1UL << 28)) == 0U))
        {
          /* Error, this block is invalid. */
          frameLen = 0;
        }
        else
        {
          /* Subtract CRC. */
          frameLen = (uint16)((rdes3 & 0x7FFF) - 4U);
        }
		debugPrintOnce = true;//qqqq
		Debug_Print_Out("RDES3 = 0x", 0u, 0, rxDescriptor->RDES3.U, dbug_num_type_HEX32);

		debugPrintOnce = true;//qqqq
		Debug_Print_Out("RDES1 = 0x", 0u, 0, rxDescriptor->RDES1.U, dbug_num_type_HEX32);
        /* Only continue with a valid frame length. */
        if (frameLen > 0)
        {
        	debugPrintOnce = true;//qqqq
			Debug_Print_Out("RX frameLen = ", (uint32_t)frameLen, 0, 0u, dbug_num_type_U32);
			debugPrintOnce = true;//qqqq
			Debug_Print_Data_Array("RX raw = ", rxData, (frameLen > 64u) ? 64u : (uint32_t)frameLen);
          /* Obtain pointer to the received data. */
          rxData = IfxGeth_Eth_getReceiveBuffer(&g_IfxGeth, IfxGeth_RxDmaChannel_0);
          /* Only continue with a valid data pointer. */
          if (rxData != NULL)
          {
            /* Copy the received data ato the uIP data buffer. */
            memcpy(uip_buf, rxData, frameLen);
            /* Update the result. */
            result = frameLen;
          }
        }
      }
      /* Free the receive buffer, enabling it for the further reception. */
      IfxGeth_Eth_freeReceiveBuffer(&g_IfxGeth, IfxGeth_RxDmaChannel_0);
    }
  }

  /* Give the result back to the caller. */
  return result;
} /*** end of netdev_read ***/


/************************************************************************************//**
** \brief     Send data on the network device.
** \details   The data to send is available in the uip_buf byte buffer and the number of
**            bytes to send is stored in uip_len.
**
****************************************************************************************/
void netdev_send(void)
{
  uint8                    * txData = NULL;
  uint32                     timeout;
  IfxGeth_TxDescr volatile * txDescriptor;

	debugPrintOnce = true;//qqqq
	Debug_Print_Out("TX uip_len = ", (uint32_t)uip_len, 0, 0u, dbug_num_type_U32);

	if (uip_len > 0u)//qqqq
	{
	    debugPrintOnce = true;
	    Debug_Print_Data_Array("TX raw = ", uip_buf, (uip_len > 64u) ? 64u : (uint32_t)uip_len);
	}
  /* Only continue if the link is actually up. */
  if (linkUpFlag != 0)
  {
    /* Set timeout time to wait for a free transmit buffer to become available. */
    timeout = TimerGet() + NETDEV_TX_BUFF_ALLOC_TIMEOUT_MS;
    /* Wait for a free transmit buffer to become available. */
    while (txData == NULL)
    {
      /* Attempt to get a free transmit buffer. */
      txData = IfxGeth_Eth_getTransmitBuffer(&g_IfxGeth, IfxGeth_TxDmaChannel_0);
      /* Break loop upon timeout. This would indicate a hardware failure. */
      if (TimerGet() > timeout)
      {
        break;
      }
    }

    /* Only continue with a valid transmit buffer. */
    if (txData != NULL)
    {
      /* Copy the packet data to the tx data buffer. */
      memcpy(txData, uip_buf, uip_len);
      /* Make sure the buffer length is set to the max. available, just in case a
       * previously packet overwrote this.
       */
      txDescriptor = IfxGeth_Eth_getActualTxDescriptor(&g_IfxGeth, IfxGeth_TxDmaChannel_0);
      txDescriptor->TDES2.R.B1L = IFXGETH_MAX_TX_BUFFER_SIZE;
      /* Clear the TX interrupt status flag. */
      IfxGeth_dma_clearInterruptFlag(g_IfxGeth.gethSFR, IfxGeth_DmaChannel_0,
                                     IfxGeth_DmaInterruptFlag_transmitInterrupt) ;
      /* Submit the buffer transmit request. */
      IfxGeth_Eth_sendTransmitBuffer(&g_IfxGeth, uip_len, IfxGeth_TxDmaChannel_0);
      /* Set timeout time to wait for transmit completion. */
      timeout = TimerGet() + NETDEV_TX_PACKET_TIMEOUT_MS;
      /* Wait for the transmit operation to complete. */
      while (IfxGeth_dma_isInterruptFlagSet(g_IfxGeth.gethSFR, IfxGeth_DmaChannel_0,
                                      IfxGeth_DmaInterruptFlag_transmitInterrupt) == FALSE)
      {
        /* Break loop upon timeout. This would indicate a hardware failure. */
        if (TimerGet() > timeout)
        {
          break;
        }
      }
    }
  }
} /*** end of netdev_send ***/


/************************************************************************************//**
** \brief     Utility function that should be called continuously to monitor the link
**            status.
**
****************************************************************************************/
static void netdev_link_refresh(void)
{
  static unsigned long              nextRefreshEvent = 0;
  unsigned long                     currentTime;
  IfxGeth_LineSpeed                 LineSpeed;
  IfxGeth_DuplexMode                DuplexMode;
  Ifx_GETH_MAC_PHYIF_CONTROL_STATUS CtrlStatus;
  unsigned char                     linkUpFlagPrevious;

  /* Get the current time. */
  currentTime = TimerGet();
  /* Check for refresh event. */
  if (currentTime >= nextRefreshEvent)
  {
    /* Schedule the next refresh event 100 milliseconds from now. */
    nextRefreshEvent = currentTime + 100;
    /* Store last known link status. */
    linkUpFlagPrevious = linkUpFlag;
    /* Obtain the current link state from the PHY. */
	CtrlStatus.U = IfxGeth_Eth_Phy_88E1512_link_status();
	//CtrlStatus.U = GETH_MAC_PHYIF_CONTROL_STATUS.U; //if IfxGeth_Eth_Phy_88E1512_link_status() could not use then use this
	
    /* Update the link status flag. */
    linkUpFlag = (CtrlStatus.B.LNKSTS == 1) ? 1 : 0;
    /* Did the link change from down to up? */
	debugPrintOnce = true;//qqqq
	Debug_Print_Out("linkUpFlag = ", (uint32_t)linkUpFlag, 0, 0u, dbug_num_type_U32);
    if ( (linkUpFlag == 1) && (linkUpFlag != linkUpFlagPrevious) )
    {
      /* Set safe default for the line speed and duplex mode. */
      DuplexMode = IfxGeth_DuplexMode_halfDuplex;
      LineSpeed = IfxGeth_LineSpeed_10Mbps;

      /* Update duplex mode. */
      if (CtrlStatus.B.LNKMOD == 1)
      {
        DuplexMode = IfxGeth_DuplexMode_fullDuplex;
      }
      IfxGeth_mac_setDuplexMode(g_IfxGeth.gethSFR, DuplexMode);

      /* Update line speed. */
      if (CtrlStatus.B.LNKSPEED == 1)
      {
        LineSpeed = IfxGeth_LineSpeed_100Mbps;
      }
      else if (CtrlStatus.B.LNKSPEED > 1)
      {
        LineSpeed = IfxGeth_LineSpeed_1000Mbps;
      }
      IfxGeth_mac_setLineSpeed(g_IfxGeth.gethSFR, LineSpeed);
    }
	else{
		__nop();	
	}
  }
#if 0
	if ((linkUpFlag == 1) && (linkUpFlag != linkUpFlagPrevious)){//qqqq
		debugPrintOnce = true;
		Debug_Print_Out("ETH link up T", 0u, 0, 0u, dbug_num_type_str);
	}
	else{
		debugPrintOnce = true;
		Debug_Print_Out("ETH link up F", 0u, 0, 0u, dbug_num_type_str);
	}
#endif
} /*** end of netdev_link_refresh ***/

void Net_Mac_Ip_Config(void)
{
	uint8_t config = 0;
	
	config |= (uint8)IfxPort_getPinState(IFXCFG_PORT_TB_SW_VER_D0.port, IFXCFG_PORT_TB_SW_VER_D0.pinIndex);
	config |= (uint8)IfxPort_getPinState(IFXCFG_PORT_TB_SW_VER_D1.port, IFXCFG_PORT_TB_SW_VER_D1.pinIndex) << 1;
	config |= (uint8)IfxPort_getPinState(IFXCFG_PORT_TB_SW_VER_D2.port, IFXCFG_PORT_TB_SW_VER_D2.pinIndex) << 2;
	config |= (uint8)IfxPort_getPinState(IFXCFG_PORT_TB_SW_VER_D3.port, IFXCFG_PORT_TB_SW_VER_D3.pinIndex) << 3;

	switch (config){
		case 2:
			bootComNetIpaddress3 = 21;
			break;
		case 4:
			bootComNetIpaddress3 = 22;
			break;
		case 8:
			bootComNetIpaddress3 = 23;
			break;
		case 1:
		default:
			bootComNetIpaddress3 = 20;
			break;
	}
	

#if (BOOT_COM_SET_UNIQUE_MAC_ENABLE == false)
		macAddress.addr[0] = NETDEV_DEFAULT_MACADDR0;
		macAddress.addr[1] = NETDEV_DEFAULT_MACADDR1;
		macAddress.addr[2] = NETDEV_DEFAULT_MACADDR2;
		macAddress.addr[3] = NETDEV_DEFAULT_MACADDR3;
		macAddress.addr[4] = NETDEV_DEFAULT_MACADDR4;
		macAddress.addr[5] = NETDEV_DEFAULT_MACADDR5;
#else
	UCID_Hash6_Get(&macAddress);
#endif

}

/* Little-endian is expanded from 32 bits to 4 consecutive bytes */
static inline void U32_To_L_End_Bytes(uint32_t w, uint8_t *dst)
{
    dst[0] = (uint8_t)( w        & 0xFFu);
    dst[1] = (uint8_t)((w >>  8) & 0xFFu);
    dst[2] = (uint8_t)((w >> 16) & 0xFFu);
    dst[3] = (uint8_t)((w >> 24) & 0xFFu);
}

int UCID_Read16(uint8_t out16[16])
{
    if (!out16) return -1;

    /* Read PFLASH/UCB directly in memory-mapped mode (read-only) */
    volatile const uint32_t *w0 = (volatile const uint32_t *)(uintptr_t)UCID_ADDR_WORD0;
    volatile const uint32_t *w1 = (volatile const uint32_t *)(uintptr_t)UCID_ADDR_WORD1;
    volatile const uint32_t *w2 = (volatile const uint32_t *)(uintptr_t)UCID_ADDR_WORD2;
    volatile const uint32_t *w3 = (volatile const uint32_t *)(uintptr_t)UCID_ADDR_WORD3;

    uint32_t a = *w0;
    uint32_t b = *w1;
    uint32_t c = *w2;
    uint32_t d = *w3;

    U32_To_L_End_Bytes(a, &out16[ 0]);
    U32_To_L_End_Bytes(b, &out16[ 4]);
    U32_To_L_End_Bytes(c, &out16[ 8]);
    U32_To_L_End_Bytes(d, &out16[12]);

    return 0;
}

/* 16B -> 6B: XOR folding + simple diffusion (non-secure hash, used to shorten recognition) */
void UCID_Hash6_From16(const uint8_t ucid16[16], struct uip_eth_addr *out6)
{
    uint8_t h[6] = {0,0,0,0,0,0};

    /*  XOR folding */
    for (uint8_t i = 0; i < 16; ++i) {
        h[i % 6] ^= ucid16[i];
    }

    /* simple diffusion */
    uint8_t t0 = (uint8_t)(h[0] ^ h[3]);
    uint8_t t1 = (uint8_t)(h[1] ^ h[4]);
    uint8_t t2 = (uint8_t)(h[2] ^ h[5]);

    h[0] ^= t1; h[1] ^= t2; h[2] ^= t0;
    h[3] ^= t1; h[4] ^= t2; h[5] ^= t0;

    /* Force setting MAC Address characteristic bits */
    /* Byte 0 Bit 1 (U/L) = 1 -> Locally Administered Address */
    /* Byte 0 Bit 0 (I/G) = 0 -> Unicast */
    h[0] = (h[0] | 0x02) & 0xFE;

    memcpy(out6->addr, h, 6);
}

int UCID_Hash6_Get(struct uip_eth_addr *out6)
{
    uint8_t ucid[16];
    
    if (UCID_Read16(ucid) != 0) {
        return -1;
    }

    UCID_Hash6_From16(ucid, out6);
	return 0;
}







/*********************************** end of netdev.c ***********************************/




