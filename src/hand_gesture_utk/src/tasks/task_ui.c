/**********************************************************************************************************************
 * File Name    : task_ui.c
 * Description  : T_UI - the conversation state machine and one screen frame per panel refresh.
 *
 * New file. It holds the start-up order, the wait table, the once-per-frame observed-bit pass
 * and the conversation step.
 *
 * DERIVED FROM: the start-up half of output_code\hand_gesture_five\src\
 * camera_display_thread_entry.c:89-133 (console, buttons, MIPI_IF_EN, frame buffers, drawing
 * engine, panel), which moves out of the camera task and into this one. The drawing itself is
 * NOT here: it is ui_screen.c, which replaces the fork's
 * src\display_layer\detection_screen_mipi.c.
 *
 * Stage 1 shape: entry function of the configurator Thread object "ui_thread", FreeRTOS
 * priority 2, stack 8,192 bytes. T_UI is NOT the first thread the scheduler runs - T_CAM is,
 * because it has the highest priority - but it is first in the order of application work,
 * because the other three tasks wait on EV_INIT_DISPLAY, which only T_UI can set.
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
#include "console_output.h"
#include "display_layer.h"
#include "time_counter.h"

#include "gesture_classify.h"

#include "app_signal.h"
#include "conv_state.h"
#include "ui_screen.h"
#include "story_api.h"
#include "story_text.h"

/**********************************************************************************************************************
 * Exported global variables and functions (to be accessed by other files)
 *********************************************************************************************************************/

/* src\app_main.c declares this and passes it to tk_cre_tsk(). The prototype is here as well
 * so the file builds on its own with -Wmissing-declarations. */
void ui_thread_entry(INT stacd, void * exinf);

/**********************************************************************************************************************
 * Private global variables and functions
 *********************************************************************************************************************/

/* T_UI's one row of the wait table. A vsync that never arrives leaves the last frame on the
 * panel; it is counted, and the frame is drawn again. */
#define UI_VSYNC_WAIT_MS      (200U)

/* How much story text is moved from the ring buffer to the screen in one frame. The ring
 * buffer is 4,096 bytes and the generator writes a few bytes per token, so this drains far
 * faster than the story is written. */
#define UI_TEXT_CHUNK         (256U)

/* The one conversation context. T_UI is its only writer. */
static conv_ctx_t g_conv;

static bool     g_overlay_on      = false;
static bool     g_story_was_running = false;
/* How many times the EV_VSYNC wait timed out. Read with a debugger, like
 * glcdc_present_refused in src\display_layer\display_layer.c.
 *
 * VOLATILE IS LOAD-BEARING, for the same reason as that counter: nothing in the project ever
 * reads this object, so it is store-only and a compiler may legally delete it and its stores.
 * This file is built at -Os. Keep the two counters the same. */
static volatile uint32_t g_ui_vsync_timeouts = 0U;

static uint32_t ui_now_ms(void);
static void     ui_startup(void);
static void     ui_handle_observed(app_sig_bits_t seen, uint32_t now_ms);
static void     ui_drain_story_text(void);

/**********************************************************************************************************************
 * Milliseconds since the time base was started.
 *
 * timer_count counts 100 microsecond periods, and TimeCounter_CountValueConvertToMs divides a
 * difference by 10 (src\time_counter\time_counter.c). Passing 0 as the first count therefore
 * gives milliseconds since TimeCounter_Init. This is the demo's only time base: every gate
 * timer and every state timeout reads it.
 *
 * @return  milliseconds since TimeCounter_Init.
 *
 * THE WRAP, STATED CORRECTLY. This value is timer_count / 10, so it does NOT wrap at 2^32: it
 * climbs to 429,496,729 and then drops to 0 when the 32-bit hardware count wraps. Differences
 * taken across that one moment are nonsense, not a small number. It costs a state timeout or a
 * gate timer being wrong once. It is about 4.97 days of unbroken running away, which no booth
 * session reaches, so it is left as it is and written down rather than fixed.
 *********************************************************************************************************************/
static uint32_t ui_now_ms(void)
{
    return TimeCounter_CountValueConvertToMs(0U, TimeCounter_CurrentCountGet());
}

/**********************************************************************************************************************
 * The start-up order. Nothing here may be reordered: the time base has to run before any
 * other task can read a timer, and the panel has to be up before EV_INIT_DISPLAY releases the
 * other three tasks.
 *********************************************************************************************************************/
