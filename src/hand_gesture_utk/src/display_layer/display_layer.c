/*
* Copyright (c) 2020 - 2025 Renesas Electronics Corporation and/or its affiliates
*
* SPDX-License-Identifier: BSD-3-Clause
*/
/**********************************************************************************************************************
 * File Name    : dsi_layer.c
 * Version      : .
 * Description  : .
 *********************************************************************************************************************/
#include "hal_data.h"
#include "display_layer.h"
#include "common_util.h"
#include "app_signal.h"

#include "time_counter.h"

#include "display_layer_config.h"

/* For app_os_delay_ms(). See graphics_wait_dlist_idle() below. */
#include "app_os.h"

static uint8_t drw_buf = 1;

/* How many times the graphics controller refused a buffer change. A failure is counted, never
 * ignored. It is read with a debugger, like g_ui_vsync_timeouts in src\tasks\task_ui.c. It
 * must stay at 0 once the panel is running; see graphics_swap_buffer.
 *
 * VOLATILE IS LOAD-BEARING: this object is static, its address is never taken, and no code in
 * the project ever reads it - only the store in graphics_swap_buffer. That makes it a
 * store-only internal object, which a compiler may legally delete together with its stores,
 * and this file is built at -Os, so the optimiser is running. Without volatile the counter
 * could vanish and then read as "nothing to see" instead
 * of "not measured", which is the worst thing a diagnostic can do. g_ui_vsync_timeouts in
 * src\tasks\task_ui.c is volatile for the same reason; keep the pair the same. */
static volatile uint32_t glcdc_present_refused = 0U;

FSP_CPP_HEADER
static fsp_err_t fill_w_color_rgb565(uint8_t * buff,
                                     uint32_t screen_w, uint32_t screen_h,
                                     uint32_t write_area_w_start, uint32_t write_area_h_start,
                                     uint32_t write_area_w, uint32_t write_area_h,
                                     uint16_t color);
FSP_CPP_FOOTER

fsp_err_t drw_init(void)
{
    /* Initialize D/AVE 2D driver.
     *
     * BOTH RESULTS ARE NOW CHECKED. The vision sample ignored them, so a drawing engine that
     * never opened showed up later as a blank screen with no message.
     * d2_opendevice returns NULL on failure and d2_inithw returns D2_OK on success
     * (ra\dave2d\inc\dave_driver.h). A failure here is APP_FATAL: nothing can be drawn. */
     d2_handle = d2_opendevice(0);

     if (NULL == d2_handle)
     {
         (void) handle_error(VISION_AI_APP_ERR_DRW_INIT, "drw_init d2_opendevice");
         return FSP_ERR_NOT_OPEN;
     }

     if (D2_OK != d2_inithw(d2_handle, 0))
     {
         (void) handle_error(VISION_AI_APP_ERR_DRW_INIT, "drw_init d2_inithw");
         return FSP_ERR_NOT_INITIALIZED;
     }

     /* Clear both buffers */
     d2_framebuffer(d2_handle, fb_background, DISPLAY_SCREEN_WIDTH, DISPLAY_SCREEN_WIDTH, DISPLAY_SCREEN_HEIGHT * DISPLAY_SCREEN_BUFF_NUMBER, DISPLAY_SCREEN_BUFF_D2_COLOR_CODE);

     /* Set various D2 parameters */
     d2_setblendmode(d2_handle, d2_bm_alpha, d2_bm_one_minus_alpha);
     d2_setalphamode(d2_handle, d2_am_constant);
     d2_setalpha(d2_handle, 0xff);
     d2_setantialiasing(d2_handle, 1);
     d2_setlinecap(d2_handle, d2_lc_butt);
     d2_setlinejoin(d2_handle, d2_lj_none);

     return FSP_SUCCESS;
}

