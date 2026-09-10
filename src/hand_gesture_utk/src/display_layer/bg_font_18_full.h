/*
* Copyright (c) 2020 - 2025 Renesas Electronics Corporation and/or its affiliates
*
* SPDX-License-Identifier: BSD-3-Clause
*/
/**********************************************************************************************************************
 * File Name    : bg_font_18_full.h
 * Description  : This file defines the exported MIPI LCD text print functions.
 **********************************************************************************************************************/

/* GIMP RGB C-Source image dump (bg_font_18_full.h) */

#include "hal_data.h"

#ifndef BG_FONT_18_FULL_H_
#define BG_FONT_18_FULL_H_

FSP_CPP_HEADER
void print_bg_font_18(d2_device *handle, d2_point _xs, d2_point _ys, float scaling, char *_str);

/* How wide a string is in pixels at one scaling, summed from the real glyph widths.
 * Added 2026-08-13 so the screen measures the story wrap width in code rather than assuming a
 * fixed character width. See the body in bg_font_18_full.c. */
uint32_t bg_font_18_text_width(const char *_str, float scaling);
FSP_CPP_FOOTER

#endif // BG_FONT_18_FULL_H_

