/**********************************************************************************************************************
 * File Name    : drw_alloc.c
 * Description  : d1_malloc / d1_free for the D/AVE 2D drawing engine.
 *
 * NEW FILE, added by the FreeRTOS -> micro T-Kernel 3.0 BSP2 port.
 *
 * WHY THIS FILE EXISTS.
 * The 2D drawing engine allocates its display lists through d1_allocmem / d1_freemem in
 * ra\fsp\src\r_drw\r_drw_memory.c. In the source project DRW_CFG_CUSTOM_MALLOC was ((0)), so
 * d1_allocmem fell into its BSP_CFG_RTOS == 2 branch and called pvPortMalloc - the 8 MB
 * FreeRTOS heap in .sdram_noinit.
 *
 * With BSP_CFG_RTOS at 0 that same code would fall into the "no RTOS" branch and call the
 * C library malloc(), silently moving every display list from 8 MB of SDRAM into the
 * 32 KB BSP_CFG_HEAP_BYTES heap in on-chip SRAM - and away from a task-safe allocator.
 * That is a behaviour change the port must NOT make.
 *
 * The fix is the configurator property "Memory Allocation: Custom" on
 * D/AVE 2D Port Interface (r_drw), which makes ra_cfg\fsp_cfg\r_drw_cfg.h read
 * #define DRW_CFG_CUSTOM_MALLOC ((1)). d1_allocmem then returns d1_malloc(size) on its FIRST
 * branch, before any BSP_CFG_RTOS test is reached (r_drw_memory.c:57-78), and these two
 * functions send it straight back to the same 8 MB store it used before.
 *
 * NO FSP FILE IS EDITED. The prototypes are the ones FSP itself generates, in
 * ra_gen\common_data.h:141-143, guarded by #if DRW_CFG_CUSTOM_MALLOC.
 *
 * RAW, WITH NO FAILURE LATCH - on purpose. d1_allocmem reached pvPortMalloc directly in the
 * source and never touched the story failure latch, so routing it through story_malloc here
 * would let a lost display list disable the story feature.
 *********************************************************************************************************************/

#include <stddef.h>

#include "app_pool.h"

void * d1_malloc(size_t size);
void   d1_free(void * ptr);

void * d1_malloc(size_t size)
{
    /* was (indirectly): pvPortMalloc(size), reached from d1_allocmem's BSP_CFG_RTOS == 2 branch. */
    return app_pool_alloc(size);
}

void d1_free(void * ptr)
{
    /* was (indirectly): vPortFree(ptr), reached from d1_freemem's BSP_CFG_RTOS == 2 branch.
     * app_pool_free is NULL-safe, which d1_freemem relies on. */
    app_pool_free(ptr);
}