fsp_err_t display_init(void)
{
    fsp_err_t fsp_status = FSP_SUCCESS;

    /* open MIPI DSI Interface */
    fsp_status = R_GLCDC_Open(&g_lcd_glcdc_ctrl, &g_lcd_glcdc_cfg);
    if (FSP_SUCCESS != fsp_status)
    {
        /* APP_FATAL. The fork's ERROR_INDICATE was LED3 plus BKPT #0, and a
         * breakpoint with no debugger attached is a dead board. handle_error prints, lights LED3
         * and stays in the slow blink loop. It does not return. */
        (void) handle_error(VISION_AI_APP_ERR_GLCDC_OPEN, "display_init R_GLCDC_Open");
    }

    /* NOTE: cannot send commands for 5 ms after HW reset */
    R_BSP_SoftwareDelay(5, BSP_DELAY_UNITS_MILLISECONDS);

    /* Note: When video is started, timing of any further messages must carefully controlled to not interfere with transmission. */
    fsp_status = R_GLCDC_Start(&g_lcd_glcdc_ctrl);
    if (FSP_SUCCESS != fsp_status)
    {
        (void) handle_error(VISION_AI_APP_ERR_GLCDC_START, "display_init R_GLCDC_Start");
    }

    /* Wait for the LCD to display valid data */
    /* Note: Please update wait periods according to LCD controller specification */
    R_BSP_SoftwareDelay(200, BSP_DELAY_UNITS_MILLISECONDS);

    /* Switch GLCDC display buffer */
    R_GLCDC_BufferChange(&g_lcd_glcdc_ctrl, &fb_background[0][0], DISPLAY_FRAME_LAYER_1);

    /* Enable the back light */
    R_IOPORT_PinWrite(&g_ioport_ctrl, LCD_BLEN, BSP_IO_LEVEL_HIGH);

    return FSP_SUCCESS;
}

