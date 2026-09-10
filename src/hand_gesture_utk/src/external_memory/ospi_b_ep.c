/***********************************************************************************************************************
 * File Name    : ospi_b_ep.c
 * Description  : Contains data structures and functions used in ospi_b_ep.c
 **********************************************************************************************************************/
/***********************************************************************************************************************
* Copyright (c) 2023 - 2025 Renesas Electronics Corporation and/or its affiliates
*
* SPDX-License-Identifier: BSD-3-Clause
***********************************************************************************************************************/
/***********************************************************************************************************************
 * DERIVED FROM : output_code\ra8p1_llm_ospi_hs\src\external_memory\ospi_b_ep.c - the story fork, which itself came
 *                from the Renesas OSPI_B example project. Copied into hand_gesture_demo for Stage 1 of the hand
 *                gesture demo.
 *
 * EDITED HERE  : three changes only.
 *
 *   1. The two Octo-SPI clock dividers, inside the BOARD_RA8P1_EK branches. Each divider
 *      carries its own note. The RA8D1, RA8M1 and RA8E2 branches are other boards and are left untouched.
 *
 *   1b. The two Octo-SPI clock SOURCE constants in the same two
 *      BOARD_RA8P1_EK branches change from BSP_CLOCKS_SOURCE_CLOCK_PLL2P to BSP_CLOCKS_SOURCE_CLOCK_PLL2R. PLL2R is
 *      already 240 MHz in the merged clock tree, so no configurator property moves. The earlier plan took PLL2P and
 *      divided the PLL2 loop by 5; that route does not exist on this part, because PL2ODIVP allows only /2, /3, /4,
 *      /6, /8 and /16 (hardware manual section 9.2.10, register PLL2CCR2). PLL2P stays at /4 = 300 MHz and unused.
 *
 *   2. THE STORY FORK'S THROUGHPUT BENCHMARK IS REMOVED. Gone are timer_init(), timer_start_measure(), timer_get_measure(),
 *      ospi_b_operation() and the three helpers ospi_b_erase_operation(), ospi_b_write_operation() and
 *      ospi_b_read_operation(), plus the g_read_data and g_write_data test buffers and the RTT Viewer menu
 *      macros that only they used. That code opened an FSP General PWM Timer instance named g_timer, which this
 *      project does not have, so the merged build would not link. Nothing in the merged demo ever called it.
 *      NO timer instance was added, so no timer channel and no interrupt slot is spent and the resource budget
 *      is untouched.
 *
 * WHAT THE DEMO USES : src\hal_entry.c enters this file at ospi_b_init() and ospi_b_set_protocol_to_opi() only.
 *                      ospi_b_init() calls ospi_b_setup_calibrate_data(). Everything they reach is kept.
 **********************************************************************************************************************/

#include "common_utils.h"
#include "ospi_b_commands.h"
#include "ospi_b_ep.h"

/*******************************************************************************************************************//**
 * @addtogroup ospi_b_ep.c
 * @{
 **********************************************************************************************************************/

/* External variable */
extern spi_flash_direct_transfer_t g_ospi_b_direct_transfer [OSPI_B_TRANSFER_MAX];

/* Global variables */
/* The g_read_data and g_write_data benchmark buffers were removed with the benchmark. */

/* Function declarations */
static fsp_err_t ospi_b_write_enable(void);
static fsp_err_t ospi_b_wait_operation(uint32_t timeout);
static fsp_err_t ospi_b_setup_calibrate_data(void);

/*******************************************************************************************************************//**
 * @brief       This function enables write and verify the read data.
 * @param       None
 * @retval      FSP_SUCCESS     Upon successful operation.
 * @retval      FSP_ERR_ABORTED Upon incorrect read data.
 * @retval      Any other error code apart from FSP_SUCCESS Unsuccessful operation.
 **********************************************************************************************************************/
