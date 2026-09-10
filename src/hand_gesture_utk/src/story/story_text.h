/**********************************************************************************************************************
 * File Name    : story_text.h
 * Description  : The story text ring buffer. One writer (T_STORY), one reader (T_UI).
 *
 * New file. Not copied from sample_code.
 *
 * The buffer is in on-chip SRAM and both ends are the CPU, so it needs no cache maintenance.
 *
 * story_text.c holds the bodies.
 *********************************************************************************************************************/

#ifndef STORY_TEXT_H_
#define STORY_TEXT_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 4,096 bytes in .bss. */
#define STORY_TEXT_RING_BYTES   (4096U)

/* Empty the buffer. T_UI, when a story is about to start. */
void story_text_reset(void);

/* Add one piece of text. T_STORY only. Also sets EV_STORY_TEXT. */
void story_text_put(const char * piece);

/* Take what is there. T_UI, once per frame.
 * @param[out] dst  where the text goes
 * @param[in]  max  size of dst in bytes, including room for the closing zero
 * @return     how many bytes were written, not counting the closing zero */
size_t story_text_get(char * dst, size_t max);

/**********************************************************************************************************************
 * How many bytes were thrown away because the buffer was full.
 *
 * It exists because nothing may be dropped silently, and a full text buffer means T_UI has
 * stopped drawing. It is 0 in normal running.
 *********************************************************************************************************************/
uint32_t story_text_dropped_get(void);

#ifdef __cplusplus
}
#endif

#endif /* STORY_TEXT_H_ */
