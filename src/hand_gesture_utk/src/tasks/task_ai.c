/**********************************************************************************************************************
 * File Name    : task_ai.c
 * Description  : T_AI - palm model, landmark model, gesture rules, the gesture event.
 *
 * New file.
 *
 * DERIVED FROM: output_code\hand_gesture_five\src\ai_inference_thread_entry.c (itself from
 * sample_code\ek_ra8p1_vision_palm_detection_hand_landmarkmodel_gesture_recognition_camera_
 * LCD_FSP640\src\ai_inference_thread_entry.c). The inference half is unchanged. Four things
 * are new:
 *   the signalling goes through app_sig_*
 *   the start-up Octo-SPI latch is read before the model is opened
 *   gesture_track_update is called once per NEW result
 *   the first input after the models come back on is discarded
 *
 * WHEN THIS FILE GOES LIVE, ai_inference_thread_entry.c MUST LEAVE THE BUILD. Both files
 * define model_buffer_int8, g_ai_detection, g_landmark_results, update_detection_result and
 * update_landmark_result, so keeping both is a duplicate-symbol link error.
 *
 * Stage 1 shape: entry function of the configurator Thread object "ai_thread", FreeRTOS
 * priority 3, stack 16,384 bytes.
 *********************************************************************************************************************/

#include <stdio.h>
#include <string.h>

/* For the INT type in the task entry signature below. This file is C, so the micro T-Kernel
 * header compiles here - unlike llama4micro.cpp. */
#include <tk/tkernel.h>

#include "common_util.h"
#include "common_data.h"
#include "application_config.h"
#include "ai_application_config.h"

#include "camera_layer.h"
#include "camera_utils.h"
#include "console_output.h"
#include "time_counter.h"

#include "landmark_display.h"
#include "gesture_classify.h"

#include "app_signal.h"
#include "conv_state.h"

/* The vendor header that registers the TensorFlow Lite Micro log callback. Same include the
 * fork makes, and it must stay below the others because it is C++-flavoured plain C. */
#include "tensorflow/lite/micro/cortex_m_generic/debug_log_callback.h"

/**********************************************************************************************************************
 * Imported global variables and functions (from other files)
 *********************************************************************************************************************/

/* The whole palm plus landmark pipeline. src\hand_gestures\palm_detection\MainLoop_obj.cc. */
extern vision_ai_app_err_t palm_detection(void);

/**********************************************************************************************************************
 * Exported global variables and functions (to be accessed by other files)
 *********************************************************************************************************************/

/* The model's input picture. T_CAM fills it, T_AI reads it. Placed by the same switch the fork
 * uses, which keeps it in synchronous dynamic random access memory. */
#if (AI_INPUT_IMAGE_ALLOCATION == ALLOCATE_TO_ONCHIP_RAM)
int8_t model_buffer_int8[AI_INPUT_IMAGE_WIDTH * AI_INPUT_IMAGE_HEIGHT * AI_INPUT_IMAGE_BYTE_PER_PIXEL] BSP_ALIGN_VARIABLE(32);
#elif (AI_INPUT_IMAGE_ALLOCATION == ALLOCATE_TO_SDRAM)
int8_t model_buffer_int8[AI_INPUT_IMAGE_WIDTH * AI_INPUT_IMAGE_HEIGHT * AI_INPUT_IMAGE_BYTE_PER_PIXEL] BSP_PLACE_IN_SECTION(".sdram")  BSP_ALIGN_VARIABLE(32);
#else
#error "Add your preferred buffer definition"
#endif
uint32_t model_buffer_int8_size = sizeof(model_buffer_int8);

/* The published result: one box and one set of 21 joints per detection slot. T_AI writes,
 * T_UI reads. The data travels beside the flag. */
st_ai_detection_point_t g_ai_detection[AI_MAX_DETECTION_NUM]     = {};
landmark_result_t       g_landmark_results[AI_MAX_DETECTION_NUM] = {};

void update_detection_result(uint16_t index, signed short x, signed short y, signed short w, signed short h);
void update_landmark_result(uint16_t det_index, const landmark_result_t * lm);

