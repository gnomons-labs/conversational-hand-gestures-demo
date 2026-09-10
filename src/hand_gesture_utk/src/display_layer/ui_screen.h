/**********************************************************************************************************************
 * File Name    : ui_screen.h
 * Description  : The whole screen: five zones, one frame per panel refresh.
 *
 * New file. It replaces the vision fork's src\display_layer\detection_screen_mipi.c, which is
 * not copied: the camera blit and the skeleton drawing are lifted out of it, and the sidebar,
 * the static labels, the dead bounding-box code and the pipeline-time print are dropped.
 *
 * THIS IS THE HEADER ONLY. The bodies are in ui_screen.c. The header exists first because T_UI
 * is written against it.
 *********************************************************************************************************************/

#ifndef UI_SCREEN_H_
#define UI_SCREEN_H_

#include <stdbool.h>
#include <stdint.h>

#include "conv_state.h"   /* conv_ctx_t */

#ifdef __cplusplus
extern "C" {
#endif

/* Once, after drw_init and display_init have both returned success. */
void ui_init(void);

/* One screen frame. T_UI calls this once per EV_VSYNC. It calls graphics_start_frame(), draws
 * the seven drawing orders below, then graphics_end_frame(). */
void ui_draw_frame(const conv_ctx_t * c, uint32_t now_ms);

/* The gesture tuning overlay, on SW1. */
void ui_set_overlay(bool on);

/**********************************************************************************************************************
 * Add story text to the three rolling lines.
 *
 * T_UI drains the story ring buffer once per frame, and the story lines are drawn by
 * ui_draw_story_lines(void), which takes no argument. So the rolling lines live in ui_screen.c
 * and this call is how the drained text reaches them.
 *
 * @param[in] text  zero-ended, may be empty. Word wrapping is measured in ui_screen.c from the
 *                  real glyph widths, not from a character count.
 *********************************************************************************************************************/
void ui_story_text_append(const char * text);

/* Empty the three rolling lines. T_UI, when a story is about to start. */
void ui_story_text_clear(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_SCREEN_H_ */
