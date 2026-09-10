/**********************************************************************************************************************
 * File Name    : ui_screen.c
 * Description  : The whole screen: five zones, one frame per panel refresh.
 *
 * New file. It holds the seven drawing orders, the pictures, the five zones and every line of
 * text.
 *
 * IT REPLACES src\display_layer\detection_screen_mipi.c, which is not copied. Two things are
 * LIFTED from it and are marked at their functions: the camera blit
 * (detection_screen_mipi.c:105-122) and the skeleton drawing (:148-194). The five-gesture fork's
 * developer overlay (:369-396) is lifted as well, because the tuning session needs it.
 * The sidebar, the static labels, the dead bounding-box code and the pipeline-time print are
 * dropped.
 *
 * TWO DRAWING RULES THAT MUST NOT BE LOST.
 *   1. d2_setalpha is GLOBAL drawing state. ui_draw_hints and ui_draw_fade both change it and
 *      both put it back to 0xff before returning, or everything drawn afterwards is
 *      transparent. ui_art_draw does the same for the pictures it draws.
 *   2. The cache flush stays as the sample wrote it - only the buffer just drawn into, never the
 *      whole cache, because a full clean starves the display controller mid-line. THAT RULE IS
 *      NOT RE-IMPLEMENTED HERE: it lives in the drawing engine's own porting layer, d1_cacheflush
 *      in src\display_layer\display_layer.c:269-282, which this file must not work around. The
 *      only cache call in this file is the one the camera picture needs before it is read by the
 *      drawing hardware, which is what the sample does too.
 *
 * THE ONE PIECE OF STATE THIS FILE OWNS is g_gesture_overlay_on, and ui_set_overlay is its only
 * writer. No interrupt writes application state any more.
 *********************************************************************************************************************/

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hal_data.h"

#include "common_util.h"
#include "application_config.h"
#include "ai_application_config.h"

#include "camera_layer.h"
#include "console_output.h"
#include "time_counter.h"

#include "gesture_classify.h"
#include "landmark_display.h"

#include "conv_state.h"
#include "gesture_gate.h"
#include "story_api.h"

#include "bg_font_18_full.h"
#include "display_layer.h"
#include "display_layer_config.h"
#include "ui_artwork.h"
#include "ui_screen.h"

/**********************************************************************************************************************
 * The five zones. The panel is 1024 x 600.
 *
 * Zone B and zone B2 together are exactly the 384 x 480 right-hand column, and zone C1 and
 * zone C2 together are exactly the 1024 x 120 strip along the bottom. THE SPLIT IS FIXED IN
 * EVERY STATE, so text never has to be re-flowed when the state changes.
 *********************************************************************************************************************/

#define ZONE_A_X            (0)          /* camera, at its native size, 1:1 */
#define ZONE_A_Y            (0)
#define ZONE_A_W            (640)
#define ZONE_A_H            (480)

#define ZONE_B_X            (640)        /* face, or the demo's own throw   */
#define ZONE_B_Y            (0)
#define ZONE_B_W            (384)
#define ZONE_B_H            (384)

#define ZONE_B2_X           (640)        /* the hand-shape hint icons       */
#define ZONE_B2_Y           (384)
#define ZONE_B2_W           (384)
#define ZONE_B2_H           (96)

#define ZONE_C1_X           (0)          /* speech, or three story lines    */
#define ZONE_C1_Y           (480)
#define ZONE_C1_W           (804)
#define ZONE_C1_H           (120)

#define ZONE_C2_X           (804)        /* CONTINUE? and the quit hint     */
#define ZONE_C2_Y           (480)
#define ZONE_C2_W           (220)
#define ZONE_C2_H           (120)

/**********************************************************************************************************************
 * The times of the conversation.
 *********************************************************************************************************************/

#define UI_FIRST_SECOND_MS  (1000U)   /* the "first 1 s" face and line of three states       */
#define UI_IDLE_INTRO_MS    (3000U)   /* SHOW AN OPEN HAND TO START, then SHOW ME YOUR HAND  */
#define UI_HELP_AFTER_MS    (5000U)   /* the first 5 s of an answer window show only the
                                       * question; then the face turns puzzled and the hand
                                       * shapes appear                                       */
#define UI_FADE_MS          (2000U)   /* the goodbye fade, none to full                      */
#define UI_COUNT_STEP_MS    (750U)    /* 3 2 1 GO inside a 3,000 ms state - see ui_speech     */
#define UI_COUNT_GO_MS      (3U * UI_COUNT_STEP_MS)   /* the GO step: both hands land here   */

/**********************************************************************************************************************
 * Text sizes.
 *********************************************************************************************************************/

#define UI_SPEECH_SCALING   (1.25f)   /* the sample's own setting                            */
#define UI_STORY_SCALING    (1.0f)    /* three lines of 22-pixel glyphs in a 120-pixel strip  */
#define UI_COLUMN_SCALING   (0.5f)    /* the hint column                                     */

#define UI_GLYPH_H          (22)      /* every entry in the font table is 22 rows tall        */

/* The three rolling story lines. */
#define UI_STORY_LINES      (3)
#define UI_STORY_LINE_CHARS (128U)    /* roughly 75 to 80 characters fit; this cannot overrun */
#define UI_STORY_MARGIN     (8)
#define UI_STORY_LINE_PITCH (30)
#define UI_STORY_TOP        (ZONE_C1_Y + 12)
#define UI_STORY_LINE_PX    (ZONE_C1_W - (2 * UI_STORY_MARGIN))