/* True while palm_detection() is running, which is the whole window in which the two shared
 * picture buffers must not move.
 * ONE WRITER (this task) and ONE READER (T_CAM), one aligned word, so no lock is needed - the
 * same argument story_text.c makes. Declared in src\common_util.h. */
volatile bool g_ai_reading_frame = false;

/* src\app_main.c declares this and passes it to tk_cre_tsk(). The prototype is here as well
 * so the file builds on its own with -Wmissing-declarations. */
void ai_thread_entry(INT stacd, void * exinf);

/**********************************************************************************************************************
 * Private global variables and functions
 *********************************************************************************************************************/

/* T_AI's two rows of the wait table. */
#define AI_INIT_WAIT_MS       (5000U)
#define AI_INPUT_WAIT_MS      (500U)

/* Ten failed invokes in a row disable the models. */
#define AI_FAIL_LIMIT         (10U)

static uint32_t g_ai_input_timeouts = 0U;
static uint32_t g_ai_fail_run       = 0U;
static bool     g_ai_models_open    = false;

static void ai_print_log(const char * s);

/**********************************************************************************************************************
 * TensorFlow Lite Micro sends its own messages here. Unchanged from the fork.
 * @param[in] s  one message, with no line ending
 *********************************************************************************************************************/
static void ai_print_log(const char * s)
{
    printf("%s\n", s);
}

/**********************************************************************************************************************
 * Publish one detection box. Called from inside the pipeline.
 *********************************************************************************************************************/
void update_detection_result(uint16_t index, signed short x, signed short y, signed short w, signed short h)
{
    if (index < AI_MAX_DETECTION_NUM)
    {
        g_ai_detection[index].m_x = x;
        g_ai_detection[index].m_y = y;
        g_ai_detection[index].m_w = w;
        g_ai_detection[index].m_h = h;
    }
}

/**********************************************************************************************************************
 * Publish one hand's 21 joints. Called from inside the pipeline.
 *********************************************************************************************************************/
void update_landmark_result(uint16_t det_index, const landmark_result_t * lm)
{
    if ((det_index < AI_MAX_DETECTION_NUM) && (lm != NULL))
    {
        memcpy(&g_landmark_results[det_index], lm, sizeof(landmark_result_t));
    }
}

/**********************************************************************************************************************
 * T_AI entry.
 * @param[in] stacd  the start code passed to tk_sta_tsk(). Not used.
 * @param[in] exinf  the T_CTSK extended information. Not used.
 *
 * The kernel calls the task through T_CTSK.task, which takes two arguments instead of one.
 * THE BODY BELOW IS UNCHANGED.
 *********************************************************************************************************************/
