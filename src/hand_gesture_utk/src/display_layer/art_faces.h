/**********************************************************************************************************************
 * File Name    : art_faces.h
 * Description  : The seven whole still faces of Stage 1.
 *
 * GENERATED FILE - DO NOT EDIT BY HAND.
 * Written by tools\make_face_art.py from the seven pictures in assets\emotional_faces, which
 * are READ ONLY. Run the script again if the artwork changes.
 *
 * The pictures live in Octo-SPI flash and the start-up copy of .sdram_from_ospi0_cs1 moves
 * them into SDRAM before any task runs, so nothing here is read from flash while the demo
 * runs. The copy runs in single-lane mode, before the high-speed switch, so these pictures are
 * off the byte-order swap path and are NOT pre-swapped.
 *
 * WHICH FACE STANDS FOR WHICH EXPRESSION IS NOT HERE. That mapping lives in ui_artwork.c, so
 * this file stays a plain list of pictures.
 *********************************************************************************************************************/

#ifndef ART_FACES_H_
#define ART_FACES_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Zone B of the screen layout. */
#define ART_FACE_W        (384)
#define ART_FACE_H        (384)

/* Seven files. */
typedef enum e_art_face_id
{
    ART_FACE_ATTENTIVE = 0,
    ART_FACE_CURIOUS,
    ART_FACE_EAGER,
    ART_FACE_EXCITED,
    ART_FACE_FAREWELL,
    ART_FACE_NEUTRAL,
    ART_FACE_THINKING,
    ART_FACE_COUNT
} art_face_id_t;

extern const uint16_t art_face_attentive[ART_FACE_W * ART_FACE_H];
extern const uint16_t art_face_curious[ART_FACE_W * ART_FACE_H];
extern const uint16_t art_face_eager[ART_FACE_W * ART_FACE_H];
extern const uint16_t art_face_excited[ART_FACE_W * ART_FACE_H];
extern const uint16_t art_face_farewell[ART_FACE_W * ART_FACE_H];
extern const uint16_t art_face_neutral[ART_FACE_W * ART_FACE_H];
extern const uint16_t art_face_thinking[ART_FACE_W * ART_FACE_H];

/* Index it with an art_face_id_t. Never NULL. */
extern const uint16_t * const art_face_table[ART_FACE_COUNT];

#ifdef __cplusplus
}
#endif

#endif /* ART_FACES_H_ */
