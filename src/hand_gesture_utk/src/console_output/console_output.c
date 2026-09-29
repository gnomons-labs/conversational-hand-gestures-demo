/*
* Copyright (c) 2020 - 2025 Renesas Electronics Corporation and/or its affiliates
*
* SPDX-License-Identifier: BSD-3-Clause
*/
/**********************************************************************************************************************
 * File Name    : console_output.c
 * Description  : This file defines the jlink console implementations.
 **********************************************************************************************************************/
#include "hal_data.h"
#include "common_data.h"

#include <stdio.h>
#include <string.h>

/* This file is C and includes only <stdio.h>, <string.h> and FSP headers, so the kernel header
 * compiles here and no wrapper is needed. */
#include <tk/tkernel.h>

#include "common_util.h"
#include "console_output.h"
#include "console_output_config.h"

/***************************************************************************************************************************
 * Macro definitions
 ***************************************************************************************************************************/
#if (CONSOLE_OUTPUT_TYPE == 0)
#include "SEGGER_RTT.h"
#endif

/***************************************************************************************************************************
 * Typedef definitions
 ***************************************************************************************************************************/
/***************************************************************************************************************************
 * Imported global variables and functions (from other files)
 ***************************************************************************************************************************/
/***************************************************************************************************************************
 * Exported global variables and functions (to be accessed by other files)
 ***************************************************************************************************************************/
char sprintf_buffer[BUFFER_LINE_LENGTH] = {};
/***************************************************************************************************************************
 * Private global variables and functions
 ***************************************************************************************************************************/
static uint8_t s_rx_buf;

#if (CONSOLE_OUTPUT_TYPE == 1)
static uint8_t  g_out_of_band_received[BUFFER_LINE_LENGTH];
static uint32_t g_out_of_band_index = 0;
static volatile uint32_t g_transfer_complete = 0;
static volatile uint32_t g_receive_complete  = 0;
#endif

/* EVERY TASK SHARES ONE CONSOLE, so all four of T_CAM, T_AI, T_UI and T_STORY reach
 * console_output_write. Without this mutex a second caller clears g_transfer_complete under
 * the first one's feet and its own write returns FSP_ERR_IN_USE. Created once in
 * console_output_init, which T_UI runs before any other task can print.
 *
 * A micro T-Kernel mutex is an ID, and a valid ID is positive, so "not created yet" is
 * g_console_mtx <= 0. The write then goes through unlocked, which is safe, because only one
 * task is alive that early. */
static ID g_console_mtx = 0;

/* The transmit-complete flag is set by an interrupt. If that interrupt never comes the waiting
 * task must still leave, because nothing stops the demo. At 230400 baud one 512-byte line
 * takes about 22 ms, so 100 ms is far more than any real line needs. */
#define CONSOLE_TX_WAIT_MS      (100U)

static fsp_err_t console_output_write(const char *buffer);
static bool key_pressed(void);
static uint8_t get_detected_key(void);

/*********************************************************************************************************************
 *  Initialize the SCI UART
 *  @param[IN]   None
 *  @retval      None
***********************************************************************************************************************/

fsp_err_t console_output_init (void)
{
    fsp_err_t fsp_err = FSP_SUCCESS;

    /* The console is shared by four tasks, so it is serialised. */
    if (g_console_mtx <= 0)
    {
        /* TA_INHERIT gives priority inheritance, which is what a shared console needs: the
         * lowest-priority task must not hold the console against T_UI. It is legal:
         * tk_cre_mtx checks against VALID_MTXATR = TA_CEILING, which is the 2-bit mask 0x03,
         * and TA_INHERIT is 0x02 (mtk3_bsp2/mtkernel/kernel/tkernel/mutex.c:211-233).
         * ceilpri is only read when the attribute is TA_CEILING, so 0 is correct here.
         * CNF_MAX_MTXID is 4 for this one mutex. */
        T_CMTX cmtx = {
            .exinf   = NULL,
            .mtxatr  = TA_TFIFO | TA_INHERIT,
            .ceilpri = 0,
        };

        g_console_mtx = tk_cre_mtx(&cmtx);
    }

#if (CONSOLE_OUTPUT_TYPE == 0)
    SEGGER_RTT_Init();
#elif (CONSOLE_OUTPUT_TYPE == 1)
    fsp_err = g_console_output_uart.p_api->open(g_console_output_uart.p_ctrl, g_console_output_uart.p_cfg);
#endif

    return fsp_err;
}

