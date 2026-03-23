/*
 * IfxGeth_Phy_88E1512.c
 *
 *  Created on: 2025年3月27日
 *      Author: WendyL_Li
 */


#include "IfxGeth_Phy_88E1512.h"
#include "boot_transport.h"

/******************************************************************************/
/*----------------------------------Macros------------------------------------*/
/******************************************************************************/
#define PHY_ADDR 0x00
#define PHY_RESET (1 << 15)  // Reset bit in BMCR
#define PHY_LINK_STATUS (1 << 2)

#define IFXGETH_PHY_88E1512_WAIT_MDIO_READY() while (GETH_MAC_MDIO_ADDRESS.B.GB) {}
/******************************************************************************/
/*-----------------------Exported Variables/Constants-------------------------*/
/*****************************************************************************/

uint32 IfxGeth_Eth_Phy_88E1512_iPhyInitDone = 0;

/******************************************************************************/
/*------------------------Private Variables/Constants-------------------------*/
/******************************************************************************/
#if defined(__GNUC__)
    #pragma section // end bss section
#endif

/******************************************************************************/
/*-------------------------Function Prototypes--------------------------------*/
/******************************************************************************/

/******************************************************************************/
/*-------------------------Function Implementations---------------------------*/
/******************************************************************************/


uint32  IfxGeth_Eth_Phy_88E1512_init(void)
{
//        uint8 is100M = 0;
        uint16 regVal;
        uint32 temp;
        uint32 phy_id;

        // === Step pre-condition: 一些前置設定 ===
#if 0
                IfxGeth_Eth_Phy_88E1512_set_mdio_page(2);  // 確保在 Page 2
                IfxGeth_Eth_Phy_88E1512_read_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_MACCR2, &temp);
                regVal = (uint16)(temp & 0xFFFF);
                regVal &= ~(0x0030);  // 清除 bit-4 & 5...RGMII Receive/Transmit Timing Control...why?
                IfxGeth_Eth_Phy_88E1512_write_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_MACCR2, regVal);


                IfxGeth_Eth_Phy_88E1512_read_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_RGMII_IOC_OVERRIDE, &temp);
                regVal = (uint16)(temp & 0xFFFF);
                regVal &= ~(0x2000);  // 清除 bit-13...indicate VDDO level = 3.3V/1.8V
                IfxGeth_Eth_Phy_88E1512_write_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_RGMII_IOC_OVERRIDE, regVal);

                // === Step 2: Reset
                IfxGeth_Eth_Phy_88E1512_set_mdio_page(0);  // 確保在 Page 0
                IfxGeth_Eth_Phy_88E1512_read_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_BMCR, &temp);
                regVal = (uint16)(temp & 0xFFFF);
                regVal |= 0x8000;
                IfxGeth_Eth_Phy_88E1512_write_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_BMCR, regVal);
                // Wait reset finish
                do {
                    IfxGeth_Eth_Phy_88E1512_read_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_BMCR, &temp);
                } while (temp & 0x8000);
#endif

        // === Step 0: 讀取 PHY ID 確認裝置存在 ===
        uint32 phy_id1 = 0, phy_id2 = 0;
        IfxGeth_Eth_Phy_88E1512_set_mdio_page(0);  // 確保在 Page 0
        IfxGeth_Eth_Phy_88E1512_read_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_PHYID1, &phy_id1);
        IfxGeth_Eth_Phy_88E1512_read_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_PHYID2, &phy_id2);
        if ((phy_id1 == 0xFFFF) || (phy_id1 == 0x0000)) {
            //printf("PHY 88E1512 not detected!\n");
            debugPrintOnce = true;//qqqq
			Debug_Print_Out("phy F", 0, 0, 0, dbug_num_type_str);
            return 1;  // 錯誤代碼 1：PHY 未偵測到
        }

        phy_id = ((phy_id1 & 0xFFFF) << 16) | (phy_id2 & 0xFFFF);

        // === Step 1: Page 18: 設定為 RGMII-to-Copper 模式 ===
        IfxGeth_Eth_Phy_88E1512_set_mdio_page(18);
        IfxGeth_Eth_Phy_88E1512_read_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_GEN_CTRL1_P18, &temp);
        regVal = (uint16)(temp & 0xFFFF);
        regVal &= ~0x0007;  // 清除 MODE[2:0]
        regVal |= 0x0000;   // RGMII-to-Copper
        IfxGeth_Eth_Phy_88E1512_write_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_GEN_CTRL1_P18, regVal);

        // === Step 2: Reset(page-18)
        IfxGeth_Eth_Phy_88E1512_read_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_GEN_CTRL1_P18, &temp);
        regVal = (uint16)(temp & 0xFFFF);
        regVal |= (1 << 15);  // Bit15 = Reset
        IfxGeth_Eth_Phy_88E1512_write_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_GEN_CTRL1_P18, regVal);
        // Wait reset finish
        do {
            IfxGeth_Eth_Phy_88E1512_read_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_GEN_CTRL1_P18, &temp);
        } while (temp & 0x8000);

        IfxGeth_Eth_Phy_88E1512_set_mdio_page(2);
        IfxGeth_Eth_Phy_88E1512_read_mdio_reg(PHY_ADDR, 0x10, &temp);
        regVal = (uint16)(temp & 0xFFFF);
