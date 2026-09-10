/**********************************************************************************************************************
 * File Name    : ui_artwork.h
 * Description  : The pictures, the expression table and the picture lookup.
 *
 * New file. Not copied from sample_code.
 *
 * WHAT THIS FILE IS FOR. Eight of the twenty-three pictures the demo needs had no files: the
 * three throw pictures and the five hand-shape hint icons. The user decided on 2026-08-12 that
 * all eight were DRAWN PLACEHOLDERS at Stage 1. This interface is what kept that decision out
 * of the screen code: ui_screen.c asks for a picture and draws it, and never learns whether the
 * picture is real.
 *
 * UPDATED 2026-08-21: THE REAL PICTURES ARRIVED. The user supplied five hand PNG files in
 * assets\hand_gestures on 2026-08-18, and the interface did what it was built to do - the two
 * tables in ui_artwork.c gained the eight pointers and NO SCREEN CODE CHANGED. The placeholder
 * path is NOT dead code: APP_DEGRADE_EXTERNAL mode still calls ui_art_set_available(false), and
 * then every picture in the demo falls back to it.
 *********************************************************************************************************************/

#ifndef UI_ARTWORK_H_
#define UI_ARTWORK_H_

#include <stdbool.h>
#include <stdint.h>

#include "gesture_classify.h"   /* gesture_t */

#ifdef __cplusplus
extern "C" {
#endif

/**********************************************************************************************************************
 * The twelve expressions the demo can show.
 *
 * Eleven of them are named by a state of the conversation, and the twelfth, EXPR_SORRY, is the
 * one entry that is not a state: every failure that keeps the screen shows it. So the failure
 * handling invents no artwork.
 *
 * At Stage 1 each of these is one whole still picture, chosen by the face table in
 * ui_artwork.c. At Stage 2 the same names become one eye picture plus one mouth picture, and
 * nothing outside ui_artwork.c has to change.
 *********************************************************************************************************************/
typedef enum e_expr
{
    EXPR_STILL = 0,     /* S_BOOT                                                        */
    EXPR_IDLE,          /* S_IDLE                                                        */
    EXPR_HAPPY,         /* S_GREETING and S_STORY_PROMPT, first second                   */
    EXPR_ASKING,        /* the waiting face of all four question states                  */
    EXPR_EAGER,         /* S_GAME_COUNT                                                  */
    EXPR_EXCITED,       /* S_GAME_THROW                                                  */
    EXPR_PLEASED,       /* S_POST_GAME, first second, after a win or a draw              */
    EXPR_CONSOLING,     /* S_POST_GAME, first second, after a loss                       */
    EXPR_TELLING,       /* S_STORY_GEN                                                   */
    EXPR_PUZZLED,       /* S_NO_GESTURE                                                  */
    EXPR_WAVING,        /* S_GOODBYE                                                     */
    EXPR_SORRY,         /* not a state: any failure that keeps the screen                */
    EXPR_COUNT
} expr_t;

/**********************************************************************************************************************
 * One picture, real or not yet supplied.
 *********************************************************************************************************************/
typedef struct
{
    const uint16_t *pixels;   /* NULL when the picture has not been supplied yet */
    uint16_t        w;
    uint16_t        h;
    const char     *label;    /* drawn inside the placeholder, e.g. "ROCK"       */
} ui_image_t;

/* The three lookups. None of them ever returns NULL, so no caller has to test. */
const ui_image_t * ui_art_face (expr_t    e);   /* one of the seven whole faces  */
const ui_image_t * ui_art_throw(gesture_t t);   /* rock, paper, scissors         */
const ui_image_t * ui_art_hint (gesture_t g);   /* the five 96 x 96 hint icons   */

/**********************************************************************************************************************
 * Draw one picture at (x, y), at one transparency.
 *
 * THIS IS THE ONLY PLACE THAT KNOWS WHETHER A PICTURE IS REAL. If pixels is not NULL it blits,
 * exactly as the camera picture is blitted. If pixels is NULL it draws the placeholder in the
 * same rectangle instead.
 *
 * It puts the drawing engine's constant alpha back to 0xff before it returns, because
 * d2_setalpha is global drawing state and anything drawn after a half-transparent picture would
 * otherwise be half transparent too.
 *
 * @param[in] img    from one of the three lookups above. NULL is allowed and draws nothing
 * @param[in] x, y   top-left corner, in whole screen pixels
 * @param[in] alpha  0xff is opaque, 0x80 is the half brightness of the fading steps
 *********************************************************************************************************************/
void ui_art_draw(const ui_image_t * img, int x, int y, uint8_t alpha);

/**********************************************************************************************************************
 * Say whether the artwork in external flash is trustworthy. Called once, from ui_init.
 *
 * WHY THIS EXISTS. The seven faces sit in the .sdram_from_ospi0_cs1 section, so they are only
 * real if the Octo-SPI opened at reset. When it did not, APP_DEGRADE_EXTERNAL mode runs the
 * demo as "a live camera picture plus text": no models, no artwork, no story. Blitting the face
 * array in that state would paint whatever the SDRAM happened to hold.
 *
 * With false, ui_art_face returns pictures whose pixels field is NULL, so the placeholder path
 * covers the faces too, and the screen code still knows nothing about it. This call is how the
 * degraded mode reaches the one place that decides what a picture looks like.
 *
 * UPDATED 2026-08-21: the three throw pictures and the five hint icons are real artwork now, in
 * the same section and reached the same way, so ui_art_throw and ui_art_hint are gated by this
 * call as well. With true it also cleans all fifteen arrays out of the data cache, once, so the
 * drawing engine reads what the reset copy wrote.
 *
 * @param[in] ok  true when g_startup_ospi_err is FSP_SUCCESS
 *********************************************************************************************************************/
void ui_art_set_available(bool ok);

#ifdef __cplusplus
}
#endif

#endif /* UI_ARTWORK_H_ */
