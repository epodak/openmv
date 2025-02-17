/*
 * This file is part of the OpenMV project.
 * Copyright (c) 2013/2014 Ibrahim Abdelkader <i.abdalkader@gmail.com>
 * This work is licensed under the MIT license, see the file LICENSE for details.
 *
 * Sensor abstraction layer.
 *
 */
#ifndef __SENSOR_H__
#define __SENSOR_H__
#include <stdint.h>
#include "imlib.h"
struct sensor_id {
    uint8_t MIDH;
    uint8_t MIDL;
    uint8_t PID;
    uint8_t VER;
};

enum sensor_pixformat {
    PIXFORMAT_RGB565,    /* 2BPP/RGB565*/
    PIXFORMAT_YUV422,    /* 2BPP/YUV422*/
    PIXFORMAT_GRAYSCALE, /* 1BPP/GRAYSCALE*/
    PIXFORMAT_JPEG,      /* JPEG/COMPRESSED */
};

enum sensor_framesize {
    FRAMESIZE_QQCIF,    /* 88x72     */
    FRAMESIZE_QQVGA,    /* 160x120   */
    FRAMESIZE_QQVGA2,   /* 128x160   */
    FRAMESIZE_QCIF,     /* 176x144   */
    FRAMESIZE_HQVGA,    /* 220x160   */
    FRAMESIZE_QVGA,     /* 320x240   */
    FRAMESIZE_CIF,      /* 352x288   */
    FRAMESIZE_VGA,      /* 640x480   */
    FRAMESIZE_SVGA,     /* 800x600   */
    FRAMESIZE_SXGA,     /* 1280x1024 */
    FRAMESIZE_UXGA,     /* 1600x1200 */
};

extern const int resolution[][2];

enum sensor_framerate {
    FRAMERATE_2FPS =0x9F,
    FRAMERATE_8FPS =0x87,
    FRAMERATE_15FPS=0x83,
    FRAMERATE_30FPS=0x81,
    FRAMERATE_60FPS=0x80,
};

enum sensor_gainceiling {
    GAINCEILING_2X,
    GAINCEILING_4X,
    GAINCEILING_8X,
    GAINCEILING_16X,
    GAINCEILING_32X,
    GAINCEILING_64X,
    GAINCEILING_128X,
};

enum sensor_sde {
    SDE_NORMAL,
    SDE_NEGATIVE,
};

enum sensor_attr {
    ATTR_CONTRAST=0,
    ATTR_BRIGHTNESS,
    ATTR_SATURATION,
    ATTR_GAINCEILING,
};

enum reset_polarity {
    ACTIVE_LOW,
    ACTIVE_HIGH
};

#define SENSOR_HW_FLAGS_VSYNC        (0) // vertical sync polarity.
#define SENSOR_HW_FLAGS_HSYNC        (1) // horizontal sync polarity.
#define SENSOR_HW_FLAGS_PIXCK        (2) // pixel clock edge.
#define SENSOR_HW_FLAGS_FSYNC        (3) // hardware frame sync.
#define SENSOR_HW_FLAGS_GET(s, x)    ((s)->hw_flags &  (1<<x))
#define SENSOR_HW_FLAGS_SET(s, x, v) ((s)->hw_flags |= (v<<x))

struct sensor_dev {
    // Sensor ID.
    struct sensor_id id;
    // Sensor I2C slave address.
    uint8_t  slv_addr;
    // Hardware flags (clock polarities/hw capabilities)
    uint32_t hw_flags;
    // Reset polarity (TODO move to hw_flags)
    enum reset_polarity reset_pol;

    // Sensor state
    enum sensor_sde sde;
    enum sensor_pixformat pixformat;
    enum sensor_framesize framesize;
    enum sensor_framerate framerate;
    enum sensor_gainceiling gainceiling;

    // Sensor function pointers
    int  (*reset)           (struct sensor_dev *sensor);
    int  (*set_pixformat)   (struct sensor_dev *sensor, enum sensor_pixformat pixformat);
    int  (*set_framesize)   (struct sensor_dev *sensor, enum sensor_framesize framesize);
    int  (*set_framerate)   (struct sensor_dev *sensor, enum sensor_framerate framerate);
    int  (*set_contrast)    (struct sensor_dev *sensor, int level);
    int  (*set_brightness)  (struct sensor_dev *sensor, int level);
    int  (*set_saturation)  (struct sensor_dev *sensor, int level);
    int  (*set_exposure)    (struct sensor_dev *sensor, int exposure);
    int  (*set_gainceiling) (struct sensor_dev *sensor, enum sensor_gainceiling gainceiling);
    int  (*set_quality)     (struct sensor_dev *sensor, int quality);
    int  (*set_colorbar)    (struct sensor_dev *sensor, int enable);
    int  (*set_special_effect)  (struct sensor_dev *sensor, enum sensor_sde sde);
};