void display_image_buffer_initialize(void)
{
#if 1
    /* Clear the buffer with 0x0 (black) */
    memset(&fb_background[0][0], 0x0, sizeof(fb_background)/2);
    memset(&fb_background[1][0], 0x0, sizeof(fb_background)/2);
#endif
#if 0
    /* Fill with Renesas blue */
    fill_w_color_rgb565(&fb_background[0][0], DISPLAY_HSIZE_INPUT0, DISPLAY_VSIZE_INPUT0,   0,   0, DISPLAY_HSIZE_INPUT0, DISPLAY_VSIZE_INPUT0, 0x2953); // Renesas blue
    fill_w_color_rgb565(&fb_background[1][0], DISPLAY_HSIZE_INPUT0, DISPLAY_VSIZE_INPUT0,   0,   0, DISPLAY_HSIZE_INPUT0, DISPLAY_VSIZE_INPUT0, 0x2953); // Renesas blue
#endif
#if 0
    /* Fill with color bar */
    fill_w_color_rgb565(&fb_background[0][0], DISPLAY_HSIZE_INPUT0, DISPLAY_VSIZE_INPUT0, (DISPLAY_HSIZE_INPUT0/8)*0, 0, (DISPLAY_HSIZE_INPUT0/8), DISPLAY_VSIZE_INPUT0, 0xF800); // Red
    fill_w_color_rgb565(&fb_background[0][0], DISPLAY_HSIZE_INPUT0, DISPLAY_VSIZE_INPUT0, (DISPLAY_HSIZE_INPUT0/8)*1, 0, (DISPLAY_HSIZE_INPUT0/8), DISPLAY_VSIZE_INPUT0, 0x07E0); // Green
    fill_w_color_rgb565(&fb_background[0][0], DISPLAY_HSIZE_INPUT0, DISPLAY_VSIZE_INPUT0, (DISPLAY_HSIZE_INPUT0/8)*2, 0, (DISPLAY_HSIZE_INPUT0/8), DISPLAY_VSIZE_INPUT0, 0x001F); // Blue
    fill_w_color_rgb565(&fb_background[0][0], DISPLAY_HSIZE_INPUT0, DISPLAY_VSIZE_INPUT0, (DISPLAY_HSIZE_INPUT0/8)*3, 0, (DISPLAY_HSIZE_INPUT0/8), DISPLAY_VSIZE_INPUT0, 0xFFE0); // Yellow
    fill_w_color_rgb565(&fb_background[0][0], DISPLAY_HSIZE_INPUT0, DISPLAY_VSIZE_INPUT0, (DISPLAY_HSIZE_INPUT0/8)*4, 0, (DISPLAY_HSIZE_INPUT0/8), DISPLAY_VSIZE_INPUT0, 0x780F); // Purple
    fill_w_color_rgb565(&fb_background[0][0], DISPLAY_HSIZE_INPUT0, DISPLAY_VSIZE_INPUT0, (DISPLAY_HSIZE_INPUT0/8)*5, 0, (DISPLAY_HSIZE_INPUT0/8), DISPLAY_VSIZE_INPUT0, 0x07FF); // Cyan
    fill_w_color_rgb565(&fb_background[0][0], DISPLAY_HSIZE_INPUT0, DISPLAY_VSIZE_INPUT0, (DISPLAY_HSIZE_INPUT0/8)*6, 0, (DISPLAY_HSIZE_INPUT0/8), DISPLAY_VSIZE_INPUT0, 0x0000); // Write
    fill_w_color_rgb565(&fb_background[0][0], DISPLAY_HSIZE_INPUT0, DISPLAY_VSIZE_INPUT0, (DISPLAY_HSIZE_INPUT0/8)*7, 0, (DISPLAY_HSIZE_INPUT0/8), DISPLAY_VSIZE_INPUT0, 0xFFFF); // Black

    fill_w_color_rgb565(&fb_background[1][0], DISPLAY_HSIZE_INPUT0, DISPLAY_VSIZE_INPUT0, 0, (DISPLAY_VSIZE_INPUT0/8)*0, DISPLAY_HSIZE_INPUT0, (DISPLAY_VSIZE_INPUT0/8), 0xF800); // Red
    fill_w_color_rgb565(&fb_background[1][0], DISPLAY_HSIZE_INPUT0, DISPLAY_VSIZE_INPUT0, 0, (DISPLAY_VSIZE_INPUT0/8)*1, DISPLAY_HSIZE_INPUT0, (DISPLAY_VSIZE_INPUT0/8), 0x07E0); // Green
    fill_w_color_rgb565(&fb_background[1][0], DISPLAY_HSIZE_INPUT0, DISPLAY_VSIZE_INPUT0, 0, (DISPLAY_VSIZE_INPUT0/8)*2, DISPLAY_HSIZE_INPUT0, (DISPLAY_VSIZE_INPUT0/8), 0x001F); // Blue
    fill_w_color_rgb565(&fb_background[1][0], DISPLAY_HSIZE_INPUT0, DISPLAY_VSIZE_INPUT0, 0, (DISPLAY_VSIZE_INPUT0/8)*3, DISPLAY_HSIZE_INPUT0, (DISPLAY_VSIZE_INPUT0/8), 0xFFE0); // Yellow
    fill_w_color_rgb565(&fb_background[1][0], DISPLAY_HSIZE_INPUT0, DISPLAY_VSIZE_INPUT0, 0, (DISPLAY_VSIZE_INPUT0/8)*4, DISPLAY_HSIZE_INPUT0, (DISPLAY_VSIZE_INPUT0/8), 0x780F); // Purple
    fill_w_color_rgb565(&fb_background[1][0], DISPLAY_HSIZE_INPUT0, DISPLAY_VSIZE_INPUT0, 0, (DISPLAY_VSIZE_INPUT0/8)*5, DISPLAY_HSIZE_INPUT0, (DISPLAY_VSIZE_INPUT0/8), 0x07FF); // Cyan
    fill_w_color_rgb565(&fb_background[1][0], DISPLAY_HSIZE_INPUT0, DISPLAY_VSIZE_INPUT0, 0, (DISPLAY_VSIZE_INPUT0/8)*6, DISPLAY_HSIZE_INPUT0, (DISPLAY_VSIZE_INPUT0/8), 0x0000); // Write
    fill_w_color_rgb565(&fb_background[1][0], DISPLAY_HSIZE_INPUT0, DISPLAY_VSIZE_INPUT0, 0, (DISPLAY_VSIZE_INPUT0/8)*7, DISPLAY_HSIZE_INPUT0, (DISPLAY_VSIZE_INPUT0/8), 0xFFFF); // Black
#endif

#if (BSP_CFG_DCACHE_ENABLED == 1)
    SCB_CleanDCache_by_Addr((uint8_t *)&fb_background[0][0], (int32_t)sizeof(fb_background)/2);
    SCB_CleanDCache_by_Addr((uint8_t *)&fb_background[1][0], (int32_t)sizeof(fb_background)/2);
#endif
}