//qqqq        regVal |= 0x0006;  // Set bit2 (disable clock gen) and bit1 (disable pin output)
        IfxGeth_Eth_Phy_88E1512_write_mdio_reg(PHY_ADDR, 0x10, regVal);
        IfxGeth_Eth_Phy_88E1512_read_mdio_reg(PHY_ADDR, 0x10, &temp);

#if 0
        // === Step 3: Reset(page-0)
        regVal |= 0x8000;
        IfxGeth_Eth_Phy_88E1512_set_mdio_page(0);
        IfxGeth_Eth_Phy_88E1512_write_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_BMCR, regVal);
        // Wait reset finish
        do {
            IfxGeth_Eth_Phy_88E1512_read_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_BMCR, &temp);
        } while (temp & 0x8000);
#endif

        // === Step 4: Page 2: 設定 RGMII clock delay === ...should be receive clock transition timing and transmit clock delay or not
#if 0
        IfxGeth_Eth_Phy_88E1512_set_mdio_page(2);
        IfxGeth_Eth_Phy_88E1512_read_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_MACCR2, &temp);
        regVal = (uint16)(temp & 0xFFFF);
        // RGMII delay（enable |=）
        regVal &= ~ (1 << 5);  // Disable RX delay...no, this is set Receive clock transition when data transition
        //regVal &= ~ (1 << 4);  // Disable TX delay...indicate transmit clock not internally delay
        IfxGeth_Eth_Phy_88E1512_write_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_MACCR2, regVal);
        IfxGeth_Eth_Phy_88E1512_read_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_MACCR2, &temp);

        // === Step 5: Reset
        regVal |= 0x8000;
        IfxGeth_Eth_Phy_88E1512_set_mdio_page(0);
        IfxGeth_Eth_Phy_88E1512_write_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_BMCR, regVal);
        // Wait reset finish
        do {
        IfxGeth_Eth_Phy_88E1512_read_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_BMCR, &temp);
        } while (temp & 0x8000);
#endif

        // === Step 6: 宣告支援 100M ===...should be auto-nego advertisement setting for 100m & 10m
#if 0
        IfxGeth_Eth_Phy_88E1512_set_mdio_page(0);
        uint16 anar_val = 0;
        anar_val |= (1 << 8); // 100M Full
        anar_val |= (1 << 7); // 100M Half
        IfxGeth_Eth_Phy_88E1512_write_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_ANAR, anar_val);
        IfxGeth_Eth_Phy_88E1512_read_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_ANAR, &temp);

        // === Step 6.5: Reset
        IfxGeth_Eth_Phy_88E1512_read_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_BMCR, &temp);
        regVal = (uint16)(temp & 0xFFFF);
        regVal |= 0x8000;
        IfxGeth_Eth_Phy_88E1512_write_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_BMCR, regVal);
        // Wait reset finish
        do {
            IfxGeth_Eth_Phy_88E1512_read_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_BMCR, &temp);
        } while (temp & 0x8000);
#endif

        // === Step 7: 啟動 Auto-Negotiation，選擇速度與雙工 ===...should no need set, default is what we need(auto-nego, full duplex, 1000m)
