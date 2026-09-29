/**********************************************************************************************************************
 * File Name    : drw_alloc.c
 * Description  : d1_malloc / d1_free for the D/AVE 2D drawing engine.
 *
 * WHY THIS FILE EXISTS.
 * The 2D drawing engine allocates its display lists through d1_allocmem / d1_freemem in
 * ra\fsp\src\r_drw\r_drw_memory.c. With BSP_CFG_RTOS at 0 and no custom allocator, that code
 * falls into its "no RTOS" branch and calls the C library malloc(), which would put every
 * display list in the 32 KB BSP_CFG_HEAP_BYTES heap in on-chip SRAM instead of the 8 MB pool in
 * SDRAM, and would not be task-safe.
 *
 * The configurator property "Memory Allocation: Custom" on D/AVE 2D Port Interface (r_drw)
 * makes ra_cfg\fsp_cfg\r_drw_cfg.h read #define DRW_CFG_CUSTOM_MALLOC ((1)). d1_allocmem then
 * returns d1_malloc(size) on its FIRST branch, before any BSP_CFG_RTOS test is reached
 * (r_drw_memory.c:57-78), and these two functions send it to the 8 MB pool.
 *
 * NO FSP FILE IS EDITED. The prototypes are the ones FSP itself generates, in
 * ra_gen\common_data.h:141-143, guarded by #if DRW_CFG_CUSTOM_MALLOC.
 *
 * RAW, WITH NO FAILURE LATCH - on purpose. Routing these through story_malloc would let a lost
 * display list disable the story feature.
 *********************************************************************************************************************/

#include <stddef.h>

#include "app_pool.h"

void * d1_malloc(size_t size);
void   d1_free(void * ptr);

void * d1_malloc(size_t size)
{
    return app_pool_alloc(size);
}

void d1_free(void * ptr)
{
    /* app_pool_free is NULL-safe, which d1_freemem relies on. */
    app_pool_free(ptr);
}
