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
#include "Compilers.h"


/****************************************************************************************
* Local data declarations
****************************************************************************************/
uint8_t bootComNetIpaddress3;

/* MAC adress buffer. */
struct uip_eth_addr macAddress;

/* GETH driver handle. */
static IfxGeth_Eth g_IfxGeth;

/* Ethernet Tx/Rx descriptor and frame storage.
 * Keep the descriptor lists on the iLLD-provided LMU section objects and place the
 * frame buffers explicitly into the same non-cached LMU section. This removes the
 * remaining dependency on hard-coded 0xB004.... aliases and lets the linker assign
 * the final system addresses consistently for both CPU and GETH DMA use.
 */
BEGIN_DATA_SECTION(lmubss)
static uint8 IFX_ALIGN(64) g_netdevGethRxBuffers[IFXGETH_MAX_RX_DESCRIPTORS * IFXGETH_MAX_RX_BUFFER_SIZE];
static uint8 IFX_ALIGN(64) g_netdevGethTxBuffers[IFXGETH_MAX_TX_DESCRIPTORS * IFXGETH_MAX_TX_BUFFER_SIZE];
END_DATA_SECTION

#define NETDEV_GETH_LMU_CACHED_TO_NONCACHED(addr) ((((uint32)(addr) & 0xF0000000u) == 0x90000000u) ? ((uint32)(addr) + 0x20000000u) : (uint32)(addr))

#define NETDEV_GETH_RXDESC_SYMBOL_ADDR ((uint32)&IfxGeth_Eth_rxDescrList[0][0])
#define NETDEV_GETH_TXDESC_SYMBOL_ADDR ((uint32)&IfxGeth_Eth_txDescrList[0][0])
#define NETDEV_GETH_RXBUF_SYMBOL_ADDR  ((uint32)&g_netdevGethRxBuffers[0])
#define NETDEV_GETH_TXBUF_SYMBOL_ADDR  ((uint32)&g_netdevGethTxBuffers[0])

#define NETDEV_GETH_RXDESC_BASE_ADDR   NETDEV_GETH_LMU_CACHED_TO_NONCACHED(NETDEV_GETH_RXDESC_SYMBOL_ADDR)
#define NETDEV_GETH_TXDESC_BASE_ADDR   NETDEV_GETH_LMU_CACHED_TO_NONCACHED(NETDEV_GETH_TXDESC_SYMBOL_ADDR)
#define NETDEV_GETH_RXBUF_BASE_ADDR    NETDEV_GETH_LMU_CACHED_TO_NONCACHED(NETDEV_GETH_RXBUF_SYMBOL_ADDR)
#define NETDEV_GETH_TXBUF_BASE_ADDR    NETDEV_GETH_LMU_CACHED_TO_NONCACHED(NETDEV_GETH_TXBUF_SYMBOL_ADDR)
#define NETDEV_GETH_RXDESC_TOTAL_SIZE  ((uint32)sizeof(IfxGeth_Eth_rxDescrList[0][0]))
#define NETDEV_GETH_TXDESC_TOTAL_SIZE  ((uint32)sizeof(IfxGeth_Eth_txDescrList[0][0]))
#define NETDEV_GETH_RXBUF_TOTAL_SIZE   ((uint32)sizeof(g_netdevGethRxBuffers))
#define NETDEV_GETH_TXBUF_TOTAL_SIZE   ((uint32)sizeof(g_netdevGethTxBuffers))

/* Boolean flag to keep track of the link status. */
static uint8 linkUpFlag;
/* RX tail pointer recovery mode.
 * 1: one-past-last descriptor (iLLD default)
 * 0: last valid descriptor
 */
static uint8 g_netdevRxTailUseOnePastLast = 1u;
static uint8 g_netdevRxUseDaBasedRouting = 0u;
static uint8 g_netdevRxAcceptanceMode = 1u; /* 1 = strict station-MAC / DA-based, 0 = permissive debug */

#define NETDEV_RX_PROGRESS_LOG_PKT_STEP           (64u)
#define NETDEV_RX_STALL_SUMMARY_LOG_MASK          (0x1FFFu)
#define NETDEV_RX_STALL_DETAIL_LOG_MASK           (0x7FFFu)
#define NETDEV_RX_MAC_ONLY_RECOVERY_STEP_MASK     (0x1Fu)
#define NETDEV_RX_MAC_ONLY_RECOVERY_LOG_MASK      (0xFFu)
#define NETDEV_RX_IDLE_MISMATCH_LOG_MASK          (0x3FFu)
#define NETDEV_RX_FRAME_DETAIL_LOG_MASK           (0x1Fu)


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
static uint32 netdev_get_rx_tail_pointer_value(uint8 useOnePastLast);
static void netdev_program_rx_ring_registers(uint8 useOnePastLast);
static void netdev_select_rx_tail_mode(uint8 useOnePastLast, uint8 verbose);
static void netdev_toggle_rx_tail_mode(const char *reason);
static void netdev_force_rx_quiet_summary(const char *reason);
static void netdev_force_rx_rearm(void);
static void netdev_force_tx_rearm(void);
static void netdev_toggle_rx_acceptance_mode(const char *reason);
static void netdev_prepare_tx_channel(void);
static void netdev_force_rx_queue_path_config(uint8 verbose);
static void netdev_debug_dump_rx_queue_path(const char *label);
static void netdev_debug_dump_rx_current_buffer_head(const char *label);
static void netdev_debug_dump_rx_low_level_state(const char *label);
static void netdev_debug_dump_rx_queue_dma_matrix(const char *label);
static void netdev_debug_dump_rx_packet_counters(const char *label);
static void netdev_debug_dump_rx_buffer_window(const char *label, uint32 bufferAddr);
static void netdev_debug_dump_rx_buffer_ring_heads(const char *label, uint32 count);
static void netdev_debug_dump_mac_dma_counters(void);
static void netdev_debug_dump_mac_programming(const char *label);
static void netdev_debug_print_ipv4_bytes(const char *label, const uint8 *ipBytes);
static void netdev_debug_print_eth_tx_summary(void);
static void netdev_debug_print_eth_rx_summary(const uint8 *frame, uint16 frameLen);
static uint32 netdev_debug_get_rx_descriptor_index(volatile IfxGeth_RxDescr *rxDescriptor);
static void netdev_debug_dump_rx_descriptor(const char *label, volatile IfxGeth_RxDescr *rxDescriptor);
static void netdev_debug_dump_rx_ring_descriptors(const char *label);
static void netdev_debug_dump_rx_visibility(const char *label);
static void netdev_debug_dump_tx_descriptor(const char *label, volatile IfxGeth_TxDescr *txDescriptor);
static volatile IfxGeth_RxDescr *netdev_find_completed_rx_descriptor(void);
static uint8 netdev_rx_dma_needs_recovery(void);
static void netdev_recover_rx_channel_without_rearm(void);

static void netdev_debug_dump_mac_programming(const char *label)
{
  uint32 macHigh;
  uint32 macLow;
  uint8  regMac[6];
  uint32 macMatch;

  macHigh = g_IfxGeth.gethSFR->MAC_ADDRESS_HIGH0.U;
  macLow  = g_IfxGeth.gethSFR->MAC_ADDRESS_LOW0.U;

  regMac[0] = (uint8)((macLow >> 0u) & 0xFFu);
  regMac[1] = (uint8)((macLow >> 8u) & 0xFFu);
  regMac[2] = (uint8)((macLow >> 16u) & 0xFFu);
  regMac[3] = (uint8)((macLow >> 24u) & 0xFFu);
  regMac[4] = (uint8)((macHigh >> 0u) & 0xFFu);
  regMac[5] = (uint8)((macHigh >> 8u) & 0xFFu);

  macMatch = ((regMac[0] == macAddress.addr[0]) &&
              (regMac[1] == macAddress.addr[1]) &&
              (regMac[2] == macAddress.addr[2]) &&
              (regMac[3] == macAddress.addr[3]) &&
              (regMac[4] == macAddress.addr[4]) &&
              (regMac[5] == macAddress.addr[5])) ? 1u : 0u;

  debugPrintOnce = true;
  Debug_Print_Out(label, 0u, 0, 0u, dbug_num_type_str);
  debugPrintOnce = true;
  Debug_Print_Out("  CFG_MAC[0] = 0x", 0u, 0, (uint32)macAddress.addr[0], dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  CFG_MAC[1] = 0x", 0u, 0, (uint32)macAddress.addr[1], dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  CFG_MAC[2] = 0x", 0u, 0, (uint32)macAddress.addr[2], dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  CFG_MAC[3] = 0x", 0u, 0, (uint32)macAddress.addr[3], dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  CFG_MAC[4] = 0x", 0u, 0, (uint32)macAddress.addr[4], dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  CFG_MAC[5] = 0x", 0u, 0, (uint32)macAddress.addr[5], dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  REG_MAC[0] = 0x", 0u, 0, (uint32)regMac[0], dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  REG_MAC[1] = 0x", 0u, 0, (uint32)regMac[1], dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  REG_MAC[2] = 0x", 0u, 0, (uint32)regMac[2], dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  REG_MAC[3] = 0x", 0u, 0, (uint32)regMac[3], dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  REG_MAC[4] = 0x", 0u, 0, (uint32)regMac[4], dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  REG_MAC[5] = 0x", 0u, 0, (uint32)regMac[5], dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  MAC register match = ", macMatch, 0, 0u, dbug_num_type_U32);
}


