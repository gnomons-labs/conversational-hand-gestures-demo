/*
* Copyright (c) 2020 - 2025 Renesas Electronics Corporation and/or its affiliates
*
* SPDX-License-Identifier: BSD-3-Clause
*/
/**********************************************************************************************************************
 * File Name    : common_utils.h
 * Version      : .
 * Description  : .
 *********************************************************************************************************************/
#ifndef COMMON_UTIL_H__
#define COMMON_UTIL_H__

#include <stdbool.h>

#include "hal_data.h"
#include "application_config.h"
#include "ai_application_config.h"

#if defined(BOARD_RA8D1_EK)
#define LED1_PIN (BSP_IO_PORT_06_PIN_00)
#define LED2_PIN (BSP_IO_PORT_04_PIN_14)
#define LED3_PIN (BSP_IO_PORT_01_PIN_07)
#elif defined(BOARD_RA8P1_WSB_EK) || defined(BOARD_RA8P1_EK)
#define LED1_PIN (BSP_IO_PORT_06_PIN_00)
#define LED2_PIN (BSP_IO_PORT_03_PIN_03)
#define LED3_PIN (BSP_IO_PORT_10_PIN_07)
#else
// Update pin number for your board
#define LED1_PIN (BSP_IO_PORT_FF_PIN_FF)
#define LED2_PIN (BSP_IO_PORT_FF_PIN_FF)
#define LED3_PIN (BSP_IO_PORT_FF_PIN_FF)
#endif

#define LED1_ON  R_IOPORT_PinWrite(NULL, LED1_PIN, BSP_IO_LEVEL_HIGH)
#define LED1_OFF R_IOPORT_PinWrite(NULL, LED1_PIN, BSP_IO_LEVEL_LOW)
#define LED2_ON  R_IOPORT_PinWrite(NULL, LED2_PIN, BSP_IO_LEVEL_HIGH)
#define LED2_OFF R_IOPORT_PinWrite(NULL, LED2_PIN, BSP_IO_LEVEL_LOW)
#define LED3_ON  R_IOPORT_PinWrite(NULL, LED3_PIN, BSP_IO_LEVEL_HIGH)
#define LED3_OFF R_IOPORT_PinWrite(NULL, LED3_PIN, BSP_IO_LEVEL_LOW)

#if (ENABLE_INFERENCE_RUNNING_LED == 1)
#define INFERENCE_START_INDICATE_LED LED1_ON
#define INFERENCE_END_INDICATE_LED   LED1_OFF
#else
#define INFERENCE_START_INDICATE_LED
#define INFERENCE_END_INDICATE_LED
#endif

#if (ENABLE_CAMERA_CAPTURE_RUNNING_LED == 1)
#define CAMERA_CAPTURE_END_INDICATE_LED_ON  LED2_ON
#define CAMERA_CAPTURE_END_INDICATE_LED_OFF LED2_OFF
#else
#define CAMERA_CAPTURE_END_INDICATE_LED_ON
#define CAMERA_CAPTURE_END_INDICATE_LED_OFF
#endif

#define ERROR_INDICATE_LED_ON  LED3_ON
#define ERROR_INDICATE_LED_OFF LED3_OFF

#define ERROR_INDICATE ERROR_INDICATE_LED_ON; __BKPT(0)

/* sync events */
#define HARDWARE_DISPLAY_INIT_DONE      (1 << 0)
#define HARDWARE_CAMERA_INIT_DONE       (1 << 1)
#define HARDWARE_ETHOSU_INIT_DONE       (1 << 2)
#define SOFTWARE_AI_INFERENCE_INIT_DONE (1 << 3)
#define GLCDC_VSYNC                     (1 << 10)
#define MIPI_MESSAGE_SENT               (1 << 11)
#define CAMERA_CAPTURE_COMPLETED        (1 << 12)
#define AI_INFERENCE_INPUT_IMAGE_READY  (1 << 13)
#define AI_INFERENCE_RESULT_UPDATED     (1 << 14)
#define DISPLAY_PAUSE                   (1 << 15)
#define CAMERA_AUTO_FOCUS_EXECUTE       (1 << 16)

