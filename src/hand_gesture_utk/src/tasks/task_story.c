/**********************************************************************************************************************
 * File Name    : task_story.c
 * Description  : T_STORY - load the model once, then write one story per request.
 *
 * New file.
 *
 * DERIVED FROM: output_code\ra8p1_llm_ospi_hs\src\new_thread0_entry.c, which this file
 * replaces. Two things are lifted out of it: the run_model() call chain, which is split here
 * into story_init() and story_run(), and the FreeRTOS heap array ucHeap - and ucHeap is NOT
 * here, because at Stage 1 the heap is a configurator property and an application-allocated
 * array. That array belongs with the heap decision, not with this task.
 *
 * The story generator itself is untouched work: src\story\llm_model\llama4micro.cpp and
 * llama2.h. This file only drives it.
 *
 * Stage 1 shape: entry function of the configurator Thread object "story_thread", FreeRTOS
 * priority 1, the lowest of the four, stack 16,384 bytes. Lowest on purpose: the camera, the
 * models and the screen must all stay ahead of the story.
 *********************************************************************************************************************/

#include <stdio.h>

/* For the INT type in the task entry signature below. This file is C, so the micro T-Kernel
 * header compiles here - unlike llama4micro.cpp. */
#include <tk/tkernel.h>

#include "common_util.h"
#include "console_output.h"

#include "ospi_b_ep.h"

#include "app_signal.h"
#include "story_alloc.h"
#include "story_api.h"
#include "story_text.h"

/**********************************************************************************************************************
 * Exported global variables and functions (to be accessed by other files)
 *********************************************************************************************************************/

/* src\app_main.c declares this and passes it to tk_cre_tsk(). The prototype is here as well
 * so the file builds on its own with -Wmissing-declarations. */
void story_thread_entry(INT stacd, void * exinf);

/**********************************************************************************************************************
 * Private global variables and functions
 *********************************************************************************************************************/

/* T_STORY's two rows of the wait table. The idle wait is the only one in the demo with no
 * timeout: there is nothing useful for this task to do between stories. */
#define STORY_INIT_WAIT_MS    (5000U)

/* Written by T_UI through story_start / story_abandon, read by T_STORY. Single words, so a
 * reader never sees half an update on this core. */
static volatile bool    g_story_running   = false;
static volatile bool    g_story_abandon   = false;
static volatile uint8_t g_story_prompt    = 0U;

/* Set once, at start-up, by T_STORY. Read by T_UI. */
static volatile bool    g_story_disabled  = false;

static void story_disable(const char * why);

/**********************************************************************************************************************
 * Switch the story feature off for the rest of the run and say why, once.
 * @param[in] why  one upper-case line, no line ending
 *********************************************************************************************************************/
static void story_disable(const char * why)
{
    char msg[96];

    g_story_disabled = true;
    g_story_running  = false;

    snprintf(msg, sizeof(msg), "STORY OFF - %s\r\n", why);
    print_to_console(msg);
}

/**********************************************************************************************************************
 * The T_UI side of the interface. These four run on T_UI's task, not on this one, and they
 * only touch the four variables above.
 *********************************************************************************************************************/

bool story_start(uint8_t prompt_index)
{
    /* This used to return void, so a refused request looked exactly like an accepted one and
     * the state machine walked into S_STORY_GEN with an empty text area. The refusal is real:
     * story_abandon() only sets a wish, and T_STORY - the lowest priority task in the demo -
     * stays inside story_run() until its token loop sees it, so both the 15-second story
     * stall and the SW2 booth reset can leave a story still unwinding while the child asks
     * for the next one. */
    if (g_story_disabled || g_story_running)
    {
        return false;
    }

    g_story_prompt  = (uint8_t) (prompt_index % STORY_PROMPT_COUNT);
    g_story_abandon = false;
    g_story_running = true;

    app_sig_set(EV_STORY_START);

    return true;
}

bool story_is_running(void)
{
    return g_story_running;
}

void story_abandon(void)
{
    if (g_story_running)
    {
        g_story_abandon = true;
    }
}

bool story_abandon_requested(void)
{
    return g_story_abandon;
}

bool story_is_disabled(void)
{
    return g_story_disabled;
}

/**********************************************************************************************************************
 * T_STORY entry.
 * @param[in] stacd  the start code passed to tk_sta_tsk(). Not used.
 * @param[in] exinf  the T_CTSK extended information. Not used.
 *
 * The kernel calls the task through T_CTSK.task, which takes two arguments instead of one.
 * THE BODY BELOW IS UNCHANGED.
 *********************************************************************************************************************/
