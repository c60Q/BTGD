/*
 * IfxGeth_Phy_88E1512.h
 *
 *  Created on: 2025年3月27日
 *      Author: WendyL_Li
 */

#ifndef LIBRARIES_ETHERNET_PHY_RTL8211F_IFXGETH_PHY_88E1512_H_
#define LIBRARIES_ETHERNET_PHY_RTL8211F_IFXGETH_PHY_88E1512_H_ 1

/******************************************************************************/
/*----------------------------------Includes----------------------------------*/
/******************************************************************************/
#include "Configuration.h"
#include "IfxCpu_cfg.h"
#include "IfxGeth_Eth.h"

/******************************************************************************/
/*-----------------------------------Macros-----------------------------------*/

/******************************************************************************/
// Page 0 - Copper Registers
#define IFXGETH_PHY_88E1512_MDIO_BMCR             0x00  // Copper Control Register
#define IFXGETH_PHY_88E1512_MDIO_BMSR             0x01  // Copper Status Register
#define IFXGETH_PHY_88E1512_MDIO_PHYID1           0x02  // PHY Identifier 1
#define IFXGETH_PHY_88E1512_MDIO_PHYID2           0x03  // PHY Identifier 2
#define IFXGETH_PHY_88E1512_MDIO_ANAR             0x04  // Copper Auto-Negotiation Advertisement
#define IFXGETH_PHY_88E1512_MDIO_ANLPAR           0x05  // Copper Link Partner Ability
#define IFXGETH_PHY_88E1512_MDIO_ANER             0x06  // Copper Auto-Negotiation Expansion
#define IFXGETH_PHY_88E1512_MDIO_ANNPTR           0x07  // Copper Next Page Transmit
#define IFXGETH_PHY_88E1512_MDIO_ANNPRR           0x08  // Copper Link Partner Next Page
#define IFXGETH_PHY_88E1512_MDIO_GBCR             0x09  // 1000BASE-T Control
#define IFXGETH_PHY_88E1512_MDIO_GBSR             0x0A  // 1000BASE-T Status
#define IFXGETH_PHY_88E1512_MDIO_EXTSR            0x0F  // Extended Status Register
#define IFXGETH_PHY_88E1512_MDIO_CSCR1            0x10  // Copper Specific Control Register 1
#define IFXGETH_PHY_88E1512_MDIO_CSSR1            0x11  // Copper Specific Status Register 1
#define IFXGETH_PHY_88E1512_MDIO_CISER            0x12  // Copper Specific Interrupt Enable Register
#define IFXGETH_PHY_88E1512_MDIO_CISR             0x13  // Copper Interrupt Status Register
#define IFXGETH_PHY_88E1512_MDIO_CSCR2            0x14  // Copper Specific Control Register 2
#define IFXGETH_PHY_88E1512_MDIO_RX_ERR_CNT       0x15  // Copper Receive Error Counter
#define IFXGETH_PHY_88E1512_MDIO_PAGSR            0x16  // Page Address Register
#define IFXGETH_PHY_88E1512_MDIO_GINT             0x17  // Global Interrupt Status Register

