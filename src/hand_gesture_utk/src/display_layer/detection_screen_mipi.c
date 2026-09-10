/*
* Copyright (c) 2020 - 2025 Renesas Electronics Corporation and/or its affiliates
*
* SPDX-License-Identifier: BSD-3-Clause
*/
/**********************************************************************************************************************
 * File Name    : detection_screen_mipi.c
 * Version      : .
 * Description  : The palm detection screen display on mipi lcd.
 *
 * Derived from:
 *   sample_code/ek_ra8p1_vision_palm_detection_hand_landmarkmodel_gesture_recognition_
 *   camera_LCD_FSP640/src/display_layer/detection_screen_mipi.c
 *
 * Changed against that file:
 *   The gesture label no longer latches. The sample wrote
 *            g_last_gesture[] only on a hit and never cleared it, so the last
 *            shape stayed on screen after the hand had left. The label now
 *            comes from the tracker, which clears itself. The tracker also
 *            picks the hand with the highest landmark score instead of the
 *            first filled slot.
 *   SW2 toggles a tuning overlay: the four finger ratios, the thumb-out
 *            ratio and the thumb direction, on screen and on the serial
 *            console. Off at reset.
 *********************************************************************************************************************/
/***************************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 ***************************************************************************************************************************/

#include "hal_data.h"
#include <stdio.h>

#include "common_util.h"
#include "console_output.h"

#include "camera_layer.h"
#include "display_layer.h"
#include "bg_font_18_full.h"

#include "time_counter.h"
#include "landmark_display.h"
#include "gesture_classify.h"

#include "display_layer_config.h"

#include "application_config.h"
#include "ai_application_config.h"

/***************************************************************************************************************************
 * Macro definitions
 * Refer to the application note for the physical definition of these values
 ***************************************************************************************************************************/

/***************************************************************************************************************************
 * Typedef definitions
 ***************************************************************************************************************************/

/***************************************************************************************************************************
 * Imported global variables and functions (from other files)
 ***************************************************************************************************************************/

/***************************************************************************************************************************
 * Exported global variables and functions (to be accessed by other files)
 ***************************************************************************************************************************/

void  do_detection_screen(bool ai_result_new);
uint8_t detection_count = 0;

uint8_t exe_count_print_static_text = 0;

/***************************************************************************************************************************
 * Private global variables and functions
 ***************************************************************************************************************************/
static d2_point top_left_x[AI_MAX_DETECTION_NUM];
static d2_point top_left_y[AI_MAX_DETECTION_NUM];
static d2_point bottom_right_x[AI_MAX_DETECTION_NUM];
static d2_point bottom_right_y[AI_MAX_DETECTION_NUM];

static void print_static_text (void);
static void draw_bounding_box(uint8_t i);
static void draw_landmark_points(uint8_t det_idx);
static void calculate_and_draw_bounding_box(uint8_t i);
static void display_camera_image(void);
static void print_gesture_info(void);
static void draw_tuning_overlay(void);
static void console_tuning_overlay(void);

/* ---- Step 9: tuning overlay layout ----
 * Drawn over the camera image, top-left, so it does not fight the sidebar.
 * The camera picture is 640x480 scaled by CAMERA_IMAGE_SCALING (1.25), so it
 * covers 800x600 of the 1024x600 screen and the sidebar starts at x = 820. */
#define OVERLAY_X            (10)
#define OVERLAY_Y            (8)
#define OVERLAY_LINE_PITCH   (26)
#define OVERLAY_FONT_SCALING (1.0f)
#define OVERLAY_TEXT_LEN     (24)

/*********************************************************************************************************************
 *  display_camera_image function
 *  			 This function selects the rotated camera image buffer in QVGA mode 240x320 format and
 *  			 displays in VGA 480x640 format at the center of the mipi lcd.
 *  @param   	None
 *  @retval     None.
***********************************************************************************************************************/
static void display_camera_image(void)
{
#if (BSP_CFG_DCACHE_ENABLED == 1)
    // Clean cache data for camera capture image buffer because this buffer will be accessed by DRW hardware
    SCB_CleanDCache_by_Addr(&camera_capture_image_rgb565[0], (int32_t)(camera_capture_image_rgb565_size));
#endif

    /* Specify camera input. */
    d2_setblitsrc(d2_handle, (void *)&camera_capture_image_rgb565[0], CAMERA_CAPTURE_IMAGE_WIDTH, CAMERA_CAPTURE_IMAGE_WIDTH, CAMERA_CAPTURE_IMAGE_HEIGHT, d2_mode_rgb565);

    /* display as VGA 640x480 on mipi lcd */
    d2_blitcopy(d2_handle,
                (d2_s32) CAMERA_CAPTURE_IMAGE_WIDTH, (d2_s32) CAMERA_CAPTURE_IMAGE_HEIGHT, // Source width/height
                (d2_blitpos) 0, (d2_blitpos) 0,                                          // Source position
                (d2_width) ((uint32_t)(CAMERA_CAPTURE_IMAGE_WIDTH * CAMERA_IMAGE_SCALING) << 4), (d2_width) ((uint32_t)(CAMERA_CAPTURE_IMAGE_HEIGHT * CAMERA_IMAGE_SCALING) << 4),   // Destination size width/height
                (d2_point) (0 << 4), (d2_point) (0 << 4), // Destination offset position
                d2_tm_filter);
}

