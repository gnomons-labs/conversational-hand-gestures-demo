/**********************************************************************************************************************
 * File Name    : conv_state.h
 * Description  : The twelve conversation states, their timers and their transitions.
 *
 * New file. Not copied from sample_code.
 *
 * conv_state.c holds the bodies. The header exists first because the four tasks are written
 * against it.
 *
 * INCLUDE ORDER: see the note in gesture_gate.h. The enum below is defined before that file is
 * pulled in, so either include order works.
 *********************************************************************************************************************/

#ifndef CONV_STATE_H_
#define CONV_STATE_H_

#include <stdbool.h>
#include <stdint.h>

#include "gesture_classify.h"   /* gesture_t */

/* The twelve states, plus the count. Built from a reference design of eight: its single
 * gameplay state becomes three here, because the screen and the quit hint behave differently
 * in each. */
typedef enum e_conv_state {
    S_BOOT = 0,
    S_IDLE,
    S_GREETING,
    S_GAME_COUNT,
    S_GAME_THROW,
    S_GAME_RESULT,
    S_POST_GAME,
    S_STORY_PROMPT,
    S_STORY_GEN,
    S_POST_STORY,
    S_NO_GESTURE,
    S_GOODBYE,
    S_COUNT
} conv_state_t;

/* Brings in gesture_gate_t. Must come after the enum above. */
#include "gesture_gate.h"

typedef struct {
    conv_state_t   state;
    uint32_t       entered_ms;        /* for every timeout and every "first 1 s" rule   */
    gesture_gate_t gate;
    gesture_t      my_throw;          /* chosen when S_GAME_COUNT is entered            */
    gesture_t      child_throw;
    int8_t         last_result;       /* -1 lose, 0 draw, +1 win                        */
    uint8_t        prompt_index;      /* 0 to 3                                         */
    bool           models_on;
    bool           story_running;
    uint32_t       last_story_text_ms;
} conv_ctx_t;

#ifdef __cplusplus
extern "C" {
#endif

void         conv_init(conv_ctx_t * c, uint32_t now_ms);
void         conv_step(conv_ctx_t * c, gesture_t accepted, uint32_t now_ms);
conv_state_t conv_state(const conv_ctx_t * c);
void         conv_force_idle(conv_ctx_t * c, uint32_t now_ms);   /* SW2, the booth reset */

/**********************************************************************************************************************
 * The models-on flag, published by T_UI and read by T_CAM and T_AI.
 *
 * The models-on flag travels beside the event bits in shared data, with exactly one writer.
 * conv_ctx_t holds the flag and T_UI owns that structure, so the other two tasks reach it
 * through these two calls and never through a pointer to the structure. That keeps the
 * single-writer rule visible in the code.
 *
 * conv_publish_models_on is called by conv_step (and by conv_init) whenever the flag moves.
 * It is the only writer. The two readers are T_CAM, which then skips the letterbox
 * pre-process, and T_AI, which then stops running the models.
 *********************************************************************************************************************/
bool conv_models_on(void);
void conv_publish_models_on(bool on);

/**********************************************************************************************************************
 * True when SHOW A FIST TO SAY BYE belongs on the lower half of the hint column in this state.
 *
 * Added 2026-08-13, when ui_screen.c was written. The rule is one column of the state table in
 * conv_state.c - on in S_IDLE and the four question states, off in all three game-round states
 * because a fist is Rock there. The screen needs the answer, and the alternative was a second
 * copy of that column in ui_screen.c, which is how two tables come to disagree later. This is a
 * reader over the one table, not a new rule.
 *********************************************************************************************************************/
bool conv_quit_hint(const conv_ctx_t * c);

#ifdef __cplusplus
}
#endif

#endif /* CONV_STATE_H_ */