/* Colours, 24-bit red-green-blue as d2_setcolor takes them. */
#define UI_BLACK            (0x00000000)
#define UI_SKELETON_DOT     (0x00FF0000)   /* red, as the fork draws the 21 points  */
#define UI_SKELETON_BONE    (0x000000FF)   /* blue, as the fork draws the 23 bones  */

/* Half brightness for a hand shape the release gate is holding shut. */
#define UI_DIM_ALPHA        (0x80U)
#define UI_FULL_ALPHA       (0xffU)

/* Every d2_render call in this project takes sixteenths of a pixel. d2_point and d2_width are
 * both signed 16-bit (ra\tes\dave2d\inc\dave_driver.h:139 and :154), and the largest value this
 * file ever shifts is 1024, which becomes 16,384 - inside the range. */
#define UI_FX(v)            ((d2_point) (((int32_t) (v)) << 4))
#define UI_FXW(v)           ((d2_width) (((int32_t) (v)) << 4))

/* The tuning overlay, lifted from the five-gesture fork (detection_screen_mipi.c:92-96). */
#define OVERLAY_X            (10)
#define OVERLAY_Y            (8)
#define OVERLAY_LINE_PITCH   (26)
#define OVERLAY_FONT_SCALING (1.0f)
#define OVERLAY_TEXT_LEN     (24)

/* The font self test line of the tuning overlay. See ui_draw_tuning_overlay. Kept next to the
 * other overlay settings so the two are read together. */
static char g_font_self_test[] = "I'I I\"I I,I I?I I!I";

/**********************************************************************************************************************
 * Every line of speech. Written out once, here, so that no state can disagree with another
 * about its own words. Screen text is plain upper-case ASCII.
 *********************************************************************************************************************/

static const char UI_TXT_BANNER[]      = "MICRO T KERNEL 3 0 ON RA8P1";
static const char UI_TXT_STORAGE_ERR[] = "STORAGE ERROR - DEMO IS LIMITED";
static const char UI_TXT_IDLE_INTRO[]  = "SHOW AN OPEN HAND TO START";
static const char UI_TXT_IDLE[]        = "SHOW ME YOUR HAND";
static const char UI_TXT_HI[]          = "HI";
static const char UI_TXT_GAME_Q[]      = "DO YOU LIKE TO PLAY A GAME?";
static const char UI_TXT_THROW[]       = "SHOW ROCK PAPER OR SCISSORS";
static const char UI_TXT_WIN[]         = "YOU WIN";
static const char UI_TXT_LOSE[]        = "I WIN";
static const char UI_TXT_DRAW[]        = "SAME";
static const char UI_TXT_CONTINUE[]    = "CONTINUE?";
static const char UI_TXT_GLAD[]        = "I'M GLAD";
static const char UI_TXT_STORY_Q[]     = "WANT TO TELL A STORY?";
static const char UI_TXT_STORY_OFF[]   = "MY STORY BOOK IS CLOSED TODAY";
static const char UI_TXT_NO_GESTURE[]  = "NO RECOGNIZED GESTURES";
static const char UI_TXT_BYE[]         = "BYE";
static const char UI_TXT_RELEASE[]     = "LOWER YOUR HAND AND SHOW IT AGAIN";
static const char UI_TXT_QUIT[]        = "SHOW A FIST TO SAY BYE";

/**********************************************************************************************************************
 * Private global variables
 *********************************************************************************************************************/

/* The three rolling story lines. Line 2 is the one being written into. */
static char     g_story_line[UI_STORY_LINES][UI_STORY_LINE_CHARS];
static uint32_t g_story_len;                       /* characters on the bottom line */

/* Set once by ui_init, from the warm-start latch. */
static bool     g_artwork_ok = true;

static uint32_t ui_text_measure(const char * s, float scaling);
static void     ui_fill_box(int x, int y, int w, int h, uint32_t colour);
static void     ui_clear_zones(void);
static bool     ui_is_question_state(conv_state_t st);
static bool     ui_release_prompt_on(const conv_ctx_t * c, uint32_t now_ms);
static expr_t   ui_expr_for(const conv_ctx_t * c, uint32_t now_ms);
static const char * ui_speech(const conv_ctx_t * c, uint32_t now_ms, char * buf, size_t buf_len);

static void     ui_draw_camera(void);
static void     ui_draw_skeleton(void);
static void     ui_draw_face(expr_t e);
static void     ui_draw_throw(gesture_t t);
static void     ui_draw_hints(const conv_ctx_t * c, uint32_t now_ms);
static void     ui_draw_text(const char * s);
static void     ui_draw_story_lines(void);
static void     ui_draw_hint_column(const conv_ctx_t * c, uint32_t now_ms);
static void     ui_draw_fade(const conv_ctx_t * c, uint32_t now_ms);
static void     ui_draw_tuning_overlay(void);

static void     ui_story_newline(void);
static void     ui_story_putc(char ch);

/**********************************************************************************************************************
 * How wide a string is, in whole pixels, at one scaling.
 *
 * The wrap width is measured in code, never assumed. Roughly 75 to 80 characters is the
 * sizing estimate, not the rule. The sum is made by the font file itself, which owns the glyph
 * widths, so the measurement and the drawing can never disagree (bg_font_18_full.c,
 * bg_font_18_text_width).
 *********************************************************************************************************************/
