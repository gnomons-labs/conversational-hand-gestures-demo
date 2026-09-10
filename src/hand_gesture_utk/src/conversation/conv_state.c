/**********************************************************************************************************************
 * File Name    : conv_state.c
 * Description  : The twelve conversation states, their timers and their transitions.
 *
 * New file. Not copied from sample_code. The enum, the structure and the four public
 * signatures are in conv_state.h.
 *
 * CONV_STEP IS THE ONLY PLACE A STATE CHANGES. Every change goes through conv_enter, which sets
 * entered_ms, calls gate_reset and republishes the models-on flag, so no transition can forget
 * one of the three. The state table below is written once, as one row per state, so that no
 * transition is coded twice.
 *
 * WHAT THIS FILE DOES NOT DO. It draws nothing and it reads no gesture. T_UI hands it one
 * already-accepted shape per frame, and ui_screen.c turns the state it reports into a picture
 * and a line of text. The two never meet except through conv_ctx_t.
 *********************************************************************************************************************/

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "time_counter.h"          /* the free-running 100 microsecond counter */

#include "conv_state.h"
#include "gesture_gate.h"
#include "story_api.h"

/**********************************************************************************************************************
 * The timers of the conversation.
 *********************************************************************************************************************/

#define CONV_BOOT_MS            (2000U)   /* S_BOOT holds, then the resting screen appears     */
#define CONV_ANSWER_MS          (15000U)  /* the answer window of the four question states     */
#define CONV_THROW_MS           (4000U)   /* shorter: the child has just been counted down     */
#define CONV_COUNT_MS           (3000U)   /* 3, 2, 1, one per second                           */
#define CONV_NOTICE_MS          (2500U)   /* S_GAME_RESULT and S_NO_GESTURE                    */
#define CONV_GOODBYE_MS         (2000U)   /* the wave and the fade                             */
#define CONV_STORY_STALL_MS     (15000U)  /* no new story text for this long: give up on it    */

/* No timeout at all. S_IDLE is the resting state, and S_STORY_GEN ends on the story, not on a
 * clock (its stall guard above is a separate rule). */
#define CONV_NO_TIMEOUT         (0U)

/**********************************************************************************************************************
 * The state table. One row per state.
 *********************************************************************************************************************/

typedef struct st_conv_row
{
    uint32_t     timeout_ms;      /* CONV_NO_TIMEOUT when the state does not time out         */
    conv_state_t on_timeout;      /* where it goes then                                       */
    bool         models_on;       /* the palm and landmark models run in this state           */
    bool         quit_hint;       /* SHOW A FIST TO SAY BYE is on the lower hint column       */
} conv_row_t;

/* The quit hint is off in all three game-round states, because a fist is Rock there and the
 * demo must not invite one at the wrong moment. */
static const conv_row_t g_conv_rows[S_COUNT] =
{
    [S_BOOT]         = { CONV_BOOT_MS,     S_IDLE,        false, false },
    [S_IDLE]         = { CONV_NO_TIMEOUT,  S_IDLE,        true,  true  },
    [S_GREETING]     = { CONV_ANSWER_MS,   S_NO_GESTURE,  true,  true  },
    [S_GAME_COUNT]   = { CONV_COUNT_MS,    S_GAME_THROW,  true,  false },
    [S_GAME_THROW]   = { CONV_THROW_MS,    S_NO_GESTURE,  true,  false },
    [S_GAME_RESULT]  = { CONV_NOTICE_MS,   S_POST_GAME,   true,  false },
    [S_POST_GAME]    = { CONV_ANSWER_MS,   S_NO_GESTURE,  true,  true  },
    [S_STORY_PROMPT] = { CONV_ANSWER_MS,   S_NO_GESTURE,  true,  true  },
    [S_STORY_GEN]    = { CONV_NO_TIMEOUT,  S_POST_STORY,  false, false },
    [S_POST_STORY]   = { CONV_ANSWER_MS,   S_NO_GESTURE,  true,  true  },
    [S_NO_GESTURE]   = { CONV_NOTICE_MS,   S_IDLE,        true,  false },
    [S_GOODBYE]      = { CONV_GOODBYE_MS,  S_IDLE,        true,  false },
};

/**********************************************************************************************************************
 * The models-on flag. One writer, T_UI through conv_step; two readers, T_CAM and T_AI
 * only. It lives outside conv_ctx_t as well as in it, so the two readers do not need a pointer
 * to T_UI's structure.
 *********************************************************************************************************************/
static volatile bool g_models_on = false;

/**********************************************************************************************************************
 * Private helpers
 *********************************************************************************************************************/

static uint32_t conv_elapsed(uint32_t then, uint32_t now)
{
    return (uint32_t) (now - then);
}

