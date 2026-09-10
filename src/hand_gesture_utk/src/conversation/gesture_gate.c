/**********************************************************************************************************************
 * File Name    : gesture_gate.c
 * Description  : The six hold rules that decide when a hand shape may be accepted.
 *
 * New file. Not copied from sample_code. The structure, the four function signatures and the
 * five rule constants are in gesture_gate.h.
 *
 * WHAT THE SIX RULES ARE, IN ONE PLACE
 * ------------------------------------
 * 1. Hold      three or more matching results in a row, spanning at least 600 ms   gate_feed
 * 2. Quality   landmark score at least 0.5 and palm length at least 40 pixels      gate_feed
 * 3. Settle    nothing is accepted in the first 700 ms of a state                  gate_take
 * 4. Release   the shape must have been absent for one unbroken 300 ms since the
 *              state began, and only while the models are on                       gate_feed
 * 5. Where     a per-state table: the release gate runs in five states and is
 *              deliberately off in S_GAME_THROW                                     gate_take
 * 6. Long hold a THUMBS UP held unbroken for 2,000 ms is accepted anyway, in the
 *              four question states only                                            gate_take
 *
 * THE ASYMMETRY IN RULE 6 was a deliberate decision, and it lives in exactly one place below,
 * the `GESTURE_THUMBS_UP ==` test inside gate_take. A held thumbs up only ever continues what
 * the child is already doing. A held fist ends the session and a held thumbs down leaves the
 * current mode, and to a camera a resting arm and a deliberate answer look identical, so those
 * two need positive evidence that the child let go.
 *
 * THE GATE IS NEVER CARRIED ACROSS A STATE CHANGE. conv_step calls gate_reset on every change.
 * With the models off during a story, a carried release timer would fill up on its own and open
 * the gate for a hand that never moved.
 *********************************************************************************************************************/

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gesture_gate.h"

/**********************************************************************************************************************
 * Rule 5, the per-state table. One row per state.
 *********************************************************************************************************************/

typedef enum e_gate_mode
{
    GATE_ACCEPTS_NOTHING = 0,  /* no shape is ever accepted in this state                    */
    GATE_RULES_1_TO_3,         /* rules 1, 2 and 3 only. S_GAME_THROW, and only S_GAME_THROW */
    GATE_WITH_RELEASE          /* rules 1 to 4. Rule 6 may also apply; see the second column */
} gate_mode_t;

typedef struct st_gate_row
{
    gate_mode_t mode;
    bool        long_hold;     /* rule 6 runs in this state */
} gate_row_t;

/* The four states with the long hold are the four question states: greeting, post game, story
 * prompt and post story. S_IDLE runs the release gate but NOT the long hold, because a thumbs
 * up is not an accepted shape there. */
static const gate_row_t g_gate_rows[S_COUNT] =
{
    [S_BOOT]         = { GATE_ACCEPTS_NOTHING, false },
    [S_IDLE]         = { GATE_WITH_RELEASE,    false },
    [S_GREETING]     = { GATE_WITH_RELEASE,    true  },
    [S_GAME_COUNT]   = { GATE_ACCEPTS_NOTHING, false },
    [S_GAME_THROW]   = { GATE_RULES_1_TO_3,    false },
    [S_GAME_RESULT]  = { GATE_ACCEPTS_NOTHING, false },
    [S_POST_GAME]    = { GATE_WITH_RELEASE,    true  },
    [S_STORY_PROMPT] = { GATE_WITH_RELEASE,    true  },
    [S_STORY_GEN]    = { GATE_ACCEPTS_NOTHING, false },
    [S_POST_STORY]   = { GATE_WITH_RELEASE,    true  },
    [S_NO_GESTURE]   = { GATE_ACCEPTS_NOTHING, false },
    [S_GOODBYE]      = { GATE_ACCEPTS_NOTHING, false },
};

/**********************************************************************************************************************
 * Private helpers
 *********************************************************************************************************************/

/* Milliseconds from `then` to `now`. Written as one subtraction on unsigned values, which gives
 * the right answer across the counter wrap as long as the gap is under about 49 days. */
static uint32_t gate_elapsed(uint32_t then, uint32_t now)
{
    return (uint32_t) (now - then);
}

/**********************************************************************************************************************
 * Public functions
 *********************************************************************************************************************/

void gate_reset(gesture_gate_t * g, uint32_t now_ms)
{
    uint32_t i;

    if (NULL == g)
    {
        return;
    }

    g->state_start_ms = now_ms;

    /* Rule 4 starts from zero for every shape. Nothing is released until the demo has actually
     * seen 300 ms without that shape, which is what stops a hand that was already up from
     * answering the new state's question. */
    for (i = 0U; i < (uint32_t) GESTURE_COUNT; i++)
    {
        g->released_since_ms[i] = now_ms;
        g->released_ok[i]       = false;
    }

    g->held_since_ms = now_ms;
    g->held_shape    = GESTURE_UNKNOWN;

    g->run_shape    = GESTURE_UNKNOWN;
    g->run_count    = 0U;
    g->run_first_ms = now_ms;
}