#if 0
        IfxGeth_Eth_Phy_88E1512_set_mdio_page(0);
        IfxGeth_Eth_Phy_88E1512_read_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_BMCR, &temp);
        regVal = (uint16)(temp & 0xFFFF);
        regVal |= (1 << 12);  // Enable Auto-Negotiation
        //regVal |= (1 << 9);   // Restart Auto-Negotiation
        regVal |= (1 << 8);   // Full Duplex

        if (is100M)
        {
            //bit(6,13) = (0,1)
            regVal |= (1 << 13); // 100M
            regVal &= 0xFFBF;  //let bit-6 to be 0
        }
        else
        {
            //bit(6,13) = (1,0)
            regVal |= (1 << 6);  // 1000M
            regVal &= 0xDFFF;  //let bit-13 to be 0
        }

        IfxGeth_Eth_Phy_88E1512_write_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_BMCR, regVal);
        IfxGeth_Eth_Phy_88E1512_read_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_BMCR, &temp);

        // === Step 7.5: Reset
        regVal = (uint16)(temp & 0xFFFF);
        regVal |= 0x8000;
        IfxGeth_Eth_Phy_88E1512_write_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_BMCR, regVal);
        // Wait reset finish
        do {
            IfxGeth_Eth_Phy_88E1512_read_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_BMCR, &temp);
        } while (temp & 0x8000);
#endif

        // === Step 8: Link up 檢查 ===
        uint32 timeout = 100000;
        do {
            IfxGeth_Eth_Phy_88E1512_read_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_CSSR1, &temp);
            timeout--;
        } while ((!(temp & (1 << 10))) && timeout);  // Bit 10 = link up

        if (timeout == 0)
        {
            // 連線失敗，不設定 LED，
            // TODO: Add error log here, or retry mechanism if needed
            timeout = 100000;
        }
        else
        {
            uint16 led_config_value = 0x0000;
            IfxGeth_Eth_Phy_88E1512_set_mdio_page(3);
            IfxGeth_Eth_Phy_88E1512_read_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_LED_FCN_CTRL, &temp);
            led_config_value = (uint16)(temp & 0xFFFF);
            led_config_value &= ~0x0FFF;  // 清除 bit-11~0
            led_config_value |= 0x0125; //LED[2]: On - Link, Blink -Activity, Off - No Link
                                        //LED[1]: On - Link, Blink - Receive, Off - No Link
                                        //LED[0]: On -Transmit, Off - No Transmit
            IfxGeth_Eth_Phy_88E1512_write_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_LED_FCN_CTRL, led_config_value);

            IfxGeth_Eth_Phy_88E1512_set_mdio_page(0);
            IfxGeth_Eth_Phy_88E1512_read_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_BMCR, &temp);
            // ===Reset
            regVal = (uint16)(temp & 0xFFFF);
            regVal |= 0x8000;
            IfxGeth_Eth_Phy_88E1512_write_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_BMCR, regVal);
            // Wait reset finish
            do {
                IfxGeth_Eth_Phy_88E1512_read_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_BMCR, &temp);
            } while (temp & 0x8000);

#if 0
            // ===Packet Generate part-1(page-18)

            IfxGeth_Eth_Phy_88E1512_read_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_PACKET_GEN, &temp);
            regVal = (uint16)(temp & 0xFFFF);
            regVal = 0x0000; //set packet generate parameter:
                             //Normal mode(not yet generate)
                             //self-clear after all packets are sent
                             //generate random value
                             //packet length = 64 bytes
                             //No error packet transmit
            IfxGeth_Eth_Phy_88E1512_write_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_PACKET_GEN, regVal);

            regVal = 0x0040; //Generate Packets on Copper Interface
            IfxGeth_Eth_Phy_88E1512_write_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_PACKET_GEN, regVal);

            // === Packet Generate part-2(page-6)
            IfxGeth_Eth_Phy_88E1512_set_mdio_page(6);
            regVal = 0x08;
            IfxGeth_Eth_Phy_88E1512_write_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_COPPER_PACKET_GEN, regVal);

            // === Step 2: Reset
            IfxGeth_Eth_Phy_88E1512_read_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_GEN_CTRL1_P18, &temp);
            regVal = (uint16)(temp & 0xFFFF);
            regVal |= (1 << 15);  // Bit15 = Reset
            IfxGeth_Eth_Phy_88E1512_write_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_GEN_CTRL1_P18, regVal);
            //IfxGeth_Eth_Phy_88E1512_read_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_GEN_CTRL1_P18, &temp);
            // Wait reset finish
            do {
                IfxGeth_Eth_Phy_88E1512_read_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_GEN_CTRL1_P18, &temp);
            } while (temp & 0x8000);
