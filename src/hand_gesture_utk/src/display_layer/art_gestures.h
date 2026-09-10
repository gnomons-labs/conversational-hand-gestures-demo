/**********************************************************************************************************************
 * File Name    : art_gestures.h
 * Description  : The three rock-paper-scissors throw pictures and the five hand-shape hint icons.
 *
 * GENERATED FILE - DO NOT EDIT BY HAND.
 * Written by tools\make_gesture_art.py from the five pictures in assets\hand_gestures, which
 * are READ ONLY. Run the script again if the artwork changes.
 *
 * These are the last eight of the twenty-three pictures the demo draws. All eight were DRAWN
 * PLACEHOLDERS at first because no files existed; the user supplied the files on 2026-08-18,
 * so the placeholders are replaced. One table in ui_artwork.c gains the pointers and NO SCREEN
 * CODE CHANGES.
 *
 * THREE PICTURES APPEAR TWICE, at two sizes. The rock, paper and scissors throws ARE the fist,
 * palm and victory hand shapes - the team leader's decision of 2026-08-21 - so one source file
 * gives both a 384 x 384 throw array and a 96 x 96 hint array. They are NOT scaled on the board:
 * ui_art_draw blits 1:1.
 *
 * The pictures live in Octo-SPI flash and the start-up copy of .sdram_from_ospi0_cs1 moves them
 * into SDRAM before any task runs, so nothing here is read from flash while the demo runs
 * runs. The copy runs in single-lane mode, before the high-speed switch, so these pictures are
 * off the byte-order swap path and are NOT pre-swapped - exactly as art_faces.h records for
 * the seven faces.
 *
 * WHICH PICTURE STANDS FOR WHICH GESTURE IS NOT HERE. That mapping is the two tables in
 * ui_artwork.c, which are indexed by gesture_t, so this file stays a plain list of pictures and
 * no second ordering of the five gestures is invented.
 *********************************************************************************************************************/

#ifndef ART_GESTURES_H_
#define ART_GESTURES_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Zone B of the screen layout, 384 x 384 at (640, 0). */
#define ART_GESTURE_THROW_W   (384)
#define ART_GESTURE_THROW_H   (384)

/* Zone B2 of the screen layout holds three of these side by side. */
#define ART_GESTURE_HINT_W    (96)
#define ART_GESTURE_HINT_H    (96)

/* The five 96 x 96 hand-shape hints. */
extern const uint16_t art_gesture_hint_open[ART_GESTURE_HINT_W * ART_GESTURE_HINT_H];
extern const uint16_t art_gesture_hint_fist[ART_GESTURE_HINT_W * ART_GESTURE_HINT_H];
extern const uint16_t art_gesture_hint_victory[ART_GESTURE_HINT_W * ART_GESTURE_HINT_H];
extern const uint16_t art_gesture_hint_thumbs_up[ART_GESTURE_HINT_W * ART_GESTURE_HINT_H];
extern const uint16_t art_gesture_hint_thumbs_down[ART_GESTURE_HINT_W * ART_GESTURE_HINT_H];

/* The three 384 x 384 throw pictures. Same three hand shapes, larger. */
extern const uint16_t art_gesture_throw_open[ART_GESTURE_THROW_W * ART_GESTURE_THROW_H];
extern const uint16_t art_gesture_throw_fist[ART_GESTURE_THROW_W * ART_GESTURE_THROW_H];
extern const uint16_t art_gesture_throw_victory[ART_GESTURE_THROW_W * ART_GESTURE_THROW_H];

#ifdef __cplusplus
}
#endif

#endif /* ART_GESTURES_H_ */