static void netdev_debug_dump_rx_queue_path(const char *label)
{
  debugPrintOnce = true;
  Debug_Print_Out(label, 0u, 0, 0u, dbug_num_type_str);
  debugPrintOnce = true;
  Debug_Print_Out("  RX route mode (DA-based) = ", g_netdevRxUseDaBasedRouting, 0, 0u, dbug_num_type_U32);
  Debug_Print_Out("  RX accept mode = ", g_netdevRxAcceptanceMode, 0, 0u, dbug_num_type_U32);
  debugPrintOnce = true;
  Debug_Print_Out("  MAC_PACKET_FILTER = 0x", 0u, 0, g_IfxGeth.gethSFR->MAC_PACKET_FILTER.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  MAC_RXQ_CTRL0 = 0x", 0u, 0, g_IfxGeth.gethSFR->MAC_RXQ_CTRL0.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  MAC_RXQ_CTRL1 = 0x", 0u, 0, g_IfxGeth.gethSFR->MAC_RXQ_CTRL1.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  MAC_RXQ_CTRL2 = 0x", 0u, 0, g_IfxGeth.gethSFR->MAC_RXQ_CTRL2.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  MAC_RXQ_CTRL4 = 0x", 0u, 0, g_IfxGeth.gethSFR->MAC_RXQ_CTRL4.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  MTL_RXQ_DMA_MAP0 = 0x", 0u, 0, g_IfxGeth.gethSFR->MTL_RXQ_DMA_MAP0.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  MAC_HW_FEATURE1 = 0x", 0u, 0, g_IfxGeth.gethSFR->MAC_HW_FEATURE1.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  MAC_HW_FEATURE1 DCBEN = ", (uint32)g_IfxGeth.gethSFR->MAC_HW_FEATURE1.B.DCBEN, 0, 0u, dbug_num_type_U32);
  debugPrintOnce = true;
  Debug_Print_Out("  MAC_HW_FEATURE1 DBGMEMA = ", (uint32)g_IfxGeth.gethSFR->MAC_HW_FEATURE1.B.DBGMEMA, 0, 0u, dbug_num_type_U32);
  debugPrintOnce = true;
  Debug_Print_Out("  MAC_HW_FEATURE2 = 0x", 0u, 0, g_IfxGeth.gethSFR->MAC_HW_FEATURE2.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  MAC_HW_FEATURE2 RXQCNT = ", (uint32)g_IfxGeth.gethSFR->MAC_HW_FEATURE2.B.RXQCNT, 0, 0u, dbug_num_type_U32);
  debugPrintOnce = true;
  Debug_Print_Out("  MTL_RXQ0_OMR = 0x", 0u, 0, g_IfxGeth.gethSFR->MTL_RXQ0.OPERATION_MODE.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  MTL_RXQ0_DBG = 0x", 0u, 0, g_IfxGeth.gethSFR->MTL_RXQ0.DEBUG.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  MTL_RXQ0_MISS = 0x", 0u, 0, g_IfxGeth.gethSFR->MTL_RXQ0.MISSED_PACKET_OVERFLOW_CNT.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  MAC_ADDR0_HIGH = 0x", 0u, 0, g_IfxGeth.gethSFR->MAC_ADDRESS_HIGH0.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  MAC_ADDR0_LOW = 0x", 0u, 0, g_IfxGeth.gethSFR->MAC_ADDRESS_LOW0.U, dbug_num_type_HEX32);
  netdev_debug_dump_mac_programming("  MAC programming decode");
}


static void netdev_debug_dump_rx_low_level_state(const char *label)
{
  debugPrintOnce = true;
  Debug_Print_Out(label, 0u, 0, 0u, dbug_num_type_str);
  debugPrintOnce = true;
  Debug_Print_Out("  DMA_DEBUG_STATUS0 = 0x", 0u, 0, g_IfxGeth.gethSFR->DMA_DEBUG_STATUS0.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  DMA_DEBUG RPS0 = ", (uint32)g_IfxGeth.gethSFR->DMA_DEBUG_STATUS0.B.RPS0, 0, 0u, dbug_num_type_U32);
  debugPrintOnce = true;
  Debug_Print_Out("  DMA_DEBUG TPS0 = ", (uint32)g_IfxGeth.gethSFR->DMA_DEBUG_STATUS0.B.TPS0, 0, 0u, dbug_num_type_U32);
  debugPrintOnce = true;
  Debug_Print_Out("  DMA_INTERRUPT_STATUS = 0x", 0u, 0, g_IfxGeth.gethSFR->DMA_INTERRUPT_STATUS.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  DMA_CH0_STATUS = 0x", 0u, 0, g_IfxGeth.gethSFR->DMA_CH[0].STATUS.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  DMA_CH0 NIS = ", (uint32)g_IfxGeth.gethSFR->DMA_CH[0].STATUS.B.NIS, 0, 0u, dbug_num_type_U32);
  debugPrintOnce = true;
  Debug_Print_Out("  DMA_CH0 AIS = ", (uint32)g_IfxGeth.gethSFR->DMA_CH[0].STATUS.B.AIS, 0, 0u, dbug_num_type_U32);
  debugPrintOnce = true;
  Debug_Print_Out("  DMA_CH0 CDE = ", (uint32)g_IfxGeth.gethSFR->DMA_CH[0].STATUS.B.CDE, 0, 0u, dbug_num_type_U32);
  debugPrintOnce = true;
  Debug_Print_Out("  DMA_CH0 ETI = ", (uint32)g_IfxGeth.gethSFR->DMA_CH[0].STATUS.B.ETI, 0, 0u, dbug_num_type_U32);
  debugPrintOnce = true;
  Debug_Print_Out("  DMA_CH0 ERI = ", (uint32)g_IfxGeth.gethSFR->DMA_CH[0].STATUS.B.ERI, 0, 0u, dbug_num_type_U32);
  debugPrintOnce = true;
  Debug_Print_Out("  DMA_CH0 FBE = ", (uint32)g_IfxGeth.gethSFR->DMA_CH[0].STATUS.B.FBE, 0, 0u, dbug_num_type_U32);
  debugPrintOnce = true;
  Debug_Print_Out("  DMA_CH0 REB = ", (uint32)g_IfxGeth.gethSFR->DMA_CH[0].STATUS.B.REB, 0, 0u, dbug_num_type_U32);
  debugPrintOnce = true;
  Debug_Print_Out("  MTL_RXQ0_DEBUG = 0x", 0u, 0, g_IfxGeth.gethSFR->MTL_RXQ0.DEBUG.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  MTL_RXQ0 RWCSTS = ", (uint32)g_IfxGeth.gethSFR->MTL_RXQ0.DEBUG.B.RWCSTS, 0, 0u, dbug_num_type_U32);
  debugPrintOnce = true;
  Debug_Print_Out("  MTL_RXQ0 RRCSTS = ", (uint32)g_IfxGeth.gethSFR->MTL_RXQ0.DEBUG.B.RRCSTS, 0, 0u, dbug_num_type_U32);
  debugPrintOnce = true;
  Debug_Print_Out("  MTL_RXQ0 RXQSTS = ", (uint32)g_IfxGeth.gethSFR->MTL_RXQ0.DEBUG.B.RXQSTS, 0, 0u, dbug_num_type_U32);
  debugPrintOnce = true;
  Debug_Print_Out("  MTL_RXQ0 PRXQ = ", (uint32)g_IfxGeth.gethSFR->MTL_RXQ0.DEBUG.B.PRXQ, 0, 0u, dbug_num_type_U32);
}



static void netdev_debug_dump_rx_queue_dma_matrix(const char *label)
{
  uint32 channelIndex;

  debugPrintOnce = true;
  Debug_Print_Out(label, 0u, 0, 0u, dbug_num_type_str);
  debugPrintOnce = true;
  Debug_Print_Out("  MAC_RXQ_CTRL0 = 0x", 0u, 0, g_IfxGeth.gethSFR->MAC_RXQ_CTRL0.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  MAC_RXQ_CTRL1 = 0x", 0u, 0, g_IfxGeth.gethSFR->MAC_RXQ_CTRL1.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  MAC_RXQ_CTRL2 = 0x", 0u, 0, g_IfxGeth.gethSFR->MAC_RXQ_CTRL2.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  MAC_RXQ_CTRL4 = 0x", 0u, 0, g_IfxGeth.gethSFR->MAC_RXQ_CTRL4.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  MTL_RXQ_DMA_MAP0 = 0x", 0u, 0, g_IfxGeth.gethSFR->MTL_RXQ_DMA_MAP0.U, dbug_num_type_HEX32);

  debugPrintOnce = true;
  Debug_Print_Out("  RXQ0 OMR = 0x", 0u, 0, g_IfxGeth.gethSFR->MTL_RXQ0.OPERATION_MODE.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  RXQ0 MISS = 0x", 0u, 0, g_IfxGeth.gethSFR->MTL_RXQ0.MISSED_PACKET_OVERFLOW_CNT.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  RXQ0 DEBUG = 0x", 0u, 0, g_IfxGeth.gethSFR->MTL_RXQ0.DEBUG.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  RXQ0 CTRL = 0x", 0u, 0, g_IfxGeth.gethSFR->MTL_RXQ0.CONTROL.U, dbug_num_type_HEX32);

  debugPrintOnce = true;
  Debug_Print_Out("  RXQ1 OMR = 0x", 0u, 0, g_IfxGeth.gethSFR->MTL_RXQ1.OPERATION_MODE.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  RXQ1 MISS = 0x", 0u, 0, g_IfxGeth.gethSFR->MTL_RXQ1.MISSED_PACKET_OVERFLOW_CNT.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  RXQ1 DEBUG = 0x", 0u, 0, g_IfxGeth.gethSFR->MTL_RXQ1.DEBUG.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  RXQ1 CTRL = 0x", 0u, 0, g_IfxGeth.gethSFR->MTL_RXQ1.CONTROL.U, dbug_num_type_HEX32);

  debugPrintOnce = true;
  Debug_Print_Out("  RXQ2 OMR = 0x", 0u, 0, g_IfxGeth.gethSFR->MTL_RXQ2.OPERATION_MODE.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  RXQ2 MISS = 0x", 0u, 0, g_IfxGeth.gethSFR->MTL_RXQ2.MISSED_PACKET_OVERFLOW_CNT.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  RXQ2 DEBUG = 0x", 0u, 0, g_IfxGeth.gethSFR->MTL_RXQ2.DEBUG.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  RXQ2 CTRL = 0x", 0u, 0, g_IfxGeth.gethSFR->MTL_RXQ2.CONTROL.U, dbug_num_type_HEX32);

  debugPrintOnce = true;
  Debug_Print_Out("  RXQ3 OMR = 0x", 0u, 0, g_IfxGeth.gethSFR->MTL_RXQ3.OPERATION_MODE.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  RXQ3 MISS = 0x", 0u, 0, g_IfxGeth.gethSFR->MTL_RXQ3.MISSED_PACKET_OVERFLOW_CNT.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  RXQ3 DEBUG = 0x", 0u, 0, g_IfxGeth.gethSFR->MTL_RXQ3.DEBUG.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  RXQ3 CTRL = 0x", 0u, 0, g_IfxGeth.gethSFR->MTL_RXQ3.CONTROL.U, dbug_num_type_HEX32);

  for (channelIndex = 0u; channelIndex < 4u; channelIndex++)
  {
    debugPrintOnce = true;
    Debug_Print_Out("  DMA channel idx = ", channelIndex, 0, 0u, dbug_num_type_U32);
    debugPrintOnce = true;
    Debug_Print_Out("    DMA_CONTROL = 0x", 0u, 0, g_IfxGeth.gethSFR->DMA_CH[channelIndex].CONTROL.U, dbug_num_type_HEX32);
    debugPrintOnce = true;
    Debug_Print_Out("    RX_CONTROL = 0x", 0u, 0, g_IfxGeth.gethSFR->DMA_CH[channelIndex].RX_CONTROL.U, dbug_num_type_HEX32);
    debugPrintOnce = true;
    Debug_Print_Out("    DMA_STATUS = 0x", 0u, 0, g_IfxGeth.gethSFR->DMA_CH[channelIndex].STATUS.U, dbug_num_type_HEX32);
    debugPrintOnce = true;
    Debug_Print_Out("    RXDESC_LIST = 0x", 0u, 0, g_IfxGeth.gethSFR->DMA_CH[channelIndex].RXDESC_LIST_ADDRESS.U, dbug_num_type_HEX32);
    debugPrintOnce = true;
    Debug_Print_Out("    RXDESC_TAIL = 0x", 0u, 0, g_IfxGeth.gethSFR->DMA_CH[channelIndex].RXDESC_TAIL_POINTER.U, dbug_num_type_HEX32);
    debugPrintOnce = true;
    Debug_Print_Out("    CUR_APP_RXDESC = 0x", 0u, 0, g_IfxGeth.gethSFR->DMA_CH[channelIndex].CURRENT_APP_RXDESC.U, dbug_num_type_HEX32);
    debugPrintOnce = true;
    Debug_Print_Out("    CUR_APP_RXBUF = 0x", 0u, 0, g_IfxGeth.gethSFR->DMA_CH[channelIndex].CURRENT_APP_RXBUFFER.U, dbug_num_type_HEX32);
    debugPrintOnce = true;
    Debug_Print_Out("    MISS_FRAME_CNT = 0x", 0u, 0, g_IfxGeth.gethSFR->DMA_CH[channelIndex].MISS_FRAME_CNT.U, dbug_num_type_HEX32);
  }
}


static void netdev_debug_dump_rx_packet_counters(const char *label)
{
  debugPrintOnce = true;
  Debug_Print_Out(label, 0u, 0, 0u, dbug_num_type_str);
  debugPrintOnce = true;
  Debug_Print_Out("  RX_PACKETS_COUNT_GOOD_BAD = 0x", 0u, 0, g_IfxGeth.gethSFR->RX_PACKETS_COUNT_GOOD_BAD.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  RX_OCTET_COUNT_GOOD_BAD = 0x", 0u, 0, g_IfxGeth.gethSFR->RX_OCTET_COUNT_GOOD_BAD.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  RX_UNICAST_PACKETS_GOOD = 0x", 0u, 0, g_IfxGeth.gethSFR->RX_UNICAST_PACKETS_GOOD.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  RX_BROADCAST_PACKETS_GOOD = 0x", 0u, 0, g_IfxGeth.gethSFR->RX_BROADCAST_PACKETS_GOOD.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  RX_MULTICAST_PACKETS_GOOD = 0x", 0u, 0, g_IfxGeth.gethSFR->RX_MULTICAST_PACKETS_GOOD.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  RX_CRC_ERROR_PACKETS = 0x", 0u, 0, g_IfxGeth.gethSFR->RX_CRC_ERROR_PACKETS.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  RX_ALIGNMENT_ERROR_PACKETS = 0x", 0u, 0, g_IfxGeth.gethSFR->RX_ALIGNMENT_ERROR_PACKETS.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  RX_LENGTH_ERROR_PACKETS = 0x", 0u, 0, g_IfxGeth.gethSFR->RX_LENGTH_ERROR_PACKETS.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  RX_RECEIVE_ERROR_PACKETS = 0x", 0u, 0, g_IfxGeth.gethSFR->RX_RECEIVE_ERROR_PACKETS.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  RX_FIFO_OVERFLOW_PACKETS = 0x", 0u, 0, g_IfxGeth.gethSFR->RX_FIFO_OVERFLOW_PACKETS.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  RX_CONTROL_PACKETS_GOOD = 0x", 0u, 0, g_IfxGeth.gethSFR->RX_CONTROL_PACKETS_GOOD.U, dbug_num_type_HEX32);
}


static void netdev_debug_dump_rx_current_buffer_head(const char *label)
{
  uint32 currentAppBuf;

  currentAppBuf = g_IfxGeth.gethSFR->DMA_CH[0].CURRENT_APP_RXBUFFER.U;

  debugPrintOnce = true;
  Debug_Print_Out(label, 0u, 0, 0u, dbug_num_type_str);
  debugPrintOnce = true;
  Debug_Print_Out("  RX_CUR_APP_BUF = 0x", 0u, 0, currentAppBuf, dbug_num_type_HEX32);

  netdev_debug_dump_rx_buffer_window("  RX current buffer window", currentAppBuf);
}


static void netdev_debug_dump_rx_buffer_window(const char *label, uint32 bufferAddr)
{
  uint32 rxBufBase;
  uint32 rxBufLimit;
  uint32 dumpLen;

  rxBufBase = NETDEV_GETH_RXBUF_BASE_ADDR;
  rxBufLimit = NETDEV_GETH_RXBUF_BASE_ADDR + NETDEV_GETH_RXBUF_TOTAL_SIZE;
  dumpLen = 32u;

  debugPrintOnce = true;
  Debug_Print_Out(label, 0u, 0, 0u, dbug_num_type_str);
  debugPrintOnce = true;
  Debug_Print_Out("    addr = 0x", 0u, 0, bufferAddr, dbug_num_type_HEX32);

  if ((bufferAddr >= rxBufBase) && ((bufferAddr + dumpLen) <= rxBufLimit))
  {
    __dsync();
    debugPrintOnce = true;
    Debug_Print_Data_Array("    buf[0:31] = ", (const uint8 *)bufferAddr, dumpLen);
  }
  else
  {
    debugPrintOnce = true;
    Debug_Print_Out("    buffer out of range", 0u, 0, 0u, dbug_num_type_str);
  }
}


static void netdev_debug_dump_rx_buffer_ring_heads(const char *label, uint32 count)
{
  uint32 i;
  uint32 maxCount;
  uint32 bufferAddr;

  maxCount = (count > (uint32)IFXGETH_MAX_RX_DESCRIPTORS) ? (uint32)IFXGETH_MAX_RX_DESCRIPTORS : count;

  debugPrintOnce = true;
  Debug_Print_Out(label, 0u, 0, 0u, dbug_num_type_str);

  for (i = 0u; i < maxCount; i++)
  {
    bufferAddr = NETDEV_GETH_RXBUF_BASE_ADDR + ((uint32)IFXGETH_MAX_RX_BUFFER_SIZE * i);
    netdev_debug_dump_rx_buffer_window("  RX ring buffer head", bufferAddr);
  }
}


static void netdev_force_rx_queue_path_config(uint8 verbose)
{
  __dsync();

  /* RX acceptance mode 1: strict station-MAC path.
   *   - queue0 -> DMA0 fixed mapping only (no DA-based DMA channel selection)
   *   - disable promiscuous / receive-all / filter-fail queues
   *   - keep broadcast reception enabled so ARP requests for our IP still pass
   *
   * RX acceptance mode 0: permissive debug path.
   *   - current broad accept-all/filter-fail steering used to observe background traffic
   */
  g_IfxGeth.gethSFR->MAC_RXQ_CTRL0.U = 0u;
  g_IfxGeth.gethSFR->MAC_RXQ_CTRL1.U = 0u;
  g_IfxGeth.gethSFR->MAC_RXQ_CTRL2.U = 0u;
  g_IfxGeth.gethSFR->MAC_RXQ_CTRL4.U = 0u;
  g_IfxGeth.gethSFR->MTL_RXQ_DMA_MAP0.U = 0u;

  g_IfxGeth.gethSFR->MAC_RXQ_CTRL0.B.RXQ0EN = 2u;
  g_IfxGeth.gethSFR->MTL_RXQ_DMA_MAP0.B.Q0MDMACH = 0u;

  g_IfxGeth.gethSFR->MTL_RXQ0.OPERATION_MODE.B.RSF = 1u;
  g_IfxGeth.gethSFR->MTL_RXQ0.OPERATION_MODE.B.FEP = 1u;
  g_IfxGeth.gethSFR->MTL_RXQ0.OPERATION_MODE.B.FUP = 1u;
  g_IfxGeth.gethSFR->MTL_RXQ0.OPERATION_MODE.B.DIS_TCP_EF = 1u;
  g_IfxGeth.gethSFR->MTL_RXQ0.OPERATION_MODE.B.RQS = (uint32)IfxGeth_QueueSize_2560Bytes;

  IfxGeth_mac_setMacAddress(g_IfxGeth.gethSFR, (uint8 *)&macAddress.addr[0]);

  if (g_netdevRxAcceptanceMode != 0u)
  {
    g_netdevRxUseDaBasedRouting = 0u;

    /* Keep the stricter station-MAC acceptance path, but remove DA-based DMA
     * channel selection entirely. We want the cleanest possible fixed mapping:
     * queue0 -> DMA0 with ordinary unicast/broadcast steering only.
     */
    g_IfxGeth.gethSFR->MTL_RXQ_DMA_MAP0.B.Q0DDMACH = 0u;
    IfxGeth_mac_setPromiscuousMode(g_IfxGeth.gethSFR, FALSE);
    IfxGeth_mac_setAllMulticastPassing(g_IfxGeth.gethSFR, FALSE);
    g_IfxGeth.gethSFR->MAC_PACKET_FILTER.B.RA = 0u;
    g_IfxGeth.gethSFR->MAC_PACKET_FILTER.B.DBF = 0u;

    /* Do not rely on power-on defaults here. Explicitly steer ordinary unicast
     * and multicast/broadcast traffic to queue0 while keeping the DMA mapping
     * fixed at queue0 -> DMA0. This removes ambiguity when we compare strict
     * vs permissive acceptance in the field logs.
     */
    g_IfxGeth.gethSFR->MAC_RXQ_CTRL1.B.AVCPQ = 0u;
    g_IfxGeth.gethSFR->MAC_RXQ_CTRL1.B.PTPQ = 0u;
    g_IfxGeth.gethSFR->MAC_RXQ_CTRL1.B.UPQ = 0u;
    g_IfxGeth.gethSFR->MAC_RXQ_CTRL1.B.MCBCQ = 0u;
    g_IfxGeth.gethSFR->MAC_RXQ_CTRL1.B.MCBCQEN = 1u;
    g_IfxGeth.gethSFR->MAC_RXQ_CTRL1.B.TACPQE = 0u;
    g_IfxGeth.gethSFR->MAC_RXQ_CTRL1.B.TPQC = 0u;

    g_IfxGeth.gethSFR->MAC_RXQ_CTRL4.B.UFFQE = 0u;
    g_IfxGeth.gethSFR->MAC_RXQ_CTRL4.B.MFFQE = 0u;
    g_IfxGeth.gethSFR->MAC_RXQ_CTRL4.B.VFFQE = 0u;
  }
  else
  {
    g_netdevRxUseDaBasedRouting = 0u;

    g_IfxGeth.gethSFR->MAC_RXQ_CTRL1.B.AVCPQ = 0u;
    g_IfxGeth.gethSFR->MAC_RXQ_CTRL1.B.PTPQ = 0u;
    g_IfxGeth.gethSFR->MAC_RXQ_CTRL1.B.UPQ = 0u;
    g_IfxGeth.gethSFR->MAC_RXQ_CTRL1.B.MCBCQ = 0u;
    g_IfxGeth.gethSFR->MAC_RXQ_CTRL1.B.MCBCQEN = 1u;
    g_IfxGeth.gethSFR->MAC_RXQ_CTRL1.B.TACPQE = 0u;
    g_IfxGeth.gethSFR->MAC_RXQ_CTRL1.B.TPQC = 0u;

    g_IfxGeth.gethSFR->MAC_RXQ_CTRL4.B.UFFQE = 1u;
    g_IfxGeth.gethSFR->MAC_RXQ_CTRL4.B.UFFQ = 0u;
    g_IfxGeth.gethSFR->MAC_RXQ_CTRL4.B.MFFQE = 1u;
    g_IfxGeth.gethSFR->MAC_RXQ_CTRL4.B.MFFQ = 0u;
    g_IfxGeth.gethSFR->MAC_RXQ_CTRL4.B.VFFQE = 1u;
    g_IfxGeth.gethSFR->MAC_RXQ_CTRL4.B.VFFQ = 0u;

    IfxGeth_mac_setPromiscuousMode(g_IfxGeth.gethSFR, TRUE);
    IfxGeth_mac_setAllMulticastPassing(g_IfxGeth.gethSFR, TRUE);
    g_IfxGeth.gethSFR->MAC_PACKET_FILTER.B.RA = 1u;
    g_IfxGeth.gethSFR->MAC_PACKET_FILTER.B.DBF = 0u;
  }

  __dsync();

  if (verbose != 0u)
  {
    netdev_debug_dump_rx_queue_path("RX queue path config");
  }
}


static void netdev_debug_print_ipv4_bytes(const char *label, const uint8 *ipBytes)
{
  debugPrintOnce = true;
  Debug_Print_Out(label, 0u, 0, 0u, dbug_num_type_str);
  Debug_Print_Out("  ip[0] = ", (uint32_t)ipBytes[0], 0, 0u, dbug_num_type_U32);
  Debug_Print_Out("  ip[1] = ", (uint32_t)ipBytes[1], 0, 0u, dbug_num_type_U32);
  Debug_Print_Out("  ip[2] = ", (uint32_t)ipBytes[2], 0, 0u, dbug_num_type_U32);
  Debug_Print_Out("  ip[3] = ", (uint32_t)ipBytes[3], 0, 0u, dbug_num_type_U32);
}

static void netdev_debug_print_eth_tx_summary(void)
{
  uint16 ethType;

  if (uip_len < 14u)
  {
    return;
  }

  ethType = ((uint16)uip_buf[12] << 8) | (uint16)uip_buf[13];
  debugPrintOnce = true;
  Debug_Print_Out("TX frame summary", 0u, 0, 0u, dbug_num_type_str);
  debugPrintOnce = true;
  Debug_Print_Data_Array("  TX dst mac = ", &uip_buf[0], 6u);
  debugPrintOnce = true;
  Debug_Print_Data_Array("  TX src mac = ", &uip_buf[6], 6u);
  debugPrintOnce = true;
  Debug_Print_Out("  TX ethType = 0x", 0u, 0, (uint32_t)ethType, dbug_num_type_HEX32);

  if ((ethType == UIP_ETHTYPE_ARP) && (uip_len >= 42u))
  {
    uint16 opcode;
    opcode = ((uint16)uip_buf[20] << 8) | (uint16)uip_buf[21];
    debugPrintOnce = true;
    Debug_Print_Out("  TX ARP", 0u, 0, 0u, dbug_num_type_str);
    debugPrintOnce = true;
    Debug_Print_Out("  TX ARP opcode = ", (uint32_t)opcode, 0, 0u, dbug_num_type_U32);
    debugPrintOnce = true;
    Debug_Print_Data_Array("  TX ARP sha = ", &uip_buf[22], 6u);
    netdev_debug_print_ipv4_bytes("  TX ARP sip", &uip_buf[28]);
    debugPrintOnce = true;
    Debug_Print_Data_Array("  TX ARP tha = ", &uip_buf[32], 6u);
    netdev_debug_print_ipv4_bytes("  TX ARP tip", &uip_buf[38]);
  }
  else if ((ethType == UIP_ETHTYPE_IP) && (uip_len >= 34u))
  {
    debugPrintOnce = true;
    Debug_Print_Out("  TX IPv4", 0u, 0, 0u, dbug_num_type_str);
    netdev_debug_print_ipv4_bytes("  TX IP src", &uip_buf[26]);
    netdev_debug_print_ipv4_bytes("  TX IP dst", &uip_buf[30]);
    if (uip_len >= 24u)
    {
      debugPrintOnce = true;
      Debug_Print_Out("  TX IP proto = ", (uint32_t)uip_buf[23], 0, 0u, dbug_num_type_U32);
    }
  }
}


static void netdev_debug_print_eth_rx_summary(const uint8 *frame, uint16 frameLen)
{
  uint16 ethType;

  if ((frame == NULL_PTR) || (frameLen < 14u))
  {
    debugPrintOnce = true;
    Debug_Print_Out("RX frame summary: short/NULL", 0u, 0, 0u, dbug_num_type_str);
    return;
  }

  ethType = ((uint16)frame[12] << 8) | (uint16)frame[13];
  debugPrintOnce = true;
  Debug_Print_Out("RX frame summary", 0u, 0, 0u, dbug_num_type_str);
  debugPrintOnce = true;
  Debug_Print_Data_Array("  RX dst mac = ", &frame[0], 6u);
  debugPrintOnce = true;
  Debug_Print_Data_Array("  RX src mac = ", &frame[6], 6u);
  debugPrintOnce = true;
  Debug_Print_Out("  RX ethType = 0x", 0u, 0, (uint32_t)ethType, dbug_num_type_HEX32);

  if ((ethType == UIP_ETHTYPE_ARP) && (frameLen >= 42u))
  {
    uint16 opcode;
    opcode = ((uint16)frame[20] << 8) | (uint16)frame[21];
    debugPrintOnce = true;
    Debug_Print_Out("  RX ARP", 0u, 0, 0u, dbug_num_type_str);
    debugPrintOnce = true;
    Debug_Print_Out("  RX ARP opcode = ", (uint32_t)opcode, 0, 0u, dbug_num_type_U32);
    debugPrintOnce = true;
    Debug_Print_Data_Array("  RX ARP sha = ", &frame[22], 6u);
    netdev_debug_print_ipv4_bytes("  RX ARP sip", &frame[28]);
    debugPrintOnce = true;
    Debug_Print_Data_Array("  RX ARP tha = ", &frame[32], 6u);
    netdev_debug_print_ipv4_bytes("  RX ARP tip", &frame[38]);
  }
  else if ((ethType == UIP_ETHTYPE_IP) && (frameLen >= 34u))
  {
    debugPrintOnce = true;
    Debug_Print_Out("  RX IPv4", 0u, 0, 0u, dbug_num_type_str);
    netdev_debug_print_ipv4_bytes("  RX IP src", &frame[26]);
    netdev_debug_print_ipv4_bytes("  RX IP dst", &frame[30]);
    if (frameLen >= 24u)
    {
      debugPrintOnce = true;
      Debug_Print_Out("  RX IP proto = ", (uint32_t)frame[23], 0, 0u, dbug_num_type_U32);
    }
  }
}

static uint32 netdev_debug_get_rx_descriptor_index(volatile IfxGeth_RxDescr *rxDescriptor)
{
  uint32 addr;
  uint32 baseAddr;
  uint32 totalSize;

  if (rxDescriptor == NULL_PTR)
  {
    return 0xFFFFFFFFu;
  }

  addr = (uint32)rxDescriptor;
  baseAddr = NETDEV_GETH_RXDESC_BASE_ADDR;
  totalSize = (uint32)sizeof(IfxGeth_RxDescr) * (uint32)IFXGETH_MAX_RX_DESCRIPTORS;

  if ((addr < baseAddr) || (addr >= (baseAddr + totalSize)))
  {
    return 0xFFFFFFFEu;
  }

  return (addr - baseAddr) / (uint32)sizeof(IfxGeth_RxDescr);
}

static void netdev_debug_dump_rx_descriptor(const char *label, volatile IfxGeth_RxDescr *rxDescriptor)
{
  uint32 descriptorIndex;

  descriptorIndex = netdev_debug_get_rx_descriptor_index(rxDescriptor);

  debugPrintOnce = true;
  Debug_Print_Out(label, 0u, 0, 0u, dbug_num_type_str);

  if (rxDescriptor == NULL_PTR)
  {
    debugPrintOnce = true;
    Debug_Print_Out("  RX descriptor = NULL", 0u, 0, 0u, dbug_num_type_str);
    return;
  }

  debugPrintOnce = true;
  Debug_Print_Out("  RX descriptor addr = 0x", 0u, 0, (uint32)rxDescriptor, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  RX descriptor idx = ", descriptorIndex, 0, 0u, dbug_num_type_U32);
  debugPrintOnce = true;
  Debug_Print_Out("  RDES0 = 0x", 0u, 0, rxDescriptor->RDES0.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  RDES1 = 0x", 0u, 0, rxDescriptor->RDES1.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  RDES2 = 0x", 0u, 0, rxDescriptor->RDES2.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  RDES3 = 0x", 0u, 0, rxDescriptor->RDES3.U, dbug_num_type_HEX32);
}

static void netdev_debug_dump_rx_ring_descriptors(const char *label)
{
  uint32 i;
  volatile IfxGeth_RxDescr *descr;

  descr = (volatile IfxGeth_RxDescr *)NETDEV_GETH_RXDESC_BASE_ADDR;

  debugPrintOnce = true;
  Debug_Print_Out(label, 0u, 0, 0u, dbug_num_type_str);

  for (i = 0u; i < (uint32)IFXGETH_MAX_RX_DESCRIPTORS; i++)
  {
    netdev_debug_dump_rx_descriptor("  RX ring descriptor", &descr[i]);
  }
}


static void netdev_debug_dump_rx_visibility(const char *label)
{
  volatile IfxGeth_RxDescr *swDescriptor;
  volatile IfxGeth_RxDescr *hwDescriptor;
  uint32 hwDescriptorAddr;
  uint32 nextSwDescriptorAddr;
  uint32 rxDescBase;
  uint32 rxDescLimit;

  swDescriptor = IfxGeth_Eth_getActualRxDescriptor(&g_IfxGeth, IfxGeth_RxDmaChannel_0);
  hwDescriptorAddr = g_IfxGeth.gethSFR->DMA_CH[0].CURRENT_APP_RXDESC.U;
  hwDescriptor = (volatile IfxGeth_RxDescr *)hwDescriptorAddr;
  rxDescBase = NETDEV_GETH_RXDESC_BASE_ADDR;
  rxDescLimit = NETDEV_GETH_RXDESC_BASE_ADDR + ((uint32)sizeof(IfxGeth_RxDescr) * (uint32)IFXGETH_MAX_RX_DESCRIPTORS);

  debugPrintOnce = true;
  Debug_Print_Out(label, 0u, 0, 0u, dbug_num_type_str);
  debugPrintOnce = true;
  Debug_Print_Out("RX_CUR_APP_DESC = 0x", 0u, 0, hwDescriptorAddr, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("RX_CUR_APP_BUF = 0x", 0u, 0, g_IfxGeth.gethSFR->DMA_CH[0].CURRENT_APP_RXBUFFER.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("RX_SW_DESC = 0x", 0u, 0, (uint32)swDescriptor, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("RXDESC_LIST = 0x", 0u, 0, g_IfxGeth.gethSFR->DMA_CH[0].RXDESC_LIST_ADDRESS.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("RXDESC_TAIL = 0x", 0u, 0, g_IfxGeth.gethSFR->DMA_CH[0].RXDESC_TAIL_POINTER.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("RXDESC_RING = 0x", 0u, 0, g_IfxGeth.gethSFR->DMA_CH[0].RXDESC_RING_LENGTH.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("RX_CONTROL = 0x", 0u, 0, g_IfxGeth.gethSFR->DMA_CH[0].RX_CONTROL.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("RX accept mode = ", g_netdevRxAcceptanceMode, 0, 0u, dbug_num_type_U32);
  debugPrintOnce = true;
  Debug_Print_Out("DMA_STAT RI = ", (uint32_t)g_IfxGeth.gethSFR->DMA_CH[0].STATUS.B.RI, 0, 0u, dbug_num_type_U32);
  debugPrintOnce = true;
  Debug_Print_Out("DMA_STAT RBU = ", (uint32_t)g_IfxGeth.gethSFR->DMA_CH[0].STATUS.B.RBU, 0, 0u, dbug_num_type_U32);
  debugPrintOnce = true;
  Debug_Print_Out("DMA_STAT RPS = ", (uint32_t)g_IfxGeth.gethSFR->DMA_CH[0].STATUS.B.RPS, 0, 0u, dbug_num_type_U32);
  debugPrintOnce = true;
  Debug_Print_Out("DMA_STAT RWT = ", (uint32_t)g_IfxGeth.gethSFR->DMA_CH[0].STATUS.B.RWT, 0, 0u, dbug_num_type_U32);
  debugPrintOnce = true;
  Debug_Print_Out("DMA_STAT FBE = ", (uint32_t)g_IfxGeth.gethSFR->DMA_CH[0].STATUS.B.FBE, 0, 0u, dbug_num_type_U32);

  netdev_debug_dump_rx_descriptor("RX visibility SW descriptor", swDescriptor);

  if ((hwDescriptorAddr >= rxDescBase) && (hwDescriptorAddr < rxDescLimit))
  {
    if (hwDescriptor != swDescriptor)
    {
      netdev_debug_dump_rx_descriptor("RX visibility HW current descriptor", hwDescriptor);
    }
  }
  else
  {
    debugPrintOnce = true;
    Debug_Print_Out("RX visibility HW current descriptor out of ring", 0u, 0, 0u, dbug_num_type_str);
  }

  if (swDescriptor != NULL_PTR)
  {
    nextSwDescriptorAddr = (uint32)swDescriptor + (uint32)sizeof(IfxGeth_RxDescr);

    if (nextSwDescriptorAddr >= rxDescLimit)
    {
      nextSwDescriptorAddr = rxDescBase;
    }

    netdev_debug_dump_rx_descriptor("RX visibility SW next descriptor", (volatile IfxGeth_RxDescr *)nextSwDescriptorAddr);
  }
}


static void netdev_debug_dump_tx_descriptor(const char *label, volatile IfxGeth_TxDescr *txDescriptor)
{
  if (txDescriptor == NULL)
  {
    debugPrintOnce = true;
    Debug_Print_Out(label, 0u, 0, 0u, dbug_num_type_str);
    debugPrintOnce = true;
    Debug_Print_Out("  TX descriptor = NULL", 0u, 0, 0u, dbug_num_type_str);
    return;
  }

  debugPrintOnce = true;
  Debug_Print_Out(label, 0u, 0, 0u, dbug_num_type_str);
  debugPrintOnce = true;
  Debug_Print_Out("  TDES0 = 0x", 0u, 0, txDescriptor->TDES0.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  TDES1 = 0x", 0u, 0, txDescriptor->TDES1.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  TDES2 = 0x", 0u, 0, txDescriptor->TDES2.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("  TDES3 = 0x", 0u, 0, txDescriptor->TDES3.U, dbug_num_type_HEX32);
}

static volatile IfxGeth_RxDescr *netdev_find_completed_rx_descriptor(void)
{
  uint32 i;
  volatile IfxGeth_RxDescr *descr;
  uint32 rdes3;

  descr = (volatile IfxGeth_RxDescr *)NETDEV_GETH_RXDESC_BASE_ADDR;
  __dsync();

  for (i = 0u; i < (uint32)IFXGETH_MAX_RX_DESCRIPTORS; i++)
  {
    rdes3 = descr[i].RDES3.U;

    if (((rdes3 & 0x80000000u) == 0u) &&
        ((rdes3 & 0x40000000u) == 0u) &&
        ((rdes3 & 0x10000000u) != 0u))
    {
      return &descr[i];
    }
  }

  return NULL_PTR;
}


static uint8 netdev_rx_dma_needs_recovery(void)
{
  uint32 dmaStatus;

  dmaStatus = g_IfxGeth.gethSFR->DMA_CH[0].STATUS.U;

  if (g_IfxGeth.gethSFR->DMA_CH[0].RX_CONTROL.B.SR == 0u)
  {
    return 1u;
  }

  if (((dmaStatus & 0x00000080u) != 0u) || /* RBU */
      ((dmaStatus & 0x00000100u) != 0u) || /* RPS */
      ((dmaStatus & 0x00000200u) != 0u) || /* RWT */
      ((dmaStatus & 0x00002000u) != 0u))   /* FBE */
  {
    return 1u;
  }

  return 0u;
}


static void netdev_recover_rx_channel_without_rearm(void)
{
  /* Perform a stronger RX kick than a plain wakeup:
   * - stop the RX DMA channel first,
   * - reprogram the ring registers using the currently selected tail mode,
   * - clear sticky RX flags,
   * - start and wake the channel again.
   */
  g_IfxGeth.gethSFR->DMA_CH[0].RX_CONTROL.B.SR = 0u;
  IfxGeth_dma_clearAllInterruptFlags(g_IfxGeth.gethSFR, IfxGeth_DmaChannel_0);
  IfxGeth_mtl_clearAllInterruptFlags(g_IfxGeth.gethSFR, IfxGeth_MtlQueue_0);
  __dsync();

  netdev_force_rx_queue_path_config(0u);
  netdev_program_rx_ring_registers(g_netdevRxTailUseOnePastLast);

  if (IfxGeth_dma_isInterruptFlagSet(g_IfxGeth.gethSFR, IfxGeth_DmaChannel_0, IfxGeth_DmaInterruptFlag_receiveBufferUnavailable) != FALSE)
  {
    IfxGeth_dma_clearInterruptFlag(g_IfxGeth.gethSFR, IfxGeth_DmaChannel_0, IfxGeth_DmaInterruptFlag_receiveBufferUnavailable);
  }

  if (IfxGeth_dma_isInterruptFlagSet(g_IfxGeth.gethSFR, IfxGeth_DmaChannel_0, IfxGeth_DmaInterruptFlag_receiveStopped) != FALSE)
  {
    IfxGeth_dma_clearInterruptFlag(g_IfxGeth.gethSFR, IfxGeth_DmaChannel_0, IfxGeth_DmaInterruptFlag_receiveStopped);
  }

  if (IfxGeth_dma_isInterruptFlagSet(g_IfxGeth.gethSFR, IfxGeth_DmaChannel_0, IfxGeth_DmaInterruptFlag_receiveWatchdogTimeout) != FALSE)
  {
    IfxGeth_dma_clearInterruptFlag(g_IfxGeth.gethSFR, IfxGeth_DmaChannel_0, IfxGeth_DmaInterruptFlag_receiveWatchdogTimeout);
  }

  if (IfxGeth_dma_isInterruptFlagSet(g_IfxGeth.gethSFR, IfxGeth_DmaChannel_0, IfxGeth_DmaInterruptFlag_receiveInterrupt) != FALSE)
  {
    IfxGeth_dma_clearInterruptFlag(g_IfxGeth.gethSFR, IfxGeth_DmaChannel_0, IfxGeth_DmaInterruptFlag_receiveInterrupt);
  }

  __dsync();
  IfxGeth_Eth_startReceivers(&g_IfxGeth, 1u);
  IfxGeth_Eth_wakeupReceiver(&g_IfxGeth, IfxGeth_RxDmaChannel_0);
  __dsync();
}


static uint32 netdev_get_rx_tail_pointer_value(uint8 useOnePastLast)
{
  uint32 rxDescBase;

  rxDescBase = NETDEV_GETH_RXDESC_BASE_ADDR;

  if (useOnePastLast != 0u)
  {
    return rxDescBase + ((uint32)sizeof(IfxGeth_RxDescr) * (uint32)IFXGETH_MAX_RX_DESCRIPTORS);
  }

  return rxDescBase + (((uint32)IFXGETH_MAX_RX_DESCRIPTORS - 1u) * (uint32)sizeof(IfxGeth_RxDescr));
}


static void netdev_program_rx_ring_registers(uint8 useOnePastLast)
{
  uint32 rxDescBase;
  uint32 rxTail;

  rxDescBase = NETDEV_GETH_RXDESC_BASE_ADDR;
  rxTail = netdev_get_rx_tail_pointer_value(useOnePastLast);

  __dsync();
  g_IfxGeth.gethSFR->DMA_CH[0].RXDESC_LIST_ADDRESS.U = rxDescBase;
  g_IfxGeth.gethSFR->DMA_CH[0].RXDESC_TAIL_POINTER.U = rxTail;
  g_IfxGeth.gethSFR->DMA_CH[0].RXDESC_RING_LENGTH.U = (uint32)IFXGETH_MAX_RX_DESCRIPTORS - 1u;
  __dsync();
}


static void netdev_select_rx_tail_mode(uint8 useOnePastLast, uint8 verbose)
{
  g_netdevRxTailUseOnePastLast = (useOnePastLast != 0u) ? 1u : 0u;
  netdev_program_rx_ring_registers(g_netdevRxTailUseOnePastLast);

  if (verbose != 0u)
  {
    debugPrintOnce = true;
    Debug_Print_Out("RX tail mode = ", g_netdevRxTailUseOnePastLast, 0, 0u, dbug_num_type_U32);
    debugPrintOnce = true;
    Debug_Print_Out("RX tail pointer = 0x", 0u, 0,
                    g_IfxGeth.gethSFR->DMA_CH[0].RXDESC_TAIL_POINTER.U,
                    dbug_num_type_HEX32);
  }
}


static void netdev_toggle_rx_tail_mode(const char *reason)
{
  uint8 newTailMode;

  newTailMode = (g_netdevRxTailUseOnePastLast == 0u) ? 1u : 0u;
  Debug_Print_Force_Out("RX toggle tail reason", 0u, 0, 0u, dbug_num_type_str);
  Debug_Print_Force_Out(reason, 0u, 0, 0u, dbug_num_type_str);
  Debug_Print_Force_Out("RX toggle tail new mode = ", newTailMode, 0, 0u, dbug_num_type_U32);
  netdev_select_rx_tail_mode(newTailMode, 1u);
}


static void netdev_toggle_rx_acceptance_mode(const char *reason)
{
  g_netdevRxAcceptanceMode ^= 1u;

  Debug_Print_Force_Out("RX toggle accept reason", 0u, 0, 0u, dbug_num_type_str);
  Debug_Print_Force_Out(reason, 0u, 0, 0u, dbug_num_type_str);
  Debug_Print_Force_Out("RX toggle accept new mode = ", g_netdevRxAcceptanceMode, 0, 0u, dbug_num_type_U32);

  netdev_force_rx_queue_path_config(1u);
}

static void netdev_force_rx_quiet_summary(const char *reason)
{
  volatile IfxGeth_RxDescr *swDescriptor;

  swDescriptor = IfxGeth_Eth_getActualRxDescriptor(&g_IfxGeth, IfxGeth_RxDmaChannel_0);

  Debug_Print_Force_Out("RX quiet summary", 0u, 0, 0u, dbug_num_type_str);
  Debug_Print_Force_Out(reason, 0u, 0, 0u, dbug_num_type_str);
  Debug_Print_Force_Out("RX quiet linkUp = ", (uint32_t)linkUpFlag, 0, 0u, dbug_num_type_U32);
  Debug_Print_Force_Out("RX quiet tail mode = ", g_netdevRxTailUseOnePastLast, 0, 0u, dbug_num_type_U32);
  Debug_Print_Force_Out("RX quiet accept mode = ", g_netdevRxAcceptanceMode, 0, 0u, dbug_num_type_U32);
  Debug_Print_Force_Out("RX quiet DMA_STAT = 0x", 0u, 0, g_IfxGeth.gethSFR->DMA_CH[0].STATUS.U, dbug_num_type_HEX32);
  Debug_Print_Force_Out("RX quiet DMA_NIS = ", (uint32_t)g_IfxGeth.gethSFR->DMA_CH[0].STATUS.B.NIS, 0, 0u, dbug_num_type_U32);
  Debug_Print_Force_Out("RX quiet DMA_AIS = ", (uint32_t)g_IfxGeth.gethSFR->DMA_CH[0].STATUS.B.AIS, 0, 0u, dbug_num_type_U32);
  Debug_Print_Force_Out("RX quiet DMA_CDE = ", (uint32_t)g_IfxGeth.gethSFR->DMA_CH[0].STATUS.B.CDE, 0, 0u, dbug_num_type_U32);
  Debug_Print_Force_Out("RX quiet DMA_ETI = ", (uint32_t)g_IfxGeth.gethSFR->DMA_CH[0].STATUS.B.ETI, 0, 0u, dbug_num_type_U32);
  Debug_Print_Force_Out("RX quiet DMA_ERI = ", (uint32_t)g_IfxGeth.gethSFR->DMA_CH[0].STATUS.B.ERI, 0, 0u, dbug_num_type_U32);
  Debug_Print_Force_Out("RX quiet DMA_FBE = ", (uint32_t)g_IfxGeth.gethSFR->DMA_CH[0].STATUS.B.FBE, 0, 0u, dbug_num_type_U32);
  Debug_Print_Force_Out("RX quiet DMA_REB = ", (uint32_t)g_IfxGeth.gethSFR->DMA_CH[0].STATUS.B.REB, 0, 0u, dbug_num_type_U32);
  Debug_Print_Force_Out("RX quiet RXPKTGB = ", (uint32_t)g_IfxGeth.gethSFR->RX_PACKETS_COUNT_GOOD_BAD.B.RXPKTGB, 0, 0u, dbug_num_type_U32);
  Debug_Print_Force_Out("RX quiet RXUNICAST = ", (uint32_t)g_IfxGeth.gethSFR->RX_UNICAST_PACKETS_GOOD.B.RXUCASTG, 0, 0u, dbug_num_type_U32);
  Debug_Print_Force_Out("RX quiet RXBCAST = ", (uint32_t)g_IfxGeth.gethSFR->RX_BROADCAST_PACKETS_GOOD.B.RXBCASTG, 0, 0u, dbug_num_type_U32);
  Debug_Print_Force_Out("RX quiet RXMCAST = ", (uint32_t)g_IfxGeth.gethSFR->RX_MULTICAST_PACKETS_GOOD.B.RXMCASTG, 0, 0u, dbug_num_type_U32);
  Debug_Print_Force_Out("RX quiet RXCRCERR = ", (uint32_t)g_IfxGeth.gethSFR->RX_CRC_ERROR_PACKETS.B.RXCRCERR, 0, 0u, dbug_num_type_U32);
  Debug_Print_Force_Out("RX quiet RXALIGNERR = ", (uint32_t)g_IfxGeth.gethSFR->RX_ALIGNMENT_ERROR_PACKETS.B.RXALGNERR, 0, 0u, dbug_num_type_U32);
  Debug_Print_Force_Out("RX quiet RXLENERR = ", (uint32_t)g_IfxGeth.gethSFR->RX_LENGTH_ERROR_PACKETS.B.RXLENERR, 0, 0u, dbug_num_type_U32);
  Debug_Print_Force_Out("RX quiet RXRCVERR = ", (uint32_t)g_IfxGeth.gethSFR->RX_RECEIVE_ERROR_PACKETS.B.RXRCVERR, 0, 0u, dbug_num_type_U32);
  Debug_Print_Force_Out("RX quiet CUR_APP_RXDESC = 0x", 0u, 0, g_IfxGeth.gethSFR->DMA_CH[0].CURRENT_APP_RXDESC.U, dbug_num_type_HEX32);
  Debug_Print_Force_Out("RX quiet SW_RXDESC = 0x", 0u, 0, (uint32)swDescriptor, dbug_num_type_HEX32);
}


static void netdev_force_rx_rearm(void)
{
  uint32                      rxDescBase;
  uint32                      rxTail;
  volatile IfxGeth_RxDescr   *descr;
  IfxGeth_Eth_RxChannelConfig rxChannelConfig;

  rxDescBase = NETDEV_GETH_RXDESC_BASE_ADDR;
  descr = (volatile IfxGeth_RxDescr *)NETDEV_GETH_RXDESC_BASE_ADDR;

  memset((void *)NETDEV_GETH_RXDESC_BASE_ADDR, 0, NETDEV_GETH_RXDESC_TOTAL_SIZE);
  memset((void *)NETDEV_GETH_RXBUF_BASE_ADDR, 0, NETDEV_GETH_RXBUF_TOTAL_SIZE);

  rxChannelConfig.channelId             = IfxGeth_RxDmaChannel_0;
  rxChannelConfig.maxBurstLength        = IfxGeth_DmaBurstLength_32;
  rxChannelConfig.rxDescrList           = (IfxGeth_RxDescrList *)NETDEV_GETH_RXDESC_BASE_ADDR;
  rxChannelConfig.rxBuffer1StartAddress = (uint32 *)NETDEV_GETH_RXBUF_BASE_ADDR;
  rxChannelConfig.rxBuffer1Size         = IFXGETH_MAX_RX_BUFFER_SIZE;

  g_IfxGeth.gethSFR->DMA_CH[0].RX_CONTROL.B.SR = 0u;
  __dsync();

  g_IfxGeth.rxChannel[IfxGeth_RxDmaChannel_0].rxDescrList = rxChannelConfig.rxDescrList;
  g_IfxGeth.rxChannel[IfxGeth_RxDmaChannel_0].rxDescrPtr  = descr;
  IfxGeth_Eth_initReceiveDescriptors(&g_IfxGeth, &rxChannelConfig);

  rxTail = netdev_get_rx_tail_pointer_value(g_netdevRxTailUseOnePastLast);
  IfxGeth_dma_setRxDescriptorListAddress(g_IfxGeth.gethSFR, IfxGeth_RxDmaChannel_0, rxDescBase);
  IfxGeth_dma_setRxDescriptorTailPointer(g_IfxGeth.gethSFR, IfxGeth_RxDmaChannel_0, rxTail);
  IfxGeth_dma_setRxDescriptorRingLength(g_IfxGeth.gethSFR, IfxGeth_RxDmaChannel_0, (IFXGETH_MAX_RX_DESCRIPTORS - 1u));
  IfxGeth_mtl_enableRxQueue(g_IfxGeth.gethSFR, IfxGeth_RxMtlQueue_0);
  __dsync();

  netdev_force_rx_queue_path_config(0u);
  IfxGeth_Eth_startReceiver(&g_IfxGeth, IfxGeth_RxDmaChannel_0);
  IfxGeth_Eth_wakeupReceiver(&g_IfxGeth, IfxGeth_RxDmaChannel_0);

  debugPrintOnce = true;
  Debug_Print_Out("RX ring rearmed", 0u, 0, 0u, dbug_num_type_str);
  debugPrintOnce = true;
  Debug_Print_Out("RX rearm init path = iLLD", 0u, 0, 0u, dbug_num_type_str);
  debugPrintOnce = true;
  Debug_Print_Out("RXDESC_LIST = 0x", 0u, 0, g_IfxGeth.gethSFR->DMA_CH[0].RXDESC_LIST_ADDRESS.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("RXDESC_TAIL = 0x", 0u, 0, g_IfxGeth.gethSFR->DMA_CH[0].RXDESC_TAIL_POINTER.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("RX_CONTROL = 0x", 0u, 0, g_IfxGeth.gethSFR->DMA_CH[0].RX_CONTROL.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  netdev_debug_dump_rx_descriptor("RX rearm descriptor[0]", &descr[0]);
  netdev_debug_dump_rx_queue_path("RX rearm queue path");
}


static void netdev_force_tx_rearm(void)
{
  uint32 txDescBase;
  uint32 txTail;
  uint32 txDescLimit;
  volatile IfxGeth_TxDescr *currentDescr;

  txDescBase = NETDEV_GETH_TXDESC_BASE_ADDR;
  txDescLimit = txDescBase + ((uint32)sizeof(IfxGeth_TxDescr) * (uint32)IFXGETH_MAX_TX_DESCRIPTORS);
  currentDescr = IfxGeth_Eth_getActualTxDescriptor(&g_IfxGeth, IfxGeth_TxDmaChannel_0);
  txTail = txDescLimit;

  if (((uint32)currentDescr >= txDescBase) && ((uint32)currentDescr < txDescLimit) &&
      (g_IfxGeth.gethSFR->DMA_CH[0].CURRENT_APP_TXDESC.U != 0u))
  {
    /* Normal steady-state path: allow the DMA to consume up to the descriptor
     * right before the software-owned current descriptor.
     */
    txTail = (uint32)currentDescr;
  }

  __dsync();
  IfxGeth_dma_stopTransmitter(g_IfxGeth.gethSFR, IfxGeth_TxDmaChannel_0);
  g_IfxGeth.gethSFR->DMA_CH[0].STATUS.U = g_IfxGeth.gethSFR->DMA_CH[0].STATUS.U;
  g_IfxGeth.gethSFR->DMA_CH[0].TX_CONTROL.B.OSF = 1u;
  g_IfxGeth.gethSFR->DMA_CH[0].TX_CONTROL.B.TXPBL = 32u;
  g_IfxGeth.gethSFR->MTL_TXQ0.OPERATION_MODE.B.TSF = 1u;
  g_IfxGeth.gethSFR->MTL_TXQ0.OPERATION_MODE.B.TXQEN = 2u;
  IfxGeth_dma_setTxDescriptorListAddress(g_IfxGeth.gethSFR, IfxGeth_TxDmaChannel_0, txDescBase);
  IfxGeth_dma_setTxDescriptorRingLength(g_IfxGeth.gethSFR, IfxGeth_TxDmaChannel_0, (IFXGETH_MAX_TX_DESCRIPTORS - 1u));
  IfxGeth_dma_setTxDescriptorTailPointer(g_IfxGeth.gethSFR, IfxGeth_TxDmaChannel_0, txTail);
  __dsync();

  IfxGeth_Eth_startTransmitter(&g_IfxGeth, IfxGeth_TxDmaChannel_0);
  IfxGeth_Eth_wakeupTransmitter(&g_IfxGeth, IfxGeth_TxDmaChannel_0);

  debugPrintOnce = true;
  Debug_Print_Out("TX ring rearmed", 0u, 0, 0u, dbug_num_type_str);
  debugPrintOnce = true;
  Debug_Print_Out("TXDESC_LIST = 0x", 0u, 0, g_IfxGeth.gethSFR->DMA_CH[0].TXDESC_LIST_ADDRESS.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("TXDESC_TAIL = 0x", 0u, 0, g_IfxGeth.gethSFR->DMA_CH[0].TXDESC_TAIL_POINTER.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("TXDESC_RING = 0x", 0u, 0, g_IfxGeth.gethSFR->DMA_CH[0].TXDESC_RING_LENGTH.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("TX_MTL_OMR = 0x", 0u, 0, g_IfxGeth.gethSFR->MTL_TXQ0.OPERATION_MODE.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("TX_CONTROL = 0x", 0u, 0, g_IfxGeth.gethSFR->DMA_CH[0].TX_CONTROL.U, dbug_num_type_HEX32);
}


static void netdev_prepare_tx_channel(void)
{
  uint32 txDescBase;
  uint32 txDescLimit;
  uint32 currentAppDesc;
  uint8  needFullRearm;

  txDescBase = NETDEV_GETH_TXDESC_BASE_ADDR;
  txDescLimit = NETDEV_GETH_TXDESC_BASE_ADDR + ((uint32)sizeof(IfxGeth_TxDescr) * (uint32)IFXGETH_MAX_TX_DESCRIPTORS);
  currentAppDesc = g_IfxGeth.gethSFR->DMA_CH[0].CURRENT_APP_TXDESC.U;
  needFullRearm = 0u;

  if (g_IfxGeth.gethSFR->DMA_CH[0].TX_CONTROL.B.ST == 0u)
  {
    needFullRearm = 1u;
  }
  else if ((currentAppDesc != 0u) && ((currentAppDesc < txDescBase) || (currentAppDesc >= txDescLimit)))
  {
    needFullRearm = 1u;
  }
  else if (g_IfxGeth.gethSFR->DMA_CH[0].STATUS.B.FBE != 0u)
  {
    needFullRearm = 1u;
  }

  if (needFullRearm != 0u)
  {
    netdev_force_tx_rearm();
  }
  else
  {
    if (IfxGeth_dma_isInterruptFlagSet(g_IfxGeth.gethSFR, IfxGeth_DmaChannel_0,
                                       IfxGeth_DmaInterruptFlag_transmitBufferUnavailable) != FALSE)
    {
      IfxGeth_dma_clearInterruptFlag(g_IfxGeth.gethSFR, IfxGeth_DmaChannel_0,
                                     IfxGeth_DmaInterruptFlag_transmitBufferUnavailable);
    }

    if (IfxGeth_dma_isInterruptFlagSet(g_IfxGeth.gethSFR, IfxGeth_DmaChannel_0,
                                       IfxGeth_DmaInterruptFlag_transmitInterrupt) != FALSE)
    {
      IfxGeth_dma_clearInterruptFlag(g_IfxGeth.gethSFR, IfxGeth_DmaChannel_0,
                                     IfxGeth_DmaInterruptFlag_transmitInterrupt);
    }

    if (IfxGeth_dma_isInterruptFlagSet(g_IfxGeth.gethSFR, IfxGeth_DmaChannel_0,
                                       IfxGeth_DmaInterruptFlag_transmitStopped) != FALSE)
    {
      IfxGeth_dma_clearInterruptFlag(g_IfxGeth.gethSFR, IfxGeth_DmaChannel_0,
                                     IfxGeth_DmaInterruptFlag_transmitStopped);
    }

    if (IfxGeth_mtl_isInterruptFlagSet(g_IfxGeth.gethSFR, IfxGeth_MtlQueue_0,
                                       IfxGeth_MtlInterruptFlag_txQueueUnderflow) != FALSE)
    {
      IfxGeth_mtl_clearInterruptFlag(g_IfxGeth.gethSFR, IfxGeth_MtlQueue_0,
                                     IfxGeth_MtlInterruptFlag_txQueueUnderflow);
    }

    g_IfxGeth.gethSFR->DMA_CH[0].TX_CONTROL.B.OSF = 1u;
    g_IfxGeth.gethSFR->DMA_CH[0].TX_CONTROL.B.TXPBL = 32u;
    g_IfxGeth.gethSFR->MTL_TXQ0.OPERATION_MODE.B.TSF = 1u;
    g_IfxGeth.gethSFR->MTL_TXQ0.OPERATION_MODE.B.TXQEN = 2u;
    __dsync();

    IfxGeth_Eth_startTransmitter(&g_IfxGeth, IfxGeth_TxDmaChannel_0);
    __dsync();

    debugPrintOnce = true;
    Debug_Print_Out("TX prepare: resume path", 0u, 0, 0u, dbug_num_type_str);
    debugPrintOnce = true;
    Debug_Print_Out("TX prepare current desc = 0x", 0u, 0, currentAppDesc, dbug_num_type_HEX32);
    debugPrintOnce = true;
    Debug_Print_Out("TX prepare DMA_STAT = 0x", 0u, 0, g_IfxGeth.gethSFR->DMA_CH[0].STATUS.U, dbug_num_type_HEX32);
  }
}


static void netdev_debug_dump_mac_dma_counters(void)
{
  debugPrintOnce = true;
  Debug_Print_Out("GPCTL = 0x", 0u, 0, g_IfxGeth.gethSFR->GPCTL.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("MAC_CONFIG = 0x", 0u, 0, g_IfxGeth.gethSFR->MAC_CONFIGURATION.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("DMA_MODE = 0x", 0u, 0, g_IfxGeth.gethSFR->DMA_MODE.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("RXDESC_LIST = 0x", 0u, 0, g_IfxGeth.gethSFR->DMA_CH[0].RXDESC_LIST_ADDRESS.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("RXDESC_TAIL = 0x", 0u, 0, g_IfxGeth.gethSFR->DMA_CH[0].RXDESC_TAIL_POINTER.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("RX_CONTROL = 0x", 0u, 0, g_IfxGeth.gethSFR->DMA_CH[0].RX_CONTROL.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("RXDESC_RING = 0x", 0u, 0, g_IfxGeth.gethSFR->DMA_CH[0].RXDESC_RING_LENGTH.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("RX_CUR_APP_DESC = 0x", 0u, 0, g_IfxGeth.gethSFR->DMA_CH[0].CURRENT_APP_RXDESC.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("RX_CUR_APP_BUF = 0x", 0u, 0, g_IfxGeth.gethSFR->DMA_CH[0].CURRENT_APP_RXBUFFER.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("TXDESC_LIST = 0x", 0u, 0, g_IfxGeth.gethSFR->DMA_CH[0].TXDESC_LIST_ADDRESS.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("TXDESC_TAIL = 0x", 0u, 0, g_IfxGeth.gethSFR->DMA_CH[0].TXDESC_TAIL_POINTER.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("TXDESC_RING = 0x", 0u, 0, g_IfxGeth.gethSFR->DMA_CH[0].TXDESC_RING_LENGTH.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("TX_CUR_APP_DESC = 0x", 0u, 0, g_IfxGeth.gethSFR->DMA_CH[0].CURRENT_APP_TXDESC.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("TX_CUR_APP_BUF = 0x", 0u, 0, g_IfxGeth.gethSFR->DMA_CH[0].CURRENT_APP_TXBUFFER.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("TX_MTL_OMR = 0x", 0u, 0, g_IfxGeth.gethSFR->MTL_TXQ0.OPERATION_MODE.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("TX_CONTROL = 0x", 0u, 0, g_IfxGeth.gethSFR->DMA_CH[0].TX_CONTROL.U, dbug_num_type_HEX32);
  debugPrintOnce = true;
  Debug_Print_Out("DMA_STAT TPS = ", (uint32_t)g_IfxGeth.gethSFR->DMA_CH[0].STATUS.B.TPS, 0, 0u, dbug_num_type_U32);
  debugPrintOnce = true;
  Debug_Print_Out("DMA_STAT TBU = ", (uint32_t)g_IfxGeth.gethSFR->DMA_CH[0].STATUS.B.TBU, 0, 0u, dbug_num_type_U32);
  debugPrintOnce = true;
  Debug_Print_Out("DMA_STAT TI = ", (uint32_t)g_IfxGeth.gethSFR->DMA_CH[0].STATUS.B.TI, 0, 0u, dbug_num_type_U32);
  debugPrintOnce = true;
  Debug_Print_Out("DMA_STAT FBE = ", (uint32_t)g_IfxGeth.gethSFR->DMA_CH[0].STATUS.B.FBE, 0, 0u, dbug_num_type_U32);
  debugPrintOnce = true;
  Debug_Print_Out("DMA_STAT RI = ", (uint32_t)g_IfxGeth.gethSFR->DMA_CH[0].STATUS.B.RI, 0, 0u, dbug_num_type_U32);
  debugPrintOnce = true;
  Debug_Print_Out("DMA_STAT RBU = ", (uint32_t)g_IfxGeth.gethSFR->DMA_CH[0].STATUS.B.RBU, 0, 0u, dbug_num_type_U32);
  debugPrintOnce = true;
  Debug_Print_Out("DMA_STAT RPS = ", (uint32_t)g_IfxGeth.gethSFR->DMA_CH[0].STATUS.B.RPS, 0, 0u, dbug_num_type_U32);
  debugPrintOnce = true;
  Debug_Print_Out("DMA_STAT RWT = ", (uint32_t)g_IfxGeth.gethSFR->DMA_CH[0].STATUS.B.RWT, 0, 0u, dbug_num_type_U32);
  debugPrintOnce = true;
  Debug_Print_Out("RX_PKT_GOOD = ", (uint32_t)g_IfxGeth.gethSFR->RX_PACKETS_COUNT_GOOD_BAD.B.RXPKTGB, 0, 0u, dbug_num_type_U32);
  debugPrintOnce = true;
  Debug_Print_Out("RX_OCT_GOOD = ", (uint32_t)g_IfxGeth.gethSFR->RX_OCTET_COUNT_GOOD.B.RXOCTG, 0, 0u, dbug_num_type_U32);
  debugPrintOnce = true;
  Debug_Print_Out("TX_PKT_GOOD = ", (uint32_t)g_IfxGeth.gethSFR->TX_PACKET_COUNT_GOOD.B.TXPKTG, 0, 0u, dbug_num_type_U32);
  debugPrintOnce = true;
  Debug_Print_Out("TX_OCT_GOOD = ", (uint32_t)g_IfxGeth.gethSFR->TX_OCTET_COUNT_GOOD.B.TXOCTG, 0, 0u, dbug_num_type_U32);
}


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
	GethConfig.dma.addressAlignedBeatsEnabled = TRUE;
	GethConfig.dma.fixedBurstEnabled = TRUE;
	GethConfig.dma.mixedBurstEnabled = FALSE;
	/* DMA Tx configuration. */
	GethConfig.dma.txChannel[0].channelId = IfxGeth_TxDmaChannel_0;
	GethConfig.dma.txChannel[0].txDescrList = (IfxGeth_TxDescrList *)NETDEV_GETH_TXDESC_BASE_ADDR;
	GethConfig.dma.txChannel[0].txBuffer1StartAddress = (uint32 *)NETDEV_GETH_TXBUF_BASE_ADDR;
	GethConfig.dma.txChannel[0].txBuffer1Size = IFXGETH_MAX_TX_BUFFER_SIZE;
	GethConfig.dma.txChannel[0].maxBurstLength = IfxGeth_DmaBurstLength_32;
	GethConfig.dma.txChannel[0].enableOSF = TRUE;
	/* DMA Rx configuration. */
	GethConfig.dma.rxChannel[0].channelId = IfxGeth_RxDmaChannel_0;
	GethConfig.dma.rxChannel[0].rxDescrList = (IfxGeth_RxDescrList *)NETDEV_GETH_RXDESC_BASE_ADDR;
	GethConfig.dma.rxChannel[0].rxBuffer1StartAddress = (uint32 *)NETDEV_GETH_RXBUF_BASE_ADDR;
	GethConfig.dma.rxChannel[0].rxBuffer1Size = IFXGETH_MAX_RX_BUFFER_SIZE;
	GethConfig.dma.rxChannel[0].maxBurstLength = IfxGeth_DmaBurstLength_32;
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
  
		/* Clear the dedicated non-cached LMU DMA area before handing it to the GETH driver. */
	memset((void *)NETDEV_GETH_RXDESC_BASE_ADDR, 0, NETDEV_GETH_RXDESC_TOTAL_SIZE);
	memset((void *)NETDEV_GETH_TXDESC_BASE_ADDR, 0, NETDEV_GETH_TXDESC_TOTAL_SIZE);
	memset((void *)NETDEV_GETH_RXBUF_BASE_ADDR, 0, NETDEV_GETH_RXBUF_TOTAL_SIZE);
	memset((void *)NETDEV_GETH_TXBUF_BASE_ADDR, 0, NETDEV_GETH_TXBUF_TOTAL_SIZE);

/* Initialize the module. */
	IfxGeth_Eth_initModule(&g_IfxGeth, &GethConfig);
	netdev_force_rx_queue_path_config(1u);
#if 1//qqqq
	Debug_Print_Force_Out("RXBUF base = 0x", 0u, 0, NETDEV_GETH_RXBUF_BASE_ADDR, dbug_num_type_HEX32);
	Debug_Print_Force_Out("TXBUF base = 0x", 0u, 0, NETDEV_GETH_TXBUF_BASE_ADDR, dbug_num_type_HEX32);
	Debug_Print_Force_Out("RXDESC base = 0x", 0u, 0, NETDEV_GETH_RXDESC_BASE_ADDR, dbug_num_type_HEX32);
	Debug_Print_Force_Out("TXDESC base = 0x", 0u, 0, NETDEV_GETH_TXDESC_BASE_ADDR, dbug_num_type_HEX32);
	Debug_Print_Force_Out("RXBUF symbol = 0x", 0u, 0, NETDEV_GETH_RXBUF_SYMBOL_ADDR, dbug_num_type_HEX32);
	Debug_Print_Force_Out("TXBUF symbol = 0x", 0u, 0, NETDEV_GETH_TXBUF_SYMBOL_ADDR, dbug_num_type_HEX32);
	Debug_Print_Force_Out("RXDESC symbol = 0x", 0u, 0, NETDEV_GETH_RXDESC_SYMBOL_ADDR, dbug_num_type_HEX32);
	Debug_Print_Force_Out("TXDESC symbol = 0x", 0u, 0, NETDEV_GETH_TXDESC_SYMBOL_ADDR, dbug_num_type_HEX32);
#endif
	/* Test patch: relax MAC filtering so broadcast/unicast frames are not dropped
	 * while we are still localizing the no-RX issue.
	 */
	IfxGeth_mac_setPromiscuousMode(g_IfxGeth.gethSFR, TRUE);
	IfxGeth_mac_setAllMulticastPassing(g_IfxGeth.gethSFR, TRUE);
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
	netdev_force_rx_queue_path_config(1u);
	netdev_select_rx_tail_mode(1u, 1u);
	netdev_force_rx_rearm();
	netdev_force_tx_rearm();
#if 1//qqqq
	Debug_Print_Force_Out("MAC_PACKET_FILTER = 0x", 0u, 0, g_IfxGeth.gethSFR->MAC_PACKET_FILTER.U, dbug_num_type_HEX32);
	debugPrintOnce = true;
	netdev_debug_dump_mac_dma_counters();
#endif
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
  uint32                     rxPktGoodBefore;
  uint32                     currentHwRxDesc;
  uint32                     currentSwRxDesc;
  static uint32              lastRxPktGood = 0u;
  static uint32              lastHwRxDesc = 0u;
  static uint32              lastSwRxDesc = 0u;
  static uint32              lastRxProgressReportPktGood = 0u;
  static uint8               rxMacOnlyDetailedPrinted = 0u;
  static uint32              lastRxStallLoggedDmaStat = 0xFFFFFFFFu;
  static uint8               lastRxStallLoggedTailMode = 0xFFu;

  /* Note this this function is called continuously to poll for new data. A good place
   * to also refresh the link status.
   */
  netdev_link_refresh();

  /* Only continue if the link is actually up. */
  if (linkUpFlag != 0)
  {
    __dsync();
    rxPktGoodBefore = (uint32)g_IfxGeth.gethSFR->RX_PACKETS_COUNT_GOOD_BAD.B.RXPKTGB;
    currentHwRxDesc = g_IfxGeth.gethSFR->DMA_CH[0].CURRENT_APP_RXDESC.U;
    currentSwRxDesc = (uint32)IfxGeth_Eth_getActualRxDescriptor(&g_IfxGeth, IfxGeth_RxDmaChannel_0);

    if ((rxPktGoodBefore != lastRxPktGood) ||
        (currentHwRxDesc != lastHwRxDesc) ||
        (currentSwRxDesc != lastSwRxDesc))
    {
      uint8 descriptorStateChanged;
      uint8 packetProgressThresholdReached;

      descriptorStateChanged = ((currentHwRxDesc != lastHwRxDesc) ||
                                (currentSwRxDesc != lastSwRxDesc)) ? 1u : 0u;
      packetProgressThresholdReached =
        ((rxPktGoodBefore >= lastRxProgressReportPktGood) &&
         ((rxPktGoodBefore - lastRxProgressReportPktGood) >= NETDEV_RX_PROGRESS_LOG_PKT_STEP)) ? 1u : 0u;

      if ((descriptorStateChanged != 0u) || (packetProgressThresholdReached != 0u))
      {
        debugPrintOnce = true;
        Debug_Print_Out("RX progress observed", 0u, 0, 0u, dbug_num_type_str);
        debugPrintOnce = true;
        Debug_Print_Out("RX_PKT_GOOD snapshot = ", rxPktGoodBefore, 0, 0u, dbug_num_type_U32);
        if (descriptorStateChanged != 0u)
        {
          netdev_debug_dump_rx_visibility("RX progress visibility");
        }
        lastRxProgressReportPktGood = rxPktGoodBefore;
      }

      lastRxPktGood = rxPktGoodBefore;
      lastHwRxDesc = currentHwRxDesc;
      lastSwRxDesc = currentSwRxDesc;
    }

    /* The Synopsys EQOS/stmmac receive path commonly scans the whole RX ring
     * instead of trusting a single software cursor. Recover that way first if
     * the current descriptor still looks owned by DMA.
     */
    if (IfxGeth_Eth_isRxDataAvailable(&g_IfxGeth, IfxGeth_RxDmaChannel_0) == FALSE)
    {
      volatile IfxGeth_RxDescr *completedDescr;
      static uint32             rxMacOnlyCounter = 0u;
      static uint32             lastRxPktGoodWithoutDescriptor = 0u;
      completedDescr = netdev_find_completed_rx_descriptor();

      if (completedDescr != NULL_PTR)
      {
        if (completedDescr != IfxGeth_Eth_getActualRxDescriptor(&g_IfxGeth, IfxGeth_RxDmaChannel_0))
        {
          debugPrintOnce = true;
          Debug_Print_Out("RX ring scan recovered descriptor", 0u, 0, 0u, dbug_num_type_str);
          netdev_debug_dump_rx_descriptor("RX recovered descriptor", completedDescr);
          g_IfxGeth.rxChannel[IfxGeth_RxDmaChannel_0].rxDescrPtr = completedDescr;
          __dsync();
        }
      }
      else if (netdev_rx_dma_needs_recovery() != 0u)
      {
        static uint32 rxStallRecoverCounter = 0u;
        uint32        rxStallDmaStat;
        uint8         rxStallTailMode;
        uint8         rxStallLogSummary;
        uint8         rxStallLogDetail;

        rxStallRecoverCounter++;
        rxStallDmaStat = g_IfxGeth.gethSFR->DMA_CH[0].STATUS.U;
        rxStallTailMode = g_netdevRxTailUseOnePastLast;
        rxStallLogSummary = 0u;
        rxStallLogDetail = 0u;

        if ((rxStallRecoverCounter == 1u) ||
            ((rxStallRecoverCounter & NETDEV_RX_STALL_SUMMARY_LOG_MASK) == 1u) ||
            (rxStallDmaStat != lastRxStallLoggedDmaStat) ||
            (rxStallTailMode != lastRxStallLoggedTailMode))
        {
          rxStallLogSummary = 1u;
        }

        if ((rxStallRecoverCounter == 1u) ||
            ((rxStallRecoverCounter & NETDEV_RX_STALL_DETAIL_LOG_MASK) == 1u) ||
            (rxStallDmaStat != lastRxStallLoggedDmaStat) ||
            (rxStallTailMode != lastRxStallLoggedTailMode))
        {
          rxStallLogDetail = 1u;
        }

        if (rxStallLogSummary != 0u)
        {
          debugPrintOnce = true;
          Debug_Print_Out("RX stall recovery kick", rxStallRecoverCounter, 0, 0u, dbug_num_type_U32);
          debugPrintOnce = true;
          Debug_Print_Out("RX stall recovery DMA_STAT = 0x", 0u, 0, rxStallDmaStat, dbug_num_type_HEX32);
          debugPrintOnce = true;
          Debug_Print_Out("RX stall recovery tail mode = ", rxStallTailMode, 0, 0u, dbug_num_type_U32);
        }

        if (rxStallLogDetail != 0u)
        {
          netdev_debug_dump_rx_visibility("RX stall recovery visibility before kick");
        }

        if (rxStallLogSummary != 0u)
        {
          lastRxStallLoggedDmaStat = rxStallDmaStat;
          lastRxStallLoggedTailMode = rxStallTailMode;
        }

        if ((rxStallRecoverCounter & 0x07u) == 0u)
        {
          netdev_toggle_rx_tail_mode("RX stall recovery toggle");
          netdev_force_rx_quiet_summary("RX stall summary before full rearm");
          netdev_force_rx_rearm();
        }
        else
        {
          netdev_recover_rx_channel_without_rearm();
        }
      }
      else if (rxPktGoodBefore != lastRxPktGoodWithoutDescriptor)
      {
        static uint32 rxMacOnlyRecoverCounter = 0u;
        lastRxPktGoodWithoutDescriptor = rxPktGoodBefore;
        rxMacOnlyCounter++;
        rxMacOnlyRecoverCounter++;

        if (rxMacOnlyDetailedPrinted == 0u)
        {
          debugPrintOnce = true;
          Debug_Print_Out("RX MAC traffic observed without descriptor completion", rxMacOnlyCounter, 0, 0u, dbug_num_type_U32);
          debugPrintOnce = true;
          Debug_Print_Out("RX MAC-only DMA_STAT = 0x", 0u, 0, g_IfxGeth.gethSFR->DMA_CH[0].STATUS.U, dbug_num_type_HEX32);
          debugPrintOnce = true;
          Debug_Print_Out("RX MAC-only tail mode = ", g_netdevRxTailUseOnePastLast, 0, 0u, dbug_num_type_U32);
          netdev_debug_dump_rx_visibility("RX MAC-only visibility");
          netdev_debug_dump_rx_queue_path("RX MAC-only queue path");
          netdev_debug_dump_rx_low_level_state("RX MAC-only low-level state");
          netdev_debug_dump_rx_queue_dma_matrix("RX MAC-only queue/dma matrix");
          netdev_debug_dump_rx_packet_counters("RX MAC-only packet counters");
          if (g_IfxGeth.gethSFR->RX_UNICAST_PACKETS_GOOD.B.RXUCASTG == 0u)
          {
            Debug_Print_Force_Out("RX MAC-only hint: directed unicast still zero", 0u, 0, 0u, dbug_num_type_str);
          }
          netdev_debug_dump_rx_current_buffer_head("RX MAC-only current buffer head");
          netdev_debug_dump_rx_buffer_ring_heads("RX MAC-only ring buffer heads", 4u);
          netdev_debug_dump_rx_ring_descriptors("RX MAC-only full ring descriptors");
          if (g_netdevRxTailUseOnePastLast == 0u)
          {
            netdev_toggle_rx_tail_mode("RX MAC-only immediate switch to one-past-last");
          }
          else
          {
            Debug_Print_Force_Out("RX MAC-only immediate mode = ", g_netdevRxTailUseOnePastLast, 0, 0u, dbug_num_type_U32);
          }
          netdev_force_rx_quiet_summary("RX MAC-only immediate full rearm");
          netdev_force_rx_rearm();
          rxMacOnlyDetailedPrinted = 1u;
        }
        if ((rxMacOnlyRecoverCounter & NETDEV_RX_MAC_ONLY_RECOVERY_STEP_MASK) == 0u)
        {
          if ((rxMacOnlyRecoverCounter & 0x0Fu) == 0u)
          {
            netdev_toggle_rx_acceptance_mode("RX MAC-only acceptance toggle");
            netdev_force_rx_quiet_summary("RX MAC-only summary before accept-mode rearm");
            netdev_force_rx_rearm();
          }
          else if ((rxMacOnlyRecoverCounter & 0x07u) == 0u)
          {
            netdev_toggle_rx_tail_mode("RX MAC-only recovery toggle");
            netdev_force_rx_quiet_summary("RX MAC-only summary before full rearm");
            netdev_force_rx_rearm();
          }
          else
          {
            netdev_recover_rx_channel_without_rearm();
          }

          if ((rxMacOnlyRecoverCounter & NETDEV_RX_MAC_ONLY_RECOVERY_LOG_MASK) == 0u)
          {
            debugPrintOnce = true;
            Debug_Print_Out("RX MAC-only recovery kick", rxMacOnlyRecoverCounter, 0, 0u, dbug_num_type_U32);
            rxMacOnlyDetailedPrinted = 0u;
          }
        }
      }
    }

    /* Only continue if new data was received. */
    if (IfxGeth_Eth_isRxDataAvailable(&g_IfxGeth, IfxGeth_RxDmaChannel_0) != FALSE)
    {
      static uint32 rxFrameLogCounter = 0u;
      uint8         rxFrameLogThisPacket = 0u;
      /* Get pointer to its descriptor. */
      rxDescriptor = IfxGeth_Eth_getActualRxDescriptor(&g_IfxGeth, IfxGeth_RxDmaChannel_0);
      /* Only continue with a valid descriptor. */
      if (rxDescriptor != NULL)
      {
        /* Determine the size of the newly received frame. */
        uint32 rdes3 = rxDescriptor->RDES3.U;
        uint32 rdes1 = rxDescriptor->RDES1.U;

        rxFrameLogCounter++;
        if ((rxFrameLogCounter == 1u) ||
            ((rxFrameLogCounter & NETDEV_RX_FRAME_DETAIL_LOG_MASK) == 0u))
        {
          rxFrameLogThisPacket = 1u;
        }

        if (((rdes3 & (1UL << 15)) != 0U) ||
            ((rdes1 & (1UL << 7)) != 0U) ||
            ((rdes3 & (1UL << 28)) == 0U))
        {
          /* Error, this block is invalid. */
          frameLen = 0;
          rxFrameLogThisPacket = 1u;
        }
        else
        {
          /* Subtract CRC. */
          frameLen = (uint16)((rdes3 & 0x7FFF) - 4U);
        }

        if (rxFrameLogThisPacket != 0u)
        {
          debugPrintOnce = true;
          Debug_Print_Out("RX data available", rxFrameLogCounter, 0, 0u, dbug_num_type_U32);
          netdev_debug_dump_rx_visibility("RX data available visibility");
          debugPrintOnce = true;
          Debug_Print_Out("RDES3 = 0x", 0u, 0, rxDescriptor->RDES3.U, dbug_num_type_HEX32);
          debugPrintOnce = true;
          Debug_Print_Out("RDES1 = 0x", 0u, 0, rxDescriptor->RDES1.U, dbug_num_type_HEX32);
        }
        /* Only continue with a valid frame length. */
        if (frameLen > 0)
        {
          /* Obtain pointer to the received data. */
          __dsync();
          rxData = IfxGeth_Eth_getReceiveBuffer(&g_IfxGeth, IfxGeth_RxDmaChannel_0);
          if (rxFrameLogThisPacket != 0u)
          {
            debugPrintOnce = true;
            Debug_Print_Out("RX frameLen = ", (uint32_t)frameLen, 0, 0u, dbug_num_type_U32);
          }
          /* Only continue with a valid data pointer. */
          if (rxData != NULL)
          {
            if (rxFrameLogThisPacket != 0u)
            {
              debugPrintOnce = true;
              Debug_Print_Data_Array("RX raw = ", rxData, (frameLen > 64u) ? 64u : (uint32_t)frameLen);
              netdev_debug_print_eth_rx_summary(rxData, frameLen);
            }
            /* Copy the received data ato the uIP data buffer. */
            memcpy(uip_buf, rxData, frameLen);
            /* Update the result. */
            result = frameLen;
          }
        }
      }
      /* Free the receive buffer, enabling it for the further reception. */
      if (rxFrameLogThisPacket != 0u)
      {
        netdev_debug_dump_rx_visibility("RX before free buffer");
      }
      IfxGeth_Eth_freeReceiveBuffer(&g_IfxGeth, IfxGeth_RxDmaChannel_0);
      __dsync();
      if (rxFrameLogThisPacket != 0u)
      {
        netdev_debug_dump_rx_visibility("RX after free buffer");
      }
    }
  }

  if ((linkUpFlag != 0u) && (result == 0u))//qqqq
  {
    static uint8_t noRxDebugPrinted = 0u;
    static uint32  rxIdleMismatchCounter = 0u;
    uint32 hwRxDescIdle;
    uint32 swRxDescIdle;
    uint32 rxPktGoodIdle;

    __dsync();
    hwRxDescIdle = g_IfxGeth.gethSFR->DMA_CH[0].CURRENT_APP_RXDESC.U;
    swRxDescIdle = (uint32)IfxGeth_Eth_getActualRxDescriptor(&g_IfxGeth, IfxGeth_RxDmaChannel_0);
    rxPktGoodIdle = (uint32)g_IfxGeth.gethSFR->RX_PACKETS_COUNT_GOOD_BAD.B.RXPKTGB;

    if ((rxPktGoodIdle > 0u) && (hwRxDescIdle != swRxDescIdle))
    {
      rxIdleMismatchCounter++;

      if ((rxIdleMismatchCounter & NETDEV_RX_IDLE_MISMATCH_LOG_MASK) == 1u)
      {
        debugPrintOnce = true;
        Debug_Print_Out("RX visibility mismatch while idle", rxIdleMismatchCounter, 0, 0u, dbug_num_type_U32);
        netdev_debug_dump_rx_visibility("RX idle mismatch visibility");
      }
    }

    if (noRxDebugPrinted == 0u)
    {
      noRxDebugPrinted = 1u;
      debugPrintOnce = true;
      Debug_Print_Out("RX idle DMA_STAT = 0x", 0u, 0, g_IfxGeth.gethSFR->DMA_CH[0].STATUS.U, dbug_num_type_HEX32);
      debugPrintOnce = true;
      Debug_Print_Out("MAC_RXQ_CTRL = 0x", 0u, 0, g_IfxGeth.gethSFR->MTL_RXQ_DMA_MAP0.U, dbug_num_type_HEX32);
      debugPrintOnce = true;
      Debug_Print_Out("MAC_PACKET_FILTER = 0x", 0u, 0, g_IfxGeth.gethSFR->MAC_PACKET_FILTER.U, dbug_num_type_HEX32);
      netdev_debug_dump_mac_dma_counters();//qqqq
      netdev_debug_dump_rx_visibility("RX idle visibility snapshot");
      debugPrintOnce = true;
      Debug_Print_Out("RX idle: skip forced rearm", 0u, 0, 0u, dbug_num_type_str);
    }

    {
      static unsigned long nextForcedQuietSummary = 0u;
      unsigned long currentTime;

      currentTime = TimerGet();
      if (currentTime >= nextForcedQuietSummary)
      {
        nextForcedQuietSummary = currentTime + 5000u;
        netdev_force_rx_quiet_summary("RX idle periodic 5s summary");
      }
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
  uint32                     txPktBefore;
  uint32                     txOctBefore;
  uint32                     txPktAfter;
  uint32                     txOctAfter;
  volatile IfxGeth_TxDescr * txDescriptor;
  volatile IfxGeth_TxDescr * txSubmittedDescriptor = NULL_PTR;
  uint8                      txCompleted = 0u;
  static uint32              txAttemptCounter = 0u;

  txAttemptCounter++;
  debugPrintOnce = true;
  Debug_Print_Out("TX attempt = ", txAttemptCounter, 0, 0u, dbug_num_type_U32);
  debugPrintOnce = true;
  Debug_Print_Out("TX uip_len = ", (uint32_t)uip_len, 0, 0u, dbug_num_type_U32);

  if (uip_len > 0u)
  {
    netdev_debug_print_eth_tx_summary();
    debugPrintOnce = true;
    Debug_Print_Data_Array("TX raw = ", uip_buf, (uip_len > 64u) ? 64u : (uint32_t)uip_len);
  }

  /* Only continue if the link is actually up. */
  if (linkUpFlag != 0u)
  {
    netdev_prepare_tx_channel();

    /* Set timeout time to wait for a free transmit buffer to become available. */
    timeout = TimerGet() + NETDEV_TX_BUFF_ALLOC_TIMEOUT_MS;
    /* Wait for a free transmit buffer to become available. */
    while (txData == NULL)
    {
      txData = IfxGeth_Eth_getTransmitBuffer(&g_IfxGeth, IfxGeth_TxDmaChannel_0);
      if (TimerGet() > timeout)
      {
        break;
      }
    }

    txDescriptor = IfxGeth_Eth_getActualTxDescriptor(&g_IfxGeth, IfxGeth_TxDmaChannel_0);
    txSubmittedDescriptor = txDescriptor;
    netdev_debug_dump_tx_descriptor("TX descriptor before submit", txDescriptor);
    debugPrintOnce = true;
    Debug_Print_Out("TXDESC_LIST = 0x", 0u, 0, g_IfxGeth.gethSFR->DMA_CH[0].TXDESC_LIST_ADDRESS.U, dbug_num_type_HEX32);
    debugPrintOnce = true;
    Debug_Print_Out("TXDESC_TAIL = 0x", 0u, 0, g_IfxGeth.gethSFR->DMA_CH[0].TXDESC_TAIL_POINTER.U, dbug_num_type_HEX32);
    debugPrintOnce = true;
    Debug_Print_Out("TX_CONTROL = 0x", 0u, 0, g_IfxGeth.gethSFR->DMA_CH[0].TX_CONTROL.U, dbug_num_type_HEX32);
    debugPrintOnce = true;
    Debug_Print_Out("DMA_STAT = 0x", 0u, 0, g_IfxGeth.gethSFR->DMA_CH[0].STATUS.U, dbug_num_type_HEX32);

    if (txData == NULL)
    {
      debugPrintOnce = true;
      Debug_Print_Out("TX no buffer timeout", 0u, 0, 0u, dbug_num_type_str);
      netdev_debug_dump_mac_dma_counters();
      return;
    }

    debugPrintOnce = true;
    Debug_Print_Out("TX buffer ptr = 0x", 0u, 0, (uint32)txData, dbug_num_type_HEX32);

    /* Copy the packet data to the tx data buffer. */
    memcpy(txData, uip_buf, uip_len);
    __dsync();

    txPktBefore = (uint32)g_IfxGeth.gethSFR->TX_PACKET_COUNT_GOOD.B.TXPKTG;
    txOctBefore = (uint32)g_IfxGeth.gethSFR->TX_OCTET_COUNT_GOOD.B.TXOCTG;
    debugPrintOnce = true;
    Debug_Print_Out("TX_PKT_GOOD before = ", txPktBefore, 0, 0u, dbug_num_type_U32);
    debugPrintOnce = true;
    Debug_Print_Out("TX_OCT_GOOD before = ", txOctBefore, 0, 0u, dbug_num_type_U32);

    IfxGeth_dma_clearInterruptFlag(g_IfxGeth.gethSFR, IfxGeth_DmaChannel_0,
                                   IfxGeth_DmaInterruptFlag_transmitInterrupt);
    IfxGeth_Eth_sendTransmitBuffer(&g_IfxGeth, uip_len, IfxGeth_TxDmaChannel_0);

    txDescriptor = IfxGeth_Eth_getActualTxDescriptor(&g_IfxGeth, IfxGeth_TxDmaChannel_0);
    if (txSubmittedDescriptor != NULL_PTR)
    {
      netdev_debug_dump_tx_descriptor("TX submitted descriptor after submit", txSubmittedDescriptor);
    }
    netdev_debug_dump_tx_descriptor("TX descriptor after submit", txDescriptor);
    debugPrintOnce = true;
    Debug_Print_Out("TXDESC_TAIL after = 0x", 0u, 0, g_IfxGeth.gethSFR->DMA_CH[0].TXDESC_TAIL_POINTER.U, dbug_num_type_HEX32);
    debugPrintOnce = true;
    Debug_Print_Out("TX_CUR_APP_DESC after = 0x", 0u, 0, g_IfxGeth.gethSFR->DMA_CH[0].CURRENT_APP_TXDESC.U, dbug_num_type_HEX32);
    debugPrintOnce = true;
    Debug_Print_Out("TX_CUR_APP_BUF after = 0x", 0u, 0, g_IfxGeth.gethSFR->DMA_CH[0].CURRENT_APP_TXBUFFER.U, dbug_num_type_HEX32);
    debugPrintOnce = true;
    Debug_Print_Out("DMA_STAT after submit = 0x", 0u, 0, g_IfxGeth.gethSFR->DMA_CH[0].STATUS.U, dbug_num_type_HEX32);
    debugPrintOnce = true;
    Debug_Print_Out("TX submit", 0u, 0, 0u, dbug_num_type_str);

    timeout = TimerGet() + NETDEV_TX_PACKET_TIMEOUT_MS;
    while (txCompleted == 0u)
    {
      if (IfxGeth_dma_isInterruptFlagSet(g_IfxGeth.gethSFR, IfxGeth_DmaChannel_0,
                                    IfxGeth_DmaInterruptFlag_transmitInterrupt) != FALSE)
      {
        txCompleted = 1u;
      }
      else if ((txSubmittedDescriptor != NULL_PTR) && (txSubmittedDescriptor->TDES3.R.OWN == 0u))
      {
        txCompleted = 1u;
      }
      else if ((uint32)g_IfxGeth.gethSFR->TX_PACKET_COUNT_GOOD.B.TXPKTG != txPktBefore)
      {
        txCompleted = 1u;
      }
      else if (TimerGet() > timeout)
      {
        break;
      }
    }

    if (txCompleted != 0u)
    {
      debugPrintOnce = true;
      Debug_Print_Out("TX done", 0u, 0, 0u, dbug_num_type_str);
    }
    else
    {
      debugPrintOnce = true;
      Debug_Print_Out("TX wait timeout", 0u, 0, 0u, dbug_num_type_str);
      netdev_force_tx_rearm();
    }

    txDescriptor = IfxGeth_Eth_getActualTxDescriptor(&g_IfxGeth, IfxGeth_TxDmaChannel_0);
    if (txSubmittedDescriptor != NULL_PTR)
    {
      netdev_debug_dump_tx_descriptor("TX submitted descriptor after wait", txSubmittedDescriptor);
    }
    netdev_debug_dump_tx_descriptor("TX descriptor after wait", txDescriptor);

    txPktAfter = (uint32)g_IfxGeth.gethSFR->TX_PACKET_COUNT_GOOD.B.TXPKTG;
    txOctAfter = (uint32)g_IfxGeth.gethSFR->TX_OCTET_COUNT_GOOD.B.TXOCTG;
    debugPrintOnce = true;
    Debug_Print_Out("TX_PKT_GOOD after = ", txPktAfter, 0, 0u, dbug_num_type_U32);
    debugPrintOnce = true;
    Debug_Print_Out("TX_OCT_GOOD after = ", txOctAfter, 0, 0u, dbug_num_type_U32);
    debugPrintOnce = true;
    Debug_Print_Out("DMA_STAT after wait = 0x", 0u, 0, g_IfxGeth.gethSFR->DMA_CH[0].STATUS.U, dbug_num_type_HEX32);
    netdev_debug_dump_mac_dma_counters();
  }
  else
  {
    debugPrintOnce = true;
    Debug_Print_Out("TX skipped: link down", 0u, 0, 0u, dbug_num_type_str);
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
  static uint8                      lastLoggedLinkUpFlag = 0xFFu;

  CtrlStatus.U = 0;
  linkUpFlagPrevious = linkUpFlag;

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
    if (linkUpFlag != lastLoggedLinkUpFlag)
    {
      Debug_Print_Force_Out("linkUpFlag = ", (uint32_t)linkUpFlag, 0, 0u, dbug_num_type_U32);
      lastLoggedLinkUpFlag = linkUpFlag;
    }
    /* Did the link change from down to up? */
    if (linkUpFlag == 1)
    {
      /* Keep MAC speed/duplex synchronized while link is up.
       * This also covers the case where PHY resolves speed after the first link-up edge.
       */
      DuplexMode = IfxGeth_DuplexMode_halfDuplex;
      LineSpeed = IfxGeth_LineSpeed_10Mbps;

      if (CtrlStatus.B.LNKMOD == 1)
      {
        DuplexMode = IfxGeth_DuplexMode_fullDuplex;
      }
      IfxGeth_mac_setDuplexMode(g_IfxGeth.gethSFR, DuplexMode);

      if (CtrlStatus.B.LNKSPEED == 1)
      {
        LineSpeed = IfxGeth_LineSpeed_100Mbps;
      }
      else if (CtrlStatus.B.LNKSPEED > 1)
      {
        LineSpeed = IfxGeth_LineSpeed_1000Mbps;
      }
      IfxGeth_mac_setLineSpeed(g_IfxGeth.gethSFR, LineSpeed);

      if (linkUpFlag != linkUpFlagPrevious)
      {
        /* Re-arm MAC/DMA on the actual link-up edge. This is a safe recovery step
         * in case the RX path stayed idle across auto-negotiation completion.
         */
        IfxGeth_Eth_startTransmitters(&g_IfxGeth, 1);
        IfxGeth_Eth_startReceivers(&g_IfxGeth, 1);
        netdev_force_rx_queue_path_config(1u);
        netdev_select_rx_tail_mode(1u, 1u);
        netdev_force_rx_rearm();
        netdev_force_tx_rearm();
        IfxGeth_Eth_wakeupTransmitter(&g_IfxGeth, IfxGeth_TxDmaChannel_0);
        IfxGeth_Eth_wakeupReceiver(&g_IfxGeth, IfxGeth_RxDmaChannel_0);
#if 1 //qqqq
        debugPrintOnce = true;
        Debug_Print_Out("LNKMOD = ", (uint32_t)CtrlStatus.B.LNKMOD, 0, 0u, dbug_num_type_U32);

        debugPrintOnce = true;
        Debug_Print_Out("LNKSPEED = ", (uint32_t)CtrlStatus.B.LNKSPEED, 0, 0u, dbug_num_type_U32);

        debugPrintOnce = true;
        Debug_Print_Out("MAC_PACKET_FILTER = 0x", 0u, 0, g_IfxGeth.gethSFR->MAC_PACKET_FILTER.U, dbug_num_type_HEX32);
#endif
      }
    }
	else{
		__nop();	
	}
  }
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