/* Demo event bits. Every bit is passed to the app_sig_* functions of src\app_signal.h and to
 * nothing else.
 *
 * The values keep the vision fork's numbering above wherever a bit means the same thing, so a
 * reader who knows the sample recognises them.
 *
 * A FreeRTOS event group has only 24 usable bits: the top eight are control bits
 * (ra\aws\FreeRTOS\FreeRTOS\Source\include\event_groups.h, eventEVENT_BITS_CONTROL_BYTES =
 * 0xff000000) and xEventGroupWaitBits asserts if any of them is asked for. NO BIT MAY EVER BE
 * ADDED ABOVE (1 << 23). The highest used here is EV_BTN2 = (1 << 19), so four are spare.
 *
 * THE BIT COLLISION IS CLOSED (2026-08-13, in the two button callbacks). Two of the old sample
 * names share a value with a new one:
 *   DISPLAY_PAUSE             (1 << 15) == EV_STORY_START
 *   CAMERA_AUTO_FOCUS_EXECUTE (1 << 16) == EV_STORY_TEXT
 * The one that could fire was the sample's SW1 handler in common_util.c: it SET DISPLAY_PAUSE,
 * so a press of SW1 released T_STORY and read as a request for a story. That handler now sets
 * EV_BTN1 (1 << 18). CAMERA_AUTO_FOCUS_EXECUTE never had a writer or a reader in this project.
 *
 * NOTHING SETS EITHER OLD NAME ANY MORE. One read is left, in
 * src\display_layer\detection_screen_mipi.c, and that file is dropped from the build:
 * ui_screen.c replaces it, and no task calls it once the two old thread-entry files leave the
 * build. The two old names stay defined only so those three unreplaced files still compile
 * until they are excluded.
 *
 * The old names below (1 << 0) to (1 << 14) alias the EV_INIT_* and camera bits in the same
 * way, and the last writers of those are src\ai_inference_thread_entry.c and
 * src\camera_display_thread_entry.c. BOTH MUST BE EXCLUDED FROM THE BUILD before the demo is
 * run, or two sets of code will drive one event group.
 */
#define EV_INIT_DISPLAY     (1U << 0)   /* set by T_UI,    read by T_CAM, T_AI, T_STORY. Sticky */
#define EV_INIT_CAMERA      (1U << 1)   /* set by T_CAM,   read by T_AI.                Sticky  */
#define EV_INIT_NPU         (1U << 2)   /* set by T_AI,    read by T_UI.                Sticky  */
#define EV_INIT_STORY       (1U << 3)   /* set by T_STORY, read by T_UI.                Sticky  */
/* EV_VSYNC HAS TWO CLEAR SITES, NOT ONE. Besides the wait in task_ui.c, graphics_swap_buffer
 * clears it again at the buffer change - bench fix 2026-08-14, see
 * src\display_layer\display_layer.c. The fix works because the two sites agree, so do not
 * treat the wait as the only owner of this bit. */
#define EV_VSYNC            (1U << 10)  /* graphics line-detect callback -> T_UI. Cleared by the wait
                                         * AND by graphics_swap_buffer; see the note above       */
#define EV_CAM_FRAME        (1U << 12)  /* video-input callback -> T_CAM.         Cleared by the wait  */
#define EV_AI_INPUT_READY   (1U << 13)  /* T_CAM   -> T_AI.                       Cleared by the wait  */
#define EV_AI_RESULT        (1U << 14)  /* T_AI    -> T_UI.       Observed, cleared by hand           */
#define EV_STORY_START      (1U << 15)  /* T_UI    -> T_STORY.                    Cleared by the wait  */
#define EV_STORY_TEXT       (1U << 16)  /* T_STORY -> T_UI.       Observed, cleared by hand           */
#define EV_STORY_DONE       (1U << 17)  /* T_STORY -> T_UI.       Observed, cleared by hand           */
#define EV_BTN1             (1U << 18)  /* SW1 callback -> T_UI.  Observed, cleared by hand           */
#define EV_BTN2             (1U << 19)  /* SW2 callback -> T_UI.  Observed, cleared by hand           */

/* The five bits T_UI reads without waiting on them. It clears exactly the ones it saw, once
 * per frame, right after its EV_VSYNC wait returns. EV_INIT_NPU and EV_INIT_STORY are
 * deliberately outside this mask: they are status, and T_UI only tests them. */
