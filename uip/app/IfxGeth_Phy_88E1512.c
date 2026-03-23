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


static void IfxGeth_Eth_Phy_88E1512_debug_print_hex(const char *prefix, uint32 value)
{
    debugPrintOnce = true;
    Debug_Print_Out(prefix, 0u, 0, value, dbug_num_type_HEX32);
}

static void IfxGeth_Eth_Phy_88E1512_debug_print_u32(const char *prefix, uint32 value)
{
    debugPrintOnce = true;
    Debug_Print_Out(prefix, value, 0, 0u, dbug_num_type_U32);
}

static void IfxGeth_Eth_Phy_88E1512_debug_dump_link_regs(uint32 phy_id1, uint32 phy_id2)
{
    uint32 bmcr = 0u;
    uint32 bmsr1 = 0u;
    uint32 bmsr2 = 0u;
    uint32 cssr1 = 0u;

    IfxGeth_Eth_Phy_88E1512_set_mdio_page(0);
    IfxGeth_Eth_Phy_88E1512_read_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_BMCR, &bmcr);
    IfxGeth_Eth_Phy_88E1512_read_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_BMSR, &bmsr1);
    IfxGeth_Eth_Phy_88E1512_read_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_BMSR, &bmsr2);
    IfxGeth_Eth_Phy_88E1512_read_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_CSSR1, &cssr1);

    IfxGeth_Eth_Phy_88E1512_debug_print_u32("PHYADDR = ", PHY_ADDR);
    IfxGeth_Eth_Phy_88E1512_debug_print_hex("PHYID1 = 0x", phy_id1);
    IfxGeth_Eth_Phy_88E1512_debug_print_hex("PHYID2 = 0x", phy_id2);
    IfxGeth_Eth_Phy_88E1512_debug_print_hex("BMCR = 0x", bmcr);
    IfxGeth_Eth_Phy_88E1512_debug_print_hex("BMSR1 = 0x", bmsr1);
    IfxGeth_Eth_Phy_88E1512_debug_print_hex("BMSR2 = 0x", bmsr2);
    IfxGeth_Eth_Phy_88E1512_debug_print_hex("CSSR1 = 0x", cssr1);
}

static void IfxGeth_Eth_Phy_88E1512_debug_dump_link_regs_if_changed(void)
{
    static uint8 debugSnapshotValid = 0u;
    static uint32 lastBmcr = 0u;
    static uint32 lastBmsr1 = 0u;
    static uint32 lastBmsr2 = 0u;
    static uint32 lastCssr1 = 0u;
    uint32 bmcr = 0u;
    uint32 bmsr1 = 0u;
    uint32 bmsr2 = 0u;
    uint32 cssr1 = 0u;

    IfxGeth_Eth_Phy_88E1512_set_mdio_page(0);
    IfxGeth_Eth_Phy_88E1512_read_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_BMCR, &bmcr);
    IfxGeth_Eth_Phy_88E1512_read_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_BMSR, &bmsr1);
    IfxGeth_Eth_Phy_88E1512_read_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_BMSR, &bmsr2);
    IfxGeth_Eth_Phy_88E1512_read_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_CSSR1, &cssr1);

    if ((debugSnapshotValid == 0u) || (bmcr != lastBmcr) || (bmsr1 != lastBmsr1) || (bmsr2 != lastBmsr2) || (cssr1 != lastCssr1))
    {
        IfxGeth_Eth_Phy_88E1512_debug_print_hex("BMCR = 0x", bmcr);
        IfxGeth_Eth_Phy_88E1512_debug_print_hex("BMSR1 = 0x", bmsr1);
        IfxGeth_Eth_Phy_88E1512_debug_print_hex("BMSR2 = 0x", bmsr2);
        IfxGeth_Eth_Phy_88E1512_debug_print_hex("CSSR1 = 0x", cssr1);
        lastBmcr = bmcr;
        lastBmsr1 = bmsr1;
        lastBmsr2 = bmsr2;
        lastCssr1 = cssr1;
        debugSnapshotValid = 1u;
    }
}

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
        IfxGeth_Eth_Phy_88E1512_debug_dump_link_regs(phy_id1, phy_id2);
        if ((phy_id1 == 0xFFFF) || (phy_id1 == 0x0000)) {
            //printf("PHY 88E1512 not detected!\n");
            Debug_Print_Force_Out("phy F", 0, 0, 0, dbug_num_type_str);
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

        /* Configure RGMII clock skew on PHY side.
         * Current symptom is link-up but no RX frames during ping/ARP,
         * which is consistent with RGMII timing being off.
         */
        IfxGeth_Eth_Phy_88E1512_set_mdio_page(2);
        IfxGeth_Eth_Phy_88E1512_read_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MACCR2, &temp);
        regVal = (uint16)(temp & 0xFFFF);
        regVal |= (uint16)((1u << 5) | (1u << 4));  /* enable RXC/TXC internal delay */
        IfxGeth_Eth_Phy_88E1512_write_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MACCR2, regVal);
        IfxGeth_Eth_Phy_88E1512_read_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MACCR2, &temp);
        IfxGeth_Eth_Phy_88E1512_debug_print_hex("MACCR2 = 0x", temp);//qqqq
