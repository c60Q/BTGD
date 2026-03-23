/************************************************************************************//**
* \file         Source/TRICORE_TC3/flash.h
* \brief        Bootloader flash driver header file.
* \ingroup      Target_TRICORE_TC3
* \internal
*----------------------------------------------------------------------------------------
*                          C O P Y R I G H T
*----------------------------------------------------------------------------------------
*   Copyright (c) 2022  by Feaser    http://www.feaser.com    All rights reserved
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
#ifndef FLASH_H
#define FLASH_H

/****************************************************************************************
* Function prototypes
****************************************************************************************/
void     FlashInit(void);
boot_bool FlashWrite(boot_addr addr, boot_int32u len, boot_int8u *data);
boot_bool FlashErase(boot_addr addr, boot_int32u len);
boot_bool FlashWriteChecksum(void);
boot_bool FlashVerifyChecksum(void);
boot_bool FlashDone(void);
boot_addr FlashGetUserProgBaseAddress(void);


#endif /* FLASH_H */
/*********************************** end of flash.h ************************************/
