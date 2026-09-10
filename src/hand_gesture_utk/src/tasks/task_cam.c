/**********************************************************************************************************************
 * File Name    : task_cam.c
 * Description  : T_CAM - camera frames in, letterbox picture out.
 *
 * New file.
 *
 * DERIVED FROM: output_code\hand_gesture_five\src\camera_display_thread_entry.c (itself from
 * sample_code\ek_ra8p1_vision_palm_detection_hand_landmarkmodel_gesture_recognition_camera_
 * LCD_FSP640\src\camera_display_thread_entry.c). The camera half of that file is kept almost
 * as it stands - the letterbox pre-process below is its code unchanged. Three things are taken
 * out and given to other tasks:
 *   the console, the buttons, the drawing engine and the panel   -> T_UI  (task_ui.c)
 *   the screen drawing (do_detection_screen)                     -> T_UI  (ui_screen.c)
 *   the per-frame console dumps                                  -> dropped
 *
 * WHEN THIS FILE GOES LIVE, camera_display_thread_entry.c MUST LEAVE THE BUILD. Both files
 * define g_palm_preprocess_meta, so keeping both is a duplicate-symbol link error.
 *
 * Stage 1 shape: this is the entry function of the configurator
 * Thread object "cam_thread", FreeRTOS priority 4, stack 4,096 bytes, and it is the first
 * application thread the scheduler runs, so it performs the Flexible Software Package common
 * initialisation before the code below starts. It then waits for T_UI.
 *********************************************************************************************************************/

#include <stdio.h>
#include <string.h>

/* For the INT type in the task entry signature below. This file is C, so the micro T-Kernel
 * header compiles here - unlike llama4micro.cpp. */
#include <tk/tkernel.h>

#include "common_util.h"
#include "application_config.h"
#include "ai_application_config.h"

#include "camera_layer.h"
#include "camera_utils.h"
#include "console_output.h"
#include "time_counter.h"

#include "palm_preprocess.h"

#include "app_signal.h"
#include "conv_state.h"

/**********************************************************************************************************************
 * Imported global variables and functions (from other files)
 *********************************************************************************************************************/

/* The letterbox picture. Defined by T_AI in task_ai.c, because the model owns its shape. */
extern int8_t   model_buffer_int8[AI_INPUT_IMAGE_WIDTH * AI_INPUT_IMAGE_HEIGHT * AI_INPUT_IMAGE_BYTE_PER_PIXEL];
extern uint32_t model_buffer_int8_size;

/**********************************************************************************************************************
 * Exported global variables and functions (to be accessed by other files)
 *********************************************************************************************************************/

/* How the camera picture was fitted into the model's square input. The landmark post-process
 * needs it to put the joints back on camera coordinates. */
palm_preprocess_meta_t g_palm_preprocess_meta;

/* src\app_main.c declares this and passes it to tk_cre_tsk(). The prototype is here as well
 * so the file builds on its own with -Wmissing-declarations. */
void cam_thread_entry(INT stacd, void * exinf);

/**********************************************************************************************************************
 * Private global variables and functions
 *********************************************************************************************************************/

/* T_CAM's two rows of the wait table. */
#define CAM_INIT_WAIT_MS      (5000U)
#define CAM_FRAME_WAIT_MS     (500U)

/* A wait timeout is counted and printed. A camera frame that never arrives leaves the picture
 * still; it is not an error the demo stops for. */
static uint32_t g_cam_frame_timeouts = 0U;

static void cam_report_timeout(void);
static void cam_letterbox_preprocess(void);

/**********************************************************************************************************************
 * Count one lost frame wait and say so on the console, but not on every frame.
 *********************************************************************************************************************/
static void cam_report_timeout(void)
{
    g_cam_frame_timeouts++;

    /* The first one, then one line per hundred. A steady stream of these with no drop counted
     * in app_signal.c means the camera stopped, not the signalling. */
    if ((1U == g_cam_frame_timeouts) || (0U == (g_cam_frame_timeouts % 100U)))
    {
        char msg[80];

        snprintf(msg, sizeof(msg), "T_CAM NO FRAME FOR %u MS - COUNT %u\r\n",
                 (unsigned int) CAM_FRAME_WAIT_MS, (unsigned int) g_cam_frame_timeouts);
        print_to_console(msg);
    }
}