fsp_err_t fill_w_color_rgb565(uint8_t * buff,
                              uint32_t screen_w, uint32_t screen_h,
                              uint32_t write_area_w_start, uint32_t write_area_h_start,
                              uint32_t write_area_w, uint32_t write_area_h,
                              uint16_t color)
{
    if(screen_w < write_area_w_start)
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    if(screen_w < write_area_w_start + write_area_w)
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    if(screen_h < write_area_h_start)
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    if(screen_h < write_area_h_start + write_area_h)
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    uint16_t * buff_start_address = (uint16_t *) buff;

    for(uint32_t i=write_area_h_start; i<write_area_h_start+write_area_h; i++)
    {
        for(uint32_t j=write_area_w_start; j< write_area_w_start+write_area_w; j++)
        {
            *(buff_start_address + (i * screen_w) + j) = color;
        }
    }

    return FSP_SUCCESS;
}

static uint32_t last_lcd_glcdc_frame_end = 0;

/**********************************************************************************************************************
 * Graphics controller line-detect interrupt: one panel refresh.
 *
 * The bit name changes and the operating-system call moves behind app_sig_set_isr. The
 * frame-period measurement stays.
 *
 * THE DEAD 120-FRAME COUNTER IS DELETED. It counted 120 vertical synchronisation periods and
 * then called mipi_dsi_enable_backlight(), which does not exist in this project - the only
 * commented-out copies are in the vendor sample and in the fork output_code\hand_gesture_five,
 * not in this file and not anywhere under src\. So the counter really did nothing at all.
 *
 * THE PANEL IS NOT LIT "WITHOUT A BACKLIGHT". It is lit by the LCD_BLEN pin write in
 * display_init() above (R_IOPORT_PinWrite at display_layer.c:103, in this same file), not by
 * any display-interface command. DO NOT REMOVE THAT LINE: an earlier wording here was the
 * twin of the sentence already corrected in src\tasks\task_ui.c, and it invited the same
 * failure - someone deletes the pin write, gets a dark panel, and hunts a graphics-controller
 * fault.
 *********************************************************************************************************************/
void lcd_glcdc_callback(display_callback_args_t *p_args)
{
    if (NULL == p_args)
    {
        return;
    }

    if (DISPLAY_EVENT_LINE_DETECTION & p_args->event )
    {
        application_processing_time.lcd_display_update_refresh_ms = TimeCounter_CountValueConvertToMs(last_lcd_glcdc_frame_end, TimeCounter_CurrentCountGet());
        last_lcd_glcdc_frame_end = TimeCounter_CurrentCountGet();

        app_sig_set_isr(EV_VSYNC);
    }
}

