/**********************************************************************************************************************
 * File Name    : app_pool.c
 * Description  : The one application heap - micro T-Kernel variable-size memory pool.
 *
 * The kernel API and constants used here:
 *   T_CMPL { exinf, mplatr, mplsz, [dsname,] bufptr }  mtk3_bsp2\mtkernel\include\tk\syscall.h:352-360
 *   TA_USERBUF  0x00000020UL                           syscall.h:119
 *   ID  tk_cre_mpl(CONST T_CMPL *)                     syscall.h:771
 *   ER  tk_get_mpl(ID, SZ, void **, TMO)               syscall.h:773
 *   ER  tk_rel_mpl(ID, void *)                         syscall.h:774
 *********************************************************************************************************************/

#include <stddef.h>
#include <stdbool.h>

#include "bsp_api.h"            /* BSP_ALIGN_VARIABLE, BSP_PLACE_IN_SECTION, BSP_UNINIT_SECTION_PREFIX */

#include <tk/tkernel.h>

#include "app_pool.h"

/**********************************************************************************************************************
 * THE HEAP ARRAY. 8 MB, in SDRAM, 8-byte aligned. The size is STORY_POOL_SIZE in app_pool.h.
 *
 * .sdram_noinit and NOT .sdram: the pool builds its own free list in tk_cre_mpl, so zeroing
 * 8 MB at reset would buy nothing.
 *
 * NOT static: it is the single largest object in the image after the models, so it is left
 * visible in the link map.
 *********************************************************************************************************************/
uint8_t g_story_pool_buf[STORY_POOL_SIZE] BSP_ALIGN_VARIABLE(8)
    BSP_PLACE_IN_SECTION(BSP_UNINIT_SECTION_PREFIX ".sdram_noinit");

/* The pool ID. 0 until app_pool_init() has run; negative would be an error code.
 * NOT static: src\app_main.c reports it. */
ID g_story_mpl = 0;

bool app_pool_init(void)
{
    /* TA_USERBUF is what keeps the pool inside g_story_pool_buf instead of taking 8 MB of
     * kernel system memory. TA_TFIFO orders any waiting task - with TMO_POL below nothing ever
     * waits, but the attribute is still required to be one of TA_TFIFO / TA_TPRI.
     *
     * Designated initialisers are not optional: T_CMPL puts .bufptr AFTER an optional
     * .dsname field (syscall.h:356-359), so a positional initialiser would silently put the
     * buffer pointer in the wrong member whenever USE_OBJECT_NAME is 1. */
    T_CMPL cmpl = {
        .exinf  = NULL,
        .mplatr = TA_TFIFO | TA_USERBUF,
        .mplsz  = (SZ) STORY_POOL_SIZE,
        .bufptr = (void *) g_story_pool_buf,
    };

    g_story_mpl = tk_cre_mpl(&cmpl);        /* RETURNS the ID; there is no out-parameter */

    return (g_story_mpl > 0);
}

void * app_pool_alloc(size_t size)
{
    void * p = NULL;
    ER     e;

    /* A 0-byte request returns NULL. tk_get_mpl rejects blksz == 0 with E_PAR when parameter
     * checking is on (mtk3_bsp2\mtkernel\kernel\tkernel\mempool.c:417,
     * CHECK_PAR(blksz > 0 ...)), but the guard is explicit so the answer does not change if
     * parameter checking is ever turned off. */
    if ((0U == size) || (g_story_mpl <= 0))
    {
        return NULL;
    }

    /* TMO_POL, NOT TMO_FEVR.
     * All three clients are written to handle NULL, and none is written to survive being
     * blocked. TMO_FEVR would turn a graceful degrade into a hang on whichever task asked.
     * TMO_POL returns at once.
     *
     * tk_get_mpl carries CHECK_DISPATCH() (mempool.c:418), so it must be called from task
     * context - which all three clients are. Nothing here may be called from an interrupt. */
    e = tk_get_mpl(g_story_mpl, (SZ) size, &p, TMO_POL);

    if (e < E_OK)
    {
        p = NULL;
    }

    return p;
}

void app_pool_free(void * p)
{
    /* tk_rel_mpl is not NULL-safe: a NULL block is a parameter error. The guard is
     * load-bearing here because d1_freemem(NULL) and operator delete(nullptr) are both legal
     * and both reach this function. */
    if ((NULL == p) || (g_story_mpl <= 0))
    {
        return;
    }

    (void) tk_rel_mpl(g_story_mpl, p);
}