static uint32_t ui_text_measure(const char * s, float scaling)
{
    return bg_font_18_text_width(s, scaling);
}

/**********************************************************************************************************************
 * One filled rectangle, in whole screen pixels.
 *
 * d2_renderbox is at ra\tes\dave2d\inc\dave_driver.h:599:
 *   d2_s32 d2_renderbox(d2_device *handle, d2_point x1, d2_point y1, d2_width w, d2_width h);
 * Neither fork uses it, so the signature above is copied out of the drawing engine's own
 * header rather than guessed.
 *********************************************************************************************************************/
static void ui_fill_box(int x, int y, int w, int h, uint32_t colour)
{
    d2_setcolor(d2_handle, 0, (d2_color) colour);
    d2_renderbox(d2_handle, UI_FX(x), UI_FX(y), UI_FXW(w), UI_FXW(h));
}

/**********************************************************************************************************************
 * Paint every zone that is not fully covered by what is drawn into it.
 *
 * The demo draws into a back buffer that still holds the frame from two refreshes ago, so a
 * shape that has moved would leave its old copy behind. Zone A needs nothing: the camera blit
 * covers all 640 x 480 of it. The right-hand column and the bottom strip do, because a face is
 * followed by a smaller placeholder, and a long line by a short one.
 *
 * BLACK is the demo's background colour everywhere: the faces are flattened onto black by
 * tools\make_face_art.py, so a black zone B has no visible edge around the picture. If that
 * colour ever changes it must change in both places.
 *********************************************************************************************************************/
static void ui_clear_zones(void)
{
    ui_fill_box(ZONE_B_X, ZONE_B_Y, ZONE_B_W, ZONE_B_H + ZONE_B2_H, UI_BLACK);
    ui_fill_box(ZONE_C1_X, ZONE_C1_Y, ZONE_C1_W + ZONE_C2_W, ZONE_C1_H, UI_BLACK);
}

/**********************************************************************************************************************
 * The four states with a 15 second answer window.
 *********************************************************************************************************************/
static bool ui_is_question_state(conv_state_t st)
{
    return ((S_GREETING == st) || (S_POST_GAME == st) ||
            (S_STORY_PROMPT == st) || (S_POST_STORY == st));
}

/**********************************************************************************************************************
 * True when LOWER YOUR HAND AND SHOW IT AGAIN belongs on the screen.
 *
 * The words appear for a held THUMBS-DOWN and for nothing else, from 5 seconds into an answer
 * window. A blocked fist is drawn dim by ui_draw_hints but never replaces the question,
 * because a rested fist is the common case and the child still has both answers in hand.
 *********************************************************************************************************************/
static bool ui_release_prompt_on(const conv_ctx_t * c, uint32_t now_ms)
{
    if (!ui_is_question_state(c->state))
    {
        return false;
    }

    if ((uint32_t) (now_ms - c->entered_ms) < UI_HELP_AFTER_MS)
    {
        return false;
    }

    return gate_is_blocked(&c->gate, GESTURE_THUMBS_DOWN);
}

/**********************************************************************************************************************
 * True when the demo's own throw belongs in zone B instead of the face.
 *
 * FAIRNESS. The demo picks its throw when S_GAME_COUNT is entered (conv_state.c, conv_enter),
 * before it has seen anything, so it cannot cheat. The screen used to keep that throw hidden
 * until S_GAME_RESULT, and S_GAME_RESULT is entered only AFTER the child's shape has been
 * recognised, so the demo looked like it answered the child. The throw is now uncovered on the
 * GO step of the countdown - the same beat the child throws on - and stays up through the throw
 * window and the result. Both hands land together and only the words WIN, I WIN or SAME arrive
 * later.
 *********************************************************************************************************************/
static bool ui_throw_is_shown(const conv_ctx_t * c, uint32_t now_ms)
{
    if ((S_GAME_THROW == c->state) || (S_GAME_RESULT == c->state))
    {
        return true;
    }

    if (S_GAME_COUNT == c->state)
    {
        return ((uint32_t) (now_ms - c->entered_ms) >= UI_COUNT_GO_MS);
    }

    return false;
}

/**********************************************************************************************************************
 * Which expression this state wants right now. After 5 seconds in an answer window the face
 * turns puzzled, whatever the state's own row says.
 *********************************************************************************************************************/
static expr_t ui_expr_for(const conv_ctx_t * c, uint32_t now_ms)
{
    const uint32_t t = (uint32_t) (now_ms - c->entered_ms);

    /* From 5 seconds into an answer window the face helps, whatever the state's own row says. */
    if (ui_is_question_state(c->state) && (t >= UI_HELP_AFTER_MS))
    {
        return EXPR_PUZZLED;
    }

    switch (c->state)
    {
        case S_BOOT:
        {
            /* A failure that keeps the screen shows the sorry face, and the
             * external flash failing at reset is the one failure that is already known here. */
            return g_artwork_ok ? EXPR_STILL : EXPR_SORRY;
        }

        case S_IDLE:         return EXPR_IDLE;
        case S_GAME_COUNT:   return EXPR_EAGER;
        case S_GAME_THROW:   return EXPR_EXCITED;
        case S_STORY_GEN:    return EXPR_TELLING;
        case S_NO_GESTURE:   return EXPR_PUZZLED;
        case S_GOODBYE:      return EXPR_WAVING;

        case S_GREETING:
        case S_STORY_PROMPT:
        {
            return (t < UI_FIRST_SECOND_MS) ? EXPR_HAPPY : EXPR_ASKING;
        }

        case S_POST_GAME:
        {
            if (t < UI_FIRST_SECOND_MS)
            {
                /* last_result is the CHILD's point of view: +1 the child won, -1 the child
                 * lost (conv_state.c). A draw is treated like a win, because pleased covers a
                 * win or a draw. */
                return (c->last_result >= 0) ? EXPR_PLEASED : EXPR_CONSOLING;
            }

            return EXPR_ASKING;
        }

        case S_GAME_RESULT:
        {
            /* Never drawn: order 3b puts the throw picture in zone B instead, and the face and
             * the throw are never both on screen. */
            return EXPR_ASKING;
        }

        case S_POST_STORY:   return EXPR_ASKING;

        default:             return EXPR_SORRY;
    }
}

