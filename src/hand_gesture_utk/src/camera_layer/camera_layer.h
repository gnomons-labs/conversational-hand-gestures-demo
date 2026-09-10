/*
* Copyright (c) 2020 - 2025 Renesas Electronics Corporation and/or its affiliates
*
* SPDX-License-Identifier: BSD-3-Clause
*/
/**********************************************************************************************************************
 * File Name    : camera_layer.h
 * Version      : .
 * Description  : .
 *********************************************************************************************************************/

#ifndef CAMERA_CAMERA_LAYER_H_
 #define CAMERA_CAMERA_LAYER_H_

/* was: #include <cam_thread.h>, deleted here. That generated header no longer exists - FSP
 * made it only because g_cam_clk and g_cam_i2c_master sat under the Camera Display Thread, and
 * it was what declared them. Both stacks moved to HAL/Common, so the "hal_data.h" line just
 * below - which this file already had - declares exactly those two symbols now
 * (ra_gen/hal_data.h:19 and :29). No replacement include is needed.
 * THIS IS THE FULL NOTE. src\camera_layer\arducam_port.c points here. */
#include "hal_data.h"
#include "arducam.h"
#include "camera_layer_config.h"

#define CONFIG_BRIGHTNESS_DISABLE (0)
#define CONFIG_BRIGHTNESS_MINUS_3 (1)
#define CONFIG_BRIGHTNESS_MINUS_2 (2)
#define CONFIG_BRIGHTNESS_MINUS_1 (3)
#define CONFIG_BRIGHTNESS_ZERO    (4)
#define CONFIG_BRIGHTNESS_PLUS_1  (5)
#define CONFIG_BRIGHTNESS_PLUS_2  (6)
#define CONFIG_BRIGHTNESS_PLUS_3  (7)

#define CONFIG_CONTRAST_DISBALE (0)
#define CONFIG_CONTRAST_MINUS_3 (1)
#define CONFIG_CONTRAST_MINUS_2 (2)
#define CONFIG_CONTRAST_MINUS_1 (3)
#define CONFIG_CONTRAST_ZERO    (4)
#define CONFIG_CONTRAST_PLUS_1  (5)
#define CONFIG_CONTRAST_PLUS_2  (6)
#define CONFIG_CONTRAST_PLUS_3  (7)

typedef struct ov_reg
{
    uint16_t      reg_num;
    unsigned char value;
} st_ov_reg_t;

typedef enum
{
    BSP_CAM_YUV422  = 0,
    BSP_CAM_RAW_RGB = 1,
} bsp_camera_output_t;

typedef enum
{
    POWER_UP   = BSP_IO_LEVEL_LOW,
    POWER_DOWN = BSP_IO_LEVEL_HIGH,
} bsp_camera_power_t;

typedef enum
{
    BSP_CAM_FRAMERATE_10FPS  = 100,
    BSP_CAM_FRAMERATE_20FPS  = 50,
    BSP_CAM_FRAMERATE_33FPS  = 30,
    BSP_CAM_FRAMERATE_40FPS  = 25,
    BSP_CAM_FRAMERATE_50FPS  = 20,
    BSP_CAM_FRAMERATE_100FPS = 10,
} bsp_camera_framerate_t;

typedef enum
{
    BSP_CAM_VGA   = 0,                 // 640x480
    BSP_CAM_CIF   = 1,                 // 352x288
    BSP_CAM_QVGA  = 2,                 // 320x240
    BSP_CAM_QCIF  = 3,                 // 176x144
    BSP_CAM_QQVGA = 4,                 // 160x120
} bsp_camera_resolution_t;

typedef enum
{
    BSP_CAM_VGA_WIDTH    = 640,
    BSP_CAM_VGA_HEIGHT   = 480,
    BSP_CAM_CIF_WIDTH    = 352,
    BSP_CAM_CIF_HEIGHT   = 288,
    BSP_CAM_QVGA_WIDTH   = 320,
    BSP_CAM_QVGA_HEIGHT  = 240,
    BSP_CAM_QCIF_WIDTH   = 176,
    BSP_CAM_QCIF_HEIGHT  = 144,
    BSP_CAM_QQVGA_WIDTH  = 160,
    BSP_CAM_QQVGA_HEIGHT = 120,
} bsp_camera_size_list_t;


extern uint8_t  camera_capture_image_rgb565[CAMERA_ACTIVE_IMAGE_WIDTH  * CAMERA_ACTIVE_IMAGE_HEIGHT * CAMERA_IMAGE_BYTE_PER_PIXEL];
extern uint32_t camera_capture_image_rgb565_size;

/* One counter per mipi_csi_event_t value (ra\fsp\inc\api\r_mipi_csi_api.h). The enum has five
 * values and starts at 0; this is the size of the counter array, not a sixth event.
 * Added for MIPI Camera Serial Interface error counting. */
#define CAM_MIPI_CSI_EVENT_SOURCES   (5U)

FSP_CPP_HEADER
fsp_err_t camera_init (bool use_test_mode);
void      camera_image_buffer_initialize(void);

void      camera_capture_start(void);
uint32_t  camera_data_ready_buffer_pointer_get(void);

void      camera_capture_post_process(void);

/* How many times one MIPI Camera Serial Interface event source has fired.
 * @param[in] source  a mipi_csi_event_t value, 0 to CAM_MIPI_CSI_EVENT_SOURCES - 1
 * @return    the count, or 0 when source is out of range */
uint32_t  camera_mipi_csi_event_count(uint32_t source);

/* Data-lane plus virtual-channel events: the two sources that mean something is wrong on the
 * camera link. It is printed every 100 events. */
uint32_t  camera_mipi_csi_error_count(void);
FSP_CPP_FOOTER

#endif /* CAMERA_CAMERA_LAYER_H_ */
