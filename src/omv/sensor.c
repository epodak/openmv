/*
 * This file is part of the OpenMV project.
 * Copyright (c) 2013/2014 Ibrahim Abdelkader <i.abdalkader@gmail.com>
 * This work is licensed under the MIT license, see the file LICENSE for details.
 *
 * Sensor abstraction layer.
 *
 */
#include <stdlib.h>
#include <string.h>
#include <stm32f4xx_hal.h>

#include "sccb.h"
#include "ov9650.h"
#include "ov2640.h"
#include "ov7725.h"
#include "sensor.h"
#include "systick.h"
#include "framebuffer.h"
#include "omv_boardconfig.h"

#define REG_PID        0x0A
#define REG_VER        0x0B

#define REG_MIDH       0x1C
#define REG_MIDL       0x1D

#define OV9650_PID     0x96
#define OV2640_PID     0x26
#define OV7725_PID     0x77

#define XCLK_FREQ      (12000000)

#define MAX_XFER_SIZE (0xFFFC)

struct sensor_dev sensor;
TIM_HandleTypeDef  TIMHandle;
DMA_HandleTypeDef  DMAHandle;
DCMI_HandleTypeDef DCMIHandle;

static int line = 0;
extern uint8_t _line_buf;

const int resolution[][2] = {
    {88,    72 },    /* QQCIF */
    {160,   120},    /* QQVGA */
    {128,   160},    /* QQVGA2*/
    {176,   144},    /* QCIF  */
    {220,   160},    /* HQVGA */
    {320,   240},    /* QVGA  */
    {352,   288},    /* CIF   */
    {640,   480},    /* VGA   */
    {800,   600},    /* SVGA  */
    {1280,  1024},   /* SXGA  */
    {1600,  1200},   /* UXGA  */
};

static int extclk_config(int frequency)
{
    /* TCLK (PCLK2 * 2) */
    int tclk  = HAL_RCC_GetPCLK2Freq() * 2;

    /* SYSCLK/TCLK = No prescaler */
    int prescaler = (uint16_t) (HAL_RCC_GetSysClockFreq()/ tclk) - 1;

    /* Period should be even */
    int period = (tclk / frequency)-1;

    /* Timer base configuration */
    TIMHandle.Instance          = DCMI_TIM;
    TIMHandle.Init.Period       = period;
    TIMHandle.Init.Prescaler    = prescaler;
    TIMHandle.Init.ClockDivision = 0;
    TIMHandle.Init.CounterMode   = TIM_COUNTERMODE_UP;

    /* Timer channel configuration */
    TIM_OC_InitTypeDef TIMOCHandle;
    TIMOCHandle.Pulse       = period/2;
    TIMOCHandle.OCMode      = TIM_OCMODE_PWM1;
    TIMOCHandle.OCPolarity  = TIM_OCPOLARITY_HIGH;
    TIMOCHandle.OCFastMode  = TIM_OCFAST_DISABLE;
    TIMOCHandle.OCIdleState = TIM_OCIDLESTATE_RESET;

    if (HAL_TIM_PWM_Init(&TIMHandle) != HAL_OK
            || HAL_TIM_PWM_ConfigChannel(&TIMHandle, &TIMOCHandle, DCMI_TIM_CHANNEL) != HAL_OK
            || HAL_TIM_PWM_Start(&TIMHandle, DCMI_TIM_CHANNEL) != HAL_OK) {
        // Initialization Error
        return -1;
    }

    return 0;
}