/*********************************************************************************************************************
 *  System uses double buffer. Show the finished buffer and move drawing to the other one.
 *  @param[IN]   None
 *  @retval      None
 *
 * ONE FRAME, END TO END. Read this before changing the two lines below.
 * ---------------------------------------------------------------------
 * The drawing engine does NOT draw while T_UI calls it. It records commands into a list, and
 * the list is executed one whole frame later. From its own source:
 *   - d2_startframe "start rendering of last frame's writelist"  (ra\tes\dave2d\src\dave_rbuffer.c:568-590)
 *   - d2_endframe   "wait for rendering to end", d2_flushframe   (:611-620, and dave_driver.c:1140-1153)
 * So inside one call of ui_draw_frame the hardware is drawing the PREVIOUS frame, and the calls
 * made in this frame are only queued.
 *
 * Call the buffer index used while frame N is recorded b(N). The list of frame N therefore
 * paints into fb_background[b(N)], and it paints it during frame N+1. drw_buf flips exactly once
 * per frame, so b(N+1) = b(N-1).
 *
 * At this point in frame N, d2_endframe has just waited for the list of frame N-1, which painted
 * fb_background[b(N-1)]. That buffer is complete. The flip below turns drw_buf into
 * b(N+1) = b(N-1), so the address handed to the graphics controller IS that finished buffer, and
 * the buffer the next list will paint is the other one, which the panel is no longer showing.
 *
 * THE FLIP MUST STAY BEFORE THE BUFFER CHANGE. Showing fb_background[b(N)] instead - the index
 * before the flip - would show a buffer that has not been painted yet, and the panel would then
 * display frame N's drawing as it happens: every zone clear and every text line, live. That is
 * the flicker, not the cure for it.
 *
 * WHY THE VSYNC BIT IS CLEARED HERE (this is the bench fix of 2026-08-14).
 * -----------------------------------------------------------------------
 * The change asked for below takes effect on the next Vsync, and the graphics controller REFUSES
 * a second change while the first one is still pending: R_GLCDC_BufferChange returns
 * FSP_ERR_INVALID_UPDATE_TIMING when GR[layer].VEN.PVEN is still 1
 * (ra\fsp\src\r_glcdc\r_glcdc.c:658-660), and it then changes nothing.
 *
 * T_UI waits for EV_VSYNC once per frame, and that wait clears the bit on release. But the line
 * detect interrupt keeps arriving while T_UI is drawing - one drawing pass easily outlasts a
 * 16.7 ms panel refresh, above all when T_CAM at priority 4 and T_AI at priority 3 preempt it.
 * The bit is then already set when T_UI comes back round, so the next wait returns AT ONCE and a
 * second frame is drawn and offered inside the same refresh. The second change is refused, the
 * panel keeps the older address, and the frame after it is painted straight into the buffer the
 * panel is scanning. That is one fully visible drawing pass per refused change.
 *
 * Clearing EV_VSYNC here, at the moment the change is asked for, means the next wait must see a
 * line detect that comes AFTER this call. The interrupt fires at line 631 of 635
 * (r_glcdc.c:421-424: back porch 30 + 600 active + 1; vtiming.total_cyc is 635 in
 * ra_gen\common_data.c), so it is 4 lines - about 105 microseconds - before the Vsync that
 * latches the change. One drawing pass takes milliseconds, far longer than that, so the next
 * call of this function always lands after the latch and is always accepted.
 *
 * The cost is at most one extra refresh of latency when a line detect falls in that small window.
 * A refusal is not fatal if one ever happens: the count below records it, and the swap heals
 * itself on the frame after next, because the flip continues and the next address offered is the
 * one the controller is already showing.
 *
 * NOT CHANGED, ON PURPOSE: d1_cacheflush below cleans fb_background[drw_buf]. It runs while a
 * frame is being recorded, before this function flips, so it still names the buffer being drawn.
***********************************************************************************************************************/
void graphics_swap_buffer()
{
    fsp_err_t err;

    drw_buf = (drw_buf == 0) ? 1 : 0;

    /* Update the layer to display in the GLCDC (will be set on next Vsync) */
    err = R_GLCDC_BufferChange(g_lcd_glcdc.p_ctrl, fb_background[drw_buf], DISPLAY_FRAME_LAYER_1);

    if (FSP_SUCCESS != err)
    {
        glcdc_present_refused++;
    }

    /* A line detect that arrived while this frame was being drawn must not release the next
     * wait. See the note above: it is what made the panel flicker. */
    app_sig_clear(EV_VSYNC);
}