/**********************************************************************************************************************
 * Something that changes often and that nobody can time: the low bits of the free-running
 * 100 microsecond counter. It is the only source of chance in the demo, and it is used for two
 * things only - which shape the demo throws, and which of the four story prompts is used.
 *
 * @param[in] n  how many different answers are wanted
 * @return    0 to n - 1
 *********************************************************************************************************************/
static uint32_t conv_pick(uint32_t n)
{
    if (0U == n)
    {
        return 0U;
    }

    return (TimeCounter_CurrentCountGet() % n);
}

/**********************************************************************************************************************
 * Who won one round of rock, paper, scissors.
 *
 * Rock is the fist, paper is the open palm, scissors is the victory sign. Written as the three
 * cases that beat each other rather than as a three by three array, because the array would
 * need one row per gesture_t value and only three of the five are throws.
 *
 * @return  +1 the child wins, 0 a draw, -1 the child loses
 *********************************************************************************************************************/
static int8_t conv_round_result(gesture_t child, gesture_t demo)
{
    if (child == demo)
    {
        return 0;
    }

    if (((GESTURE_FIST       == child) && (GESTURE_VICTORY   == demo)) ||
        ((GESTURE_OPEN_PALM  == child) && (GESTURE_FIST      == demo)) ||
        ((GESTURE_VICTORY    == child) && (GESTURE_OPEN_PALM == demo)))
    {
        return 1;
    }

    return -1;
}

/**********************************************************************************************************************
 * Enter a state. The only way a state ever changes.
 *
 * Three things must happen together on every change and they are all here: the clock every
 * timeout and every "first 1 second" rule is measured from, the gate reset that stops a
 * half-built run or a stale release gap from carrying across, and the models-on flag the
 * other two tasks read.
 *********************************************************************************************************************/
static void conv_enter(conv_ctx_t * c, conv_state_t next, uint32_t now_ms)
{
    c->state      = next;
    c->entered_ms = now_ms;

    gate_reset(&c->gate, now_ms);

    /* Entry work that belongs to one state only. */
    if (S_GAME_COUNT == next)
    {
        /* The demo picks its throw before it has seen anything, so it cannot be accused of
         * cheating. */
        switch (conv_pick(3U))
        {
            case 0:  c->my_throw = GESTURE_FIST;      break;   /* rock     */
            case 1:  c->my_throw = GESTURE_OPEN_PALM; break;   /* paper    */
            default: c->my_throw = GESTURE_VICTORY;   break;   /* scissors */
        }

        c->child_throw = GESTURE_UNKNOWN;
    }
    else if (S_STORY_GEN == next)
    {
        c->prompt_index        = (uint8_t) conv_pick(STORY_PROMPT_COUNT);
        c->last_story_text_ms  = now_ms;

        /* A refused request used to be invisible here: the state moved
         * to S_STORY_GEN anyway, story_is_running() was still true because of the PREVIOUS
         * story, and the child watched an empty text area until the old story ended and dropped
         * the demo into S_POST_STORY. It looked like a random freeze.
         *
         * A refusal now takes the same route story_is_disabled() takes at S_STORY_PROMPT: back
         * to the game invitation. Nothing below this point needs redoing - entered_ms and the
         * gate reset above used this same now_ms, and models_on is read from `next` after it. */
        if (!story_start(c->prompt_index))
        {
            next     = S_GREETING;
            c->state = next;
        }
    }
    else
    {
        /* Every other state needs nothing on entry. */
    }

    c->models_on   = g_conv_rows[next].models_on;
    c->story_running = story_is_running();

    conv_publish_models_on(c->models_on);
}

/**********************************************************************************************************************
 * The answer of one question state. Returns true when the shape moved the demo on.
 *
 * The three question shapes are the same in all four states and only the thumbs-up destination
 * differs, so this is written once.
 *********************************************************************************************************************/
static bool conv_answer(conv_ctx_t * c, gesture_t accepted, conv_state_t on_yes,
                        conv_state_t on_no, uint32_t now_ms)
{
    if (GESTURE_THUMBS_UP == accepted)
    {
        conv_enter(c, on_yes, now_ms);
        return true;
    }

    if (GESTURE_THUMBS_DOWN == accepted)
    {
        conv_enter(c, on_no, now_ms);
        return true;
    }

    if (GESTURE_FIST == accepted)
    {
        conv_enter(c, S_GOODBYE, now_ms);
        return true;
    }

    /* A shape the demo knows, shown in a state that does not accept it, is ignored and nothing
     * else happens: the window keeps running and the question stays. */
    return false;
}

/**********************************************************************************************************************
 * Public functions
 *********************************************************************************************************************/