static fsp_err_t ospi_b_write_enable(void)
{
    fsp_err_t                   err         = FSP_SUCCESS;
    spi_flash_direct_transfer_t transfer    =
    {
        .command        = RESET_VALUE,
        .address        = RESET_VALUE,
        .data           = RESET_VALUE,
        .command_length = RESET_VALUE,
        .address_length = RESET_VALUE,
        .data_length    = RESET_VALUE,
        .dummy_cycles   = RESET_VALUE
    };

    /* Transfer write enable command */
    transfer = (SPI_FLASH_PROTOCOL_EXTENDED_SPI == g_ospi_b_ctrl.spi_protocol)
               ? g_ospi_b_direct_transfer[OSPI_B_TRANSFER_WRITE_ENABLE_SPI]
               : g_ospi_b_direct_transfer[OSPI_B_TRANSFER_WRITE_ENABLE_OPI];
    err = R_OSPI_B_DirectTransfer(&g_ospi_b_ctrl, &transfer, SPI_FLASH_DIRECT_TRANSFER_DIR_WRITE);
    APP_ERR_RETURN(err, "R_OSPI_B_DirectTransfer API FAILED\r\n");

    /* Read Status Register */
    transfer = (SPI_FLASH_PROTOCOL_EXTENDED_SPI == g_ospi_b_ctrl.spi_protocol)
               ? g_ospi_b_direct_transfer[OSPI_B_TRANSFER_READ_STATUS_SPI]
               : g_ospi_b_direct_transfer[OSPI_B_TRANSFER_READ_STATUS_OPI];
    err = R_OSPI_B_DirectTransfer(&g_ospi_b_ctrl, &transfer, SPI_FLASH_DIRECT_TRANSFER_DIR_READ);
    APP_ERR_RETURN(err, "R_OSPI_B_DirectTransfer API FAILED\r\n");

    /* Check Write Enable bit in Status Register */
    if(OSPI_B_WEN_BIT_MASK != (transfer.data & OSPI_B_WEN_BIT_MASK))
    {
        APP_ERR_RETURN(FSP_ERR_ABORTED, "Write enable FAILED\r\n");
    }
    return err;
}

/*******************************************************************************************************************//**
 * @brief       This function waits until OSPI operation completes.
 * @param[in]   timeout         Maximum waiting time.
 * @retval      FSP_SUCCESS     Upon successful wait OSPI operating.
 * @retval      FSP_ERR_TIMEOUT Upon time out.
 * @retval      Any other error code apart from FSP_SUCCESS Unsuccessful operation.
 **********************************************************************************************************************/
static fsp_err_t ospi_b_wait_operation(uint32_t timeout)
{
    fsp_err_t          err    = FSP_SUCCESS;
    spi_flash_status_t status = {RESET_VALUE};

    status.write_in_progress = true;
    while (status.write_in_progress)
    {
        /* Get device status */
        R_OSPI_B_StatusGet(&g_ospi_b_ctrl, &status);
        APP_ERR_RETURN(err, "R_OSPI_B_StatusGet API FAILED\r\n");
        if(RESET_VALUE == timeout)
        {
            APP_ERR_RETURN(FSP_ERR_TIMEOUT, "OSPI time out occurred\r\n");
        }
        R_BSP_SoftwareDelay(1, OSPI_B_TIME_UNIT);
        timeout --;
    }
    return err;
}

/* ospi_b_erase_operation(), ospi_b_write_operation() and ospi_b_read_operation() stood here. They were the
 * story fork's timed erase, write and read helpers. All three were removed with the benchmark. */

/*******************************************************************************************************************//**
 * @brief       This functions initializes OSPI module and Flash device.
 * @param       None.
 * @retval      FSP_SUCCESS     Upon successful initialization of OSPI module and Flash device.
 * @retval      Any other error code apart from FSP_SUCCESS  Unsuccessful open.
 **********************************************************************************************************************/