/**********************************************************************************************************************
 * The one line of speech this state wants, or NULL when the text area belongs to the story.
 *
 * Every line comes from the speech table above, word for word.
 *
 * @param[in]  buf, buf_len  scratch, for the one line that is built rather than chosen
 *********************************************************************************************************************/
static const char * ui_speech(const conv_ctx_t * c, uint32_t now_ms, char * buf, size_t buf_len)
{
    const uint32_t t = (uint32_t) (now_ms - c->entered_ms);

    /* The release prompt takes the place of the question, wherever the question is. In
     * S_POST_STORY the question lives in the hint column, so it is NOT taken here: the three
     * story lines are never touched. */
    if ((S_POST_STORY != c->state) && ui_release_prompt_on(c, now_ms))
    {
        return UI_TXT_RELEASE;
    }

    switch (c->state)
    {
        case S_BOOT:
        {
            /* The Stage 2 banner. This build runs on micro T-Kernel 3.0, so it says so. */
            return g_artwork_ok ? UI_TXT_BANNER : UI_TXT_STORAGE_ERR;
        }

        case S_IDLE:
        {
            return (t < UI_IDLE_INTRO_MS) ? UI_TXT_IDLE_INTRO : UI_TXT_IDLE;
        }

        case S_GREETING:
        {
            return (t < UI_FIRST_SECOND_MS) ? UI_TXT_HI : UI_TXT_GAME_Q;
        }

        case S_GAME_COUNT:
        {
            /* 3 2 1 GO. THE STATE IS 3,000 ms LONG (conv_state.c, CONV_COUNT_MS) and four
             * numbers at one per second would need 4,000, so the three seconds are cut into
             * four equal steps of 750 ms instead. The countdown still reads 3, 2, 1, GO and the
             * state length does not move. The GO step is also when the demo's own throw is
             * uncovered - see ui_throw_is_shown. */
            const uint32_t step = t / UI_COUNT_STEP_MS;

            switch (step)
            {
                case 0:  snprintf(buf, buf_len, "3");  break;
                case 1:  snprintf(buf, buf_len, "2");  break;
                case 2:  snprintf(buf, buf_len, "1");  break;
                default: snprintf(buf, buf_len, "GO"); break;
            }

            return buf;
        }

        case S_GAME_THROW:   return UI_TXT_THROW;

        case S_GAME_RESULT:
        {
            if (c->last_result > 0)
            {
                return UI_TXT_WIN;
            }

            if (c->last_result < 0)
            {
                return UI_TXT_LOSE;
            }

            return UI_TXT_DRAW;
        }

        case S_POST_GAME:    return UI_TXT_CONTINUE;

        case S_STORY_PROMPT:
        {
            /* With the story feature off the demo says so instead of
             * inviting an answer it cannot honour. conv_state.c already sends a yes here
             * straight back to the game invitation. */
            if (story_is_disabled())
            {
                return UI_TXT_STORY_OFF;
            }

            return (t < UI_FIRST_SECOND_MS) ? UI_TXT_GLAD : UI_TXT_STORY_Q;
        }

        case S_STORY_GEN:
        case S_POST_STORY:
        {
            /* The text area belongs to the three rolling lines, and at the end of a story they
             * stay exactly where they are through the whole of S_POST_STORY. */
            return NULL;
        }

        case S_NO_GESTURE:   return UI_TXT_NO_GESTURE;
        case S_GOODBYE:      return UI_TXT_BYE;

        default:             return NULL;
    }
}

/**********************************************************************************************************************
 * Order 1: the camera picture, zone A, 1:1.
 *
 * LIFTED FROM detection_screen_mipi.c:105-122, with one change the design orders: the fork
 * scaled the 640 x 480 picture up by CAMERA_IMAGE_SCALING (1.25) to 800 x 600, and functional
 * demo draws it at 1:1 instead, because 640 x 480 leaves the 384-pixel column for the face
 * and a 1:1 blit is the cheaper one.
 *
 * The cache clean stays: the buffer is written by the camera path and read by the drawing
 * hardware, so the two must see the same bytes.
 *********************************************************************************************************************/
