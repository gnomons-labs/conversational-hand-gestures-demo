/*
* Copyright (c) 2020 - 2025 Renesas Electronics Corporation and/or its affiliates
*
* SPDX-License-Identifier: BSD-3-Clause
*/
/**********************************************************************************************************************
 * File Name    : common_util.c
 * Description  : This file contains implementation of the the error handling function.
 **********************************************************************************************************************/
/***************************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 ***************************************************************************************************************************/

#include "console_output.h"
#include "hal_data.h"
#include <stdio.h>
#include <stdarg.h>      /* e_printf passes its arguments on - see the note there */
#include "common_util.h"
#include "app_signal.h"

/***************************************************************************************************************************
 * Macro definitions
 ***************************************************************************************************************************/

/* THE BREAKPOINT IS GONE. A booth demo never stops at a breakpoint. The vision sample ended
 * handle_error with BKPT #0, which without a debugger attached is a dead board in front of a
 * visitor. handle_error now prints, lights LED3 and RETURNS an action. */

/***************************************************************************************************************************
 * Typedef definitions
 ***************************************************************************************************************************/

/***************************************************************************************************************************
 * Imported global variables and functions (from other files)
 ***************************************************************************************************************************/
/***************************************************************************************************************************
 * Exported global variables and functions (to be accessed by other files)
 ***************************************************************************************************************************/
processinf_time_info_t application_processing_time;

/* Gesture tuning overlay on or off. Off at reset.
 *
 * ONE WRITER, AND IT IS NOT AN INTERRUPT ANY MORE. The five-gesture fork toggled this straight
 * from the SW2 interrupt. Both buttons are event bits now, so the SW1 bit reaches T_UI, which
 * calls ui_set_overlay, which is the only place this is written. */
volatile bool g_gesture_overlay_on = false;

/***********************************************************************************************************************
 * Private global variables and functions
 ************************************************************************************************************************/

/*********************************************************************************************************************
 * This function is used by the AI for debugging output. Refer to log_macros.h.
 * By default, this function is not being used.
 * @param format
 * @return 0
  **********************************************************************************************************************/
int e_printf(const char *format, ...)
{
    /* THE ONLY COMPILER WARNING IN THIS PROJECT'S OWN CODE, from the 2026-08-14 build log:
     * "format string is not a string literal (potentially insecure) [-Wformat-security]".
     * It was not only a warning. The fork wrote sprintf(sprintf_buffer, format), which THREW
     * THE VARIADIC ARGUMENTS AWAY - a "%d" in the format read whatever happened to be in the
     * argument registers - and sprintf has no length limit, so a long line ran past the end of
     * the 1,024-byte buffer. vsnprintf fixes both: the arguments are passed on, and the length
     * is bounded. */
    va_list args;
    int     written;

    va_start(args, format);
    written = vsnprintf(sprintf_buffer, BUFFER_LINE_LENGTH, format, args);
    va_end(args);

    print_to_console(sprintf_buffer);

    return (written < 0) ? 0 : written;
}

/**********************************************************************************************************************
 * One failure, one console line, one action.
 *
 * WHAT CHANGED AGAINST THE VISION FORK. The fork's handle_error ended in BKPT #0, which without
 * a debugger attached is a dead board in front of a visitor, and it returned nothing, so no
 * caller could degrade. It is replaced by this: print, light LED3, return an action. The long
 * switch of one sprintf per code becomes the table below, so a new code is one row.
 *
 * THE MESSAGES ARE THE FORK'S OWN WORDS, shortened. They are console text, not screen text, so
 * the plain upper-case ASCII rule for screen text does not apply to them.
 *********************************************************************************************************************/

typedef struct st_app_err_row
{
    vision_ai_app_err_t err;
    app_action_t        action;
    const char        * text;
} app_err_row_t;

/* The action column is the failure table, read down its "On failure" column. Three codes that
 * table does not name are marked below with the row they were mapped to and why. Nothing else
 * is decided here. */
