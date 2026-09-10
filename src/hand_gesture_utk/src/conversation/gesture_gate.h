/**********************************************************************************************************************
 * File Name    : gesture_gate.h
 * Description  : The six hold rules that decide when a hand shape may be accepted.
 *
 * New file. Not copied from sample_code.
 *
 * gesture_gate.c holds the bodies. The header exists first because the four tasks are written
 * against it.
 *
 * INCLUDE ORDER. This file and conv_state.h need each other: conv_ctx_t holds a gesture_gate_t,
 * and gate_take takes a conv_state_t. C has no way to forward-declare an enum, so the struct
 * below is defined BEFORE conv_state.h is pulled in. That makes either include order work.
 *********************************************************************************************************************/

#ifndef GESTURE_GATE_H_
#define GESTURE_GATE_H_

#include <stdbool.h>
#include <stdint.h>

#include "gesture_classify.h"   /* gesture_t, GESTURE_COUNT */

/**********************************************************************************************************************
 * The rule constants, one per rule.
 *********************************************************************************************************************/

#define GATE_RUN_RESULTS    (3U)      /* rule 1: matching results needed                      */
#define GATE_RUN_MS         (600U)    /* rule 1: and the run must span at least this long     */
#define GATE_SETTLE_MS      (700U)    /* rule 3: nothing is accepted this soon after a change */
#define GATE_RELEASE_MS     (300U)    /* rule 4: unbroken release gap, counted since the state
                                       *         began, and only while the models are on      */
#define GATE_LONG_HOLD_MS   (2000U)   /* rule 6: thumbs up only, in the four question states.
                                       *         UNCONFIRMED - tuned on the bench. It must
                                       *         stay well above GATE_RELEASE_MS and well
                                       *         below the 15 second answer window, and above
                                       *         GATE_SETTLE_MS + GATE_RUN_MS = 1300 ms.      */

/* Rule 2 reuses the recogniser's own two limits, GESTURE_MIN_LANDMARK_SCORE (0.5) and
 * GESTURE_MIN_PALM_PIXELS (40), and adds no third quality test. */

/**********************************************************************************************************************
 * State of the gate. One instance, inside conv_ctx_t.
 *********************************************************************************************************************/
typedef struct {
    uint32_t  state_start_ms;                   /* when the current state began               */
    uint32_t  released_since_ms[GESTURE_COUNT]; /* start of the current release gap           */
    bool      released_ok[GESTURE_COUNT];       /* rule 4 satisfied for this shape            */
    uint32_t  held_since_ms;                    /* start of the current unbroken hold         */
    gesture_t held_shape;                       /* which shape that hold is                   */
    gesture_t run_shape;                        /* rule 1: the shape being counted            */
    uint8_t   run_count;                        /* rule 1: results in the run                 */
    uint32_t  run_first_ms;                     /* rule 1: when the run began                 */
} gesture_gate_t;

/* Brings in conv_state_t. Must come after the struct above; see the note in the file header. */
#include "conv_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Start again. Called on every state change, and never carried across one: with the models
 * off during a story a carried timer would fill on its own. */
void gate_reset(gesture_gate_t * g, uint32_t now_ms);

/* Once per NEW inference result, not once per drawn frame. */
void gate_feed(gesture_gate_t * g, gesture_t raw, float score, float palm_px, uint32_t now_ms);

/* Once per drawn frame. Returns GESTURE_UNKNOWN when nothing may be accepted. */
gesture_t gate_take(gesture_gate_t * g, conv_state_t st, uint32_t now_ms);

/* True when this shape is being refused by the release rule, so the on-screen hint for it is
 * drawn at half brightness. */
bool gate_is_blocked(const gesture_gate_t * g, gesture_t shape);

#ifdef __cplusplus
}
#endif

#endif /* GESTURE_GATE_H_ */
