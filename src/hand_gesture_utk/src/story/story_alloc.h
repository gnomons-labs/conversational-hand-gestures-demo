/**********************************************************************************************************************
 * File Name    : story_alloc.h
 * Description  : Allocation wrappers for the story generator.
 *
 * The story generator allocates only through these three calls, and they are the only story
 * code that names the memory pool. story_alloc.c holds the bodies.
 *********************************************************************************************************************/

#ifndef STORY_ALLOC_H_
#define STORY_ALLOC_H_

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Give out memory for the story generator. Returns NULL when the request cannot
 * be met. Never throws: this project builds with -fno-exceptions. */
void * story_malloc(size_t size);

/* story_malloc plus memset to zero. Returns NULL on failure. */
void * story_calloc(size_t n, size_t size);

/* Give memory back. A NULL pointer is accepted and ignored. */
void story_free(void * p);

/* True once any request has failed. The story feature is then disabled and the heap is never
 * asked again in that run. */
bool story_alloc_failed(void);

/* Latch a failed check inside the story model and name it on the console, once. The story
 * feature is then disabled with the failing check named. Called from the generator wherever a
 * model check fails, so that no check can stop the task.
 * @param[in] why  one short upper-case name, no line ending */
void story_model_fail(const char * why);

/* True once story_model_fail has been called. T_STORY reads it after story_init(). */
bool story_model_failed(void);

#ifdef __cplusplus
}
#endif

#endif /* STORY_ALLOC_H_ */