fsp_err_t ospi_b_init(void)
{
    /* By default, the flash device is in SPI mode, so it is necessary to open the OSPI module in SPI mode */
    fsp_err_t                   err      = FSP_SUCCESS;
    spi_flash_direct_transfer_t transfer =
    {
        .command        = RESET_VALUE,
        .address        = RESET_VALUE,
        .data           = RESET_VALUE,
        .command_length = RESET_VALUE,
        .address_length = RESET_VALUE,
        .data_length    = RESET_VALUE,
        .dummy_cycles   = RESET_VALUE
    };

    /* Open OSPI module */
    err = R_OSPI_B_Open(&g_ospi_b_ctrl, &g_ospi_b_cfg);
    APP_ERR_RETURN(err, "R_OSPI_B_Open API FAILED\r\n");

    /* Switch OSPI module to 1S-1S-1S mode to configure flash device */
    err = R_OSPI_B_SpiProtocolSet(&g_ospi_b_ctrl, SPI_FLASH_PROTOCOL_EXTENDED_SPI);
    APP_ERR_RETURN(err, "R_OSPI_B_SpiProtocolSet API FAILED\r\n");

    /* Reset flash device */
    R_XSPI0->LIOCTL_b.RSTCS0 = 0;
    R_BSP_SoftwareDelay(OSPI_B_TIME_RESET_PULSE, OSPI_B_TIME_UNIT);
    R_XSPI0->LIOCTL_b.RSTCS0 = 1;
    R_BSP_SoftwareDelay(OSPI_B_TIME_RESET_SETUP, OSPI_B_TIME_UNIT);

    /* Transfer write enable command */
    err = ospi_b_write_enable();
    APP_ERR_RETURN(err, "ospi_b_write_enable FAILED\r\n");

#if defined (BOARD_RA8D1_EK) || defined (BOARD_RA8M1_EK)
    /* Write to CFR2V to configure Address Byte Length and Memory Array Read Latency */
    transfer = g_ospi_b_direct_transfer[OSPI_B_TRANSFER_WRITE_CFR2V_SPI];
    transfer.address_length = OSPI_B_ADDRESS_LENGTH_THREE;  /* Default Address Byte Length is 3 bytes */
    err = R_OSPI_B_DirectTransfer(&g_ospi_b_ctrl, &transfer, SPI_FLASH_DIRECT_TRANSFER_DIR_WRITE);
    APP_ERR_RETURN(err, "R_OSPI_B_DirectTransfer API FAILED\r\n");;

    /* Write to CFR3V to configure Volatile Register Read Latency */
    transfer = g_ospi_b_direct_transfer[OSPI_B_TRANSFER_WRITE_CFR3V_SPI];
    err = R_OSPI_B_DirectTransfer(&g_ospi_b_ctrl, &transfer, SPI_FLASH_DIRECT_TRANSFER_DIR_WRITE);
    APP_ERR_RETURN(err, "R_OSPI_B_DirectTransfer API FAILED\r\n");

    /* Read back and verify CFR2V register data */
    transfer = g_ospi_b_direct_transfer[OSPI_B_TRANSFER_READ_CFR2V_SPI];
    err = R_OSPI_B_DirectTransfer(&g_ospi_b_ctrl, &transfer, SPI_FLASH_DIRECT_TRANSFER_DIR_READ);
    APP_ERR_RETURN(err, "R_OSPI_B_DirectTransfer API FAILED\r\n");
    if(OSPI_B_DATA_CFR2V_REGISTER != (uint8_t)transfer.data)
    {
        APP_ERR_RETURN(FSP_ERR_ABORTED, "Verify CFR2V register data FAILED\r\n");
    }

    /* Read back and verify CFR3V register data */
    transfer = g_ospi_b_direct_transfer[OSPI_B_TRANSFER_READ_CFR3V_SPI];
    err = R_OSPI_B_DirectTransfer(&g_ospi_b_ctrl, &transfer, SPI_FLASH_DIRECT_TRANSFER_DIR_READ);
    APP_ERR_RETURN(err, "R_OSPI_B_DirectTransfer API FAILED\r\n");
    if(OSPI_B_DATA_CFR3V_REGISTER != (uint8_t)transfer.data)
    {
        APP_ERR_RETURN(FSP_ERR_ABORTED, "Verify CFR3V register data FAILED\r\n");
    }

#elif defined (BOARD_RA8E2_EK) || defined (BOARD_RA8P1_EK)
    /* Write to ADDR 00000300H of CR2 to configure dummy cycle */
    transfer = g_ospi_b_direct_transfer[OSPI_B_TRANSFER_WRITE_CR2_300H_SPI];
    transfer.data    = OSPI_B_DATA_SET_CR2_300H;
    err = R_OSPI_B_DirectTransfer(&g_ospi_b_ctrl, &transfer, SPI_FLASH_DIRECT_TRANSFER_DIR_WRITE);
    APP_ERR_RETURN(err, "R_OSPI_B_DirectTransfer API FAILED\r\n");

    /* Read back and verify CR2 register data */
    transfer = g_ospi_b_direct_transfer[OSPI_B_TRANSFER_READ_CR2_300H_SPI];
    err = R_OSPI_B_DirectTransfer(&g_ospi_b_ctrl, &transfer, SPI_FLASH_DIRECT_TRANSFER_DIR_READ);
    APP_ERR_RETURN(err, "R_OSPI_B_DirectTransfer API FAILED\r\n");

    if(OSPI_B_DATA_SET_CR2_300H != (uint8_t)transfer.data)
    {
        APP_ERR_RETURN(FSP_ERR_ABORTED, "Data mismatched in SPI mode\r\n");
    }

#endif

#if (OSPI_B_CFG_AUTOCALIBRATION_SUPPORT_ENABLE)
    /* Setup calibrate data */
    err = ospi_b_setup_calibrate_data();
    APP_ERR_RETURN(err, "ospi_b_setup_calibrate_data FAILED\r\n");
#endif

    return err;
}