void story_thread_entry(INT stacd, void * exinf)
{
    app_sig_bits_t pattern;
    fsp_err_t      fsp_status;

    FSP_PARAMETER_NOT_USED(stacd);
    FSP_PARAMETER_NOT_USED(exinf);

    /* Nothing here may print before T_UI has opened the console. */
    pattern = app_sig_wait(EV_INIT_DISPLAY, false, STORY_INIT_WAIT_MS);

    if (!app_sig_released(pattern, EV_INIT_DISPLAY))
    {
        print_to_console("T_STORY TIMED OUT WAITING FOR THE DISPLAY\r\n");
    }

    /* The external flash never opened, so the weights and the tokeniser are not readable at
     * all. Do not touch the controller. */
    if (FSP_SUCCESS != g_startup_ospi_err)
    {
        story_disable("STORAGE DID NOT OPEN");
    }
    else
    {
        /* Raise the Octo-SPI clock and switch device and controller to 8D-8D-8D. This also
         * writes the read-strobe calibration preamble if the one in flash does not match.
         * The source is PLL2R and the divider is 1, so the Octo-SPI clock is 240 MHz and the
         * flash clock is 120 MHz. */
        fsp_status = ospi_b_set_protocol_to_opi();

        if (FSP_ERR_CALIBRATE_FAILED == fsp_status)
        {
            /* Named on its own, because it points at one thing: the calibration pattern at
             * 0x93FFF000. */
            story_disable("READ CALIBRATION FAILED");
        }
        else if (FSP_SUCCESS != fsp_status)
        {
            story_disable("HIGH SPEED SWITCH FAILED");
        }
        else
        {
            /* Build the transformer straight off the Octo-SPI address, build the tokeniser
             * into one block, build the sampler. This is the slow part of start-up and the
             * only place the model header is checked, which is the byte-order net. */
            story_init();

            /* story_init() RETURNS when a check inside the model fails, instead of spinning
             * inside the fork's exit() stub. Both latches are read here, and the model check
             * is named first because it is the byte-order net. */
            if (story_model_failed())
            {
                (void) handle_error(VISION_AI_APP_ERR_STORY_MODEL_HEADER, "T_STORY story_init");
                story_disable("MODEL HEADER FAILED");
            }
            else if (story_alloc_failed())
            {
                story_disable("NOT ENOUGH MEMORY");
            }
            else
            {
                app_sig_set(EV_INIT_STORY);
            }
        }
    }

    while (true)
    {
        /* The demo's only wait with no timeout. Cleared by the wait. */
        pattern = app_sig_wait(EV_STORY_START, true, APP_SIG_FOREVER);

        if (!app_sig_released(pattern, EV_STORY_START))
        {
            /* Cannot happen with no timeout, but a refused wait must never turn into a busy
             * loop that starves the three tasks above this one. */
            continue;
        }

        if (g_story_disabled)
        {
            g_story_running = false;
            app_sig_set(EV_STORY_DONE);
            continue;
        }

        /* The ring buffer reset used to be here. It is not this
         * task's to make: story_text.c gives g_tail to the reader, T_UI, and T_UI moves it in
         * story_text_get. A reset from here gave that index two writers. T_UI now resets the
         * ring on the first frame of a story, beside ui_story_text_clear(), and it does so
         * before this task can run, because it is the higher priority of the two and it never
         * blocks between story_start() and the reset. */

        story_run(g_story_prompt);

        g_story_running = false;

        /* Read the same two latches the start-up path above reads. Three story_model_fail()
         * sites run PER STORY, not at load time - "PROMPT BUFFER" and "PROMPT ENCODE" in
         * generate(), and "NULL PROMPT TEXT" in encode(). Before this, a failure raised inside
         * a run was latched and never read, so the story feature stayed enabled: every later
         * yes gave an empty text area and the child waited out the stall timeout. A check that
         * fails inside a story is just as final as one that fails at load time, so the feature
         * is disabled with the failing check named. story_disable() prints that one line and
         * clears g_story_running itself. */
        if (story_model_failed())
        {
            story_disable("MODEL CHECK FAILED");
        }
        else if (story_alloc_failed())
        {
            story_disable("NOT ENOUGH MEMORY");
        }

        /* T_UI leaves S_STORY_GEN on this, switches the models back on and drains the last
         * pieces of text. It goes out either way, so a story that fails half way still ends
         * the state normally instead of stalling the conversation. */
        app_sig_set(EV_STORY_DONE);
    }
}
