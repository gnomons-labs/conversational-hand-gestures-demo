/**********************************************************************************************************************
 * File Name    : story_alloc.c
 * Description  : Allocation wrappers for the story generator.
 *
 * The story generator allocates only through these wrappers, so the failure path is in one
 * place.
 *
 * When an allocation fails the story feature is disabled and the heap is never
 * asked again in that run. That is why the failure is latched below and why a
 * latched wrapper returns NULL without calling the heap.
 *********************************************************************************************************************/

#include <stdio.h>
#include <string.h>

/* The heap array and both allocation calls live behind app_pool.h. */
#include "app_pool.h"

#include "console_output.h"
#include "story_alloc.h"

/**********************************************************************************************************************
 * THE HEAP ARRAY IS NOT HERE. It is g_story_pool_buf in src\app_pool.c: 8 MB in the
 * .sdram_noinit section, 8-byte aligned, with one tk_cre_mpl() laid over it. The heap is not a
 * story-side decision, because it has three clients: this file, every C++ new in the image, and
 * the D/AVE 2D drawing engine through src\drw_alloc.c. All three must reach the same memory.
 *
 * What IS here - the failure latch, the one console line, and the rule that a latched wrapper
 * never asks the heap again - is on the story path only.
 *********************************************************************************************************************/

/* Set by the first failed request. Read by story_alloc_failed(). */
static volatile bool g_story_alloc_failed = false;

/* So the console gets one line, not one line per retry. */
static bool g_story_alloc_reported = false;

/* Set by the story generator when a check inside the model fails. Read by
 * story_model_failed(). */
static volatile bool g_story_model_failed = false;

/* So the console gets one line, not one per check. */
static bool g_story_model_reported = false;

/**********************************************************************************************************************
 * Latch the failure and say so once on the console.
 * @param[in] size   the request that could not be met, in bytes
 *********************************************************************************************************************/
static void story_alloc_latch_failure(size_t size)
{
    g_story_alloc_failed = true;

    if (!g_story_alloc_reported)
    {
        char msg[96];

        g_story_alloc_reported = true;
        snprintf(msg, sizeof(msg),
                 "STORY ALLOC FAILED FOR %u BYTES - STORY FEATURE DISABLED\r\n",
                 (unsigned int) size);
        print_to_console(msg);
    }
}

void * story_malloc(size_t size)
{
    void * p;

    /* Once one request has failed the heap is never asked again in this run. */
    if (g_story_alloc_failed)
    {
        return NULL;
    }

    p = app_pool_alloc(size);

    if (NULL == p)
    {
        story_alloc_latch_failure(size);
    }

    return p;
}

void * story_calloc(size_t n, size_t size)
{
    size_t total = n * size;
    void * p     = story_malloc(total);

    if (NULL != p)
    {
        memset(p, 0, total);
    }

    return p;
}

void story_free(void * p)
{
    /* Return at once on NULL. tk_rel_mpl(mplid, NULL) is a parameter error, so this guard is
     * not optional. */
    if (NULL == p)
    {
        return;
    }

    app_pool_free(p);
}

bool story_alloc_failed(void)
{
    return g_story_alloc_failed;
}

/**********************************************************************************************************************
 * A failed check inside the model disables the story feature and names the check on the
 * console - it is the byte-order net. It must never stop the task: T_STORY has to return and
 * report, so the rest of the demo carries on without the story.
 *
 * @param[in] why  one short upper-case name for the check that failed
 *********************************************************************************************************************/
void story_model_fail(const char * why)
{
    g_story_model_failed = true;

    if (!g_story_model_reported)
    {
        char msg[96];

        g_story_model_reported = true;
        snprintf(msg, sizeof(msg), "STORY MODEL CHECK FAILED: %s\r\n",
                 (NULL != why) ? why : "UNKNOWN");
        print_to_console(msg);
    }
}

bool story_model_failed(void)
{
    return g_story_model_failed;
}