/*******************************************************************************************************************//**
 * @brief       This function configures OSPI to extended SPI mode.
 * @param[IN]   None
 * @retval      FSP_SUCCESS     Upon successful transition to SPI operating mode.
 * @retval      FSP_ERR_ABORTED Upon incorrect read data.
 * @retval      Any other error code apart from FSP_SUCCESS  Unsuccessful operation.
 **********************************************************************************************************************/
fsp_err_t ospi_b_set_protocol_to_spi(void)
{
    fsp_err_t                   err      = FSP_SUCCESS;
    spi_flash_direct_transfer_t transfer =
    {
        .command        = RESET_VALUE,
        .address        = RESET_VALUE,
        .data           = RESET_VALUE,
        .command_length = RESET_VALUE,
        .address_length = RESET_VALUE,
        .data_length    = RESET_VALUE,
        .dummy_cycles   = RESET_VALUE
    };
    bsp_octaclk_settings_t      octaclk  =
    {
        .source_clock  = RESET_VALUE,
        .divider       = RESET_VALUE
    };

    if(SPI_FLASH_PROTOCOL_EXTENDED_SPI == g_ospi_b_ctrl.spi_protocol)
    {
        /* Do nothing */
    }
    else if(SPI_FLASH_PROTOCOL_8D_8D_8D == g_ospi_b_ctrl.spi_protocol)
    {
        /* Transfer write enable command */
        err = ospi_b_write_enable();
        APP_ERR_RETURN(err, "ospi_b_write_enable FAILED\r\n");
#if defined (BOARD_RA8D1_EK) || defined (BOARD_RA8M1_EK)
        /* Write to CFR5V Register to Configure flash device interface mode */
        transfer = g_ospi_b_direct_transfer[OSPI_B_TRANSFER_WRITE_CFR5V_OPI];
        transfer.data = OSPI_B_DATA_SET_SPI_CFR5V_REGISTER;
        err = R_OSPI_B_DirectTransfer(&g_ospi_b_ctrl, &transfer, SPI_FLASH_DIRECT_TRANSFER_DIR_WRITE);
        APP_ERR_RETURN(err, "R_OSPI_B_DirectTransfer API FAILED\r\n");

        /* Change the OCTACLK clock to 100 MHz in SDR mode without OM_DQS */
        octaclk.source_clock = BSP_CLOCKS_SOURCE_CLOCK_PLL2P;
        octaclk.divider      = BSP_CLOCKS_OCTA_CLOCK_DIV_4;
        R_BSP_OctaclkUpdate(&octaclk);

        /* Switch OSPI module mode to SPI mode */
        err = R_OSPI_B_SpiProtocolSet(&g_ospi_b_ctrl, SPI_FLASH_PROTOCOL_EXTENDED_SPI);
        APP_ERR_RETURN(err, "R_OSPI_SpiProtocolSet API FAILED\r\n");

        /* Read back and verify CFR5V register data */
        transfer = g_ospi_b_direct_transfer[OSPI_B_TRANSFER_READ_CFR5V_SPI];
        err = R_OSPI_B_DirectTransfer(&g_ospi_b_ctrl, &transfer, SPI_FLASH_DIRECT_TRANSFER_DIR_READ);
        APP_ERR_RETURN(err, "R_OSPI_B_DirectTransfer API FAILED\r\n");
        if(OSPI_B_DATA_SET_SPI_CFR5V_REGISTER != (uint8_t)transfer.data)
        {
            APP_ERR_RETURN(FSP_ERR_ABORTED, "Verify CFR5V register data FAILED\r\n");
        }
#elif defined (BOARD_RA8E2_EK) || defined (BOARD_RA8P1_EK)
        /* Change to SPI mode */
        transfer = g_ospi_b_direct_transfer[OSPI_B_TRANSFER_WRITE_CR2_000H_OPI];
        transfer.data = OSPI_B_DATA_SET_SPI_CR2_000H;
        err = R_OSPI_B_DirectTransfer(&g_ospi_b_ctrl, &transfer, SPI_FLASH_DIRECT_TRANSFER_DIR_WRITE);
        APP_ERR_RETURN(err, "R_OSPI_B_DirectTransfer API FAILED\r\n");

#if defined (BOARD_RA8E2_EK)
        /* Change the OCTACLK clock to 96 MHz in SDR mode without OM_DQS */
        octaclk.source_clock = BSP_CLOCKS_SOURCE_CLOCK_PLL1Q;
        octaclk.divider      = BSP_CLOCKS_OCTA_CLOCK_DIV_2;
        R_BSP_OctaclkUpdate(&octaclk);
#elif defined (BOARD_RA8P1_EK)
        /* MERGED PROJECT (hand_gesture_demo).
         * PLL2R /2 = 120 MHz Octo-SPI clock. This function is not called by the demo. */
        octaclk.source_clock = BSP_CLOCKS_SOURCE_CLOCK_PLL2R;
        octaclk.divider      = BSP_CLOCKS_OCTA_CLOCK_DIV_2;
        R_BSP_OctaclkUpdate(&octaclk);
#endif /* OCTACLK clock settings for EK-RA8E2/EK-RA8P1*/

        /* Switch OSPI module mode to SPI mode */
        err = R_OSPI_B_SpiProtocolSet(&g_ospi_b_ctrl, SPI_FLASH_PROTOCOL_EXTENDED_SPI);
        APP_ERR_RETURN(err, "R_OSPI_SpiProtocolSet API FAILED\r\n");

        /* Read back and verify CR2 register data */
        transfer = g_ospi_b_direct_transfer[OSPI_B_TRANSFER_READ_CR2_000H_SPI];
        err = R_OSPI_B_DirectTransfer(&g_ospi_b_ctrl, &transfer, SPI_FLASH_DIRECT_TRANSFER_DIR_READ);
        APP_ERR_RETURN(err, "R_OSPI_B_DirectTransfer API FAILED\r\n");

        if(OSPI_B_DATA_SET_SPI_CR2_000H != (uint8_t)transfer.data)
        {
            APP_ERR_RETURN(FSP_ERR_ABORTED, "Data mismatched in SPI mode\r\n");
        }
#endif
    }

    else
    {
        APP_ERR_RETURN(FSP_ERR_INVALID_MODE, "Flash device do not support this mode\r\n");
    }
    return err;
}