/*********************************************************************************************************************
 *  Get pointer address of backup buffer
 *  @param[IN]   None
 *  @retval      the address of the buffer that is NOT the current drawing target
 *
 * NOTHING IN THIS PROJECT CALLS THIS. It is kept because it is part of the display layer this
 * demo inherited from the vendor sample. Its meaning did not move with the 2026-08-14 flicker
 * fix, because the index order in graphics_swap_buffer did not move either: between one swap and
 * the next, drw_buf is the buffer the next list will paint, so the index returned here is the
 * buffer the panel is scanning. A caller that wants the finished picture wants this one; a caller
 * that wants the buffer being painted wants fb_background[drw_buf] instead.
***********************************************************************************************************************/
uint32_t graphics_backup_buffer_pointer_get(void)
{
    uint8_t backup_buffer = (drw_buf == 0) ? 1 : 0;

    return (uint32_t)&fb_background[backup_buffer][0];
}

/*********************************************************************************************************************
 *  Start a new frame with the current draw buffer.
 *  @param[IN]   None
 *  @retval      None
 *
 * The two calls must stay in this order. d2_startframe closes the list of the previous frame and
 * starts the hardware on it, and only then may d2_framebuffer change the target for the list this
 * frame is about to record (ra\tes\dave2d\src\dave_rbuffer.c:568-590, dave_viewport.c:196-210:
 * d2_framebuffer sets device state that later commands are recorded with). Calling d2_framebuffer
 * first would send the previous frame's drawing to the wrong buffer.
***********************************************************************************************************************/
void graphics_start_frame()
{
    /* Start a new display list */
    d2_startframe(d2_handle);

    /* Set the new buffer to the current draw buffer */
    d2_framebuffer(d2_handle, &(fb_background[drw_buf][0]), DISPLAY_SCREEN_WIDTH, DISPLAY_SCREEN_WIDTH, DISPLAY_SCREEN_HEIGHT, DISPLAY_SCREEN_BUFF_D2_COLOR_CODE);
}

/*********************************************************************************************************************
 *  Wait the current frame to end and swap the framebuffer
 *  @param[IN]   None
 *  @retval      None
***********************************************************************************************************************/
/**********************************************************************************************************************
 * WAIT FOR THE 2D ENGINE WITHOUT BURNING THE PROCESSOR.
 *
 * NEW, added 2026-08-21 after the port was found running much slower than the source demo.
 *
 * WHAT GOES WRONG WITHOUT IT.
 * d2_endframe waits for the previous frame's display list through d2_flushframe -> d2hw_finish
 * (ra\tes\dave2d\src\dave_hardware.c:107-131), which loops on the D2_STATUS busy bit and calls
 * d1_queryirq(hwId, d1_irq_dlist, 200) inside the loop. Under FreeRTOS that call was
 * xSemaphoreTake(g_d1_queryirq_sem, 200) - T_UI BLOCKED and the processor went to the other
 * tasks (ra\fsp\src\r_drw\r_drw_irq.c:155-166). With BSP_CFG_RTOS at 0 the same call becomes
 *     while (!g_dlist_done && timeout) { timeout--; }
 * (r_drw_irq.c:176-190), and `timeout` changes meaning from 200 milliseconds to 200 raw loop
 * iterations - a few microseconds at 1 GHz. So d2hw_finish turns into a full-speed poll of the
 * drawing engine's status register, and T_UI - priority 7, above T_STORY at 12 - keeps the
 * processor for the whole time the engine is drawing. T_STORY gets nothing, and the token rate
 * collapses.
 *
 * WHAT THIS DOES.
 * Wait for the same D2C_DLISTACTIVE bit d2hw_finish waits for, but yield between polls, so the
 * lower-priority tasks run exactly as they did under FreeRTOS. By the time d2_endframe is
 * called the engine is already idle, so its own loop exits on the first status read and the
 * busy poll never runs. NO FILE UNDER ra\ IS EDITED.
 *
 * A short spin comes first because the steady state is "already finished": display_layer.c's
 * own frame note above says the list d2_endframe waits for has had a whole panel refresh to
 * complete. When that holds this function costs a handful of register reads and no delay at all.
 *
 * THE BOUND IS THE SAME 200 THE DRIVER USED, now honestly in milliseconds. If it ever expires
 * this function simply returns and d2_endframe waits the old way, so the failure behaviour is
 * never worse than before.
 *
 * D2C_DLISTACTIVE is BIT(3), from ra\tes\dave2d\src\dave_registermap.h:187 - a private driver
 * header that cannot be included from here, so the value is repeated with its source named.
 * R_DRW->STATUS is the same register the FSP driver reads at r_drw_irq.c:218.
 *********************************************************************************************************************/
