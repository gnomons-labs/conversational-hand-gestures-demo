/***********************************************************************************************************************
 * File Name    : ospi_b_ep.h
 * Description  : Contains data structures and functions used in ospi_ep.c.
 **********************************************************************************************************************/
/***********************************************************************************************************************
* Copyright (c) 2023 - 2025 Renesas Electronics Corporation and/or its affiliates
*
* SPDX-License-Identifier: BSD-3-Clause
***********************************************************************************************************************/
/***********************************************************************************************************************
 * DERIVED FROM : the Renesas OSPI_B example project for the RA family.
 *
 * NOTE         : the extern "C" wrapper below is required, because the story glue that includes this header
 *                is C++.
 **********************************************************************************************************************/

#ifndef OSPI_B_EP_H_
#define OSPI_B_EP_H_

#include "fsp_common_api.h"

/* Self-select the board macro when the build passes none (see ospi_b_commands.h). */
#if !defined(BOARD_RA8P1_EK) && !defined(BOARD_RA8E2_EK) && !defined(BOARD_RA8D1_EK) && !defined(BOARD_RA8M1_EK)
#define BOARD_RA8P1_EK
#endif

/* Macros for flash device */
#if defined (BOARD_RA8D1_EK) || defined (BOARD_RA8M1_EK)
#define OSPI_B_DEVICE_ID                    (0x0F1A5B34)    /* For S28HS512T flash */
#define OSPI_B_DEVICE_HL_ID                 (0x0F1A5A34)    /* For S28HL512T flash */
#define OSPI_B_MANUFACTURER_ID              (0x34)
#define OSPI_B_DEVICE_ID_TYPE               (0x5B)          /* For S28HS512T flash */
#define OSPI_B_DEVICE_HL_ID_TYPE            (0x5A)          /* For S28HL512T flash */
#define OSPI_B_DEVICE_ID_DENSITY            (0x1A)
#elif defined (BOARD_RA8E2_EK) || defined (BOARD_RA8P1_EK)
#define OSPI_B_DEVICE_ID                    (0x3A86C2)      /* For MX25LW51245G flash */
#endif
/* Flash device sector size */
#define OSPI_B_SECTOR_SIZE_4K               (0x1000)

/* Flash device timing */
#define OSPI_B_TIME_UNIT                    (BSP_DELAY_UNITS_MICROSECONDS)
#define OSPI_B_TIME_RESET_SETUP             (2U)             /*  Type 50ns */
#define OSPI_B_TIME_RESET_PULSE             (1000U)          /*  Type 500us */
#define OSPI_B_TIME_ERASE_4K                (100000U)        /*  Type 4KB sector is 95 KBps -> Type 0.042s */
#define OSPI_B_TIME_WRITE                   (10000U)         /*  Type 256B page (4KB/256KB) is 595/533 KBps -> Type */

/* Flash device status bit */
#define OSPI_B_WEN_BIT_MASK                 (0x00000002)
#define OSPI_B_BUSY_BIT_MASK                (0x00000001)

/* Flash device address space mapping */
#define OSPI_B_CS0_START_ADDRESS            (0x80000000)
#define OSPI_B_CS1_START_ADDRESS            (0x90000000)


/* Function declarations */
FSP_CPP_HEADER

fsp_err_t ospi_b_init(void);
fsp_err_t ospi_b_set_protocol_to_spi(void);
fsp_err_t ospi_b_set_protocol_to_opi(void);
fsp_err_t ospi_b_read_device_id(uint32_t * const p_id);

FSP_CPP_FOOTER

#endif /* OSPI_B_EP_H_ */