/*******************************************************************************************************************//**
 * @brief       This function configures OSPI to OPI mode.
 * @param[IN]   None
 * @retval      FSP_SUCCESS     Upon successful transition to OPI operating mode.
 * @retval      FSP_ERR_ABORTED Upon incorrect read data.
 * @retval      Any other error code apart from FSP_SUCCESS  Unsuccessful operation
 **********************************************************************************************************************/
fsp_err_t ospi_b_set_protocol_to_opi(void)
{
    fsp_err_t                   err      = FSP_SUCCESS;
    spi_flash_direct_transfer_t transfer =
    {
        .command        = RESET_VALUE,
        .address        = RESET_VALUE,
        .data           = RESET_VALUE,
        .command_length = RESET_VALUE,
        .address_length = RESET_VALUE,
        .data_length    = RESET_VALUE,
        .dummy_cycles   = RESET_VALUE
    };
    bsp_octaclk_settings_t      octaclk  =
    {
        .source_clock  = RESET_VALUE,
        .divider       = RESET_VALUE
    };

    if(SPI_FLASH_PROTOCOL_8D_8D_8D == g_ospi_b_ctrl.spi_protocol)
    {
        /* Do nothing */
    }
    else if(SPI_FLASH_PROTOCOL_EXTENDED_SPI == g_ospi_b_ctrl.spi_protocol)
    {
        /* Transfer write enable command */
        err = ospi_b_write_enable();
        APP_ERR_RETURN(err, "ospi_b_write_enable FAILED\r\n");
#if defined (BOARD_RA8D1_EK) || defined (BOARD_RA8M1_EK)
        /* Write to CFR5V Register to Configure flash device interface mode */
        transfer = g_ospi_b_direct_transfer[OSPI_B_TRANSFER_WRITE_CFR5V_SPI];
        transfer.data = OSPI_B_DATA_SET_OPI_CFR5V_REGISTER;
        err = R_OSPI_B_DirectTransfer(&g_ospi_b_ctrl, &transfer, SPI_FLASH_DIRECT_TRANSFER_DIR_WRITE);
        APP_ERR_RETURN(err, "R_OSPI_B_DirectTransfer API FAILED\r\n");

        /* Change the OCTACLK clock to 200 MHz in DDR mode */
        octaclk.source_clock = BSP_CLOCKS_SOURCE_CLOCK_PLL2P;
        octaclk.divider      = BSP_CLOCKS_OCTA_CLOCK_DIV_2;
        R_BSP_OctaclkUpdate(&octaclk);

        /* Switch OSPI module mode to OPI mode */
        err = R_OSPI_B_SpiProtocolSet(&g_ospi_b_ctrl, SPI_FLASH_PROTOCOL_8D_8D_8D);
        APP_ERR_RETURN(err, "R_OSPI_SpiProtocolSet API FAILED\r\n");

        /* Read back and verify CFR5V register data */
        transfer = g_ospi_b_direct_transfer[OSPI_B_TRANSFER_READ_CFR5V_OPI];
        err = R_OSPI_B_DirectTransfer(&g_ospi_b_ctrl, &transfer, SPI_FLASH_DIRECT_TRANSFER_DIR_READ);
        APP_ERR_RETURN(err, "R_OSPI_B_DirectTransfer API FAILED\r\n");
        if(OSPI_B_DATA_SET_OPI_CFR5V_REGISTER != (uint8_t)transfer.data)
        {
            APP_ERR_RETURN(FSP_ERR_ABORTED, "Verify CFR5V register data FAILED\r\n");
        }
#elif defined (BOARD_RA8E2_EK) || defined (BOARD_RA8P1_EK)
        /* Change to DOPI mode */
        transfer = g_ospi_b_direct_transfer[OSPI_B_TRANSFER_WRITE_CR2_000H_SPI];
        transfer.data    = OSPI_B_DATA_SET_OPI_CR2_000H;
        err = R_OSPI_B_DirectTransfer(&g_ospi_b_ctrl, &transfer, SPI_FLASH_DIRECT_TRANSFER_DIR_WRITE);
        APP_ERR_RETURN(err, "R_OSPI_B_DirectTransfer API FAILED\r\n");

        /* MERGED PROJECT (hand_gesture_demo).
         * Octo-SPI clock source is PLL2R, already 240 MHz in this tree
         * (PLL2R = PLL2 loop 1200 MHz /5). PLL2P cannot reach 240 MHz on this part,
         * because PL2ODIVP has no /5. Divider /1 gives a 240 MHz Octo-SPI clock and a
         * 120 MHz flash clock, which is the clock the 3.69 tokens per second was
         * measured at. */
        octaclk.source_clock = BSP_CLOCKS_SOURCE_CLOCK_PLL2R;
        octaclk.divider      = BSP_CLOCKS_OCTA_CLOCK_DIV_1;
        R_BSP_OctaclkUpdate(&octaclk);

        /* Switch OSPI module mode to SPI mode */
        err = R_OSPI_B_SpiProtocolSet(&g_ospi_b_ctrl, SPI_FLASH_PROTOCOL_8D_8D_8D);
        APP_ERR_RETURN(err, "R_OSPI_SpiProtocolSet API FAILED\r\n");

        /* Read back and verify CR2 register data */
        transfer = g_ospi_b_direct_transfer[OSPI_B_TRANSFER_READ_CR2_000H_OPI];
        err = R_OSPI_B_DirectTransfer(&g_ospi_b_ctrl, &transfer, SPI_FLASH_DIRECT_TRANSFER_DIR_READ);
        APP_ERR_RETURN(err, "R_OSPI_B_DirectTransfer API FAILED\r\n");

        if(OSPI_B_DATA_SET_OPI_CR2_000H != (uint8_t) transfer.data)
        {
            APP_ERR_RETURN(FSP_ERR_ABORTED, "Data mismatched in OPI mode\r\n");
        }
#endif
    }
    else
    {
        APP_ERR_RETURN(FSP_ERR_INVALID_MODE, "Flash device do not support this mode\r\n");
    }
    return err;
}