static int dcmi_config(uint32_t jpeg_mode)
{
    /* DCMI configuration */
    DCMIHandle.Instance         = DCMI;
    DCMIHandle.Init.VSPolarity  = SENSOR_HW_FLAGS_GET(&sensor, SENSOR_HW_FLAGS_VSYNC) ? /* VSYNC clock polarity */
                                    DCMI_VSPOLARITY_HIGH : DCMI_VSPOLARITY_LOW; 
    DCMIHandle.Init.HSPolarity  = SENSOR_HW_FLAGS_GET(&sensor, SENSOR_HW_FLAGS_HSYNC) ? /* HSYNC clock polarity */
                                    DCMI_HSPOLARITY_HIGH : DCMI_HSPOLARITY_LOW; 
    DCMIHandle.Init.PCKPolarity = SENSOR_HW_FLAGS_GET(&sensor, SENSOR_HW_FLAGS_PIXCK) ? /* PXCLK clock polarity */
                                    DCMI_PCKPOLARITY_RISING : DCMI_PCKPOLARITY_FALLING; 
    DCMIHandle.Init.SynchroMode = DCMI_SYNCHRO_HARDWARE;        /* Enable Hardware synchronization      */
    DCMIHandle.Init.CaptureRate = DCMI_CR_ALL_FRAME;            /* Capture rate all frames              */
    DCMIHandle.Init.ExtendedDataMode = DCMI_EXTEND_DATA_8B;     /* Capture 8 bits on every pixel clock  */
    DCMIHandle.Init.JPEGMode = jpeg_mode;                       /* Set JPEG Mode                        */

    /* Associate the DMA handle to the DCMI handle */
    __HAL_LINKDMA(&DCMIHandle, DMA_Handle, DMAHandle);

    /* Configure and enable DCMI IRQ Channel */
    HAL_NVIC_SetPriority(DCMI_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(DCMI_IRQn);

    /* Init DCMI */
    if (HAL_DCMI_Init(&DCMIHandle) != HAL_OK) {
        // Initialization Error
        return -1;
    }

    return 0;
}

static int dma_config()
{
    /* DMA Stream configuration */
    DMAHandle.Instance              = DMA2_Stream1;             /* Select the DMA instance          */
    DMAHandle.Init.Channel          = DMA_CHANNEL_1;            /* DMA Channel                      */
    DMAHandle.Init.Direction        = DMA_PERIPH_TO_MEMORY;     /* Peripheral to memory transfer    */
    DMAHandle.Init.MemInc           = DMA_MINC_ENABLE;          /* Memory increment mode Enable     */
    DMAHandle.Init.PeriphInc        = DMA_PINC_DISABLE;         /* Peripheral increment mode Enable */
    DMAHandle.Init.PeriphDataAlignment  = DMA_PDATAALIGN_WORD;  /* Peripheral data alignment : Word */
    DMAHandle.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;      /* Memory data alignment : Word     */
    DMAHandle.Init.Mode             = DMA_NORMAL;               /* Normal DMA mode                  */
    DMAHandle.Init.Priority         = DMA_PRIORITY_HIGH;        /* Priority level : high            */
    DMAHandle.Init.FIFOMode         = DMA_FIFOMODE_ENABLE;      /* FIFO mode enabled                */
    DMAHandle.Init.FIFOThreshold    = DMA_FIFO_THRESHOLD_FULL;  /* FIFO threshold full              */
    DMAHandle.Init.MemBurst         = DMA_MBURST_INC4;          /* Memory burst                     */
    DMAHandle.Init.PeriphBurst      = DMA_PBURST_SINGLE;        /* Peripheral burst                 */

    /* Configure and enable DMA IRQ Channel */
    HAL_NVIC_SetPriority(DMA2_Stream1_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(DMA2_Stream1_IRQn);

    /* Initialize the DMA stream */
    if (HAL_DMA_Init(&DMAHandle) != HAL_OK) {
        /// Initialization Error
        return -1;
    }

    return 0;
}

void sensor_init0()
{
    // Clear framebuffer
    memset(fb, 0, sizeof(*fb));
}

int sensor_init()
{
    /* Do a power cycle */
    DCMI_PWDN_HIGH();
    systick_sleep(10);

    DCMI_PWDN_LOW();
    systick_sleep(10);

    /* Initialize the SCCB interface */
    SCCB_Init();
    systick_sleep(10);

    /* Configure the sensor external clock (XCLK) to XCLK_FREQ.
       Note: The sensor's internal PLL (when CLKRC=0x80) doubles the XCLK_FREQ
             (XCLK=XCLK_FREQ*2), and the unscaled PIXCLK output is XCLK_FREQ*4 */
    if (extclk_config(XCLK_FREQ) != 0) {
        // Timer problem
        return -1;
    }

    /* Uncomment this to pass through the MCO1 clock (HSI=16MHz) this results in a
       64MHz PIXCLK output from the sensor.
       Note: The maximum pixel clock input on the STM32F4xx is 54MHz,
             the STM32F7 can probably handle higher input pixel clock.
       */
    //(void) extclk_config;
    //HAL_RCC_MCOConfig(RCC_MCO1, RCC_MCO1SOURCE_HSI, RCC_MCODIV_1);

    /* Reset the sesnor state */
    memset(&sensor, 0, sizeof(struct sensor_dev));

    /* Some sensors have different reset polarities, and we can't know which sensor
       is connected before initializing SCCB and probing the sensor, which in turn
       requires pulling the sensor out of the reset state. So we try to probe the
       sensor with both polarities to determine line state. */
    sensor.reset_pol = ACTIVE_HIGH;

    /* Reset the sensor */
    DCMI_RESET_HIGH();
    systick_sleep(10);

    DCMI_RESET_LOW();
    systick_sleep(10);

    /* Probe the sensor */
    sensor.slv_addr = SCCB_Probe();
    if (sensor.slv_addr == 0) {
        /* Sensor has been held in reset,
           so the reset line is active low */
        sensor.reset_pol = ACTIVE_LOW;

        /* Pull the sensor out of the reset state */
        DCMI_RESET_HIGH();
        systick_sleep(10);

        /* Probe again to set the slave addr */
        sensor.slv_addr = SCCB_Probe();
        if (sensor.slv_addr == 0)  {
            // Probe failed
            return -2;
        }
    }

    /* Read the sensor information */
    sensor.id.PID  = SCCB_Read(sensor.slv_addr, REG_PID);
    sensor.id.VER  = SCCB_Read(sensor.slv_addr, REG_VER);
    sensor.id.MIDL = SCCB_Read(sensor.slv_addr, REG_MIDL);
    sensor.id.MIDH = SCCB_Read(sensor.slv_addr, REG_MIDH);

    /* Call the sensor-specific init function */
    switch (sensor.id.PID) {
        case OV9650_PID:
            ov9650_init(&sensor);
            break;
        case OV2640_PID:
            ov2640_init(&sensor);
            break;
        case OV7725_PID:
            ov7725_init(&sensor);
            break;
        default:
            /* Sensor not supported */
            return -3;
    }

    /* Configure the DCMI DMA Stream */
    if (dma_config() != 0) {
        // DMA problem
        return -4;
    }

    /* Configure the DCMI interface. This should be called
       after ovxxx_init to set VSYNC/HSYNC/PCLK polarities */
    if (dcmi_config(DCMI_JPEG_DISABLE) != 0){
        // DCMI config failed
        return -5;
    }

    /* All good! */
    return 0;
}

int sensor_reset()
{
    // Reset the sesnor state
    sensor.sde = 0xFF;
    sensor.pixformat=0xFF;
    sensor.framesize=0xFF;
    sensor.framerate=0xFF;
    sensor.gainceiling=0xFF;

    // Call sensor-specific reset function
    sensor.reset(&sensor);

    // Just in case there's a running DMA request.
    HAL_DMA_Abort(&DMAHandle);
    return 0;
}

int sensor_read_reg(uint8_t reg)
{
    return SCCB_Read(sensor.slv_addr, reg);
}

int sensor_write_reg(uint8_t reg, uint8_t val)
{
    return SCCB_Write(sensor.slv_addr, reg, val);
}

int sensor_set_pixformat(enum sensor_pixformat pixformat)
{
    uint32_t jpeg_mode = DCMI_JPEG_DISABLE;

    if (sensor.pixformat == pixformat) {
        /* no change */
        return 0;
    }

    if (sensor.set_pixformat == NULL
        || sensor.set_pixformat(&sensor, pixformat) != 0) {
        /* operation not supported */
        return -1;
    }

    /* set pixel format */
    sensor.pixformat = pixformat;

    /* set bytes per pixel */
    switch (pixformat) {
        case PIXFORMAT_GRAYSCALE:
            fb->bpp    = 1;
            break;
        case PIXFORMAT_RGB565:
        case PIXFORMAT_YUV422:
            fb->bpp    = 2;
            break;
        case PIXFORMAT_JPEG:
            fb->bpp    = 0;
            break;
        default:
            return -1;
    }

    if (pixformat == PIXFORMAT_JPEG) {
        jpeg_mode = DCMI_JPEG_ENABLE;
    }

    return dcmi_config(jpeg_mode);
}

int sensor_set_framesize(enum sensor_framesize framesize)
{
    if (sensor.framesize == framesize) {
       /* no change */
        return 0;
    }

    /* call the sensor specific function */
    if (sensor.set_framesize == NULL
        || sensor.set_framesize(&sensor, framesize) != 0) {
        /* operation not supported */
        return -1;
    }

    /* set framebuffer size */
    sensor.framesize = framesize;

    /* set framebuffer dimensions */
    if (framesize < FRAMESIZE_QQCIF
            || framesize > FRAMESIZE_UXGA) {
        return -1;
    } else {
        fb->w = resolution[framesize][0];
        fb->h = resolution[framesize][1];
    }

    return 0;
}

int sensor_set_framerate(enum sensor_framerate framerate)
{
    if (sensor.framerate == framerate) {
       /* no change */
        return 0;
    }

    /* call the sensor specific function */
    if (sensor.set_framerate == NULL
        || sensor.set_framerate(&sensor, framerate) != 0) {
        /* operation not supported */
        return -1;
    }

    /* set the frame rate */
    sensor.framerate = framerate;

    return 0;
}

int sensor_set_contrast(int level)
{
    if (sensor.set_contrast != NULL) {
        return sensor.set_contrast(&sensor, level);
    }
    return -1;
}

int sensor_set_brightness(int level)
{
    if (sensor.set_brightness != NULL) {
        return sensor.set_brightness(&sensor, level);
    }
    return -1;
}

int sensor_set_saturation(int level)
{
    if (sensor.set_saturation != NULL) {
        return sensor.set_saturation(&sensor, level);
    }
    return -1;
}

int sensor_set_exposure(int exposure)
{
    return 0;
}

int sensor_set_gainceiling(enum sensor_gainceiling gainceiling)
{
    if (sensor.gainceiling == gainceiling) {
        /* no change */
        return 0;
    }

    /* call the sensor specific function */
    if (sensor.set_gainceiling == NULL
        || sensor.set_gainceiling(&sensor, gainceiling) != 0) {
        /* operation not supported */
        return -1;
    }

    sensor.gainceiling = gainceiling;
    return 0;
}

int sensor_set_quality(int qs)
{
    /* call the sensor specific function */
    if (sensor.set_quality == NULL
        || sensor.set_quality(&sensor, qs) != 0) {
        /* operation not supported */
        return -1;
    }
    return 0;
}

int sensor_set_colorbar(int enable)
{
    /* call the sensor specific function */
    if (sensor.set_colorbar == NULL
        || sensor.set_colorbar(&sensor, enable) != 0) {
        /* operation not supported */
        return -1;
    }
    return 0;
}

int sensor_set_special_effect(enum sensor_sde sde)
{
    if (sensor.sde == sde) {
        /* no change */
        return 0;
    }

    /* call the sensor specific function */
    if (sensor.set_special_effect == NULL
        || sensor.set_special_effect(&sensor, sde) != 0) {
        /* operation not supported */
        return -1;
    }

    sensor.sde = sde;
    return 0;
}

// This function is called back after each line transfer is complete,
// with a pointer to the line buffer that was used. At this point the
// DMA transfers the next line to the other half of the line buffer.
// Note:  For JPEG this function is called once (and ignored) at the end of the transfer.
void DCMI_DMAConvCpltUser(uint32_t addr)
{
    uint8_t *src = (uint8_t*) addr;
    uint8_t *dst = fb->pixels + FB_JPEG_OFFS_SIZE;

    if (sensor.pixformat == PIXFORMAT_GRAYSCALE) {
        dst += line++ * fb->w;
        // If GRAYSCALE extract Y channel from YUV
        for (int i=0; i<fb->w; i++) {
            dst[i] = src[i<<1];
        }
    } else if (sensor.pixformat == PIXFORMAT_RGB565) {
        dst += line++ * fb->w * 2;
        for (int i=0; i<fb->w * 2; i++) {
            dst[i] = src[i];
        }
    }
}

// The JPEG offset allows JPEG compression of the framebuffer without overwriting the pixels.
// The offset size may need to be adjusted depending on the quality, otherwise JPEG data may
// overwrite image pixels before they are compressed.
int sensor_snapshot(struct image *image)
{
    volatile uint32_t addr;
    volatile uint16_t length;
    uint32_t snapshot_start;

    // Compress the framebuffer for the IDE only for non-JPEG
    // images and only if the IDE has requested a framebuffer.
    // Note: This doesn't run unless the camera is connected to PC.
    if (fb->request && sensor.pixformat != PIXFORMAT_JPEG) {
        // The framebuffer is compressed in place.
        // Assuming we have at least 128KBs of SRAM.
        image_t src = {.w=fb->w, .h=fb->h, .bpp=fb->bpp,  .pixels=fb->pixels+FB_JPEG_OFFS_SIZE};
        image_t dst = {.w=fb->w, .h=fb->h, .bpp=128*1024, .pixels=fb->pixels};

        // Note: lower quality results in a faster IDE
        // framerates, since it saves on USB bandwidth.
        jpeg_compress(&src, &dst, 50);
        fb->bpp = dst.bpp;
    }

    // Note: fb->bpp is set to zero for the first JPEG frame.
    fb->ready = (fb->bpp>0);

    // Wait for the IDE to read the framebuffer before it gets overwritten with a new frame, and
    // after all the image processing code has run (which possibily draws over the framebuffer).
    // This fakes double buffering without having to allocate a second buffer and allows us to
    // re-use the framebuffer for software JPEG compression.
    while (fb->ready && fb->request) {
        // Note: This delay is only executed when the USB debug is active.
        systick_sleep(2);
    }
    fb->ready = 0;

    // Setup the size and address of the transfer
    if (sensor.pixformat == PIXFORMAT_JPEG) {
        // Sensor has hardware JPEG set max frame size.
        length = MAX_XFER_SIZE;
        addr = (uint32_t) (fb->pixels);
    } else {
        // No hardware JPEG, set w*h*2 bytes per pixel.
        length =(fb->w * fb->h * 2)/4;
        addr = (uint32_t) &_line_buf;
    }

    // Clear line counter
    line = 0;

    // Snapshot start tick
    snapshot_start = HAL_GetTick();

    if (sensor.pixformat == PIXFORMAT_JPEG) {
        // Start a regular transfer
        HAL_DCMI_Start_DMA(&DCMIHandle,
                DCMI_MODE_SNAPSHOT, addr, length);
    } else {
        // Start a multibuffer transfer (line by line)
        HAL_DCMI_Start_DMA_MB(&DCMIHandle,
                DCMI_MODE_SNAPSHOT, addr, length, fb->h);
    }

    // Wait for frame
    while ((DCMI->CR & DCMI_CR_CAPTURE) != 0) {
        if ((HAL_GetTick() - snapshot_start) >= 3000) {
            // Sensor timeout, most likely a HW issue.
            // Abort the DMA request.
            HAL_DMA_Abort(&DMAHandle);
            return -1;
        }
    }

    // Fix the BPP
    switch (sensor.pixformat) {
        case PIXFORMAT_GRAYSCALE:
            fb->bpp = 1;
            break;
        case PIXFORMAT_YUV422:
        case PIXFORMAT_RGB565:
            fb->bpp = 2;
            break;
        case PIXFORMAT_JPEG:
            // The frame readout has finished, however the DMA's still waiting for data
            // because the max frame size is set, so we need to abort the DMA transfer.
            HAL_DMA_Abort(&DMAHandle);
            // Read the number of data items transferred
            fb->bpp = (MAX_XFER_SIZE - DMAHandle.Instance->NDTR)*4;
            break;
    }

    // Set the user image.
    if (image != NULL) {
        image->w = fb->w;
        image->h = fb->h;
        image->bpp = fb->bpp;
        image->pixels = fb->pixels;
        if (sensor.pixformat != PIXFORMAT_JPEG) {
            image->pixels += FB_JPEG_OFFS_SIZE;
        }
    }

    return 0;
}