/**********************************************************************************************************************
 * Build the model's 192 x 192 x 3 letterbox picture out of the 640 x 480 camera picture.
 *
 * Unchanged from output_code\hand_gesture_five\src\camera_display_thread_entry.c:190-241.
 * Nearest neighbour, integer arithmetic, red-green-blue 565 in and signed 8-bit out.
 * scale = min(192/640, 192/480) = 0.3, so the resized picture is 192 x 144 with 24 rows of
 * padding above and below.
 *********************************************************************************************************************/
static void cam_letterbox_preprocess(void)
{
    const uint16_t * src16 = (const uint16_t *) &camera_capture_image_rgb565[0];
    int8_t         * dst   = (int8_t *) &model_buffer_int8[0];

    const int src_w = CAMERA_CAPTURE_IMAGE_WIDTH;
    const int src_h = CAMERA_CAPTURE_IMAGE_HEIGHT;
    const int dst_w = AI_INPUT_IMAGE_WIDTH;
    const int dst_h = AI_INPUT_IMAGE_HEIGHT;

    const int32_t scale_w = (dst_w << 16) / src_w;
    const int32_t scale_h = (dst_h << 16) / src_h;
    const int32_t scale   = (scale_w < scale_h) ? scale_w : scale_h;

    const int resized_w = (int) ((((int64_t) src_w * scale) + (1 << 15)) >> 16);
    const int resized_h = (int) ((((int64_t) src_h * scale) + (1 << 15)) >> 16);
    const int start_x   = (dst_w - resized_w) / 2;
    const int start_y   = (dst_h - resized_h) / 2;

    memset(dst, (uint8_t) (-128), (size_t) (dst_w * dst_h * 3));

    const int32_t inv_scale_x = (src_w << 16) / resized_w;
    const int32_t inv_scale_y = (src_h << 16) / resized_h;

    for (int dy = 0; dy < resized_h; dy++)
    {
        const int        out_y   = start_y + dy;
        const int        sy      = (int) (((int64_t) dy * inv_scale_y) >> 16);
        const uint16_t * src_row = &src16[sy * src_w];
        int8_t         * dst_row = &dst[(out_y * dst_w + start_x) * 3];

        for (int dx = 0; dx < resized_w; dx++)
        {
            const int      sx = (int) (((int64_t) dx * inv_scale_x) >> 16);
            const uint16_t px = src_row[sx];

            uint8_t r = (uint8_t) (((px >> 11) & 0x1F) << 3);
            uint8_t g = (uint8_t) (((px >> 5) & 0x3F) << 2);
            uint8_t b = (uint8_t) ((px & 0x1F) << 3);

            dst_row[dx * 3 + 0] = (int8_t) (r - 128);
            dst_row[dx * 3 + 1] = (int8_t) (g - 128);
            dst_row[dx * 3 + 2] = (int8_t) (b - 128);
        }
    }

    g_palm_preprocess_meta.square_standard_size     = (src_h > src_w) ? src_h : src_w;
    g_palm_preprocess_meta.square_padding_half_size = (src_h > src_w) ? (src_h - src_w) / 2 : (src_w - src_h) / 2;
    g_palm_preprocess_meta.resized_w                = resized_w;
    g_palm_preprocess_meta.resized_h                = resized_h;
    g_palm_preprocess_meta.start_x                  = start_x;
    g_palm_preprocess_meta.start_y                  = start_y;
}

/**********************************************************************************************************************
 * T_CAM entry.
 * @param[in] stacd  the start code passed to tk_sta_tsk(). Not used.
 * @param[in] exinf  the T_CTSK extended information. Not used.
 *
 * The kernel calls the task through T_CTSK.task, which takes two arguments instead of one.
 * THE BODY BELOW IS UNCHANGED.
 *********************************************************************************************************************/