/**********************************************************************************************************************
 * @brief       This function reads flash device id.
 * @param[out]  *p_device_id        Pointer will be used to store device id.
 * @retval      FSP_SUCCESS         Upon successful direct transfer operation.
 * @retval      FSP_ERR_ABORTED     On incorrect device id read.
 * @retval      Any other error code apart from FSP_SUCCESS  Unsuccessful operation.
 **********************************************************************************************************************/
fsp_err_t ospi_b_read_device_id (uint32_t * const p_id)
{
    fsp_err_t                   err         = FSP_SUCCESS;
    spi_flash_direct_transfer_t transfer    =
    {
        .command        = RESET_VALUE,
        .address        = RESET_VALUE,
        .data           = RESET_VALUE,
        .command_length = RESET_VALUE,
        .address_length = RESET_VALUE,
        .data_length    = RESET_VALUE,
        .dummy_cycles   = RESET_VALUE
    };

    /* Read and check flash device ID */
    transfer = (SPI_FLASH_PROTOCOL_EXTENDED_SPI == g_ospi_b_ctrl.spi_protocol)
               ? g_ospi_b_direct_transfer[OSPI_B_TRANSFER_READ_DEVICE_ID_SPI]
               : g_ospi_b_direct_transfer[OSPI_B_TRANSFER_READ_DEVICE_ID_OPI];
    err = R_OSPI_B_DirectTransfer(&g_ospi_b_ctrl, &transfer, SPI_FLASH_DIRECT_TRANSFER_DIR_READ);
    APP_ERR_RETURN(err, "R_OSPI_B_DirectTransfer API FAILED\r\n");

#if defined (BOARD_RA8D1_EK) || defined (BOARD_RA8M1_EK)
    if((OSPI_B_DEVICE_ID != transfer.data) && (OSPI_B_DEVICE_HL_ID != transfer.data))
#elif defined (BOARD_RA8E2_EK) || defined (BOARD_RA8P1_EK)
    if(OSPI_B_DEVICE_ID != transfer.data)
#endif
    {
        APP_ERR_RETURN(FSP_ERR_ABORTED, "Device ID is incorrect\r\n");
    }

    /* Get flash device ID */
    *p_id = transfer.data;
    return err;
}

