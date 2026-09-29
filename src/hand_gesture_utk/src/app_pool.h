/**********************************************************************************************************************
 * File Name    : app_pool.h
 * Description  : The one application heap - micro T-Kernel variable-size memory pool.
 *
 * ONE 8 MB array in the .sdram_noinit section, with one tk_cre_mpl() laid over it.
 *
 * THREE CLIENTS, NOT ONE:
 *   1. the story generator, through story_malloc / story_free / story_calloc (T_STORY)
 *   2. EVERY C++ new and delete in the image - llama4micro.cpp replaces the six global
 *      operators with story_malloc / story_free, so the vision C++ under
 *      src\hand_gestures\ reaches this pool too (any task, including T_AI)
 *   3. the D/AVE 2D drawing engine, through d1_malloc / d1_free in src\drw_alloc.c (T_UI)
 *
 * Task-safe: tk_get_mpl and tk_rel_mpl guard with BEGIN_CRITICAL_SECTION.
 *********************************************************************************************************************/

#ifndef APP_POOL_H_
#define APP_POOL_H_

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The size of the one application heap: 8 MB. */
#define STORY_POOL_SIZE    (0x800000)

/**********************************************************************************************************************
 * Create the pool. Must be called from usermain() BEFORE the four tasks are created.
 *
 * The pool does not exist until this runs, so every allocation before it returns NULL.
 *
 * @return true on success. false means tk_cre_mpl failed and the whole heap is unavailable.
 *********************************************************************************************************************/
bool app_pool_init(void);

/* Take a block from the pool. Returns NULL on failure and NEVER blocks. */
void * app_pool_alloc(size_t size);

/* Give a block back to the pool. NULL-safe. */
void app_pool_free(void * p);

#ifdef __cplusplus
}
#endif

#endif /* APP_POOL_H_ */