static void ui_draw_camera(void)
{
#if (BSP_CFG_DCACHE_ENABLED == 1)
    SCB_CleanDCache_by_Addr(&camera_capture_image_rgb565[0], (int32_t) camera_capture_image_rgb565_size);
#endif

    d2_setblitsrc(d2_handle, (void *) &camera_capture_image_rgb565[0],
                  CAMERA_CAPTURE_IMAGE_WIDTH, CAMERA_CAPTURE_IMAGE_WIDTH,
                  CAMERA_CAPTURE_IMAGE_HEIGHT, d2_mode_rgb565);

    d2_blitcopy(d2_handle,
                (d2_s32) CAMERA_CAPTURE_IMAGE_WIDTH, (d2_s32) CAMERA_CAPTURE_IMAGE_HEIGHT,
                (d2_blitpos) 0, (d2_blitpos) 0,
                UI_FXW(ZONE_A_W), UI_FXW(ZONE_A_H),
                UI_FX(ZONE_A_X), UI_FX(ZONE_A_Y),
                d2_tm_filter);
}

/**********************************************************************************************************************
 * Order 2: the skeleton over the camera picture. Only while the models are on.
 *
 * LIFTED FROM draw_landmark_points, detection_screen_mipi.c:148-194: the same 21 dots, the same
 * 23 bones and the same connection table. Two things are dropped and one changed.
 *   - The scaling is gone. Landmark points are in camera pixels and the picture is now drawn at
 *     1:1, so the point coordinates are the screen coordinates.
 *   - The "Left" or "Right" label with its handedness hysteresis is dropped. It is developer
 *     information, and zone A carries the picture and the skeleton only.
 *   - The dot radius drops from 6 to 4, because the picture is no longer scaled up by 1.25.
 *********************************************************************************************************************/

/**********************************************************************************************************************
 * Is one landmark point inside the camera zone?
 *
 * UI_FX shifts left by four into d2_point, which is signed 16-bit (dave_driver.h:154), so any
 * coordinate outside about plus or minus 2,047 wraps and the dot or the bone jumps to a random
 * place on the screen. landmark_postprocess remaps from a ROTATED crop, so it can return points
 * outside the 640 x 480 frame when a hand is half out of view. A point outside the zone is
 * skipped rather than clamped: a clamped joint would be drawn on the zone edge and read as a
 * real, wrong measurement.
 *
 * @param[in] x  camera pixel column
 * @param[in] y  camera pixel row
 * @return    true when the point may be drawn
 *********************************************************************************************************************/
static bool ui_pt_in_zone_a(int32_t x, int32_t y)
{
    return ((x >= ZONE_A_X) && (x < (ZONE_A_X + ZONE_A_W)) &&
            (y >= ZONE_A_Y) && (y < (ZONE_A_Y + ZONE_A_H)));
}

static void ui_draw_skeleton(void)
{
    /* The 23 bones of the hand, exactly as the fork lists them. */
    static const uint8_t skeleton[23][2] =
    {
        {0,1},{1,2},{2,3},{3,4},         /* thumb       */
        {0,5},{5,6},{6,7},{7,8},         /* index       */
        {0,9},{9,10},{10,11},{11,12},    /* middle      */
        {0,13},{13,14},{14,15},{15,16},  /* ring        */
        {0,17},{17,18},{18,19},{19,20},  /* little      */
        {5,9},{9,13},{13,17}             /* palm cross  */
    };

    for (uint8_t slot = 0U; slot < AI_MAX_DETECTION_NUM; slot++)
    {
        const landmark_result_t * lm = &g_landmark_results[slot];

        if ((lm->num_points <= 0) || (lm->hand_score < GESTURE_MIN_LANDMARK_SCORE))
        {
            continue;
        }

        d2_setcolor(d2_handle, 0, (d2_color) UI_SKELETON_DOT);

        for (int k = 0; k < lm->num_points; k++)
        {
            /* Never shift a point that would wrap d2_point. */
            if (!ui_pt_in_zone_a((int32_t) lm->pts[k].x, (int32_t) lm->pts[k].y))
            {
                continue;
            }

            d2_rendercircle(d2_handle,
                            UI_FX((int32_t) lm->pts[k].x), UI_FX((int32_t) lm->pts[k].y),
                            UI_FXW(4), UI_FXW(0));
        }

        d2_setcolor(d2_handle, 0, (d2_color) UI_SKELETON_BONE);

        for (uint32_t s = 0U; s < 23U; s++)
        {
            const uint8_t a = skeleton[s][0];
            const uint8_t b = skeleton[s][1];

            /* Both ends must be inside, because a line takes two points and either one of them
             * would wrap on its own. */
            if (!ui_pt_in_zone_a((int32_t) lm->pts[a].x, (int32_t) lm->pts[a].y) ||
                !ui_pt_in_zone_a((int32_t) lm->pts[b].x, (int32_t) lm->pts[b].y))
            {
                continue;
            }

            d2_renderline(d2_handle,
                          UI_FX((int32_t) lm->pts[a].x), UI_FX((int32_t) lm->pts[a].y),
                          UI_FX((int32_t) lm->pts[b].x), UI_FX((int32_t) lm->pts[b].y),
                          UI_FXW(1), 0);
        }
    }
}

/**********************************************************************************************************************
 * Order 3: the face, zone B.
 *
 * Stage 1: ONE WHOLE 384 x 384 PICTURE and no time argument, so this order is one blit
 * instead of three. The blink and the talking mouth come back at Stage 2, and nothing outside
 * ui_artwork.c changes when they do.
 *********************************************************************************************************************/
static void ui_draw_face(expr_t e)
{
    ui_art_draw(ui_art_face(e), ZONE_B_X, ZONE_B_Y, UI_FULL_ALPHA);
}

