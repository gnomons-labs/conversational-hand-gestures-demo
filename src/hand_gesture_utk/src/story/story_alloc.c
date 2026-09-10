/**********************************************************************************************************************
 * File Name    : story_alloc.c
 * Description  : Allocation wrappers for the story generator - Stage 1 bodies.
 *
 * New file. Not copied from sample_code. The Stage 1 bodies did what the story
 * fork (output_code\ra8p1_llm_ospi_hs\src\ai_demo\llama4micro.cpp:83-132) already
 * did inline. What is new here is the failure path.
 *
 * When an allocation fails the story feature is disabled and the heap is never
 * asked again in that run. That is why the failure is latched below and why a
 * latched wrapper returns NULL without calling the heap.
 *********************************************************************************************************************/

#include <stdio.h>
#include <string.h>

/* was: #include "bsp_api.h" (for the heap array's section attributes) and
 * #include "FreeRTOS.h" (for configTOTAL_HEAP_SIZE, pvPortMalloc and vPortFree).
 * The array and both calls have moved behind app_pool.h. */
#include "app_pool.h"

#include "console_output.h"
#include "story_alloc.h"

/**********************************************************************************************************************
 * THE HEAP ARRAY HAS MOVED TO src\app_pool.c.
 *
 * was: uint8_t ucHeap[configTOTAL_HEAP_SIZE] BSP_ALIGN_VARIABLE(8)
 *          BSP_PLACE_IN_SECTION(BSP_UNINIT_SECTION_PREFIX ".sdram_noinit");
 *      declared here, together with a long comment about the two FreeRTOS configurator
 *      properties ("Total Heap Size 0x800000" and "Application Allocated Heap: Enabled")
 *      that made heap_4.c adopt it.
 *
 * It is now g_story_pool_buf in src\app_pool.c - SAME 8 MB, SAME .sdram_noinit section,
 * SAME 8-byte alignment - with one tk_cre_mpl() laid over it. It moved because the heap is
 * no longer a story-side decision: there are three clients now, and the other
 * two (every C++ new, and the D/AVE 2D drawing engine through src\drw_alloc.c) must reach
 * the same memory. The two FreeRTOS properties disappear with heap 4.
 *
 * Everything below this line - the failure latch, the one console line, and the rule that a
 * latched wrapper never asks the heap again - is UNCHANGED. It stays on the story path only.
 *********************************************************************************************************************/

/* Set by the first failed request. Read by story_alloc_failed(). */
static volatile bool g_story_alloc_failed = false;

/* So the console gets one line, not one line per retry. */
static bool g_story_alloc_reported = false;

/* Set by the story generator when a check inside the model fails. Read by story_model_failed().
 * Those checks used to call exit(), which the fork stubs as a spin. */
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

    p = app_pool_alloc(size);    /* was: pvPortMalloc(size) */

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

    app_pool_free(p);            /* was: vPortFree(p) */
}

bool story_alloc_failed(void)
{
    return g_story_alloc_failed;
}

/**********************************************************************************************************************
 * A failed check inside the model disables the story feature and names the check on the
 * console - it is the byte-order net. The forked generator called exit() instead, which
 * llama4micro.cpp stubs as a while(1) spin, so T_STORY never returned and never told anyone.
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