static const app_err_row_t g_app_err_rows[] =
{
    { VISION_AI_APP_ERR_AI_INIT,            APP_DEGRADE_MODELS,   "AI init failed"                     },
    { VISION_AI_APP_ERR_AI_INFERENCE,       APP_DEGRADE_MODELS,   "AI inference failed"                },
    { VISION_AI_APP_ERR_IMG_PROCESS,        APP_OK,               "image cropping failed"              },
    { VISION_AI_APP_ERR_IMG_ROTATION,       APP_OK,               "image rotation failed"              },
    { VISION_AI_APP_ERR_NULL_POINTER,       APP_OK,               "input buffer not allocated"         },
    { VISION_AI_APP_ERR_GLCDC_OPEN,         APP_FATAL,            "R_GLCDC_Open returned error"        },
    { VISION_AI_APP_ERR_MIPI_CMD,           APP_FATAL,            "R_MIPI_DSI_Command returned error"  },
    { VISION_AI_APP_ERR_GLCDC_START,        APP_FATAL,            "R_GLCDC_Start returned error"       },
    { VISION_AI_APP_ERR_GLCDC_LAYER_CHANGE, APP_FATAL,            "R_GLCDC_LayerChange returned error" },
    { VISION_AI_APP_ERR_GRAPHICS_INIT,      APP_FATAL,            "graphics init returned error"       },
    /* NOT NAMED IN THE FAILURE TABLE. GPT0 is the demo's only time base and GPT12 is the camera
     * master clock, so a failure here always costs the camera. Mapped to the camera row. */
    { VISION_AI_APP_ERR_GPT_OPEN,           APP_DEGRADE_CAMERA,   "R_GPT_Open returned error"          },
    { VISION_AI_APP_ERR_CEU_OPEN,           APP_DEGRADE_CAMERA,   "R_CEU_Open returned error"          },
    /* One sensor register write is counted, and more than ten of them give the camera failure
     * below. So the single write is APP_OK and the table walk is the camera row. */
    { VISION_AI_APP_ERR_WRITE_OV3640_REG,   APP_OK,               "sensor register write failed"       },
    { VISION_AI_APP_ERR_WRITE_SENSOR_ARRAY, APP_DEGRADE_CAMERA,   "sensor register table failed"       },
    { VISION_AI_APP_ERR_CAMERA_INIT,        APP_DEGRADE_CAMERA,   "camera init returned error"         },
    { VISION_AI_APP_ERR_IIC_MASTER_OPEN,    APP_DEGRADE_CAMERA,   "R_IIC_MASTER_Open returned error"   },
    { VISION_AI_APP_ERR_IIC_MASTER_WRITE,   APP_DEGRADE_CAMERA,   "R_IIC_MASTER_Write returned error"  },
    { VISION_AI_APP_ERR_IIC_MASTER_READ,    APP_DEGRADE_CAMERA,   "R_IIC_MASTER_Read returned error"   },
    /* The demo runs on with no console. Not fatal - a visitor never sees it. */
    { VISION_AI_APP_ERR_CONSOLE_OPEN,       APP_OK,               "console open returned error"        },
    { VISION_AI_APP_ERR_CONSOLE_WRITE,      APP_OK,               "console write returned error"       },
    { VISION_AI_APP_ERR_CONSOLE_READ,       APP_OK,               "console read returned error"        },
    /* NOT NAMED IN THE FAILURE TABLE. Losing the two buttons costs the tuning overlay and the
     * booth reset. A visitor uses neither, so the demo carries on. */
    { VISION_AI_APP_ERR_EXTERNAL_IRQ_INIT,  APP_OK,               "external IRQ init returned error"   },
    { VISION_AI_APP_ERR_OSPI_OPEN,          APP_DEGRADE_EXTERNAL, "Octo-SPI open failed at reset"      },
    { VISION_AI_APP_ERR_OSPI_HS_SWITCH,     APP_DEGRADE_STORY,    "Octo-SPI high-speed switch failed"  },
    { VISION_AI_APP_ERR_STORY_MODEL_HEADER, APP_DEGRADE_STORY,    "story model header check failed"    },
    { VISION_AI_APP_ERR_NPU_OPEN,           APP_DEGRADE_MODELS,   "RM_ETHOSU_Open returned error"      },
    { VISION_AI_APP_ERR_NPU_INVOKE,         APP_DEGRADE_MODELS,   "ten model invocations failed"       },
    { VISION_AI_APP_ERR_VIN_OPEN,           APP_DEGRADE_CAMERA,   "video input open or start failed"   },
    { VISION_AI_APP_ERR_DRW_INIT,           APP_FATAL,            "2D drawing engine open failed"      },
    { VISION_AI_APP_ERR_KERNEL_OBJECT,      APP_FATAL,            "a task or event object is missing"  },
    { VISION_AI_APP_ERR_POOL_ALLOC,         APP_DEGRADE_STORY,    "story heap refused a block"         },
    /* Counted and printed. A camera frame that never arrives leaves the picture still; a
     * vsync that never arrives leaves the last frame. */
    { VISION_AI_APP_ERR_FLAG_TIMEOUT,       APP_OK,               "an event wait timed out"            },
};