/**********************************************************************************************************************
 * Order 3b: the demo's own throw, zone B, in S_GAME_RESULT only. It replaces the face.
 *********************************************************************************************************************/
static void ui_draw_throw(gesture_t t)
{
    ui_art_draw(ui_art_throw(t), ZONE_B_X, ZONE_B_Y, UI_FULL_ALPHA);
}

/**********************************************************************************************************************
 * Order 4: the accepted hand shapes, zone B2, from 5 seconds into an answer window.
 *
 * A shape gate_is_blocked reports is drawn at HALF brightness, so the help never asks the child
 * for the one thing that cannot work.
 *
 * The three shapes are the same in all four question states: thumbs up, thumbs down and the
 * fist. They are laid out evenly across the 384-pixel zone: three 96 x 96 icons with four equal
 * 24-pixel gaps.
 *********************************************************************************************************************/
static void ui_draw_hints(const conv_ctx_t * c, uint32_t now_ms)
{
    static const gesture_t shapes[3] = { GESTURE_THUMBS_UP, GESTURE_THUMBS_DOWN, GESTURE_FIST };

    const int gap = (ZONE_B2_W - (3 * 96)) / 4;

    if (!ui_is_question_state(c->state))
    {
        return;
    }

    if ((uint32_t) (now_ms - c->entered_ms) < UI_HELP_AFTER_MS)
    {
        return;
    }

    for (int i = 0; i < 3; i++)
    {
        const uint8_t alpha = gate_is_blocked(&c->gate, shapes[i]) ? UI_DIM_ALPHA : UI_FULL_ALPHA;
        const int     x     = ZONE_B2_X + gap + (i * (96 + gap));

        ui_art_draw(ui_art_hint(shapes[i]), x, ZONE_B2_Y, alpha);
    }

    /* PUT THE ALPHA BACK. ui_art_draw already does it, and this second line is deliberate:
     * this function is one of the two that must restore it, and the rule has to be visible
     * where the reader looks for it. */
    d2_setalpha(d2_handle, (d2_alpha) UI_FULL_ALPHA);
}

/**********************************************************************************************************************
 * Order 5a: one line of speech, centred in zone C1, at the sample's own 1.25 scaling.
 *********************************************************************************************************************/
static void ui_draw_text(const char * s)
{
    uint32_t w;
    int      x;
    int      y;

    if ((NULL == s) || ('\0' == s[0]))
    {
        return;
    }

    w = ui_text_measure(s, UI_SPEECH_SCALING);
    x = ZONE_C1_X + ((ZONE_C1_W - (int) w) / 2);
    y = ZONE_C1_Y + ((ZONE_C1_H - (int) ((float) UI_GLYPH_H * UI_SPEECH_SCALING)) / 2);

    if (x < ZONE_C1_X)
    {
        x = ZONE_C1_X;
    }

    print_bg_font_18(d2_handle, (d2_point) x, (d2_point) y, UI_SPEECH_SCALING, (char *) s);
}

/**********************************************************************************************************************
 * Order 5b: the three rolling story lines, zone C1, at scaling 1.0.
 *********************************************************************************************************************/
static void ui_draw_story_lines(void)
{
    for (int i = 0; i < UI_STORY_LINES; i++)
    {
        if ('\0' == g_story_line[i][0])
        {
            continue;
        }

        print_bg_font_18(d2_handle,
                         (d2_point) (ZONE_C1_X + UI_STORY_MARGIN),
                         (d2_point) (UI_STORY_TOP + (i * UI_STORY_LINE_PITCH)),
                         UI_STORY_SCALING,
                         g_story_line[i]);
    }
}

/**********************************************************************************************************************
 * Order 6: the hint column, zone C2, both lines at scaling 0.5.
 *
 * Upper half: CONTINUE? in S_POST_STORY, or the release prompt in its place while a thumbs-down
 * is being held shut. That is the ONLY line that can take CONTINUE?'s place there, and the three
 * story lines never move for it.
 *
 * Lower half: SHOW A FIST TO SAY BYE, in the five states conv_quit_hint names. It is off in all
 * three game-round states, because a fist is Rock there.
 *
 * WHEN THE EXTERNAL FLASH NEVER OPENED, the demo keeps running the whole conversation with
 * placeholder artwork rather than being pinned to S_IDLE, because a visitor still gets a
 * working gesture demo instead of a dead screen.
 * But the operator must still be able to SEE that the board is degraded, not only hear it on the
 * console and see LED3. So the degraded line lives here, in the upper half of the hint column,
 * for the whole run.
 *
 * WHY HERE AND NOT IN THE TEXT STRIP. The text strip carries the questions the child answers. A
 * permanent error line there would make the demo unusable.
 * The upper half of this column is empty in every state except S_POST_STORY, so it costs nothing
 * and collides with nothing. In S_POST_STORY, CONTINUE? and the release prompt keep the slot,
 * because they are what the child must read to answer.
 *********************************************************************************************************************/