/*********************************************************************************************************************
 *  draw_bounding_box function
 *  			 This function picks the index of the detection result which has a bounding box and
 *  			 uses DRW to render a red bounding box on the mipi lcd.
 *  @param[IN]   i: index of the detection result
 *  @retval     None
***********************************************************************************************************************/
static void draw_bounding_box(uint8_t i)
{
    d2_setcolor(d2_handle, 0, AI_INFERENCE_RESULT_BOUNDING_BOX_COLOR);

    d2_renderline(d2_handle, (d2_point) ((top_left_x[i]) << 4), (d2_point) ((top_left_y[i])<< 4), (d2_point) ((bottom_right_x[i]) << 4), (d2_point) ((top_left_y[i]) << 4), (d2_point) (2 << 4), 0);
    d2_renderline(d2_handle, (d2_point) ((bottom_right_x[i]) << 4), (d2_point) ((top_left_y[i]) << 4), (d2_point) ((bottom_right_x[i]) << 4), (d2_point) ((bottom_right_y[i]) << 4), (d2_point) (2 << 4), 0);
    d2_renderline(d2_handle, (d2_point) ((bottom_right_x[i]) << 4), (d2_point) ((bottom_right_y[i]) << 4), (d2_point) ((top_left_x[i]) << 4), (d2_point) ((bottom_right_y[i]) << 4), (d2_point) (2 << 4), 0);
    d2_renderline(d2_handle, (d2_point) ((top_left_x[i]) << 4), (d2_point) ((bottom_right_y[i]) << 4), (d2_point) ((top_left_x[i]) << 4), (d2_point) ((top_left_y[i]) << 4), (d2_point) (2 << 4), 0);
}

/*********************************************************************************************************************
 *  draw_landmark_points function
 *  Draws the 21 hand landmark keypoints as small green circles on the LCD.
 *  Landmark coordinates are in camera space (640x480) and are scaled to display.
 *  @param[IN]   det_idx: detection slot index
 *  @retval      None
***********************************************************************************************************************/
static void draw_landmark_points(uint8_t det_idx)
{
    const landmark_result_t* lm = &g_landmark_results[det_idx];
    if (lm->num_points <= 0 || lm->hand_score < 0.5f) return;

    /* Red color for keypoints */
    d2_setcolor(d2_handle, 0, 0x00FF0000);

    for (int k = 0; k < lm->num_points; k++)
    {
        /* Landmark points are in camera pixel coords (640x480).
         * Scale to display coords using CAMERA_IMAGE_SCALING. */
        d2_point dx = (d2_point)((float)lm->pts[k].x * CAMERA_IMAGE_SCALING);
        d2_point dy = (d2_point)((float)lm->pts[k].y * CAMERA_IMAGE_SCALING);

        /* Draw a filled circle (radius = 6 display pixels) */
        d2_rendercircle(d2_handle,
            (d2_point)(dx << 4), (d2_point)(dy << 4),
            (d2_width)(6 << 4), (d2_width)(0));
    }

    /* Draw skeleton lines connecting key joints (optional but nice visual) */
    /* Hand skeleton connections: wrist→thumb, wrist→index, etc. */
    static const int skeleton[][2] = {
        {0,1},{1,2},{2,3},{3,4},         /* thumb */
        {0,5},{5,6},{6,7},{7,8},         /* index */
        {0,9},{9,10},{10,11},{11,12},    /* middle */
        {0,13},{13,14},{14,15},{15,16},  /* ring */
        {0,17},{17,18},{18,19},{19,20},  /* pinky */
        {5,9},{9,13},{13,17}             /* palm cross */
    };

    d2_setcolor(d2_handle, 0, 0x000000FF);
    for (int s = 0; s < 23; s++)
    {
        int a = skeleton[s][0];
        int b = skeleton[s][1];
        d2_point ax = (d2_point)((float)lm->pts[a].x * CAMERA_IMAGE_SCALING);
        d2_point ay = (d2_point)((float)lm->pts[a].y * CAMERA_IMAGE_SCALING);
        d2_point bx = (d2_point)((float)lm->pts[b].x * CAMERA_IMAGE_SCALING);
        d2_point by = (d2_point)((float)lm->pts[b].y * CAMERA_IMAGE_SCALING);

        d2_renderline(d2_handle,
            (d2_point)(ax << 4), (d2_point)(ay << 4),
            (d2_point)(bx << 4), (d2_point)(by << 4),
            (d2_point)(1 << 4), 0);
    }

    /* Draw "Left" or "Right" label near the wrist (point 0) with hysteresis to avoid flicker */
    {
        static uint8_t last_lr[AI_MAX_DETECTION_NUM] = {0};  /* 0=unset, 1=Left, 2=Right */

        /* Only flip if handedness crosses a wide threshold (hysteresis).
         * NOTE: Camera image is mirrored, so invert handedness:
         *   model says >0.5 (right) → display "Left", and vice versa. */
        if (last_lr[det_idx] == 0) {
            last_lr[det_idx] = (lm->handedness > 0.5f) ? 1 : 2;
        } else if (last_lr[det_idx] == 2 && lm->handedness > 0.7f) {
            last_lr[det_idx] = 1;
        } else if (last_lr[det_idx] == 1 && lm->handedness < 0.3f) {
            last_lr[det_idx] = 2;
        }

        d2_point wx = (d2_point)((float)lm->pts[0].x * CAMERA_IMAGE_SCALING);
        d2_point wy = (d2_point)((float)lm->pts[0].y * CAMERA_IMAGE_SCALING);
        char *lr_str = (last_lr[det_idx] == 1) ? (char*)"Left" : (char*)"Right";
        print_bg_font_18(d2_handle, (int16_t)(wx - 15), (int16_t)(wy + 15),
                         DISPLAY_FONT_SCALING, lr_str);
    }

    /* The gesture is no longer worked out here. This function runs once per
     * detection slot and once per display frame, so classifying here counted
     * the same inference result many times. gesture_track_update() is called
     * once per new inference result instead, from do_detection_screen(). */
}