/**********************************************************************************************************************
 * The slow blink loop. APP_FATAL only, and it never returns.
 *
 * A busy delay on purpose: this path is reached when something the whole demo needs is missing,
 * and the loop must work whether or not the scheduler is running.
 *********************************************************************************************************************/
static void app_fatal_blink(void)
{
    while (true)
    {
        ERROR_INDICATE_LED_ON;
        R_BSP_SoftwareDelay(500, BSP_DELAY_UNITS_MILLISECONDS);
        ERROR_INDICATE_LED_OFF;
        R_BSP_SoftwareDelay(500, BSP_DELAY_UNITS_MILLISECONDS);
    }
}

app_action_t handle_error (vision_ai_app_err_t err, const char * where)
{
    /* The old guard was one shared "already in here" flag. It stopped the recursion it was
     * written for, but it also threw away a failure reported by a second task while the first
     * was still inside this function, and its test-and-set was not atomic.
     *
     * The guard is gone. The recursion is cut at its source instead: this function prints
     * through print_to_console_quiet, which never reports a write failure, so there is no way
     * back into handle_error. Every task's report is now printed, and the console mutex keeps
     * two reports from interleaving. */
    app_action_t action = APP_OK;
    const char * text   = "unknown failure";
    char         line[160];
    size_t       i;

    for (i = 0U; i < (sizeof(g_app_err_rows) / sizeof(g_app_err_rows[0])); i++)
    {
        if (g_app_err_rows[i].err == err)
        {
            action = g_app_err_rows[i].action;
            text   = g_app_err_rows[i].text;
            break;
        }
    }

    ERROR_INDICATE_LED_ON;

    snprintf(line, sizeof(line), "\r\n[failure %d at %s] %s\r\n",
             (int) err, (NULL != where) ? where : "?", text);
    print_to_console_quiet(line);

    if (APP_FATAL == action)
    {
        app_fatal_blink();      /* never returns */
    }

    return action;
}

fsp_err_t external_irq_configure(void)
{
    fsp_err_t fsp_status = FSP_SUCCESS;

    fsp_status = R_ICU_ExternalIrqOpen(&g_external_irq_sw1_ctrl, &g_external_irq_sw1_cfg);
    if(FSP_SUCCESS != fsp_status)
    {
        return fsp_status;
    }

    fsp_status = R_ICU_ExternalIrqOpen(&g_external_irq_sw2_ctrl, &g_external_irq_sw2_cfg);
    if(FSP_SUCCESS != fsp_status)
    {
        return fsp_status;
    }

    fsp_status = R_ICU_ExternalIrqEnable(&g_external_irq_sw1_ctrl);
    if(FSP_SUCCESS != fsp_status)
    {
        return fsp_status;
    }

    fsp_status = R_ICU_ExternalIrqEnable(&g_external_irq_sw2_ctrl);
    if(FSP_SUCCESS != fsp_status)
    {
        return fsp_status;
    }

    return FSP_SUCCESS;
}

/**********************************************************************************************************************
 * User button SW1.
 *
 * Was a DISPLAY_PAUSE toggle; now it sets EV_BTN1. T_UI turns that bit into the gesture
 * tuning overlay.
 *
 * THIS EDIT CLOSES A LIVE BIT COLLISION, and that is why it could not wait. The sample's
 * DISPLAY_PAUSE is (1 << 15) and the demo's EV_STORY_START is the same value, so while this
 * handler still set DISPLAY_PAUSE a press of SW1 released T_STORY and looked like a request for
 * a story. The toggle itself is gone as well: a press now reports a press, and T_UI keeps the
 * on-or-off state, in ui_set_overlay.
 *********************************************************************************************************************/
void external_irq_sw1_cb(external_irq_callback_args_t *p_args)
{
    FSP_PARAMETER_NOT_USED(p_args);

    app_sig_set_isr(EV_BTN1);
}

/**********************************************************************************************************************
 * User button SW2 - the booth reset.
 *
 * Was empty; now it sets EV_BTN2. T_UI calls conv_force_idle on it.
 *
 * The five-gesture fork toggled g_gesture_overlay_on straight from this interrupt. That job
 * moves to SW1 and to T_UI, so the overlay flag now has exactly one writer, in ui_screen.c, and
 * no interrupt writes application state any more.
 *********************************************************************************************************************/
void external_irq_sw2_cb(external_irq_callback_args_t *p_args)
{
    FSP_PARAMETER_NOT_USED(p_args);

    app_sig_set_isr(EV_BTN2);
}