static void ui_draw_hint_column(const conv_ctx_t * c, uint32_t now_ms)
{
    const char * upper = NULL;

    if (S_POST_STORY == c->state)
    {
        upper = ui_release_prompt_on(c, now_ms) ? UI_TXT_RELEASE : UI_TXT_CONTINUE;
    }
    else if (!g_artwork_ok)
    {
        upper = UI_TXT_STORAGE_ERR;
    }
    else
    {
        /* Nothing to say in the upper half. */
    }

    if (NULL != upper)
    {
        print_bg_font_18(d2_handle,
                         (d2_point) (ZONE_C2_X + 6), (d2_point) (ZONE_C2_Y + 18),
                         UI_COLUMN_SCALING, (char *) upper);
    }

    if (conv_quit_hint(c))
    {
        print_bg_font_18(d2_handle,
                         (d2_point) (ZONE_C2_X + 6), (d2_point) (ZONE_C2_Y + 70),
                         UI_COLUMN_SCALING, (char *) UI_TXT_QUIT);
    }
}

/**********************************************************************************************************************
 * Order 7: the goodbye fade, zone B, S_GOODBYE only.
 *
 * A black rectangle whose transparency rises from none to full over 2,000 ms. The face is
 * drawn first, as usual, and this covers it.
 *********************************************************************************************************************/
static void ui_draw_fade(const conv_ctx_t * c, uint32_t now_ms)
{
    const uint32_t t = (uint32_t) (now_ms - c->entered_ms);
    uint32_t       alpha;

    alpha = (t >= UI_FADE_MS) ? 255U : ((t * 255U) / UI_FADE_MS);

    d2_setalpha(d2_handle, (d2_alpha) alpha);
    ui_fill_box(ZONE_B_X, ZONE_B_Y, ZONE_B_W, ZONE_B_H, UI_BLACK);

    /* PUT THE ALPHA BACK. This function and ui_draw_hints are the two the rule names, and this
     * one is the one that would be noticed last: the fade ends at full black, so a forgotten
     * 0xff would leave the NEXT frame's whole screen invisible. */
    d2_setalpha(d2_handle, (d2_alpha) UI_FULL_ALPHA);
}

/**********************************************************************************************************************
 * The developer tuning overlay, over the top-left of the camera picture. SW1 turns it on.
 *
 * LIFTED FROM the five-gesture fork, detection_screen_mipi.c:345-396 (format_ratio and
 * draw_tuning_overlay). The overlay comes across with the two drawing functions, because the
 * tuning session reads the thumb-direction sign off it.
 *
 * The C library here is built without floating point in printf, so "%f" would print nothing.
 * The values are formatted by hand on integers, exactly as the fork does.
 *********************************************************************************************************************/
static void ui_format_ratio(char * dst, float v)
{
    float rounded = (v >= 0.0f) ? ((v * 100.0f) + 0.5f) : ((v * 100.0f) - 0.5f);

    if (rounded >  99999.0f) { rounded =  99999.0f; }
    if (rounded < -99999.0f) { rounded = -99999.0f; }

    int32_t hundredths = (int32_t) rounded;
    bool    negative   = (hundredths < 0);

    if (negative)
    {
        hundredths = -hundredths;
    }

    sprintf(dst, "%s%d.%02d", negative ? "-" : "",
            (int) (hundredths / 100), (int) (hundredths % 100));
}

static void ui_draw_tuning_overlay(void)
{
    static const char * const labels[7] = { "IDX ", "MID ", "RNG ", "LIT ", "THUMB ", "DIR ", "ABOVE " };

    const gesture_metrics_t * m = gesture_track_metrics();
    const float values[7] = { m->r_index, m->r_middle, m->r_ring, m->r_little, m->t, m->d, m->u };

    char    value[12];
    char    line[OVERLAY_TEXT_LEN];
    int16_t y = OVERLAY_Y;

    for (int i = 0; i < 7; i++)
    {
        ui_format_ratio(value, values[i]);
        snprintf(line, sizeof(line), "%s%s", labels[i], value);
        print_bg_font_18(d2_handle, OVERLAY_X, y, OVERLAY_FONT_SCALING, line);
        y = (int16_t) (y + OVERLAY_LINE_PITCH);
    }

    snprintf(line, sizeof(line), "PALM %d", (int) m->s);
    print_bg_font_18(d2_handle, OVERLAY_X, y, OVERLAY_FONT_SCALING, line);
    y = (int16_t) (y + OVERLAY_LINE_PITCH);

    snprintf(line, sizeof(line), "RAW %s",
             m->usable ? gesture_name(gesture_track_raw()) : "no hand");
    print_bg_font_18(d2_handle, OVERLAY_X, y, OVERLAY_FONT_SCALING, line);
    y = (int16_t) (y + OVERLAY_LINE_PITCH);

    /* THE FONT SELF TEST, added 2026-08-19 with bench fix 2.
     *
     * Every mark this font draws by hand is shown here between two capital I's, at the SAME
     * scaling 1.0 the story text uses. One photo of this line says whether each mark draws, so
     * nobody has to wait for a story that happens to contain the mark they want to check. The
     * two I's give a control glyph on each side, so a mark that draws nothing is obvious.
     *
     * It costs the demo nothing: the whole overlay is off until SW1 turns it on.
     *
     * The array is static and writable, so no cast is needed and no -Wwrite-strings warning is
     * possible. print_bg_font_18 never writes through the pointer. */
    print_bg_font_18(d2_handle, OVERLAY_X, y, OVERLAY_FONT_SCALING, g_font_self_test);
}

/**********************************************************************************************************************
 * The three rolling lines: scroll up by one and start a fresh bottom line.
 *********************************************************************************************************************/
static void ui_story_newline(void)
{
    for (int i = 0; i < (UI_STORY_LINES - 1); i++)
    {
        memcpy(g_story_line[i], g_story_line[i + 1], UI_STORY_LINE_CHARS);
    }

    g_story_line[UI_STORY_LINES - 1][0] = '\0';
    g_story_len = 0U;
}