#define UI_OBSERVED (EV_AI_RESULT | EV_STORY_TEXT | EV_STORY_DONE | EV_BTN1 | EV_BTN2)

#define APP_ERROR_TRAP(err)        if(err) { __asm("BKPT #0\n");} /* system execution breaks  */

/**********************************************************************************************************************
 * Typedef definitions
 **********************************************************************************************************************/

/* The coordinate of the bounding box corner, bounding box width and height based on 192x192 gray pixel area */
typedef struct ai_detection_point_t {
  signed short      m_x;
  signed short      m_y;
  signed short      m_w;
  signed short      m_h;
} st_ai_detection_point_t;

typedef enum
{
    CAM_VGA_WIDTH          = 640,
    CAM_VGA_HEIGHT         = 480,
    CAM_QVGA_WIDTH         = 320,
    CAM_QVGA_HEIGHT        = 240,

} camera_size_list_t;

#define CAM_BYTE_PER_PIXEL              (2)
#define RGB888_BYTE_PER_PIXEL           (3)

/* image intermediate input size to convert to MIPI display size */
#define IMAGE_INPUT_WIDTH       CAM_QVGA_HEIGHT
#define IMAGE_INPUT_HEIGHT      CAM_QVGA_HEIGHT

/** Common error codes */
typedef enum e_vision_ai_app_err
{
    VISION_AI_APP_SUCCESS                = 0,

    VISION_AI_APP_ERR_AI_INIT            = 1,  ///< AI init failed
    VISION_AI_APP_ERR_AI_INFERENCE       = 2,  ///< AI inference failed
    VISION_AI_APP_ERR_IMG_PROCESS        = 3,  ///< Image crop failed
    VISION_AI_APP_ERR_IMG_ROTATION       = 4,  ///< Image rotation failed
    VISION_AI_APP_ERR_NULL_POINTER       = 5,  ///< null pointer
    VISION_AI_APP_ERR_GLCDC_OPEN         = 6,  ///< glcdc open failed
    VISION_AI_APP_ERR_MIPI_CMD           = 7,  ///< mipi command failed
    VISION_AI_APP_ERR_GLCDC_START        = 8,  ///< glcdc start failed
    VISION_AI_APP_ERR_GLCDC_LAYER_CHANGE = 9,  ///< graphics layer change failed
    VISION_AI_APP_ERR_GRAPHICS_INIT      = 10, ///< One of the graphics system initialization failed
    VISION_AI_APP_ERR_GPT_OPEN           = 11, ///< GPT open failed
    VISION_AI_APP_ERR_CEU_OPEN           = 12, ///< CEU open failed
    VISION_AI_APP_ERR_WRITE_OV3640_REG   = 13, ///< Write OV3640 register failed
    VISION_AI_APP_ERR_WRITE_SENSOR_ARRAY = 14, ///< Write OV3640 register array failed
    VISION_AI_APP_ERR_CAMERA_INIT        = 15, ///< Camera init failed
    VISION_AI_APP_ERR_IIC_MASTER_OPEN    = 16, ///< IIC master open failed
    VISION_AI_APP_ERR_IIC_MASTER_WRITE   = 17, ///< IIC master write failed
    VISION_AI_APP_ERR_IIC_MASTER_READ    = 18, ///< IIC master read failed
    VISION_AI_APP_ERR_CONSOLE_OPEN       = 19, ///< jlink uart open error
    VISION_AI_APP_ERR_CONSOLE_WRITE      = 20, ///< JLink console write failed
    VISION_AI_APP_ERR_CONSOLE_READ       = 21, ///< JLink console read failed
    VISION_AI_APP_ERR_EXTERNAL_IRQ_INIT  = 22, ///< External IRQ init failed

    /* Ten codes appended for the merged demo. Codes 0 to 22 above are the vision fork's own
     * and are unchanged, so every existing call site still compiles. These are application
     * names, not Flexible Software Package names, so nothing outside this project has to
     * supply them. */
    VISION_AI_APP_ERR_OSPI_OPEN          = 23, ///< ospi_b_init failed in the warm-start hook
    VISION_AI_APP_ERR_OSPI_HS_SWITCH     = 24, ///< ospi_b_set_protocol_to_opi failed
    VISION_AI_APP_ERR_STORY_MODEL_HEADER = 25, ///< story model header check failed
    VISION_AI_APP_ERR_NPU_OPEN           = 26, ///< RM_ETHOSU_Open failed
    VISION_AI_APP_ERR_NPU_INVOKE         = 27, ///< ten model invocations in a row failed
    VISION_AI_APP_ERR_VIN_OPEN           = 28, ///< R_VIN_Open or R_VIN_CaptureStart failed
    VISION_AI_APP_ERR_DRW_INIT           = 29, ///< d2_opendevice or d2_inithw failed
    VISION_AI_APP_ERR_KERNEL_OBJECT      = 30, ///< a task or the event group was not created
    VISION_AI_APP_ERR_POOL_ALLOC         = 31, ///< the story heap refused a block
    VISION_AI_APP_ERR_FLAG_TIMEOUT       = 32, ///< an event wait timed out
} vision_ai_app_err_t;