#if 0//qqqq
		/* Page 2, Reg 21: RGMII timing control */
		IfxGeth_Eth_Phy_88E1512_set_mdio_page(2);
		IfxGeth_Eth_Phy_88E1512_read_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MACCR2, &temp);
		regVal = (uint16)(temp & 0xFFFF);

		/* test: enable PHY-side RX/TX clock delay */
		regVal |= (1u << 5);   /* RX_CLK delay */
		regVal |= (1u << 4);   /* TX_CLK delay */

		IfxGeth_Eth_Phy_88E1512_write_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MACCR2, regVal);

		/* back to page 0 and soft reset */
		IfxGeth_Eth_Phy_88E1512_set_mdio_page(0);
		IfxGeth_Eth_Phy_88E1512_read_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_BMCR, &temp);
		regVal = (uint16)(temp & 0xFFFF);
		regVal |= 0x8000;
		IfxGeth_Eth_Phy_88E1512_write_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_BMCR, regVal);

		do {
		    IfxGeth_Eth_Phy_88E1512_read_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_BMCR, &temp);
		} while (temp & 0x8000);
#endif
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

        /* Explicitly advertise common copper capabilities and restart auto-negotiation.
         * Previous logs showed BMCR = 0x0 and CSSR1 = 0xC08 (10M/half), which strongly
         * suggests the PHY was left in a forced/limited mode. For ping/ARP reliability,
         * let the PHY negotiate a normal link first.
         */
        {
            uint16 anar_val;
            uint16 gbcr_val;

            IfxGeth_Eth_Phy_88E1512_set_mdio_page(0);

            /* Advertise 10/100 half/full, selector = IEEE 802.3. */
            anar_val = 0x01E1u;
            IfxGeth_Eth_Phy_88E1512_write_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_ANAR, anar_val);
            IfxGeth_Eth_Phy_88E1512_read_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_ANAR, &temp);
            IfxGeth_Eth_Phy_88E1512_debug_print_hex("ANAR = 0x", temp);//qqqq

            /* Test patch: temporarily disable 1000BASE-T advertisement.
             * Current logs show PHY link-up at 1000M/full but absolutely no RX frames.
             * For the next validation, force negotiation to top out at 100M so we can
             * distinguish a Gigabit RGMII timing issue from a generic MAC/DMA problem.
             */
            gbcr_val = 0x0000u;
            IfxGeth_Eth_Phy_88E1512_write_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_GBCR, gbcr_val);
            IfxGeth_Eth_Phy_88E1512_read_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_GBCR, &temp);
            IfxGeth_Eth_Phy_88E1512_debug_print_hex("GBCR = 0x", temp);//qqqq

            /* Enable and restart auto-negotiation. */
            regVal = 0x1200u;
            IfxGeth_Eth_Phy_88E1512_write_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_BMCR, regVal);
            IfxGeth_Eth_Phy_88E1512_read_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_BMCR, &temp);
            IfxGeth_Eth_Phy_88E1512_debug_print_hex("BMCR restart = 0x", temp);//qqqq
        }

        // === Step 8: Link up + speed/duplex resolved 檢查 ===
        uint32 timeout = 500000;
        IfxGeth_Eth_Phy_88E1512_set_mdio_page(0);
        do {
            uint32 bmsr_now;
            IfxGeth_Eth_Phy_88E1512_read_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_BMSR, &bmsr_now);
            IfxGeth_Eth_Phy_88E1512_read_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_BMSR, &bmsr_now);
            IfxGeth_Eth_Phy_88E1512_read_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_CSSR1, &temp);
            timeout--;
        } while ((((temp & (1u << 10)) == 0u) || ((temp & (1u << 11)) == 0u)) && timeout);  // Bit10=link up, Bit11=resolved

        if (timeout == 0)
        {
            /* 連線尚未完成協商，不設定 LED，後續由 polling 持續追蹤 */
            IfxGeth_Eth_Phy_88E1512_debug_print_hex("CSSR1 timeout = 0x", temp);//qqqq
            IfxGeth_Eth_Phy_88E1512_debug_dump_link_regs_if_changed();//qqqq
            timeout = 100000;
        }
        else
        {
            IfxGeth_Eth_Phy_88E1512_debug_print_hex("CSSR1 ready = 0x", temp);//qqqq
            IfxGeth_Eth_Phy_88E1512_debug_dump_link_regs_if_changed();//qqqq
            uint16 led_config_value = 0x0000;
            IfxGeth_Eth_Phy_88E1512_set_mdio_page(3);
            IfxGeth_Eth_Phy_88E1512_read_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_LED_FCN_CTRL, &temp);
            led_config_value = (uint16)(temp & 0xFFFF);
            led_config_value &= (uint16)~0x0FFFu;  // 清除 bit-11~0
            led_config_value |= 0x0125u; //LED[2]: On - Link, Blink -Activity, Off - No Link
                                        //LED[1]: On - Link, Blink - Receive, Off - No Link
                                        //LED[0]: On -Transmit, Off - No Transmit
            IfxGeth_Eth_Phy_88E1512_write_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_LED_FCN_CTRL, led_config_value);

            /* Do not reset BMCR here. The link was already up/resolved and another
             * software reset can drop the copper link right before polling starts.
             */
            IfxGeth_Eth_Phy_88E1512_set_mdio_page(0);

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
        IfxGeth_Eth_Phy_88E1512_set_mdio_page(0);
        IfxGeth_Eth_Phy_88E1512_iPhyInitDone = 1;
		Debug_Print_Force_Out("phy T", 0, 0, 0, dbug_num_type_str);
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
    /* Page Address Register lives on page 0, register 0x16. */
    IfxGeth_Eth_Phy_88E1512_write_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_PAGSR, page);
}