// Page 1 - Fiber Registers
#define IFXGETH_PHY_88E1512_PAGE1_FCR             0x00  // Fiber Control Register
#define IFXGETH_PHY_88E1512_PAGE1_FSR             0x01  // Fiber Status Register
#define IFXGETH_PHY_88E1512_PAGE1_PHYID1          0x02  // Fiber PHY ID1
#define IFXGETH_PHY_88E1512_PAGE1_PHYID2          0x03  // Fiber PHY ID2
#define IFXGETH_PHY_88E1512_PAGE1_FANAR           0x04  // Fiber Auto-Negotiation Advertisement
#define IFXGETH_PHY_88E1512_PAGE1_FANLPAR         0x05  // Fiber Link Partner Ability
#define IFXGETH_PHY_88E1512_PAGE1_FANER           0x06  // Fiber Auto-Negotiation Expansion
#define IFXGETH_PHY_88E1512_PAGE1_FANNPTR         0x07  // Fiber Next Page Transmit
#define IFXGETH_PHY_88E1512_PAGE1_FANPRR          0x08  // Fiber Link Partner Next Page
#define IFXGETH_PHY_88E1512_PAGE1_EXTSR           0x0F  // Extended Status Register
#define IFXGETH_PHY_88E1512_PAGE1_FCR1            0x10  // Fiber Specific Control Register 1
#define IFXGETH_PHY_88E1512_PAGE1_FSR1            0x11  // Fiber Specific Status Register
#define IFXGETH_PHY_88E1512_PAGE1_FIER            0x12  // Fiber Interrupt Enable Register
#define IFXGETH_PHY_88E1512_PAGE1_FISR            0x13  // Fiber Interrupt Status Register
#define IFXGETH_PHY_88E1512_PAGE1_PRBS_CTRL       0x17  // PRBS Control
#define IFXGETH_PHY_88E1512_PAGE1_PRBS_LSB        0x18  // PRBS Error Counter LSB
#define IFXGETH_PHY_88E1512_PAGE1_PRBS_MSB        0x19  // PRBS Error Counter MSB
#define IFXGETH_PHY_88E1512_PAGE1_FCR2            0x1A  // Fiber Specific Control Register 2

// Page 2 - MAC + RGMII
#define IFXGETH_PHY_88E1512_MACCR1                0x10
#define IFXGETH_PHY_88E1512_MACIR                 0x12
#define IFXGETH_PHY_88E1512_MACSR                 0x13
#define IFXGETH_PHY_88E1512_MACCR2                0x15
#define IFXGETH_PHY_88E1512_MDIO_RGMII_IOC_OVERRIDE 0x18  // RGMII Output Impedance Calibration Override
#define IFXGETH_PHY_88E1512_MAC_Control_Reg_1       0x10  // Phy clk125

//Page 6
#define IFXGETH_PHY_88E1512_Page6_CPPG                  0x10  //Enable packet generator

// Page 3 - LED
#define IFXGETH_PHY_88E1512_MDIO_LED_FCN_CTRL     0x10
#define IFXGETH_PHY_88E1512_MDIO_LED_POL_CTRL     0x11
#define IFXGETH_PHY_88E1512_MDIO_LED_TIMER_CTRL   0x12

// Page 18 - General Control
#define IFXGETH_PHY_88E1512_MDIO_GEN_CTRL1_P18    0x14  // General Control Register 1
#define IFXGETH_PHY_88E1512_MDIO_PACKET_GEN_P18   0x10  //Generate Packets

/******************************************************************************/

/******************************************************************************/
/*--------------------------------Enumerations--------------------------------*/
/******************************************************************************/

/******************************************************************************/
/*-----------------------------Data Structures--------------------------------*/
/******************************************************************************/

/******************************************************************************/
/*------------------------------Global variables------------------------------*/
/******************************************************************************/
IFX_EXTERN uint32 IfxGeth_Eth_Phy_88E1512_iPhyInitDone;

/******************************************************************************/
/*-------------------------Function Prototypes--------------------------------*/
/******************************************************************************/
IFX_EXTERN uint32 IfxGeth_Eth_Phy_88E1512_init(void);
IFX_EXTERN void IfxGeth_Eth_Phy_88E1512_read_mdio_reg(uint32 layeraddr, uint32 regaddr, uint32 *pdata);
IFX_EXTERN void IfxGeth_Eth_Phy_88E1512_write_mdio_reg(uint32 layeraddr, uint32 regaddr, uint32 data);
void IfxGeth_Eth_Phy_88E1512_set_mdio_page(uint32 page);
IFX_EXTERN uint32 IfxGeth_Eth_Phy_88E1512_link_status(void);


#endif /* LIBRARIES_ETHERNET_PHY_RTL8211F_IFXGETH_PHY_88E1512_H_ */
