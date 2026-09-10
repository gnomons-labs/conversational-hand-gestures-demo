/**********************************************************************************************************************
 * File Name    : story_api.h
 * Description  : The small interface T_UI uses to start, watch and stop a story.
 *
 * New file. Not copied from sample_code. The bodies of the four signatures below live in
 * src\tasks\task_story.c and in the edited story generator
 * (src\story\llm_model\llama4micro.cpp, where the fork's run_model() is split into a load that
 * happens once and a story that happens per request).
 *
 * This header exists before its bodies because T_UI and T_STORY are written against it.
 *********************************************************************************************************************/

#ifndef STORY_API_H_
#define STORY_API_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Four prompts, picked at random. */
#define STORY_PROMPT_COUNT      (4U)

/* At most this many tokens per story, with the generator's own end-of-sequence stop still
 * active. One build constant. */
#define STORY_MAX_TOKENS        (128U)

/**********************************************************************************************************************
 * T_STORY, once at start-up: build the transformer straight off the Octo-SPI address, build the
 * tokeniser into one block, and build the sampler. Slow. It must run after the Octo-SPI has
 * been switched to 8D-8D-8D.
 *********************************************************************************************************************/
void story_init(void);

/**********************************************************************************************************************
 * T_UI: ask for one story. Sets EV_STORY_START, which releases T_STORY.
 *
 * It returns whether the request was taken. A request is REFUSED when the story feature is
 * off, or when the story before it has not finished unwinding - abandoning a story only sets a
 * wish, and T_STORY is the lowest priority task in the demo, so both the 15-second story stall
 * and the SW2 booth reset leave a window where a new request cannot be met. A caller that
 * ignores the answer sends the child to an empty text area.
 *
 * @param[in] prompt_index  0 to STORY_PROMPT_COUNT - 1
 * @return    true when the request was taken, false when it was refused
 *********************************************************************************************************************/
bool story_start(uint8_t prompt_index);

/**********************************************************************************************************************
 * T_STORY, once per request: write one story. Slow - it returns only when the story is
 * finished or abandoned. Every piece of text goes to the ring buffer and to the console.
 *
 * The story fork's run_model() is split, and its TellStory() becomes this function. Its body
 * lives with the generator, in src\story\llm_model\llama4micro.cpp.
 *
 * @param[in] prompt_index  0 to STORY_PROMPT_COUNT - 1
 *********************************************************************************************************************/
void story_run(uint8_t prompt_index);

/* True between story_start and the moment T_STORY sets EV_STORY_DONE. */
bool story_is_running(void);

/* T_UI, on the 15 second stall: stop asking for more text and let the state machine move on. */
void story_abandon(void);

/**********************************************************************************************************************
 * True when T_UI has abandoned the story that is running.
 *
 * The generator's own token loop tests this and stops early. story_abandon only sets the
 * wish; something inside the loop has to see it, and this is that call. It exists because
 * story_abandon cannot work without it.
 *********************************************************************************************************************/
bool story_abandon_requested(void);

/**********************************************************************************************************************
 * True when the story feature is switched off for this run: the Octo-SPI never opened, the
 * high-speed switch failed, the model header did not check out, or the heap refused a block
 * on a first try. T_UI reads it so that S_STORY_PROMPT can answer "MY STORY BOOK IS CLOSED
 * TODAY" instead of hanging.
 *********************************************************************************************************************/
bool story_is_disabled(void);

#ifdef __cplusplus
}
#endif

#endif /* STORY_API_H_ */