void cam_thread_entry(INT stacd, void * exinf)
{
    fsp_err_t      fsp_status = FSP_SUCCESS;
    app_sig_bits_t pattern;
    uint32_t       t_start;
    uint32_t       t_end;

    FSP_PARAMETER_NOT_USED(stacd);
    FSP_PARAMETER_NOT_USED(exinf);

    /* T_UI opens the console, the drawing engine and the panel, and starts the demo's only
     * time base, so nothing here may print or read the clock before this wait returns. The
     * four EV_INIT_* bits are sticky and are never cleared, so this wait does not clear. */
    pattern = app_sig_wait(EV_INIT_DISPLAY, false, CAM_INIT_WAIT_MS);

    if (!app_sig_released(pattern, EV_INIT_DISPLAY))
    {
        /* T_UI never got the panel up. Carry on: the camera can still run, and the demo is
         * more useful with a live picture than stopped. */
        print_to_console("T_CAM TIMED OUT WAITING FOR THE DISPLAY\r\n");
    }

    /* Sensor, master clock, IIC, register table, video input, continuous capture. camera_init
     * does all of it (camera_layer.c). */
    camera_image_buffer_initialize();

    fsp_status = camera_init(false);

    if (FSP_SUCCESS == fsp_status)
    {
        app_sig_set(EV_INIT_CAMERA);
        camera_capture_start();
    }
    else
    {
        /* The camera zone stays black, the text strip says so, LED3 is on
         * and the state machine stays in S_IDLE. EV_INIT_CAMERA is NOT set, so T_AI stays in
         * its start-up wait and never runs a model against an empty picture. */
        (void) handle_error(VISION_AI_APP_ERR_CAMERA_INIT, "T_CAM camera_init");
    }

    while (true)
    {
        /* One frame. Cleared by the wait, because only T_CAM reads it. */
        pattern = app_sig_wait(EV_CAM_FRAME, true, CAM_FRAME_WAIT_MS);

        if (!app_sig_released(pattern, EV_CAM_FRAME))
        {
            cam_report_timeout();
            continue;
        }

        /* An inference is running, and it is reading BOTH buffers this loop would rewrite: the
         * working picture camera_capture_post_process() copies 614,400 bytes into, and the
         * model input cam_letterbox_preprocess() rewrites. This task is priority 4 and T_AI is
         * priority 3, so without this test T_AI's landmark stage can crop a DIFFERENT frame
         * from the one the palm box was measured on.
         *
         * The EV_CAM_FRAME wait above has already run and cleared the bit, which is the point:
         * frames are dropped here, not queued, so the picture simply updates at the inference
         * rate while a model runs. Nothing is reported, because this is normal running, not a
         * failure. */
        if (g_ai_reading_frame)
        {
            continue;
        }

        t_start = TimeCounter_CurrentCountGet();

        /* Invalidate the cache over the buffer the capture hardware just wrote, then copy it
         * into the working picture every other task reads (camera_layer.c). */
        camera_capture_post_process();

        t_end = TimeCounter_CurrentCountGet();
        application_processing_time.camera_post_processing_time_ms =
            TimeCounter_CountValueConvertToMs(t_start, t_end);

        /* Models off during a story: T_CAM keeps capturing so the picture stays live, and only
         * skips the pre-process. */
        if (!conv_models_on())
        {
            continue;
        }

        t_start = TimeCounter_CurrentCountGet();

        cam_letterbox_preprocess();

#if (BSP_CFG_DCACHE_ENABLED == 1)
        /* Clean before the neural processing unit reads what the processor wrote. */
        SCB_CleanDCache_by_Addr((uint8_t *) &model_buffer_int8[0], (int32_t) model_buffer_int8_size);
#endif

        t_end = TimeCounter_CurrentCountGet();
        application_processing_time.ai_inference_pre_processing_time_ms =
            TimeCounter_CountValueConvertToMs(t_start, t_end);

        app_sig_set(EV_AI_INPUT_READY);
    }
}