static void ui_startup(void)
{
    fsp_err_t fsp_status;

    /* 1. The demo's only time base. First, because everything else measures against it. */
    TimeCounter_Init();
    TimeCounter_CountReset();

    /* 2. The console. */
    fsp_status = console_output_init();

    if (FSP_SUCCESS != fsp_status)
    {
        /* The demo runs on with no console. A visitor never sees it. */
        (void) handle_error(VISION_AI_APP_ERR_CONSOLE_OPEN, "T_UI console_output_init");
    }

    /* The banner names the kernel this build runs on. The screen shows the same words, drawn
     * by ui_screen.c.
     *
     * FSP_VERSION_STRING is parenthesised in ra\fsp\inc\fsp_version.h, so it cannot be glued
     * to another string literal; it goes through the formatter instead. */
    {
        char banner[80];

        print_to_console("\r\nMICRO T KERNEL 3 0 ON RA8P1\r\n");
        snprintf(banner, sizeof(banner), "FLEXIBLE SOFTWARE PACKAGE %s\r\n", FSP_VERSION_STRING);
        print_to_console(banner);
    }

    /* 3. Anything the warm-start hook could not report, because it ran before the console
     *    existed. */
    if (FSP_SUCCESS != g_startup_ospi_err)
    {
        print_to_console("STORAGE ERROR - DEMO IS LIMITED\r\n");
        ERROR_INDICATE_LED_ON;
    }

    /* 4. The two buttons. SW1 is the tuning overlay, SW2 is the booth reset. */
    fsp_status = external_irq_configure();

    if (FSP_SUCCESS != fsp_status)
    {
        (void) handle_error(VISION_AI_APP_ERR_EXTERNAL_IRQ_INIT, "T_UI external_irq_configure");
    }

    /* 5. Enable the display interface on the EK-RA8P1. */
    R_IOPORT_PinWrite(&g_ioport_ctrl, MIPI_IF_EN, BSP_IO_LEVEL_LOW);

    /* 6. Both frame buffers to black, so an unpainted zone has no rubbish in it. */
    display_image_buffer_initialize();

    /* 7. The two-dimensional drawing engine. The fork ignores this result; it is checked here,
     *    and drw_init itself now checks d2_opendevice and d2_inithw. Both are APP_FATAL, so
     *    handle_error does not return from here. */
    fsp_status = drw_init();

    if (FSP_SUCCESS != fsp_status)
    {
        (void) handle_error(VISION_AI_APP_ERR_DRW_INIT, "T_UI drw_init");
    }

    /* 8. The graphics controller and the panel. Also APP_FATAL. */
    fsp_status = display_init();

    if (FSP_SUCCESS != fsp_status)
    {
        (void) handle_error(VISION_AI_APP_ERR_GRAPHICS_INIT, "T_UI display_init");
    }

    /* 9. Let the panel settle before the first picture is shown.
     *
     *    NO MIPI DSI BACKLIGHT COMMAND IS NEEDED AND NONE EXISTS. This was settled on
     *    2026-08-14. mipi_dsi_enable_backlight() is not a function this project has; the only
     *    copies of it are commented out in the vendor sample and in the fork
     *    output_code\hand_gesture_five, not here. The 120-vsync counter that used to wrap the
     *    call is deleted as dead code.
     *
     *    THE BACKLIGHT IS STILL DRIVEN, BY A PIN. display_init(), called at step 8 just above,
     *    writes LCD_BLEN high at src\display_layer\display_layer.c:103, on the last line of
     *    that function (it was :98 before the bench fix 1 comment block moved it). That is what
     *    lights the panel. DO NOT REMOVE THAT LINE: an earlier wording here said there was no
     *    backlight call to make at all, which read as if display_layer.c:98 were dead and
     *    invited someone to delete it and get a dark panel. */
    R_BSP_SoftwareDelay(200, BSP_DELAY_UNITS_MILLISECONDS);

    ui_init();

    /* The screen is alive. This releases T_CAM, T_AI and T_STORY, all of which are waiting. */
    app_sig_set(EV_INIT_DISPLAY);
}

/**********************************************************************************************************************
 * Act on the bits T_UI watches but does not wait for.
 * @param[in] seen     the observed bits that were set in this frame's pattern
 * @param[in] now_ms   this frame's time
 *********************************************************************************************************************/
static void ui_handle_observed(app_sig_bits_t seen, uint32_t now_ms)
{
    /* The booth reset. Checked first, so a press always wins over whatever else arrived in the
     * same frame. */
    if (0U != (seen & EV_BTN2))
    {
        conv_force_idle(&g_conv, now_ms);
    }

    if (0U != (seen & EV_BTN1))
    {
        g_overlay_on = !g_overlay_on;
        ui_set_overlay(g_overlay_on);
    }

    if (0U != (seen & EV_AI_RESULT))
    {
        /* One new inference result, so the gate is fed exactly once: gate_feed runs per
         * result, gate_take runs per frame. The raw answer and the raw measurements come from
         * the tracker, which T_AI has just updated. Both numbers the gate's rule 2 tests are
         * real measured values: hand_score is the landmark model's own score for this hand and
         * s is the palm length in camera pixels. */
        const gesture_metrics_t * m = gesture_track_metrics();

        gate_feed(&g_conv.gate, gesture_track_raw(), m->hand_score, m->s, now_ms);
    }

    if (0U != (seen & EV_STORY_TEXT))
    {
        /* conv_ctx_t.last_story_text_ms is the stall clock of the story state: fifteen seconds
         * with no new text and conv_step gives up on the story. T_UI owns this structure and is
         * the only writer of this field, which is why it is written here directly instead of
         * through a function of its own. */
        g_conv.last_story_text_ms = now_ms;
    }

    if (0U != (seen & EV_STORY_DONE))
    {
        /* Nothing to do here: conv_step reads story_is_running(), which T_STORY has already
         * cleared. The bit only makes sure this frame runs at once. */
    }
}