/*********************************************************************************************************************
 *  Global API: Print a string buffer to Jlink console
 *  @param[IN]   None
 *  @retval      return success
***********************************************************************************************************************/
vision_ai_app_err_t print_to_console(char * p_data)
{
    fsp_err_t err = FSP_SUCCESS;
    vision_ai_app_err_t vision_ai_status = VISION_AI_APP_SUCCESS;

    err = console_output_write(p_data);
    if(FSP_SUCCESS != err)
    {
        (void) handle_error(VISION_AI_APP_ERR_CONSOLE_WRITE, "print_to_console");
        return VISION_AI_APP_ERR_CONSOLE_WRITE;
    }

    return vision_ai_status;
}

/*********************************************************************************************************************
 *  Global API: print a line WITHOUT reporting a write failure.
 *
 *  handle_error prints its own line, and print_to_console reports a failed write back through
 *  handle_error, so without care the two could call each other for ever.
 *
 *  The loop is broken at the one place it starts: handle_error prints through this function,
 *  which never calls handle_error. There is no recursion to guard and no report is ever
 *  dropped. A console write that fails while reporting another failure is simply lost - there
 *  is nowhere left to say it.
 *
 *  @param[IN]   p_data: the line to print
 *  @retval      None
***********************************************************************************************************************/
void print_to_console_quiet(char * p_data)
{
    (void) console_output_write(p_data);
}

/*********************************************************************************************************************
 *  Read user input from the Jlink console
 *  @param[IN]   None
 *  @retval      return input buffer
***********************************************************************************************************************/
int8_t input_from_console (void)
{
    fsp_err_t err = FSP_SUCCESS;

    s_rx_buf = 0;
#if (CONSOLE_OUTPUT_TYPE == 1)
    g_receive_complete = false;

    err = g_console_output_uart.p_api->read(g_console_output_uart.p_ctrl, &s_rx_buf, 1);
    if (FSP_SUCCESS != err)
    {
        (void) handle_error(VISION_AI_APP_ERR_CONSOLE_READ, "console_input");
    }
#endif

    while(key_pressed() == false)
    {
        /* A one-millisecond yield. tk_dly_tsk takes milliseconds directly, because
         * CNF_TIMER_PERIOD is 1. This is the second of the two delays in this file; the other
         * is in console_output_write(). */
        (void) tk_dly_tsk((RELTIM) 1);
    }

    return ((int8_t)get_detected_key());
}