uint32 IfxGeth_Eth_Phy_88E1512_link_status(void)
{
    Ifx_GETH_MAC_PHYIF_CONTROL_STATUS link_status;
    uint32 cssr1;
    uint32 bmsr1;
    uint32 bmsr2;
    uint32 speed_bits;

    link_status.U = 0;

    if (IfxGeth_Eth_Phy_88E1512_iPhyInitDone)
    {
        /* All status registers used here are on page 0. */
        IfxGeth_Eth_Phy_88E1512_set_mdio_page(0);
        IfxGeth_Eth_Phy_88E1512_read_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_BMSR, &bmsr1);
        IfxGeth_Eth_Phy_88E1512_read_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_BMSR, &bmsr2);
        IfxGeth_Eth_Phy_88E1512_read_mdio_reg(PHY_ADDR, IFXGETH_PHY_88E1512_MDIO_CSSR1, &cssr1);
        IfxGeth_Eth_Phy_88E1512_debug_dump_link_regs_if_changed();//qqqq

        /* Use the standard BMSR link bit as the primary link source.
         * BMSR bit2 is latch-low, so the second read reflects the current state.
         */
        if ((bmsr2 & PHY_LINK_STATUS) != 0u)
        {
            link_status.B.LNKSTS = 1u;

            /* CSSR1 duplex/speed are only valid after bit11 resolved is set. */
            if ((cssr1 & (1u << 11)) != 0u)
            {
                link_status.B.LNKMOD = ((cssr1 & (1u << 13)) != 0u) ? 1u : 0u;

                speed_bits = (cssr1 >> 14) & 0x03u;
                if (speed_bits == 0x01u)
                {
                    link_status.B.LNKSPEED = 1u; /* 100 Mbps */
                }
                else if (speed_bits == 0x02u)
                {
                    link_status.B.LNKSPEED = 2u; /* 1000 Mbps */
                }
                else
                {
                    link_status.B.LNKSPEED = 0u; /* 10 Mbps or unresolved/other */
                }
            }
        }
    }

    return link_status.U;
}