/*********************************************************************************************************************
 *  calculate_and_draw_bounding_box function
 *  This function takes the ai inference boundary box center of the image and scales it to the 480x640
 *  mipi lcd center area as a bounding box.
 *  @param[IN]   i: index of the detection result
 *  @retval     None
***********************************************************************************************************************/
static void calculate_and_draw_bounding_box(uint8_t i)
{
    detection_count++;

    /* m_x/y/w/h are now in camera pixel coords (640×480).
     * Just scale directly to display coords. */
    top_left_x[i]     = (d2_point)((float)g_ai_detection[i].m_x * CAMERA_IMAGE_SCALING);
    top_left_y[i]     = (d2_point)((float)g_ai_detection[i].m_y * CAMERA_IMAGE_SCALING);
    bottom_right_x[i] = (d2_point)((float)(g_ai_detection[i].m_x + g_ai_detection[i].m_w) * CAMERA_IMAGE_SCALING);
    bottom_right_y[i] = (d2_point)((float)(g_ai_detection[i].m_y + g_ai_detection[i].m_h) * CAMERA_IMAGE_SCALING);

    draw_bounding_box(i);
}

/*********************************************************************************************************************
 *  print_static_text function
 *  This function prints the static text which does not change based on inference result.
 *  @param   	None
 *  @retval     None
***********************************************************************************************************************/
static void print_static_text(void)
{
    /* show model information */
    print_bg_font_18(d2_handle, 820,  50, DISPLAY_FONT_SCALING, (char*)"Model:");
    print_bg_font_18(d2_handle, 820,  90, DISPLAY_FONT_SCALING, (char*)"Landmark &");
    print_bg_font_18(d2_handle, 820, 120, DISPLAY_FONT_SCALING, (char*)"Gesture");

    /*show pipeline time in ms*/
    print_bg_font_18(d2_handle, 820, 200, DISPLAY_FONT_SCALING, (char*)"Pipeline");
    print_bg_font_18(d2_handle, 820, 230, DISPLAY_FONT_SCALING, (char*)"time:");

    /*print the number of palms detected */
    print_bg_font_18(d2_handle, 820, 350, DISPLAY_FONT_SCALING, (char*)"No of ");
    print_bg_font_18(d2_handle, 820, 380, DISPLAY_FONT_SCALING, (char*)"Hands:");

    /*print the gesture label */
    print_bg_font_18(d2_handle, 820, 480, DISPLAY_FONT_SCALING, (char*)"Gesture:");
}