/*********************************************************************************************************************
 *  Local function: write a string over the JLink console
 *  @param[IN]   buffer: string buffer
 *  @retval      None
***********************************************************************************************************************/
static fsp_err_t console_output_write(const char *buffer)
{
    fsp_err_t err = FSP_SUCCESS;

    /* One writer at a time. An unset ID means console_output_init has not run yet, and then
     * there is only one task alive that can print. */
    if (g_console_mtx > 0)
    {
        (void) tk_loc_mtx(g_console_mtx, TMO_FEVR);
    }

#if (CONSOLE_OUTPUT_TYPE == 0)
    SEGGER_RTT_Write(0, (uint8_t *)buffer, strlen(buffer));
#elif (CONSOLE_OUTPUT_TYPE == 1)
    g_transfer_complete = false;

    err = g_console_output_uart.p_api->write(g_console_output_uart.p_ctrl, (uint8_t *)buffer, strlen(buffer));

    if (FSP_SUCCESS != err)
    {
        /* NO TRAP AND NO WAIT ON THIS PATH. Nothing was started, so nothing will ever set
         * g_transfer_complete, and falling into the wait below would freeze the calling task
         * for ever. The failure is not reported from here either: the caller reports it, so
         * one failed write is not printed twice, and a quiet caller can choose not to report
         * at all. */
    }
    else
    {
        /* Bounded. A transmit-complete interrupt that never arrives must cost one late line,
         * not a dead task. */
        for (uint32_t waited = 0U; (0U == g_transfer_complete) && (waited < CONSOLE_TX_WAIT_MS); waited++)
        {
            /* THIS DELAY IS NOT OPTIONAL AND MUST NOT BE GUARDED BY BSP_CFG_RTOS, which is 0
             * in this project. An empty loop body would turn the bounded 100 ms yielding wait
             * into a 100-iteration busy spin that expires in microseconds: every console write
             * would time out and be aborted, and NO LINE WOULD EVER BE PRINTED.
             *
             * tk_dly_tsk takes milliseconds directly, because CNF_TIMER_PERIOD is 1. */
            (void) tk_dly_tsk((RELTIM) 1);
        }

        if (0U == g_transfer_complete)
        {
            /* The wait expired, so the transmitter is still reading
             * buffer - and buffer is nearly always the caller's stack array, which dies the
             * moment this function returns. Stop the transfer first, so the port sends no bytes
             * off a dead frame and the next caller does not get FSP_ERR_IN_USE and report a
             * second, misleading failure. communicationAbort and UART_DIR_TX are both confirmed
             * in this project's own ra\fsp\inc\api\r_uart_api.h.
             *
             * The result is an error, not FSP_SUCCESS: the line was not printed, so the caller
             * must not be told it was. */
            (void) g_console_output_uart.p_api->communicationAbort(g_console_output_uart.p_ctrl,
                                                                  UART_DIR_TX);
            err = FSP_ERR_TIMEOUT;
        }
    }
#endif

    if (g_console_mtx > 0)
    {
        (void) tk_unl_mtx(g_console_mtx);
    }

    return err;
}

/*********************************************************************************************************************
 *  Get key pressed
 *  @param[IN]   None
 *  @retval      uint8_t: key ascii
***********************************************************************************************************************/
uint8_t get_detected_key(void)
{
#if (CONSOLE_OUTPUT_TYPE == 0)
    SEGGER_RTT_Read(0, &s_rx_buf, sizeof(s_rx_buf));
    return (s_rx_buf);
#elif (CONSOLE_OUTPUT_TYPE == 1)
    return (s_rx_buf);
#endif
}


static bool key_pressed(void)
{
#if (CONSOLE_OUTPUT_TYPE == 0)
    return (SEGGER_RTT_HasKey());
#elif (CONSOLE_OUTPUT_TYPE == 1)
    return (g_receive_complete);
#endif
}

#if (CONSOLE_OUTPUT_TYPE == 1)
/*********************************************************************************************************************
 *  Console console callback
 *  @param[IN]   uart_callback_args_t *p_args: callback information
 *  @retval      None
***********************************************************************************************************************/
void console_output_uart_callback(uart_callback_args_t *p_args)
{
    /* Handle the UART event */
    switch (p_args->event)
    {
        /* Received a character */
        case UART_EVENT_RX_CHAR:
        {
            /* Only put the next character in the receive buffer if there is space for it */
            if (sizeof(g_out_of_band_received) > g_out_of_band_index)
            {
                /* Write either the next one or two bytes depending on the receive data size */
                if (UART_DATA_BITS_8 >= g_console_output_uart_cfg.data_bits)
                {
                    g_out_of_band_received[g_out_of_band_index++] = (uint8_t) p_args->data;
                }
                else
                {
                    uint16_t * p_dest = (uint16_t *) &g_out_of_band_received[g_out_of_band_index];
                    *p_dest              = (uint16_t) p_args->data;
                    g_out_of_band_index += 2;
                }
            }
            break;
        }
        /* Receive complete */
        case UART_EVENT_RX_COMPLETE:
        {
            g_receive_complete = 1;
            break;
        }
        /* Transmit complete */
        case UART_EVENT_TX_COMPLETE:
        {
            g_transfer_complete = 1;
            break;
        }
        default:
        {
            /* Do nothing */
        }
    }
}
#endif