#endif

        }
		debugPrintOnce = true;//qqqq
		Debug_Print_Out("phy T", 0, 0, 0, dbug_num_type_str);
return 0;
}



void IfxGeth_Eth_Phy_88E1512_read_mdio_reg(uint32 layeraddr, uint32 regaddr, uint32 *pdata)
{
    // 5bit Physical Layer Adddress, 5bit GMII Regnr, 4bit csrclock divider, Read, Busy
    GETH_MAC_MDIO_ADDRESS.U = (layeraddr << 21) | (regaddr << 16) | (0 << 8) | (3 << 2) | (1 << 0);

    IFXGETH_PHY_88E1512_WAIT_MDIO_READY();

    // get data
    uint32 temp = GETH_MAC_MDIO_DATA.U;
    *pdata = temp & 0xFFFF;
    //*pdata = GETH_MAC_MDIO_DATA.U;
}


void IfxGeth_Eth_Phy_88E1512_write_mdio_reg(uint32 layeraddr, uint32 regaddr, uint32 data)
{
    // put data
    GETH_MAC_MDIO_DATA.U = data;

    // 5bit Physical Layer Adddress, 5bit GMII Regnr, 4bit csrclock divider, Write, Busy
    GETH_MAC_MDIO_ADDRESS.U = (layeraddr << 21) | (regaddr << 16) | (0 << 8) |  (1 << 2) | (1 << 0);

    IFXGETH_PHY_88E1512_WAIT_MDIO_READY();
}
void IfxGeth_Eth_Phy_88E1512_set_mdio_page(uint32 page)
{
    //    IfxGeth_Eth_Phy_88E1512_write_mdio_reg(0, 0x1F, page);
        IfxGeth_Eth_Phy_88E1512_write_mdio_reg(0, 0x16, page);
}

uint32 IfxGeth_Eth_Phy_88E1512_link_status(void)
{
    Ifx_GETH_MAC_PHYIF_CONTROL_STATUS link_status;
    uint32 value;
    link_status.U = 0;

    // if (IfxGeth_Eth_Phy_88E1512_iPhyInitDone) 
    {
        /* 1. 切換到 Page 0 (Copper Register Bank) */
        /* 88E1512 的狀態暫存器位於 Page 0 */
        IfxGeth_Eth_Phy_88E1512_write_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_PAGSR, 0);

        /* 2. 讀取 Register 17 (CSSR1) */
        /* 這個暫存器包含了解析後的 Speed/Duplex/Link 資訊 */
        IfxGeth_Eth_Phy_88E1512_read_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_CSSR1, &value);

        /* 3. 解析 Link Status (Bit 10) */
        /* Bit 10: Real Time Link Status (1=Link Up, 0=Link Down) */
        if (value & (1 << 10))
        {
            link_status.B.LNKSTS = 1; // 設定 Link Up

            /* 4. 解析 Duplex Mode (Bit 13) */
            /* Bit 13: Duplex Resolved (1=Full Duplex, 0=Half Duplex) */
            if (value & (1 << 13))
            {
                link_status.B.LNKMOD = 1; // Full Duplex
            }
            else
            {
                link_status.B.LNKMOD = 0; // Half Duplex
            }

            /* 5. 解析 Speed (Bit 15:14) */
            /* Bits 15:14: Speed Resolved */
            /* 00 = 10 Mbps */
            /* 01 = 100 Mbps */
            /* 10 = 1000 Mbps */
            uint32 speed_bits = (value >> 14) & 0x03;

            if (speed_bits == 0x00)
            {
                link_status.B.LNKSPEED = 0; // 10 Mbps
            }
            else if (speed_bits == 0x01)
            {
                link_status.B.LNKSPEED = 1; // 100 Mbps
            }
            else if (speed_bits == 0x02)
            {
                link_status.B.LNKSPEED = 2; // 1000 Mbps (Infineon GETH 定義通常 2 或 3 表 1G)
            }
        }
        else
        {
            /* Link Down: 保持 link_status.U = 0 */
        }
    }

    return link_status.U;
}