void conv_init(conv_ctx_t * c, uint32_t now_ms)
{
    if (NULL == c)
    {
        return;
    }

    c->my_throw          = GESTURE_UNKNOWN;
    c->child_throw       = GESTURE_UNKNOWN;
    c->last_result       = 0;
    c->prompt_index      = 0U;
    c->models_on         = false;
    c->story_running     = false;
    c->last_story_text_ms = now_ms;

    /* S_BOOT holds for two seconds, then the resting screen appears. */
    conv_enter(c, S_BOOT, now_ms);
}

conv_state_t conv_state(const conv_ctx_t * c)
{
    if (NULL == c)
    {
        return S_BOOT;
    }

    return c->state;
}

void conv_force_idle(conv_ctx_t * c, uint32_t now_ms)
{
    if (NULL == c)
    {
        return;
    }

    /* SW2, the booth reset. If a story is being written it is dropped, or the next visitor would
     * watch the last one finish. */
    if (story_is_running())
    {
        story_abandon();
    }

    conv_enter(c, S_IDLE, now_ms);
}

void conv_step(conv_ctx_t * c, gesture_t accepted, uint32_t now_ms)
{
    const conv_row_t * row;
    bool               moved = false;

    if (NULL == c)
    {
        return;
    }

    c->story_running = story_is_running();

    row = &g_conv_rows[c->state];

    /* 1. What an accepted shape does here. Nothing below may run before this: a child who
     *    answers in the last moment of the window must be understood, not timed out. */
    if (GESTURE_UNKNOWN != accepted)
    {
        switch (c->state)
        {
            case S_IDLE:
            {
                if (GESTURE_OPEN_PALM == accepted)
                {
                    conv_enter(c, S_GREETING, now_ms);
                    moved = true;
                }
                else if (GESTURE_FIST == accepted)
                {
                    conv_enter(c, S_GOODBYE, now_ms);
                    moved = true;
                }
                else
                {
                    /* Not an accepted shape here. Ignored. */
                }

                break;
            }

            case S_GREETING:
            {
                moved = conv_answer(c, accepted, S_GAME_COUNT, S_STORY_PROMPT, now_ms);
                break;
            }

            case S_POST_GAME:
            {
                moved = conv_answer(c, accepted, S_GAME_COUNT, S_STORY_PROMPT, now_ms);
                break;
            }

            case S_STORY_PROMPT:
            case S_POST_STORY:
            {
                /* With the story feature off, a yes here must not hang the
                 * demo. It answers MY STORY BOOK IS CLOSED TODAY - drawn by ui_screen.c from
                 * story_is_disabled() - and returns to the game invitation. */
                conv_state_t on_yes = story_is_disabled() ? S_GREETING : S_STORY_GEN;

                moved = conv_answer(c, accepted, on_yes, S_GREETING, now_ms);
                break;
            }

            case S_GAME_THROW:
            {
                if ((GESTURE_FIST      == accepted) ||
                    (GESTURE_OPEN_PALM == accepted) ||
                    (GESTURE_VICTORY   == accepted))
                {
                    c->child_throw = accepted;
                    c->last_result = conv_round_result(accepted, c->my_throw);

                    conv_enter(c, S_GAME_RESULT, now_ms);
                    moved = true;
                }

                break;
            }

            default:
            {
                /* S_BOOT, S_GAME_COUNT, S_GAME_RESULT, S_STORY_GEN, S_NO_GESTURE and S_GOODBYE
                 * accept nothing. The gate returns GESTURE_UNKNOWN in all six anyway; this is
                 * the second lock on the same door. */
                break;
            }
        }
    }

    if (moved)
    {
        return;
    }

    /* 2. The story state leaves on the story, not on a clock. */
    if (S_STORY_GEN == c->state)
    {
        if (!story_is_running())
        {
            conv_enter(c, S_POST_STORY, now_ms);
        }
        else if (conv_elapsed(c->last_story_text_ms, now_ms) >= CONV_STORY_STALL_MS)
        {
            /* Fifteen seconds with no new text. The generator is stuck or far slower than the
             * measured 3.69 tokens per second. Give the visitor what there is rather than a
             * frozen screen. */
            story_abandon();
            conv_enter(c, S_POST_STORY, now_ms);
        }
        else
        {
            /* Still writing. */
        }

        return;
    }

    /* 3. The timeout of every other state. */
    if ((CONV_NO_TIMEOUT != row->timeout_ms) &&
        (conv_elapsed(c->entered_ms, now_ms) >= row->timeout_ms))
    {
        conv_enter(c, row->on_timeout, now_ms);
    }
}

/**********************************************************************************************************************
 * The models-on flag (see conv_state.h).
 *********************************************************************************************************************/

bool conv_quit_hint(const conv_ctx_t * c)
{
    if (NULL == c)
    {
        return false;
    }

    return g_conv_rows[c->state].quit_hint;
}

bool conv_models_on(void)
{
    return g_models_on;
}

void conv_publish_models_on(bool on)
{
    g_models_on = on;
}