/*********************************************************************************************************************
 *  print_inf_time_and_detections function
 *  This function prints the time used in the previously finished inference and the number of palms detected.
 *  @param   	None
 *  @retval     None
***********************************************************************************************************************/
static void print_inf_time_and_detections(void)
{
       /* The inference_time is acquired in MainLoop_obj.cc.
     * This time does not include the pre and post processing routine.
     * It is the time used for inference only.
     */

    uint32_t time = (uint32_t)(application_processing_time.ai_inference_time_ms); // ms

    // Clear last draw
    print_bg_font_18(d2_handle, 820, 280, DISPLAY_FONT_SCALING,  "             ");
    print_bg_font_18(d2_handle, 820, 430, DISPLAY_FONT_SCALING,  "       ");

    // update string on display
    char time_str[8] = {'0', '0', '0', '0', ' ', 'm', 's', '\0'};
    time_str[0] += (char)(time / 1000);
    time_str[1] += (char)((time / 100) % 10);
    time_str[2] += (char)((time / 10) % 10);
    time_str[3] += (char)(time % 10);
    print_bg_font_18(d2_handle, 820, 280, DISPLAY_FONT_SCALING, (char*)time_str);

    char num_str[3] = {'0', '0', '\0'};
    num_str[0] += (char) (detection_count / 10);
    num_str[1] +=  (char) (detection_count % 10);
    print_bg_font_18(d2_handle, 820, 430, DISPLAY_FONT_SCALING, (char*)num_str);

}

/*********************************************************************************************************************
 *  print_gesture_info function
 *  Displays the current gesture name(s) in the sidebar below the hand count.
 *  @param       None
 *  @retval      None
***********************************************************************************************************************/
static void print_gesture_info(void)
{
    /* Clear previous gesture text */
    print_bg_font_18(d2_handle, 820, 520, DISPLAY_FONT_SCALING, "            ");

    /* One label, from the hand with the highest landmark score. It clears
     * itself once the hand has left, so a stale shape is never shown. */
    gesture_t g = gesture_track_stable();
    if (g < GESTURE_COUNT)
    {
        print_bg_font_18(d2_handle, 820, 520, DISPLAY_FONT_SCALING, (char*)gesture_name(g));
    }
}

/*********************************************************************************************************************
 *  gesture_text function
 *  Like gesture_name, but says "none" instead of an empty string, so a blank
 *  never looks like a missing value on the overlay.
 *  @param[IN]   g: gesture value
 *  @retval      Static string
***********************************************************************************************************************/
static const char* gesture_text(gesture_t g)
{
    return (g < GESTURE_COUNT) ? gesture_name(g) : "none";
}

/*********************************************************************************************************************
 *  format_ratio function
 *  Writes a float as text with two decimals, for example "-0.74".
 *  Done by hand on integers: the C library here is built without floating
 *  point in printf, so "%f" would not work.
 *  @param[OUT]  dst: buffer, at least 12 bytes
 *  @param[IN]   v:   value to format
 *  @retval      None
***********************************************************************************************************************/
static void format_ratio(char* dst, float v)
{
    float rounded = (v >= 0.0f) ? (v * 100.0f + 0.5f) : (v * 100.0f - 0.5f);

    /* Keep the cast inside a range int32_t can hold, whatever the landmarks do. */
    if (rounded >  99999.0f) { rounded =  99999.0f; }
    if (rounded < -99999.0f) { rounded = -99999.0f; }

    int32_t hundredths = (int32_t)rounded;
    bool    negative   = (hundredths < 0);
    if (negative) { hundredths = -hundredths; }

    sprintf(dst, "%s%d.%02d", negative ? "-" : "",
            (int)(hundredths / 100), (int)(hundredths % 100));
}

