/**********************************************************************************************************************
 * File Name    : app_pool.h
 * Description  : The one application heap - micro T-Kernel variable-size memory pool.
 *
 * NEW FILE, added by the FreeRTOS -> micro T-Kernel 3.0 BSP2 port.
 *
 * WHAT THIS REPLACES: FreeRTOS heap 4 and its 8 MB ucHeap array, which used to live in
 * src\story\story_alloc.c. The array itself has moved here unchanged - same 8 MB, same
 * .sdram_noinit section, same 8-byte alignment - and one tk_cre_mpl() is laid over it.
 *
 * THREE CLIENTS, NOT ONE:
 *   1. the story generator, through story_malloc / story_free / story_calloc (T_STORY)
 *   2. EVERY C++ new and delete in the image - llama4micro.cpp replaces the six global
 *      operators with story_malloc / story_free, so the vision C++ under
 *      src\hand_gestures\ reaches this pool too (any task, including T_AI)
 *   3. the D/AVE 2D drawing engine, through d1_malloc / d1_free in src\drw_alloc.c (T_UI)
 *
 * Task-safe: tk_get_mpl and tk_rel_mpl guard with BEGIN_CRITICAL_SECTION, exactly as
 * pvPortMalloc guarded with vTaskSuspendAll.
 *********************************************************************************************************************/

#ifndef APP_POOL_H_
#define APP_POOL_H_

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The 8 MB that used to be configTOTAL_HEAP_SIZE.
 * was: #define configTOTAL_HEAP_SIZE (0x800000) in ra_cfg\aws\FreeRTOSConfig.h:131.
 * Identical value, identical section, identical alignment. */
#define STORY_POOL_SIZE    (0x800000)

/**********************************************************************************************************************
 * Create the pool. Must be called from usermain() BEFORE the four tasks are created.
 *
 * FreeRTOS heap 4 needed no init call - pvPortMalloc built its free list lazily on first use,
 * so it worked from C-runtime init. This pool does not exist until this runs, so every
 * allocation before it returns NULL.
 *
 * @return true on success. false means tk_cre_mpl failed and the whole heap is unavailable.
 *********************************************************************************************************************/
bool app_pool_init(void);

/* Direct replacement for pvPortMalloc. Returns NULL on failure and NEVER blocks. */
void * app_pool_alloc(size_t size);

/* Direct replacement for vPortFree. NULL-safe. */
void app_pool_free(void * p);

#ifdef __cplusplus
}
#endif

#endif /* APP_POOL_H_ */