/* ospi_b_operation() stood here, followed by timer_start_measure(), timer_get_measure() and timer_init().
 * ospi_b_operation() was the story fork's benchmark entry point: an RTT Viewer menu that ran timed erase, write
 * and read operations. The three timer functions drove an FSP General PWM Timer instance named g_timer, which
 * this project does not have. Nothing in the merged demo called any of them, so all four were removed. */

/*******************************************************************************************************************//**
 * @brief       This function sets up the auto-calibrate data for the flash.
 * @param       None
 * @retval      FSP_SUCCESS Upon successful operation
 * @retval      Any other error code apart from FSP_SUCCESS  Unsuccessful operation
 **********************************************************************************************************************/
static fsp_err_t ospi_b_setup_calibrate_data(void)
{
    fsp_err_t err = FSP_SUCCESS;
    ospi_b_extended_cfg_t * p_extended_cfg = (ospi_b_extended_cfg_t *)g_ospi_b_cfg.p_extend;

#if defined (BOARD_RA8D1_EK) || defined (BOARD_RA8M1_EK)
    uint32_t g_autocalibration_data[] =
    {
        0xFFFF0000U,
        0x000800FFU,
        0x00FFF700U,
        0xF700F708U
    };
#elif defined (BOARD_RA8E2_EK) || defined (BOARD_RA8P1_EK)
    uint32_t g_autocalibration_data[] =
    {
        0xFFFF0000U,
        0x0800FF00U,
        0xFF0000F7U,
        0x00F708F7U
    };
#endif
    /* D-CACHE COHERENCY (root cause of FSP_ERR_CALIBRATE_FAILED on this board):
     * The preamble source below is a stack array and R_OSPI_B_Write uses the DMAC, which reads
     * physical RAM and bypasses the CPU cache. With write-back D-cache enabled, invalidate the
     * flash-mapped preamble region so the memcmp sees real flash, and clean the source array so
     * the DMAC writes the correct bytes. Without this, a garbage preamble reaches flash and DQS
     * auto-calibration can never match. */
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    SCB_InvalidateDCache_by_Addr((void *)p_extended_cfg->p_autocalibration_preamble_pattern_addr,
                                 (int32_t)sizeof(g_autocalibration_data));
#endif

    /* Verify auto-calibration data */
    if (RESET_VALUE != memcmp((uint8_t *)p_extended_cfg->p_autocalibration_preamble_pattern_addr,
            &g_autocalibration_data, sizeof(g_autocalibration_data)))
    {
        /* Erase the flash sector that stores auto-calibration data */
        err = R_OSPI_B_Erase(&g_ospi_b_ctrl,
                            (uint8_t *)p_extended_cfg->p_autocalibration_preamble_pattern_addr, OSPI_B_SECTOR_SIZE_4K);
        APP_ERR_RETURN(err, "R_OSPI_B_Erase API FAILED\r\n");

        /* Wait until erase operation completes */
        err = ospi_b_wait_operation(OSPI_B_TIME_ERASE_4K);
        APP_ERR_RETURN(err, "ospi_b_wait_operation FAILED\r\n");

        /* Clean the source array out of D-cache so the DMAC reads the correct bytes. */
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
        SCB_CleanDCache_by_Addr((void *)&g_autocalibration_data, (int32_t)sizeof(g_autocalibration_data));
#endif

        /* Write auto-calibration data to the flash */
        err = R_OSPI_B_Write(&g_ospi_b_ctrl, (uint8_t *)&g_autocalibration_data,\
                             (uint8_t *)p_extended_cfg->p_autocalibration_preamble_pattern_addr,\
                             sizeof(g_autocalibration_data));
        APP_ERR_RETURN(err, "R_OSPI_B_Write API FAILED\r\n");

        /* Wait until write operation completes */
        err = ospi_b_wait_operation(OSPI_B_TIME_WRITE);
        APP_ERR_RETURN(err, "ospi_b_wait_operation FAILED\r\n");

        /* Invalidate again so any later CPU read of the preamble sees the freshly written flash. */
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
        SCB_InvalidateDCache_by_Addr((void *)p_extended_cfg->p_autocalibration_preamble_pattern_addr,
                                     (int32_t)sizeof(g_autocalibration_data));
#endif
    }
    __NOP();
    return err;
}

/*******************************************************************************************************************//**
 * @} (end addtogroup ospi_b_ep.c)
 **********************************************************************************************************************/