void ai_thread_entry(INT stacd, void * exinf)
{
    app_sig_bits_t      pattern;
    vision_ai_app_err_t vision_ai_status = VISION_AI_APP_SUCCESS;
    uint32_t            t_start;
    uint32_t            t_end;

    /* The first input that arrives after the models come back on is stale, because T_CAM built
     * it before the flag moved. It is thrown away and the task runs on the next one. One
     * boolean, and it starts true so the very first frame after start-up is not judged
     * either. */
    bool     discard_next  = true;
    bool     models_were_on = false;

    FSP_PARAMETER_NOT_USED(stacd);
    FSP_PARAMETER_NOT_USED(exinf);

    /* The only two-bit AND-wait in the demo, and it is safe: both bits are set once and stay
     * set. Neither is cleared, because more than one task reads each of them. */
    pattern = app_sig_wait(EV_INIT_DISPLAY | EV_INIT_CAMERA, false, AI_INIT_WAIT_MS);

    if (!app_sig_released(pattern, EV_INIT_DISPLAY | EV_INIT_CAMERA))
    {
        /* Either the panel or the camera never came up. The demo stays alive with whatever did
         * start, so this is reported and not fatal. */
        print_to_console("T_AI TIMED OUT WAITING FOR THE DISPLAY AND CAMERA\r\n");
    }

    /* With the external flash unopened, both model images are garbage in memory, so the models
     * are never opened and no inference is ever run. */
    if (FSP_SUCCESS != g_startup_ospi_err)
    {
        print_to_console("T_AI SKIPS THE MODELS - STORAGE DID NOT OPEN\r\n");
    }
    else if (FSP_SUCCESS != RM_ETHOSU_Open(&g_rm_ethosu0_ctrl, &g_rm_ethosu0_cfg))
    {
        /* The models are disabled, the camera picture stays live and the text strip says so.
         * EV_INIT_NPU is not set, and T_UI reads that as "no models". */
        (void) handle_error(VISION_AI_APP_ERR_AI_INIT, "T_AI RM_ETHOSU_Open");
    }
    else
    {
        RegisterDebugLogCallback(ai_print_log);
        g_ai_models_open = true;
        app_sig_set(EV_INIT_NPU);
    }

    while (true)
    {
        /* One letterbox picture from T_CAM. Cleared by the wait, because only T_AI reads it. */
        pattern = app_sig_wait(EV_AI_INPUT_READY, true, AI_INPUT_WAIT_MS);

        if (!app_sig_released(pattern, EV_AI_INPUT_READY))
        {
            /* T_CAM is not producing. It reports the camera side itself, so this is only
             * counted here. */
            g_ai_input_timeouts++;
            continue;
        }

        /* Watch the models-on flag ourselves rather than being told, so the rule holds however
         * the flag moved. Off to on arms the discard. */
        {
            const bool models_on_now = conv_models_on();

            if (models_on_now && !models_were_on)
            {
                discard_next = true;
            }

            models_were_on = models_on_now;

            if (!models_on_now || !g_ai_models_open)
            {
                /* Models off during a story, or never opened at all. Nothing to do; T_CAM is
                 * not even building input in the first case. */
                continue;
            }
        }

        if (discard_next)
        {
            discard_next = false;
            continue;
        }

        /* Start each inference from a clean result set, so a hand that has left the view does
         * not leave its last joints on the screen. */
        for (int i = 0; i < AI_MAX_DETECTION_NUM; i++)
        {
            memset(&g_ai_detection[i], 0, sizeof(g_ai_detection[i]));
            memset(&g_landmark_results[i], 0, sizeof(g_landmark_results[i]));
        }

        t_start = TimeCounter_CurrentCountGet();

        INFERENCE_START_INDICATE_LED;

        /* THIS FLAG IS THE WHOLE FIX, and it must be set BEFORE palm_detection and cleared
         * AFTER it. T_CAM is priority 4 and this task is priority 3, so T_CAM preempts an
         * inference at will, and palm_detection reads BOTH shared buffers: model_buffer_int8 at
         * its start (MainLoop_obj.cc:86) and camera_capture_image_rgb565 much later, for the
         * landmark crop (MainLoop_obj.cc:324). Without this the landmark model can crop a
         * different frame from the one the palm box was measured on, and the joints land off
         * the hand.
         *
         * A flag was chosen over swapping the two priorities. The picture then updates at the
         * inference rate while a model runs - the same thing the vision sample got by
         * construction from its priority order. */
        g_ai_reading_frame = true;
        vision_ai_status = palm_detection();
        g_ai_reading_frame = false;

        INFERENCE_END_INDICATE_LED;

        t_end = TimeCounter_CurrentCountGet();
        application_processing_time.ai_inference_time_ms = TimeCounter_CountValueConvertToMs(t_start, t_end);

        if (VISION_AI_APP_ERR_AI_INFERENCE == vision_ai_status)
        {
            /* One failure is ignored; ten in a row disable the models. */
            g_ai_fail_run++;

            if (AI_FAIL_LIMIT <= g_ai_fail_run)
            {
                g_ai_models_open = false;
                (void) handle_error(VISION_AI_APP_ERR_AI_INFERENCE, "T_AI palm_detection");
            }

            continue;
        }

        g_ai_fail_run = 0U;

        /* Once per NEW inference result, never once per drawn frame, or the same result is
         * counted again and the three-in-a-row filter does nothing. */
        gesture_track_update(g_landmark_results, AI_MAX_DETECTION_NUM);

        app_sig_set(EV_AI_RESULT);
    }
}