void gate_feed(gesture_gate_t * g, gesture_t raw, float score, float palm_px, uint32_t now_ms)
{
    gesture_t shape = raw;
    uint32_t  i;

    if (NULL == g)
    {
        return;
    }

    /* RULE 2, quality. A result from a hand that is too far away or that the landmark model is
     * unsure of is not a recognised shape at all. It must not build a run, and it must not
     * restart another shape's release gap either, so it is turned into "nothing recognised"
     * here, once, and the rest of the function needs no second test.
     *
     * The two limits are the recogniser's own (src\hand_gestures\palm_detection\
     * gesture_classify.c). Rule 2 adds no third quality test of its own. */
    if ((score < GESTURE_MIN_LANDMARK_SCORE) || (palm_px < GESTURE_MIN_PALM_PIXELS))
    {
        shape = GESTURE_UNKNOWN;
    }

    /* RULE 4, the release gate, per shape.
     *
     * This function is called once per NEW inference result, and results only arrive while the
     * models are on, so "only while the models are on" needs no flag here: with the models off
     * nothing is fed and no gap accrues, which is why no timer is carried across a story.
     *
     * released_ok latches. Once a shape has been absent for one unbroken 300 ms inside this
     * state it stays accepted-able, so a child who lowers a fist and shows it again is
     * understood. */
    for (i = 0U; i < (uint32_t) GESTURE_COUNT; i++)
    {
        if ((gesture_t) i == shape)
        {
            /* Seen. One result reporting this shape restarts the count from zero. */
            g->released_since_ms[i] = now_ms;
        }
        else if (!g->released_ok[i] &&
                 (gate_elapsed(g->released_since_ms[i], now_ms) >= GATE_RELEASE_MS))
        {
            g->released_ok[i] = true;
        }
        else
        {
            /* Still accruing, or already open. Nothing to do. */
        }
    }

    /* RULE 6's timer, the unbroken hold. A different shape, a no match or no hand at all
     * restarts it, which is rule 4 read the other way round. */
    if ((shape != g->held_shape) || (GESTURE_UNKNOWN == shape))
    {
        g->held_shape    = shape;
        g->held_since_ms = now_ms;
    }

    /* RULE 1, the run of matching results. GESTURE_UNKNOWN never builds a run. */
    if ((GESTURE_UNKNOWN == shape) || (shape != g->run_shape))
    {
        g->run_shape    = shape;
        g->run_count    = (GESTURE_UNKNOWN == shape) ? 0U : 1U;
        g->run_first_ms = now_ms;
    }
    else if (g->run_count < 0xFFU)
    {
        g->run_count++;
    }
    else
    {
        /* Held for a very long time. The count has done its job; do not wrap it. */
    }
}

gesture_t gate_take(gesture_gate_t * g, conv_state_t st, uint32_t now_ms)
{
    const gate_row_t * row;
    gesture_t          candidate;

    if ((NULL == g) || (st >= S_COUNT))
    {
        return GESTURE_UNKNOWN;
    }

    row = &g_gate_rows[st];

    if (GATE_ACCEPTS_NOTHING == row->mode)
    {
        return GESTURE_UNKNOWN;
    }

    /* RULE 3, the settle window. Nothing is accepted in the first 700 ms of a state. gate_feed
     * keeps running throughout, which is what lets a shape that is absent at the state change
     * collect its release gap inside this window. */
    if (gate_elapsed(g->state_start_ms, now_ms) < GATE_SETTLE_MS)
    {
        return GESTURE_UNKNOWN;
    }

    candidate = g->run_shape;

    if (GESTURE_UNKNOWN == candidate)
    {
        return GESTURE_UNKNOWN;
    }

    /* RULE 1. Both halves. The result count alone would depend on the inference rate, which
     * nobody has measured; the time alone would accept one lucky result after a long gap.
     *
     * The span is measured to NOW rather than to the last result, because the structure carries
     * run_first_ms and no run_last_ms. The difference only shows if the inference results stop
     * arriving mid-run, and a run that stops matching is cleared by gate_feed anyway. */
    if ((g->run_count < GATE_RUN_RESULTS) ||
        (gate_elapsed(g->run_first_ms, now_ms) < GATE_RUN_MS))
    {
        return GESTURE_UNKNOWN;
    }

    if (GATE_RULES_1_TO_3 == row->mode)
    {
        /* S_GAME_THROW. The countdown is itself the cue to show a shape, so a hand held ready
         * through 3, 2, 1 is the normal way to play and must be accepted. */
        return candidate;
    }

    /* RULE 4. The shape must have been let go inside this state. */
    if (g->released_ok[candidate])
    {
        return candidate;
    }

    /* RULE 6, and this is the only place the thumbs up is treated differently from the other
     * four shapes. Decided by the user on 2026-08-07. */
    if (row->long_hold &&
        (GESTURE_THUMBS_UP == candidate) &&
        (GESTURE_THUMBS_UP == g->held_shape) &&
        (gate_elapsed(g->held_since_ms, now_ms) >= GATE_LONG_HOLD_MS))
    {
        return candidate;
    }

    return GESTURE_UNKNOWN;
}

bool gate_is_blocked(const gesture_gate_t * g, gesture_t shape)
{
    if ((NULL == g) || (shape >= GESTURE_COUNT))
    {
        return false;
    }

    /* Only rule 4 is asked about. The screen uses this to draw an accepted shape's hint picture
     * at half brightness, and that only happens in the answer window of a state where the
     * release gate runs. The thumbs up is almost never seen dim, because rule 6 accepts a held
     * thumbs up at about 2 seconds and the hints appear at 5. */
    return !g->released_ok[shape];
}
