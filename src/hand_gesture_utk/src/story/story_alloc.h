/**********************************************************************************************************************
 * File Name    : story_alloc.h
 * Description  : Allocation wrappers for the story generator.
 *
 * New file. Not copied from sample_code. The Stage 1 bodies do what the story fork
 * (output_code\ra8p1_llm_ospi_hs\src\ai_demo\llama4micro.cpp:83-132) already did
 * inline: pvPortMalloc, vPortFree, and pvPortMalloc plus memset.
 *
 * Stage 1 (FreeRTOS): pvPortMalloc / vPortFree / pvPortMalloc + memset.
 * Stage 2 (micro T-Kernel): the kernel memory-pool calls.
 * Only this file and story_alloc.c change between the two stages.
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
 * feature is disabled with the failing check named. Called from the generator in place of the
 * forked exit(EXIT_FAILURE) calls.
 * @param[in] why  one short upper-case name, no line ending */
void story_model_fail(const char * why);

/* True once story_model_fail has been called. T_STORY reads it after story_init(). */
bool story_model_failed(void);

#ifdef __cplusplus
}
#endif

#endif /* STORY_ALLOC_H_ */