/**********************************************************************************************************************
 * What the demo does about a failure.
 *
 * THE RULE: a booth demo never stops at a breakpoint. handle_error prints the code and the
 * place, lights LED3 and returns one of these. APP_FATAL is the only value that stops the
 * caller, and it stops in a slow blink loop with the message already on the console.
 *********************************************************************************************************************/
typedef enum e_app_action
{
    APP_OK = 0,             ///< reported, and the demo carries on unchanged
    APP_DEGRADE_STORY,      ///< the story feature is off for this run
    APP_DEGRADE_MODELS,     ///< no palm or landmark model; the camera picture stays live
    APP_DEGRADE_CAMERA,     ///< the camera zone stays black; the rest of the demo runs
    APP_DEGRADE_EXTERNAL,   ///< nothing from the external flash is trustworthy: no models, no
                            ///< artwork, no story. A live camera picture and text is all there is
    APP_FATAL               ///< nothing can be shown or driven. Does not return
} app_action_t;

/** process_time report */
typedef struct st_processing_time_info_t
{
    uint32_t camera_image_capture_time_ms;          ///< Camera frame capture time
    uint32_t camera_post_processing_time_ms;        ///< Post processing time for captured camera image
    uint32_t lcd_display_update_refresh_ms;         ///< LCD display refresh time
    uint32_t ai_inference_pre_processing_time_ms;   ///< Pre processing time for AI inference
    uint32_t ai_inference_time_ms;                  ///< AI inference processing time
} processinf_time_info_t;

/**********************************************************************************************************************
 * Exported global variables
 **********************************************************************************************************************/

extern char sprintf_buffer[];
extern processinf_time_info_t application_processing_time;
extern st_ai_detection_point_t g_ai_detection[AI_MAX_DETECTION_NUM];

/* Gesture tuning overlay on or off. Toggled by user button SW2. */
extern volatile bool g_gesture_overlay_on;

/* The Octo-SPI open result, latched inside R_BSP_WarmStart, where nothing can print yet.
 * Defined in src\hal_entry.c. Anything other than FSP_SUCCESS means the external flash never
 * opened, so the vision models, the artwork and the story are all lost and the demo runs as a
 * live camera picture with text. T_UI, T_AI and T_STORY each test it once, at their own
 * start-up. */
extern volatile fsp_err_t g_startup_ospi_err;

/* True while T_AI is inside palm_detection(), which reads BOTH the camera working picture and
 * the model input buffer. T_CAM is the higher priority of the two, so it must not rewrite
 * either buffer while this is true. Written by T_AI only (src\tasks\task_ai.c), read by T_CAM
 * only. */
extern volatile bool g_ai_reading_frame;

FSP_CPP_HEADER

/**********************************************************************************************************************
 * Report one failure and say what the demo does about it.
 *
 * It prints the code and the place, lights LED3, and returns the action. It NEVER breakpoints.
 * On APP_FATAL it does not return: it stays in a slow LED3 blink loop, with the message already
 * on the console.
 *
 * @param[in] err    which failure
 * @param[in] where  a short place name for the console line, for example "T_UI drw_init".
 *                   May be NULL.
 * @return    what the caller should do next
 *********************************************************************************************************************/
app_action_t handle_error (vision_ai_app_err_t err, const char * where);

fsp_err_t external_irq_configure(void);
FSP_CPP_FOOTER

#endif /* COMMON_UTIL_H__ */