/**
 * Initialize the sensor.
 * This function will initialize SCCB and XCLK, and will attempt to detect
 * the connected sensor. If a sensor supported sensor is detected, its driver will be used.
 *
 * @param sensor A pointer to the sensor device handle.
 * @return On success, 0 is returned. If the sensor is not supported, or not detected, -1 is returned.
 */
int sensor_init();
/**
 * Initialize the sensor state.
 */
void sensor_init0();
/**
 * Reset the sensor to its default state.
 *
 * @param sensor A pointer to the sensor device handle.
 * @return On success, 0 is returned. If the sensor is not supported, or not detected, -1 is returned.
 */
int sensor_reset();
/**
 * Read a sensor register.
 *
 * @param sensor A pointer to the sensor device handle.
 * @param reg    Register address.
 * @return On success, the regsiter value is returned. Otherwise, -1 is returned.
 */
int sensor_read_reg(uint8_t reg);
/**
 * Write a sensor register.
 *
 * @param sensor A pointer to the sensor device handle.
 * @param reg Register address.
 * @param val Register value.
 * @return On success, 0 is returned. Otherwise, -1 is returned.
 */
int sensor_write_reg(uint8_t reg, uint8_t val);
/**
 * Capture a Snapshot.
 *
 * @param sensor A pointer to the sensor device handle.
 * @return  On success, 0 is returned. If the format is not supported by this sensor, -1 is returned.
 */
int sensor_snapshot(struct image *image);
/**
 * Set the sensor pixel format.
 *
 * @see   sensor_pixelformat.
 * @param sensor A pointer to the sensor device handle.
 * @param pixformat The new pixel format.
 * @return  On success, 0 is returned. If the operation not supported by the sensor, -1 is returned.
 */
int sensor_set_pixformat(enum sensor_pixformat pixformat);
/**
 * Set the sensor frame size.
 *
 * @see   sensor_framesize.
 * @param sensor A pointer to the sensor device handle.
 * @param framesize The new frame size.
 * @return  On success, 0 is returned. If the operation not supported by the sensor, -1 is returned.
 */
int sensor_set_framesize(enum sensor_framesize framesize);
/**
 * Set the sensor frame rate.
 *
 * @see   sensor_framerate.
 * @param sensor A pointer to the sensor device handle.
 * @param pixformat The new frame rate.
 * @return  On success, 0 is returned. If the operation not supported by the sensor, -1 is returned.
 */
int sensor_set_framerate(enum sensor_framerate framerate);
/**
 * Set the sensor contrast level.
 *
 * @param sensor A pointer to the sensor device handle.
 * @param level The new contrast level allowed values from -3 to +3.
 * @return  On success, 0 is returned. If the operation not supported by the sensor, -1 is returned.
 */
int sensor_set_contrast(int level);
/**
 * Set the sensor brightness level.
 *
 * @param sensor A pointer to the sensor device handle.
 * @param level The new brightness level allowed values from -3 to +3.
 * @return  On success, 0 is returned. If the operation not supported by the sensor, -1 is returned.
 */
int sensor_set_brightness(int level);
/**
 * Set the sensor saturation level.
 *
 * @param sensor A pointer to the sensor device handle.
 * @param level The new saturation level allowed values from -3 to +3.
 * @return  On success, 0 is returned. If the operation not supported by the sensor, -1 is returned.
 */
int sensor_set_saturation(int level);
/**
 * Set the sensor exposure level. This function has no
 * effect when AEC (Automatic Exposure Control) is enabled.
 *
 * @param sensor A pointer to the sensor device handle.
 * @param exposure The new exposure level.
 * @return  On success, 0 is returned. If the operation not supported by the sensor, -1 is returned.
 */
int sensor_set_exposure(int exposure);
/**
 * Set the sensor AGC gain ceiling.
 *
 * @param sensor A pointer to the sensor device handle.
 * @param exposure The new exposure level.
 * @return  On success, 0 is returned. If the operation not supported by the sensor, -1 is returned.
 */
int sensor_set_gainceiling(enum sensor_gainceiling gainceiling);
/**
 * Set the quantization scale factor, controls JPEG quality.
 *
 * @param sensor A pointer to the sensor device handle.
 * @param quality 0-255.
 * @return  On success, 0 is returned. If the operation not supported by the sensor, -1 is returned.
 */
int sensor_set_quality(int qs);
/**
 * Set the colorbar mode.
 *
 * @param sensor A pointer to the sensor device handle.
 * @param Enable enable or disable colorbar mode.
 * @return  On success, 0 is returned. If the operation not supported by the sensor, -1 is returned.
 */
int sensor_set_colorbar(int enable);
/**
 * Set special digital effects (SDE).
 *
 * @param sde Special digital effect.
 * @return  On success, 0 is returned. If the operation not supported by the sensor, -1 is returned.
 */
int sensor_set_special_effect(enum sensor_sde sde);
#endif /* __SENSOR_H__ */