/**********************************************************************************************************************
 * Add one character to the bottom line, wrapping at a whole word.
 *
 * The wrap point is the sum of the real glyph widths, not a character count: the line breaks
 * when the word being written would pass 788 pixels, which is zone C1 less its two margins.
 *********************************************************************************************************************/
static void ui_story_putc(char ch)
{
    char * line = g_story_line[UI_STORY_LINES - 1];

    if (('\n' == ch) || ('\r' == ch))
    {
        if (0U != g_story_len)
        {
            ui_story_newline();
        }

        return;
    }

    /* A leading space on a fresh line is dropped, so a wrapped sentence starts at the margin. */
    if ((0U == g_story_len) && (' ' == ch))
    {
        return;
    }

    if (g_story_len >= (UI_STORY_LINE_CHARS - 1U))
    {
        ui_story_newline();
        line = g_story_line[UI_STORY_LINES - 1];
    }

    line[g_story_len]      = ch;
    line[g_story_len + 1U] = '\0';
    g_story_len++;

    if (ui_text_measure(line, UI_STORY_SCALING) <= (uint32_t) UI_STORY_LINE_PX)
    {
        return;
    }

    /* The line is too wide. Move the unfinished word down to the next line. */
    {
        char     carry[UI_STORY_LINE_CHARS];
        uint32_t split = g_story_len;   /* first character of the word that moves down */
        bool     at_space;

        while ((split > 0U) && (' ' != line[split - 1U]))
        {
            split--;
        }

        at_space = (split > 0U);

        if (!at_space)
        {
            /* One word longer than a whole line, so there is no space to break at. Break it
             * where it stands, so the text keeps moving instead of growing off the right-hand
             * edge. Only the last character moves down, and NO CHARACTER IS LOST. */
            split = g_story_len - 1U;
        }

        (void) memcpy(carry, &line[split], (size_t) (g_story_len - split));
        carry[g_story_len - split] = '\0';

        /* With a space to break at, the space itself is dropped, so the next line starts at the
         * margin. Without one, nothing is dropped. */
        line[at_space ? (split - 1U) : split] = '\0';

        ui_story_newline();

        line = g_story_line[UI_STORY_LINES - 1];
        (void) strcpy(line, carry);
        g_story_len = (uint32_t) strlen(line);
    }
}

/**********************************************************************************************************************
 * Public functions
 *********************************************************************************************************************/

void ui_init(void)
{
    ui_story_text_clear();

    /* APP_DEGRADE_EXTERNAL: with the external flash unopened the seven face
     * arrays hold nothing, so the artwork layer is told to draw placeholders instead of blitting
     * whatever the SDRAM happens to contain. The camera picture and all the text still work. */
    g_artwork_ok = (FSP_SUCCESS == g_startup_ospi_err);
    ui_art_set_available(g_artwork_ok);

    /* The tuning overlay is off at reset, as it is in the five-gesture fork. */
    ui_set_overlay(false);
}

void ui_set_overlay(bool on)
{
    /* THE ONLY WRITER of this flag. In the fork the SW2 interrupt handler
     * toggled it straight from the interrupt; now the button callback only reports the press and
     * T_UI keeps the on-or-off state. */
    g_gesture_overlay_on = on;
}

void ui_story_text_clear(void)
{
    for (int i = 0; i < UI_STORY_LINES; i++)
    {
        g_story_line[i][0] = '\0';
    }

    g_story_len = 0U;
}

void ui_story_text_append(const char * text)
{
    if (NULL == text)
    {
        return;
    }

    for (size_t i = 0U; '\0' != text[i]; i++)
    {
        ui_story_putc(text[i]);
    }
}

void ui_draw_frame(const conv_ctx_t * c, uint32_t now_ms)
{
    char         scratch[8];
    const char * speech;
    expr_t       e;

    if (NULL == c)
    {
        return;
    }

    graphics_start_frame();

    ui_clear_zones();

    /* 1. The camera picture. Live in every state. */
    ui_draw_camera();

    /* 2. The skeleton, only while the models are on. */
    if (conv_models_on())
    {
        ui_draw_skeleton();
    }

    /* 3 and 3b. The face, or the demo's own throw. Never both. */
    if (ui_throw_is_shown(c, now_ms))
    {
        ui_draw_throw(c->my_throw);
    }
    else
    {
        e = ui_expr_for(c, now_ms);
        ui_draw_face(e);
    }

    /* 4. The hand-shape hints, from 5 seconds into an answer window. */
    ui_draw_hints(c, now_ms);

    /* 5. One line of speech, or the three rolling story lines. */
    speech = ui_speech(c, now_ms, scratch, sizeof(scratch));

    if (NULL != speech)
    {
        ui_draw_text(speech);
    }
    else
    {
        ui_draw_story_lines();
    }

    /* 6. The hint column. */
    ui_draw_hint_column(c, now_ms);

    /* 7. The goodbye fade, over the face. */
    if (S_GOODBYE == c->state)
    {
        ui_draw_fade(c, now_ms);
    }

    /* The developer overlay sits on top of everything, so a tuning session can read the numbers
     * whatever the demo is doing. */
    if (g_gesture_overlay_on)
    {
        ui_draw_tuning_overlay();
    }

    graphics_end_frame();
}