/**********************************************************************************************************************
 * Move story text from the ring buffer onto the screen's three rolling lines.
 *********************************************************************************************************************/
static void ui_drain_story_text(void)
{
    char   chunk[UI_TEXT_CHUNK];
    size_t got;

    do
    {
        got = story_text_get(chunk, sizeof(chunk));

        if (0U != got)
        {
            ui_story_text_append(chunk);
        }
    } while (got == (sizeof(chunk) - 1U));
}

/**********************************************************************************************************************
 * T_UI entry.
 * @param[in] stacd  the start code passed to tk_sta_tsk(). Not used.
 * @param[in] exinf  the T_CTSK extended information. Not used.
 *
 * The kernel calls the task through T_CTSK.task, which takes two arguments instead of one.
 * THE BODY BELOW IS UNCHANGED.
 *********************************************************************************************************************/
void ui_thread_entry(INT stacd, void * exinf)
{
    FSP_PARAMETER_NOT_USED(stacd);
    FSP_PARAMETER_NOT_USED(exinf);

    ui_startup();

    /* S_BOOT holds for two seconds, then the resting screen appears. conv_init sets the state
     * and the clock it is measured from. */
    conv_init(&g_conv, ui_now_ms());

    while (true)
    {
        app_sig_bits_t pattern;
        app_sig_bits_t seen;
        uint32_t       now_ms;
        gesture_t      accepted;

        /* One panel refresh. Cleared by the wait, because only T_UI reads it.
         *
         * BENCH FIX 2026-08-14, THE SCREEN FLICKER. The bit is ALSO cleared at the end of the
         * previous frame, inside graphics_swap_buffer (src\display_layer\display_layer.c), at the
         * moment the finished buffer is offered to the graphics controller. Without that, a line
         * detect that arrived while this task was drawing releases this wait at once, a second
         * frame is offered inside the same refresh, the controller refuses it, and the frame
         * after it is painted onto the panel in plain sight. The full reasoning is written at
         * graphics_swap_buffer; do not clear it here as well, or the two places can disagree. */
        pattern = app_sig_wait(EV_VSYNC, true, UI_VSYNC_WAIT_MS);

        now_ms = ui_now_ms();

        if (!app_sig_released(pattern, EV_VSYNC))
        {
            /* Counted, and the last frame is drawn again so the panel does
             * not freeze on a half-finished picture. No bit is observed on this path. */
            g_ui_vsync_timeouts++;
            ui_draw_frame(&g_conv, now_ms);
            continue;
        }

        /* The once-per-frame observed-bit pass. Clear exactly the bits
         * that were seen, never the whole UI_OBSERVED mask: a bit that arrives between the
         * wait returning and this clear was not in `seen`, so it is not lost - T_UI sees it on
         * the next frame. And at Stage 1 the bits themselves are passed, never their
         * complement. */
        seen = pattern & UI_OBSERVED;

        if (0U != seen)
        {
            app_sig_clear(seen);
            ui_handle_observed(seen, now_ms);
        }

        /* Once per frame, whatever arrived. Returns GESTURE_UNKNOWN when the six hold rules
         * say nothing may be accepted yet. */
        accepted = gate_take(&g_conv.gate, conv_state(&g_conv), now_ms);

        /* The only place a state changes. It starts and stops the story itself, because it
         * owns the transitions into S_STORY_GEN and out of it. */
        conv_step(&g_conv, accepted, now_ms);

        /* A story that has just started gets a clean text area; a story that is running gets
         * its new text moved across. */
        if (story_is_running())
        {
            if (!g_story_was_running)
            {
                /* The ring buffer reset belongs to T_UI, not to
                 * T_STORY: story_text.c gives g_tail to the reader and only the reader may move
                 * it. It is safe here because conv_step() above is what started the story, and
                 * this task does not block between the two - T_STORY is the lower priority of
                 * the pair, so it cannot have written a byte yet. Both halves of "a new story
                 * gets a clean slate" now sit together. */
                story_text_reset();
                ui_story_text_clear();
            }

            ui_drain_story_text();
        }
        else if (g_story_was_running)
        {
            /* Take the last few pieces that arrived with EV_STORY_DONE. */
            ui_drain_story_text();
        }
        else
        {
            /* No story. Nothing to drain. */
        }

        g_story_was_running = story_is_running();

        ui_draw_frame(&g_conv, now_ms);
    }
}