/*********************************************************************************************************************
 *  draw_tuning_overlay function
 *  Step 9. Draws the seven live test values plus the palm length and the raw
 *  per-frame answer, over the top-left of the camera image. SW2 turns it on.
 *  This is also how the sign of the thumb direction is read at bring-up.
 *  @param       None
 *  @retval      None
***********************************************************************************************************************/
static void draw_tuning_overlay(void)
{
    const gesture_metrics_t* m = gesture_track_metrics();
    char value[12];
    char line[OVERLAY_TEXT_LEN];
    int16_t y = OVERLAY_Y;

    /* ABOVE is a measurement only, used to settle the fist-versus-thumbs-up
     * question. No shape rule reads it. See gesture_classify.h. */
    static const char* labels[7] = { "IDX ", "MID ", "RNG ", "LIT ", "THUMB ", "DIR ", "ABOVE " };
    const float values[7] = { m->r_index, m->r_middle, m->r_ring, m->r_little, m->t, m->d, m->u };

    /* The glyphs are blitted RGB565 images, so d2_setcolor does not apply. */
    for (int i = 0; i < 7; i++)
    {
        format_ratio(value, values[i]);
        snprintf(line, sizeof(line), "%s%s", labels[i], value);
        print_bg_font_18(d2_handle, OVERLAY_X, y, OVERLAY_FONT_SCALING, line);
        y = (int16_t)(y + OVERLAY_LINE_PITCH);
    }

    snprintf(line, sizeof(line), "PALM %d", (int)m->s);
    print_bg_font_18(d2_handle, OVERLAY_X, y, OVERLAY_FONT_SCALING, line);
    y = (int16_t)(y + OVERLAY_LINE_PITCH);

    snprintf(line, sizeof(line), "RAW %s", m->usable ? gesture_text(gesture_track_raw()) : "no hand");
    print_bg_font_18(d2_handle, OVERLAY_X, y, OVERLAY_FONT_SCALING, line);
}

/*********************************************************************************************************************
 *  console_tuning_overlay function
 *  Step 9. Sends the same seven values to the serial console, one line per new
 *  inference result. Only while the overlay is on.
 *  @param       None
 *  @retval      None
***********************************************************************************************************************/
static void console_tuning_overlay(void)
{
    const gesture_metrics_t* m = gesture_track_metrics();
    char idx[12], mid[12], rng[12], lit[12], thumb[12], dir[12], above[12];

    format_ratio(idx,   m->r_index);
    format_ratio(mid,   m->r_middle);
    format_ratio(rng,   m->r_ring);
    format_ratio(lit,   m->r_little);
    format_ratio(thumb, m->t);
    format_ratio(dir,   m->d);
    format_ratio(above, m->u);

    sprintf(sprintf_buffer,
            "  Gesture tuning: idx=%s mid=%s rng=%s lit=%s thumb=%s dir=%s above=%s palm=%d raw=%s steady=%s\r\n",
            idx, mid, rng, lit, thumb, dir, above, (int)m->s,
            m->usable ? gesture_text(gesture_track_raw()) : "no hand",
            gesture_text(gesture_track_stable()));
    print_to_console(sprintf_buffer);
}

/*********************************************************************************************************************
 *  do_detection_screen function: display the camera image and palm detection result on the mipi lcd
 *  @param       None
 *  @retval      None
***********************************************************************************************************************/
void  do_detection_screen(bool ai_result_new)
{
    vision_ai_app_err_t vision_ai_status = VISION_AI_APP_SUCCESS;

    if(!(xEventGroupGetBits(g_ai_app_event) & DISPLAY_PAUSE))
    {
        /* Clear stale vsync flag, then wait for the NEXT vsync so the
         * GLCDC has finished scanning the front-buffer before we touch it. */
        xEventGroupClearBits(g_ai_app_event, GLCDC_VSYNC);
        xEventGroupWaitBits(g_ai_app_event, GLCDC_VSYNC, pdTRUE, pdTRUE, portMAX_DELAY);

        graphics_start_frame();

        /* show static information */
        display_camera_image();

        /* Print static test */
        if(exe_count_print_static_text < 2)
        {
            print_static_text();
            exe_count_print_static_text++;
        }

        /* if a new inference has finished, update the detection result: bounding box and number of palms */
        if(ai_result_new)
        {
            detection_count = 0;
            for(uint8_t i = 0; i < AI_MAX_DETECTION_NUM; i++)
            {
                if((g_ai_detection[i].m_x != 0) && (g_ai_detection[i].m_y != 0))
                {
                    detection_count++;
                }
                /* Draw landmark keypoints if available */
                draw_landmark_points(i);
            }

            /* Work out the gesture once per NEW inference result. The display
             * runs faster than the models, so doing this per frame would feed
             * the same result to the steady filter again and again. */
            gesture_track_update(g_landmark_results, AI_MAX_DETECTION_NUM);

            if(g_gesture_overlay_on)
            {
                console_tuning_overlay();
            }
        }
        else
        {
            /* Human movement is slower than the mipi lcd refresh rate. Keep the previous bounding box until a new inference is finished. */
            for(uint8_t i = 0; i < AI_MAX_DETECTION_NUM; i++)
            {
                draw_landmark_points(i);
            }
        }

        print_inf_time_and_detections();
        print_gesture_info();

        if(g_gesture_overlay_on)
        {
            draw_tuning_overlay();
        }

        /* Wait for previous frame rendering to finish, then finalize this frame and flip the buffers */
        graphics_end_frame();
    }
}