#define GRAPHICS_D2C_DLISTACTIVE    (1UL << 3)   /* dave_registermap.h:187, BIT(3)             */
#define GRAPHICS_DLIST_SPIN_READS   (64U)        /* the "already idle" fast path               */
#define GRAPHICS_DLIST_WAIT_MS      (200U)       /* the driver's own 200, now in milliseconds  */

static void graphics_wait_dlist_idle(void)
{
    for (uint32_t i = 0U; i < GRAPHICS_DLIST_SPIN_READS; i++)
    {
        if (0U == (R_DRW->STATUS & GRAPHICS_D2C_DLISTACTIVE))
        {
            return;
        }
    }

    for (uint32_t waited = 0U; waited < GRAPHICS_DLIST_WAIT_MS; waited++)
    {
        /* One kernel tick. tk_dly_tsk() blocks this task, which is the whole point: T_STORY
         * and anything else below T_UI runs while the drawing engine works. */
        app_os_delay_ms(1U);

        if (0U == (R_DRW->STATUS & GRAPHICS_D2C_DLISTACTIVE))
        {
            return;
        }
    }
}

void graphics_end_frame()
{
    /* Give the processor away while the engine finishes, so the call below finds it idle. */
    graphics_wait_dlist_idle();

    /* End the current display list */
    d2_endframe(d2_handle);

    /* Flip the framebuffer */
    graphics_swap_buffer();

}

/**********************************************************************************************************************
 * Porting layer code of cache maintenance for r_drw
 **********************************************************************************************************************/
#include "r_drw_base.h"

d1_int_t d1_cacheflush (d1_device * handle, d1_int_t memtype)
{
    FSP_PARAMETER_NOT_USED(handle);
    FSP_PARAMETER_NOT_USED(memtype);

    /* IMPORTANT: Do NOT use SCB_CleanDCache() here!
     * It flushes the ENTIRE D-cache to SDRAM, creating a massive burst
     * of write traffic that starves the GLCDC mid-scanline → LCD blinks.
     * Instead, flush only the active draw framebuffer. */
    SCB_CleanDCache_by_Addr((void *)&fb_background[drw_buf][0],
                            (int32_t)(sizeof(fb_background) / 2));

    return 1;
}

d1_int_t d1_cacheblockflush (d1_device * handle, d1_int_t memtype, const void * ptr, d1_uint_t size)
{
    FSP_PARAMETER_NOT_USED(handle);
    FSP_PARAMETER_NOT_USED(memtype);

    SCB_CleanDCache_by_Addr((void *)ptr, (int32_t)size);

    return 1;
}
