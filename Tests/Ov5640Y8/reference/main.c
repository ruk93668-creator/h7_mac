/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "oled.h"
#include "oled_port.h"
#include "sdram_test.h"
#include "usb_device_app.h"
#include "ov5640.h"
#include "rtl8723bs_sdio_probe.h"
#include "eth_phy_probe.h"
#include <stdio.h>
#include <string.h>

/* V0 board: R31 removed; U10 CE is tied high. */
#define ETH_ONLY_MODE 1
#define OLED_ONLY_MODE 0
#define CAMERA_PROBE_MODE 1
#define WIFI_PROBE_MODE 1
#define WIFI_ONLY_MODE 1

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
#if CAMERA_PROBE_MODE
#define CAMERA_SDA_PIN GPIO_PIN_8
#define CAMERA_SCL_PIN GPIO_PIN_9
volatile uint32_t g_camera_probe_count;
volatile uint32_t g_camera_i2c_status;
volatile uint32_t g_camera_i2c_error;
volatile uint32_t g_camera_chip_id;
volatile uint32_t g_camera_id_valid;
static uint32_t s_camera_last_probe;
static uint8_t s_camera_report_pending;

static void Camera_DelayUs(uint32_t us)
{
  uint32_t start = DWT->CYCCNT;
  uint32_t cycles = (SystemCoreClock / 1000000U) * us;
  while ((uint32_t)(DWT->CYCCNT - start) < cycles) {}
}

static void Camera_Sda(uint8_t high)
{
  HAL_GPIO_WritePin(GPIOB, CAMERA_SDA_PIN, high ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void Camera_Scl(uint8_t high)
{
  HAL_GPIO_WritePin(GPIOB, CAMERA_SCL_PIN, high ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void Camera_Start(void)
{
  Camera_Sda(1U);
  Camera_Scl(1U);
  Camera_DelayUs(5U);
  Camera_Sda(0U);
  Camera_DelayUs(5U);
  Camera_Scl(0U);
}

static void Camera_Stop(void)
{
  Camera_Sda(0U);
  Camera_DelayUs(5U);
  Camera_Scl(1U);
  Camera_DelayUs(5U);
  Camera_Sda(1U);
  Camera_DelayUs(5U);
}

static uint8_t Camera_WriteByte(uint8_t value)
{
  uint32_t bit;
  uint8_t ack;
  for (bit = 0U; bit < 8U; ++bit)
  {
    Camera_Sda((value & 0x80U) != 0U);
    Camera_DelayUs(5U);
    Camera_Scl(1U);
    Camera_DelayUs(5U);
    Camera_Scl(0U);
    value <<= 1;
  }
  Camera_Sda(1U);
  Camera_DelayUs(5U);
  Camera_Scl(1U);
  Camera_DelayUs(5U);
  ack = (HAL_GPIO_ReadPin(GPIOB, CAMERA_SDA_PIN) == GPIO_PIN_RESET) ? 1U : 0U;
  Camera_Scl(0U);
  return ack;
}

static uint8_t Camera_ReadByte(void)
{
  uint32_t bit;
  uint8_t value = 0U;
  Camera_Sda(1U);
  for (bit = 0U; bit < 8U; ++bit)
  {
    Camera_DelayUs(5U);
    Camera_Scl(1U);
    Camera_DelayUs(5U);
    value = (uint8_t)((value << 1) |
            ((HAL_GPIO_ReadPin(GPIOB, CAMERA_SDA_PIN) == GPIO_PIN_SET) ? 1U : 0U));
    Camera_Scl(0U);
  }
  /* NACK ends this single-register read. */
  Camera_Sda(1U);
  Camera_DelayUs(5U);
  Camera_Scl(1U);
  Camera_DelayUs(5U);
  Camera_Scl(0U);
  return value;
}

static uint32_t Camera_ReadReg(uint16_t reg, uint8_t *value)
{
  uint32_t error = 0U;
  Camera_Sda(1U);
  Camera_Scl(1U);
  Camera_DelayUs(10U);
  if (HAL_GPIO_ReadPin(GPIOB, CAMERA_SDA_PIN) == GPIO_PIN_RESET) error |= 0x01U;
  if (HAL_GPIO_ReadPin(GPIOB, CAMERA_SCL_PIN) == GPIO_PIN_RESET) error |= 0x02U;
  if (error != 0U) return error;

  Camera_Start();
  if (Camera_WriteByte(0x78U) == 0U) { error = 0x04U; goto done; }
  if (Camera_WriteByte((uint8_t)(reg >> 8)) == 0U) { error = 0x08U; goto done; }
  if (Camera_WriteByte((uint8_t)reg) == 0U) { error = 0x10U; goto done; }
  Camera_Stop();
  Camera_Start();
  if (Camera_WriteByte(0x79U) == 0U) { error = 0x20U; goto done; }
  *value = Camera_ReadByte();
done:
  Camera_Stop();
  return error;
}

static uint32_t Camera_WriteReg(uint16_t reg, uint8_t value)
{
  uint32_t error = 0U;
  Camera_Start();
  if (Camera_WriteByte(0x78U) == 0U) { error = 0x04U; goto done; }
  if (Camera_WriteByte((uint8_t)(reg >> 8)) == 0U) { error = 0x08U; goto done; }
  if (Camera_WriteByte((uint8_t)reg) == 0U) { error = 0x10U; goto done; }
  if (Camera_WriteByte(value) == 0U) { error = 0x40U; goto done; }
done:
  Camera_Stop();
  if (error == 0U && reg == 0x3008U && value == 0x82U) HAL_Delay(5U);
  return error;
}

static int32_t Camera_BusInit(void) { return 0; }
static int32_t Camera_BusDeInit(void) { return 0; }
static int32_t Camera_BusGetTick(void) { return (int32_t)HAL_GetTick(); }
static int32_t Camera_BusRead(uint16_t address, uint16_t reg, uint8_t *data, uint16_t length)
{
  uint16_t i;
  (void)address;
  for (i = 0U; i < length; ++i)
  {
    if (Camera_ReadReg((uint16_t)(reg + i), &data[i]) != 0U) return -1;
  }
  return 0;
}
static int32_t Camera_BusWrite(uint16_t address, uint16_t reg, uint8_t *data, uint16_t length)
{
  uint16_t i;
  (void)address;
  for (i = 0U; i < length; ++i)
  {
    if (Camera_WriteReg((uint16_t)(reg + i), data[i]) != 0U) return -1;
  }
  return 0;
}

static void Camera_ProbeInit(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /* Hold RESETB low until XCLK is running. */
  HAL_GPIO_WritePin(GPIOH, GPIO_PIN_15, GPIO_PIN_RESET);
  gpio.Pin = GPIO_PIN_15;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOH, &gpio);

  /* PLL1Q 240 MHz / 10 = 24 MHz at PA8, through R6 to camera XCLK. */
  HAL_RCC_MCOConfig(RCC_MCO1, RCC_MCO1SOURCE_PLL1QCLK, RCC_MCODIV_10);

  gpio.Pin = GPIO_PIN_8 | GPIO_PIN_9;
  gpio.Mode = GPIO_MODE_OUTPUT_OD;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_WritePin(GPIOB, CAMERA_SDA_PIN | CAMERA_SCL_PIN, GPIO_PIN_SET);
  HAL_GPIO_Init(GPIOB, &gpio);

  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0U;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

  HAL_Delay(10U);
  HAL_GPIO_WritePin(GPIOH, GPIO_PIN_15, GPIO_PIN_SET);
  HAL_Delay(20U);
  s_camera_report_pending = 1U;
}

static void Camera_ProbePoll(void)
{
  uint8_t id[2] = {0};
  uint32_t error;
  char line[96];
  uint32_t now = HAL_GetTick();

  if ((uint32_t)(now - s_camera_last_probe) >= 1000U)
  {
    s_camera_last_probe = now;
    ++g_camera_probe_count;
    error = Camera_ReadReg(0x300AU, &id[0]);
    if (error == 0U)
    {
      error = Camera_ReadReg(0x300BU, &id[1]);
      if (error != 0U) error |= 0x100U;
    }
    g_camera_i2c_status = (error == 0U) ? (uint32_t)HAL_OK : (uint32_t)HAL_ERROR;
    g_camera_i2c_error = error;
    g_camera_chip_id = ((uint32_t)id[0] << 8) | id[1];
    g_camera_id_valid = (error == 0U && g_camera_chip_id == 0x5640U) ? 1U : 0U;
    s_camera_report_pending = 1U;
    OLED_RequestRefresh();
  }

  if (s_camera_report_pending != 0U)
  {
    (void)snprintf(line, sizeof(line), "CAM ID=%04lX OK=%lu ST=%lu ERR=%08lX\r\n",
                   (unsigned long)g_camera_chip_id,
                   (unsigned long)g_camera_id_valid,
                   (unsigned long)g_camera_i2c_status,
                   (unsigned long)g_camera_i2c_error);
    if (USB_DeviceApp_SendText(line) != 0U)
    {
      s_camera_report_pending = 0U;
    }
  }
}

static void Camera_DrawStatus(void)
{
  char line[22];
  OLED_DrawString(0, 0, "OV5640 PROBE", 1U);
  (void)snprintf(line, sizeof(line), "ID:%04lX", (unsigned long)g_camera_chip_id);
  OLED_DrawString(0, 12, line, 1U);
  if (g_camera_id_valid != 0U)
  {
    OLED_DrawString(0, 24, "SCCB OK", 1U);
  }
  else
  {
    (void)snprintf(line, sizeof(line), "ERR:%03lX", (unsigned long)g_camera_i2c_error);
    OLED_DrawString(0, 24, line, 1U);
  }
}

#define CAMERA_FRAME_WIDTH 160U
#define CAMERA_FRAME_HEIGHT 120U
#define CAMERA_FRAME_BYTES (CAMERA_FRAME_WIDTH * CAMERA_FRAME_HEIGHT)
#define CAMERA_FRAME_WORDS (CAMERA_FRAME_BYTES / 4U)
#define CAMERA_FRAME_ADDRESS 0xD0000000UL

static OV5640_Object_t s_sensor;
static DCMI_HandleTypeDef s_dcmi;
static DMA_HandleTypeDef s_dcmi_dma;
static volatile uint8_t * const s_frame = (volatile uint8_t *)CAMERA_FRAME_ADDRESS;
static volatile uint8_t s_frame_ready;
static uint8_t s_colorbar_enabled;
static uint32_t s_frame_started_at;
static uint32_t s_live_last_report;
static uint32_t s_wifi_last_report;
volatile uint32_t g_live_stage;
volatile uint32_t g_live_frames;
volatile uint32_t g_live_dcmi_error;
volatile uint32_t g_live_dma_error;
volatile uint32_t g_live_dma_remaining;
volatile uint32_t g_live_hash;
volatile uint32_t g_live_min;
volatile uint32_t g_live_max;

static void Camera_DcmiPin(GPIO_TypeDef *port, uint32_t pins)
{
  GPIO_InitTypeDef gpio = {0};
  gpio.Pin = pins;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio.Alternate = GPIO_AF13_DCMI;
  HAL_GPIO_Init(port, &gpio);
}

static uint8_t Camera_DcmiInit(void)
{
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_DMA1_CLK_ENABLE();
  __HAL_RCC_DCMI_CLK_ENABLE();

  /* D0..D7: PC6 PC7 PG10 PH12 PE4 PB6 PE5 PE6. */
  Camera_DcmiPin(GPIOC, GPIO_PIN_6 | GPIO_PIN_7);
  Camera_DcmiPin(GPIOG, GPIO_PIN_10);
  Camera_DcmiPin(GPIOH, GPIO_PIN_12);
  Camera_DcmiPin(GPIOE, GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6);
  Camera_DcmiPin(GPIOB, GPIO_PIN_6 | GPIO_PIN_7); /* D5, VSYNC */
  Camera_DcmiPin(GPIOA, GPIO_PIN_4 | GPIO_PIN_6); /* HSYNC, PCLK */

  s_dcmi_dma.Instance = DMA1_Stream0;
  s_dcmi_dma.Init.Request = DMA_REQUEST_DCMI;
  s_dcmi_dma.Init.Direction = DMA_PERIPH_TO_MEMORY;
  s_dcmi_dma.Init.PeriphInc = DMA_PINC_DISABLE;
  s_dcmi_dma.Init.MemInc = DMA_MINC_ENABLE;
  s_dcmi_dma.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
  s_dcmi_dma.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
  s_dcmi_dma.Init.Mode = DMA_NORMAL;
  s_dcmi_dma.Init.Priority = DMA_PRIORITY_HIGH;
  s_dcmi_dma.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
  if (HAL_DMA_Init(&s_dcmi_dma) != HAL_OK) return 0U;

  s_dcmi.Instance = DCMI;
  s_dcmi.Init.SynchroMode = DCMI_SYNCHRO_HARDWARE;
  s_dcmi.Init.PCKPolarity = DCMI_PCKPOLARITY_RISING;
  s_dcmi.Init.VSPolarity = DCMI_VSPOLARITY_HIGH;
  s_dcmi.Init.HSPolarity = DCMI_HSPOLARITY_HIGH;
  s_dcmi.Init.CaptureRate = DCMI_CR_ALL_FRAME;
  s_dcmi.Init.ExtendedDataMode = DCMI_EXTEND_DATA_8B;
  s_dcmi.Init.JPEGMode = DCMI_JPEG_DISABLE;
  s_dcmi.Init.ByteSelectMode = DCMI_BSM_ALL;
  s_dcmi.Init.LineSelectMode = DCMI_LSM_ALL;
  __HAL_LINKDMA(&s_dcmi, DMA_Handle, s_dcmi_dma);
  if (HAL_DCMI_Init(&s_dcmi) != HAL_OK) return 0U;

  HAL_NVIC_SetPriority(DMA1_Stream0_IRQn, 5U, 0U);
  HAL_NVIC_EnableIRQ(DMA1_Stream0_IRQn);
  HAL_NVIC_SetPriority(DCMI_IRQn, 5U, 0U);
  HAL_NVIC_EnableIRQ(DCMI_IRQn);
  return 1U;
}

static uint8_t Camera_StartFrame(void)
{
  s_frame_ready = 0U;
  memset((void *)s_frame, 0xA5, CAMERA_FRAME_BYTES);
  s_frame_started_at = HAL_GetTick();
  if (HAL_DCMI_Start_DMA(&s_dcmi, DCMI_MODE_SNAPSHOT,
                         CAMERA_FRAME_ADDRESS, CAMERA_FRAME_WORDS) != HAL_OK)
  {
    g_live_dcmi_error = HAL_DCMI_GetError(&s_dcmi);
    g_live_stage = 6U;
    return 0U;
  }
  return 1U;
}

static void Camera_LiveInit(void)
{
  OV5640_IO_t io = {0};
  g_live_stage = 1U;
  if (SDRAM_PrepareClockMHz(110U) == 0U || SDRAM_Init() == 0U)
  {
    g_live_stage = 7U;
    return;
  }

  io.Init = Camera_BusInit;
  io.DeInit = Camera_BusDeInit;
  io.Address = 0x78U;
  io.WriteReg = Camera_BusWrite;
  io.ReadReg = Camera_BusRead;
  io.GetTick = Camera_BusGetTick;
  if (OV5640_RegisterBusIO(&s_sensor, &io) != OV5640_OK ||
      OV5640_Init(&s_sensor, OV5640_R160x120, OV5640_Y8) != OV5640_OK)
  {
    g_live_stage = 2U;
    return;
  }
  /* Keep the proven 48 MHz PCLK. HTS=1550, VTS=1000 targets
     48 MHz / (1550 * 1000) = 30.97 frames/s. */
  if (OV5640_SetPCLK(&s_sensor, OV5640_PCLK_48M) != OV5640_OK ||
      Camera_WriteReg(0x380CU, 0x06U) != 0U ||
      Camera_WriteReg(0x380DU, 0x0EU) != 0U ||
      Camera_WriteReg(0x380EU, 0x03U) != 0U ||
      Camera_WriteReg(0x380FU, 0xE8U) != 0U ||
      OV5640_ColorbarModeConfig(&s_sensor, COLORBAR_MODE_ENABLE) != OV5640_OK ||
      OV5640_Start(&s_sensor) != OV5640_OK)
  {
    g_live_stage = 3U;
    return;
  }
  s_colorbar_enabled = 1U;
  HAL_Delay(100U);
  if (Camera_DcmiInit() == 0U) { g_live_stage = 4U; return; }
  g_live_stage = 5U;
  (void)Camera_StartFrame();
}

static void Camera_DrawFrame(void)
{
  static const uint8_t bayer4[16] =
      {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
  uint32_t x, y, source_x, source_y, index;
  for (y = 0U; y < 32U; ++y)
  {
    source_y = 40U + (y * 40U) / 32U;
    for (x = 0U; x < 128U; ++x)
    {
      source_x = (x * CAMERA_FRAME_WIDTH) / 128U;
      index = source_y * CAMERA_FRAME_WIDTH + source_x;
      OLED_DrawPixel((int16_t)x, (int16_t)y,
                     (s_frame[index] > (uint8_t)(bayer4[(y & 3U) * 4U + (x & 3U)] * 16U + 8U)) ? 1U : 0U);
    }
  }
  OLED_RequestRefresh();
}

static void Camera_LivePoll(void)
{
  uint32_t i, hash, min_value, max_value;
  char line[96];
  if (g_live_stage != 5U)
  {
    if ((uint32_t)(HAL_GetTick() - s_live_last_report) >= 1000U)
    {
      s_live_last_report = HAL_GetTick();
      (void)snprintf(line, sizeof(line), "CAM INIT STAGE=%lu SCCB=%08lX\r\n",
                     (unsigned long)g_live_stage, (unsigned long)g_camera_i2c_error);
      (void)USB_DeviceApp_SendText(line);
    }
    if (OLED_TryBeginFrame() != 0U)
    {
      OLED_DrawString(0, 0, "CAM INIT FAIL", 1U);
      (void)snprintf(line, sizeof(line), "STAGE:%lu", (unsigned long)g_live_stage);
      OLED_DrawString(0, 12, line, 1U);
      OLED_RequestRefresh();
    }
    return;
  }

  if (s_frame_ready == 0U)
  {
    if ((uint32_t)(HAL_GetTick() - s_frame_started_at) > 1500U)
    {
      g_live_dma_remaining = DMA1_Stream0->NDTR;
      g_live_dcmi_error = HAL_DCMI_GetError(&s_dcmi);
      g_live_dma_error = HAL_DMA_GetError(&s_dcmi_dma);
      (void)HAL_DCMI_Stop(&s_dcmi);
      (void)snprintf(line, sizeof(line), "CAM TIMEOUT NDTR=%lu D=%lX M=%lX\r\n",
                     (unsigned long)g_live_dma_remaining,
                     (unsigned long)g_live_dcmi_error,
                     (unsigned long)g_live_dma_error);
      (void)USB_DeviceApp_SendText(line);
      (void)Camera_StartFrame();
    }
    return;
  }

  if (OLED_TryBeginFrame() == 0U) return;
  g_live_dma_error = HAL_DMA_GetError(&s_dcmi_dma);
  (void)HAL_DCMI_Stop(&s_dcmi);
  hash = 2166136261UL;
  min_value = 255U;
  max_value = 0U;
  for (i = 0U; i < CAMERA_FRAME_BYTES; ++i)
  {
    uint32_t value = s_frame[i];
    hash = (hash ^ value) * 16777619UL;
    if (value < min_value) min_value = value;
    if (value > max_value) max_value = value;
  }
  g_live_hash = hash;
  g_live_min = min_value;
  g_live_max = max_value;
  g_live_dma_remaining = DMA1_Stream0->NDTR;
  g_live_dcmi_error = HAL_DCMI_GetError(&s_dcmi);
  Camera_DrawFrame();

  if (g_live_frames <= 5U || (uint32_t)(HAL_GetTick() - s_live_last_report) >= 1000U)
  {
    s_live_last_report = HAL_GetTick();
    (void)snprintf(line, sizeof(line), "CAM F=%lu H=%08lX L=%lu U=%lu D=%lX M=%lX O=%lu/%lu\r\n",
                   (unsigned long)g_live_frames, (unsigned long)hash,
                   (unsigned long)min_value, (unsigned long)max_value,
                   (unsigned long)g_live_dcmi_error, (unsigned long)g_live_dma_error,
                   (unsigned long)g_oled_i2c_ok, (unsigned long)g_oled_i2c_fail);
    (void)USB_DeviceApp_SendText(line);
  }
  if (s_colorbar_enabled != 0U && HAL_GetTick() >= 5000U)
  {
    if (OV5640_ColorbarModeConfig(&s_sensor, COLORBAR_MODE_DISABLE) == OV5640_OK)
    {
      s_colorbar_enabled = 0U;
      (void)USB_DeviceApp_SendText("CAM LIVE SCENE\r\n");
    }
  }
  (void)Camera_StartFrame();
}

static void Wifi_ProbeReport(void)
{
  char line[96];
  static uint8_t rx_detail_report;
  if ((uint32_t)(HAL_GetTick() - s_wifi_last_report) < 1000U) return;
  s_wifi_last_report = HAL_GetTick();
  if (g_rtl8723bs_rx_stage != 0U)
  {
    if (rx_detail_report == 0U)
    {
      (void)snprintf(line, sizeof(line), "RX S=%lu E=%lX CR=%lX N=%lu C=%lu A=%lX OC=%lu CC=%lu OF=%lu CF=%lu\r\n",
                     (unsigned long)g_rtl8723bs_rx_stage,
                     (unsigned long)g_rtl8723bs_rx_error,
                     (unsigned long)g_rtl8723bs_rx_cr,
                     (unsigned long)g_rtl8723bs_rx_length,
                     (unsigned long)g_rtl8723bs_rx_channel,
                     (unsigned long)g_rtl8723bs_rx_ant948,
                     (unsigned long)g_rtl8723bs_rx_ofdm_cca,
                     (unsigned long)g_rtl8723bs_rx_cck_cca,
                     (unsigned long)g_rtl8723bs_rx_ofdm_fail,
                     (unsigned long)g_rtl8723bs_rx_cck_fail);
    }
    else if (rx_detail_report == 1U)
    {
      (void)snprintf(line, sizeof(line), "RX2 RF0=%lX C04=%08lX SF=%lX RFC=%lX G=%lX W=%lX\r\n",
                     (unsigned long)g_rtl8723bs_rx_rf0,
                     (unsigned long)g_rtl8723bs_rx_c04,
                     (unsigned long)g_rtl8723bs_rx_sysfunc,
                     (unsigned long)g_rtl8723bs_rx_rfctrl,
                     (unsigned long)g_rtl8723bs_rx_gnt_bt,
                     (unsigned long)g_rtl8723bs_rx_wlan_act);
    }
    else if (rx_detail_report == 2U)
    {
      (void)snprintf(line, sizeof(line), "RXP E=%lX CRC=%lu D0=%08lX D1=%08lX F0=%08lX\r\n",
                     (unsigned long)g_rtl8723bs_rx_fifo_error,
                     (unsigned long)g_rtl8723bs_rx_crc_error,
                     (unsigned long)g_rtl8723bs_rx_desc0,
                     (unsigned long)g_rtl8723bs_rx_desc1,
                     (unsigned long)g_rtl8723bs_rx_frame0);
    }
    else if (rx_detail_report == 3U)
    {
      (void)snprintf(line, sizeof(line), "AP V=%lu B=%02X:%02X:%02X:%02X:%02X:%02X SSID=%s\r\n",
                     (unsigned long)g_rtl8723bs_rx_valid_beacon,
                     g_rtl8723bs_rx_bssid[0], g_rtl8723bs_rx_bssid[1],
                     g_rtl8723bs_rx_bssid[2], g_rtl8723bs_rx_bssid[3],
                     g_rtl8723bs_rx_bssid[4], g_rtl8723bs_rx_bssid[5],
                     g_rtl8723bs_rx_ssid);
    }
    else if (rx_detail_report == 4U)
    {
      (void)snprintf(line, sizeof(line), "AP2 CH=%lu CAP=%04lX AKM=%08lX PAIR=%08lX RSNC=%04lX\r\n",
                     (unsigned long)g_rtl8723bs_rx_beacon_channel,
                     (unsigned long)g_rtl8723bs_rx_ap_capability,
                     (unsigned long)g_rtl8723bs_rx_rsn_akm_mask,
                     (unsigned long)g_rtl8723bs_rx_rsn_pairwise_mask,
                     (unsigned long)g_rtl8723bs_rx_rsn_capability);
    }
    else if (rx_detail_report == 5U)
    {
      (void)snprintf(line, sizeof(line), "AUTH S=%lu E=%lX STA=%lX RX=%lu FC=%04lX STATUS=%lu\r\n",
                     (unsigned long)g_rtl8723bs_tx_auth_stage,
                     (unsigned long)g_rtl8723bs_tx_auth_error,
                     (unsigned long)g_rtl8723bs_tx_auth_sta,
                     (unsigned long)g_rtl8723bs_tx_auth_rx_count,
                     (unsigned long)g_rtl8723bs_tx_auth_frame0,
                     (unsigned long)g_rtl8723bs_tx_auth_status);
    }
    else if (rx_detail_report == 6U)
    {
      (void)snprintf(line, sizeof(line), "ASSOC S=%lu E=%lX STA=%lX RX=%lu STATUS=%lu AID=%lu\r\n",
                     (unsigned long)g_rtl8723bs_tx_assoc_stage,
                     (unsigned long)g_rtl8723bs_tx_assoc_error,
                     (unsigned long)g_rtl8723bs_tx_assoc_sta,
                     (unsigned long)g_rtl8723bs_tx_assoc_rx_count,
                     (unsigned long)g_rtl8723bs_tx_assoc_status,
                     (unsigned long)g_rtl8723bs_tx_assoc_aid);
    }
    else if (rx_detail_report == 7U)
    {
      (void)snprintf(line, sizeof(line), "EAPOL S=%lu E=%lX RX=%lu D=%lu DE=%lu FC=%04lX KI=%04lX RC=%lu\r\n",
                     (unsigned long)g_rtl8723bs_eapol_stage,
                     (unsigned long)g_rtl8723bs_eapol_error,
                     (unsigned long)g_rtl8723bs_eapol_rx_count,
                     (unsigned long)g_rtl8723bs_eapol_data_count,
                     (unsigned long)g_rtl8723bs_eapol_deauth_reason,
                     (unsigned long)g_rtl8723bs_eapol_frame0,
                     (unsigned long)g_rtl8723bs_eapol_key_info,
                     (unsigned long)g_rtl8723bs_eapol_replay_low);
    }
    else if (rx_detail_report == 8U)
    {
      (void)snprintf(line, sizeof(line), "WPA S=%lu E=%lX TX=%lX M3RX=%lu KI=%04lX MIC=%lu KDL=%lu\r\n",
                     (unsigned long)g_rtl8723bs_wpa_stage,
                     (unsigned long)g_rtl8723bs_wpa_error,
                     (unsigned long)g_rtl8723bs_wpa_tx_sta,
                     (unsigned long)g_rtl8723bs_wpa_m3_rx_count,
                     (unsigned long)g_rtl8723bs_wpa_m3_key_info,
                     (unsigned long)g_rtl8723bs_wpa_m3_mic_valid,
                     (unsigned long)g_rtl8723bs_wpa_m3_key_data_length);
    }
    else if (rx_detail_report == 9U)
    {
      (void)snprintf(line, sizeof(line), "KEY S=%lu E=%lX GTK=%lu CAM=%lX\r\n",
                     (unsigned long)g_rtl8723bs_wpa_stage,
                     (unsigned long)g_rtl8723bs_wpa_error,
                     (unsigned long)g_rtl8723bs_wpa_gtk_key_id,
                     (unsigned long)g_rtl8723bs_wpa_cam_error);
    }
    else if (rx_detail_report == 10U)
    {
      (void)snprintf(line, sizeof(line), "DHCP S=%lu E=%lX R=%lu D=%lu V=%lu O=%lu A=%lu N=%lu\r\n",
                     (unsigned long)g_rtl8723bs_dhcp_stage,
                     (unsigned long)g_rtl8723bs_dhcp_error,
                     (unsigned long)g_rtl8723bs_dhcp_rx_count,
                     (unsigned long)g_rtl8723bs_dhcp_data_count,
                     (unsigned long)g_rtl8723bs_dhcp_ipv4_count,
                     (unsigned long)g_rtl8723bs_dhcp_offer_count,
                     (unsigned long)g_rtl8723bs_dhcp_ack_count,
                     (unsigned long)g_rtl8723bs_dhcp_nak_count);
    }
    else if (rx_detail_report == 11U)
    {
      (void)snprintf(line, sizeof(line), "IP=%u.%u.%u.%u SRV=%u.%u.%u.%u\r\n",
                     (unsigned)g_rtl8723bs_dhcp_ip[0],
                     (unsigned)g_rtl8723bs_dhcp_ip[1],
                     (unsigned)g_rtl8723bs_dhcp_ip[2],
                     (unsigned)g_rtl8723bs_dhcp_ip[3],
                     (unsigned)g_rtl8723bs_dhcp_server[0],
                     (unsigned)g_rtl8723bs_dhcp_server[1],
                     (unsigned)g_rtl8723bs_dhcp_server[2],
                     (unsigned)g_rtl8723bs_dhcp_server[3]);
    }
    else
    {
      (void)snprintf(line, sizeof(line), "D2 U=%lu I=%lu F=%lu DA=%lu DS=%lu R=%lu\r\n",
                     (unsigned long)g_rtl8723bs_dhcp_unicast_count,
                     (unsigned long)g_rtl8723bs_dhcp_icv_count,
                     (unsigned long)g_rtl8723bs_dhcp_icv_for_us,
                     (unsigned long)g_rtl8723bs_dhcp_deauth_count,
                     (unsigned long)g_rtl8723bs_dhcp_disassoc_count,
                     (unsigned long)g_rtl8723bs_dhcp_disconnect_reason);
    }
    (void)USB_DeviceApp_SendText(line);
    rx_detail_report = (uint8_t)((rx_detail_report + 1U) % 13U);
    return;
  }
  if (g_rtl8723bs_queue_stage != 0U)
  {
    (void)snprintf(line, sizeof(line), "Q S=%lu E=%lX R=%08lX L=%08lX M=%lX RF=%lu\r\n",
                   (unsigned long)g_rtl8723bs_queue_stage,
                   (unsigned long)g_rtl8723bs_queue_error,
                   (unsigned long)g_rtl8723bs_queue_rqpn,
                   (unsigned long)g_rtl8723bs_queue_llt,
                   (unsigned long)g_rtl8723bs_queue_map,
                   (unsigned long)g_rtl8723bs_rf_stage);
    (void)USB_DeviceApp_SendText(line);
    return;
  }
  if (g_rtl8723bs_rf_stage != 0U)
  {
    (void)snprintf(line, sizeof(line), "RF S=%lu E=%lX N=%lu R1=%lX R18=%lX EF=%lu\r\n",
                   (unsigned long)g_rtl8723bs_rf_stage,
                   (unsigned long)g_rtl8723bs_rf_error,
                   (unsigned long)g_rtl8723bs_rf_written,
                   (unsigned long)g_rtl8723bs_rf_reg1,
                   (unsigned long)g_rtl8723bs_rf_reg18,
                   (unsigned long)g_rtl8723bs_efuse_stage);
    (void)USB_DeviceApp_SendText(line);
    return;
  }
  if (g_rtl8723bs_efuse_stage != 0U)
  {
    (void)snprintf(line, sizeof(line), "EF S=%lu E=%lX N=%lu ID=%lX PK=%lu XT=%lX M=%02X%02X%02X%02X%02X%02X\r\n",
                   (unsigned long)g_rtl8723bs_efuse_stage,
                   (unsigned long)g_rtl8723bs_efuse_error,
                   (unsigned long)g_rtl8723bs_efuse_used,
                   (unsigned long)g_rtl8723bs_efuse_id,
                   (unsigned long)g_rtl8723bs_efuse_package,
                   (unsigned long)g_rtl8723bs_efuse_xtal,
                   (unsigned)g_rtl8723bs_efuse_mac[0], (unsigned)g_rtl8723bs_efuse_mac[1],
                   (unsigned)g_rtl8723bs_efuse_mac[2], (unsigned)g_rtl8723bs_efuse_mac[3],
                   (unsigned)g_rtl8723bs_efuse_mac[4], (unsigned)g_rtl8723bs_efuse_mac[5]);
    (void)USB_DeviceApp_SendText(line);
    return;
  }
  if (g_rtl8723bs_bb_stage != 0U)
  {
    (void)snprintf(line, sizeof(line), "BB S=%lu E=%lX P=%lu A=%lu X=%08lX Y=%08lX FW=%lu\r\n",
                   (unsigned long)g_rtl8723bs_bb_stage,
                   (unsigned long)g_rtl8723bs_bb_error,
                   (unsigned long)g_rtl8723bs_bb_phy_written,
                   (unsigned long)g_rtl8723bs_bb_agc_written,
                   (unsigned long)g_rtl8723bs_bb_reg800,
                   (unsigned long)g_rtl8723bs_bb_regc50,
                   (unsigned long)g_rtl8723bs_fw_stage);
    (void)USB_DeviceApp_SendText(line);
    return;
  }
  if (g_rtl8723bs_host_stage == 1U)
  {
    (void)snprintf(line, sizeof(line), "HOST S=%lu E=%lX CR=%lX\r\n",
                   (unsigned long)g_rtl8723bs_host_stage,
                   (unsigned long)g_rtl8723bs_host_error,
                   (unsigned long)g_rtl8723bs_host_cr);
    (void)USB_DeviceApp_SendText(line);
    return;
  }
  if (g_rtl8723bs_mac_stage != 0U)
  {
    (void)snprintf(line, sizeof(line), "MAC S=%lu E=%lX N=%lu A=%lX B=%lX FW=%lu CR=%lX\r\n",
                   (unsigned long)g_rtl8723bs_mac_stage,
                   (unsigned long)g_rtl8723bs_mac_error,
                   (unsigned long)g_rtl8723bs_mac_written,
                   (unsigned long)g_rtl8723bs_mac_reg428,
                   (unsigned long)g_rtl8723bs_mac_reg700,
                   (unsigned long)g_rtl8723bs_fw_stage,
                   (unsigned long)g_rtl8723bs_host_cr);
    (void)USB_DeviceApp_SendText(line);
    return;
  }
  if (g_rtl8723bs_fw_stage != 0U)
  {
    (void)snprintf(line, sizeof(line), "FW S=%lu E=%lX N=%lu R=%lX C=%lX H=%lX RX=%lu TX=%lX\r\n",
                   (unsigned long)g_rtl8723bs_fw_stage,
                   (unsigned long)g_rtl8723bs_fw_error,
                   (unsigned long)g_rtl8723bs_fw_bytes,
                   (unsigned long)g_rtl8723bs_fw_status,
                   (unsigned long)g_rtl8723bs_fw_mac_cr,
                   (unsigned long)g_rtl8723bs_fw_local_hisr,
                   (unsigned long)g_rtl8723bs_fw_rx_len,
                   (unsigned long)g_rtl8723bs_fw_tx_pages);
    (void)USB_DeviceApp_SendText(line);
    return;
  }
  if (g_rtl8723bs_probe_stage == RTL8723BS_PROBE_POWER_ERROR)
  {
    (void)snprintf(line, sizeof(line), "W S=25 E=%lX P=%lu V=%lX\r\n",
                   (unsigned long)g_rtl8723bs_probe_error,
                   (unsigned long)g_rtl8723bs_probe_power_step,
                   (unsigned long)g_rtl8723bs_probe_power_value);
    (void)USB_DeviceApp_SendText(line);
    return;
  }
  if (g_rtl8723bs_probe_stage == RTL8723BS_PROBE_CMD53_OK)
  {
    (void)snprintf(line, sizeof(line), "W S=23 R=%lu F=%lu B=%lu H=%lX BL=%lu X=%08lX\r\n",
                   (unsigned long)g_rtl8723bs_probe_cmd53_rounds,
                   (unsigned long)(g_rtl8723bs_probe_actual_hz / 1000000U),
                   (unsigned long)(g_rtl8723bs_probe_bus_if & 0x03U),
                   (unsigned long)g_rtl8723bs_probe_hs_speed_reg,
                   (unsigned long)g_rtl8723bs_probe_block_ok,
                   (unsigned long)g_rtl8723bs_probe_block_hash);
    (void)USB_DeviceApp_SendText(line);
    return;
  }
  if (g_rtl8723bs_probe_stage == RTL8723BS_PROBE_CMD53_ERROR)
  {
    (void)snprintf(line, sizeof(line), "W S=22 E=%lX ST=%lX N=%lu R=%lu F=%lu H=%lX\r\n",
                   (unsigned long)g_rtl8723bs_probe_error,
                   (unsigned long)g_rtl8723bs_probe_cmd53_sta,
                   (unsigned long)g_rtl8723bs_probe_cmd53_dcount,
                   (unsigned long)g_rtl8723bs_probe_cmd53_rounds,
                   (unsigned long)(g_rtl8723bs_probe_actual_hz / 1000000U),
                   (unsigned long)g_rtl8723bs_probe_hs_speed_reg);
    (void)USB_DeviceApp_SendText(line);
    return;
  }
  if (g_rtl8723bs_probe_stage == RTL8723BS_PROBE_BLOCK_ERROR)
  {
    (void)snprintf(line, sizeof(line), "W S=26 E=%lX ST=%lX N=%lu BL=%lu\r\n",
                   (unsigned long)g_rtl8723bs_probe_error,
                   (unsigned long)g_rtl8723bs_probe_cmd53_sta,
                   (unsigned long)g_rtl8723bs_probe_cmd53_dcount,
                   (unsigned long)g_rtl8723bs_probe_block_ok);
    (void)USB_DeviceApp_SendText(line);
    return;
  }
  (void)snprintf(line, sizeof(line), "W S=%lu E=%lX ST=%lX N=%lu L=%lu T=%lu K=%lu Q=%lu\r\n",
                 (unsigned long)g_rtl8723bs_probe_stage,
                 (unsigned long)g_rtl8723bs_probe_error,
                 (unsigned long)g_rtl8723bs_probe_cmd53_sta,
                 (unsigned long)g_rtl8723bs_probe_cmd53_dcount,
                 (unsigned long)g_rtl8723bs_probe_d0_low_samples,
                 (unsigned long)g_rtl8723bs_probe_d0_edges,
                 (unsigned long)g_rtl8723bs_probe_clk_edges,
                 (unsigned long)g_rtl8723bs_probe_cmd_edges);
  (void)USB_DeviceApp_SendText(line);
}


void DMA1_Stream0_IRQHandler(void) { HAL_DMA_IRQHandler(&s_dcmi_dma); }
void DCMI_IRQHandler(void) { HAL_DCMI_IRQHandler(&s_dcmi); }
void HAL_DCMI_FrameEventCallback(DCMI_HandleTypeDef *hdcmi)
{
  (void)hdcmi;
  ++g_live_frames;
  s_frame_ready = 1U;
}
#endif

static uint32_t s_init_elapsed_ms;
static uint32_t s_scan_elapsed_ms;
static uint32_t s_test_start_ms;
static uint32_t s_phase_start_ms;
static uint32_t s_data_elapsed_ms;
static uint32_t s_address_elapsed_ms;
static uint32_t s_write_elapsed_ms;
static uint32_t s_verify_elapsed_ms;
static uint32_t s_stress_start_ms;
static uint32_t s_stress_elapsed_ms;
static uint8_t s_data_pass;
static uint8_t s_address_pass;
static uint8_t s_write_pass;
static uint8_t s_verify_pass;
static uint32_t s_failed_phase;

#define SDRAM_PROFILE_COUNT 1U
static const uint32_t s_profile_clock_mhz[SDRAM_PROFILE_COUNT] = {110U};
typedef struct
{
  uint32_t clock_mhz;
  uint32_t passed;
  uint32_t init_ok;
  uint32_t line_ok;
  uint32_t dq_pass_count;
  uint32_t dq_data_bad;
  uint32_t dq_bias_bad;
  uint32_t dq_bad;
  uint32_t addr_pass_count;
  uint32_t addr_tested;
  uint32_t addr_bad;
  uint32_t addr_last_ok;
  uint32_t dqm_low_ok;
  uint32_t dqm_high_ok;
  uint32_t dqm_low_read;
  uint32_t dqm_high_read;
  uint32_t sdcr1;
  uint32_t sdcr2;
  uint32_t sdtr1;
  uint32_t sdtr2;
  uint32_t sdrtr;
  uint32_t data_pass;
  uint32_t address_pass;
  uint32_t write_pass;
  uint32_t verify_pass;
  uint32_t stress_sweeps;
  uint32_t stress_round;
  uint32_t stress_pattern;
  uint32_t stress_masks[SDRAM_STRESS_ROUNDS];
  uint32_t init_ms;
  uint32_t scan_ms;
  uint32_t write_ms;
  uint32_t verify_ms;
  uint32_t data_ms;
  uint32_t address_ms;
  uint32_t stress_ms;
  uint32_t total_ms;
  uint32_t fail_phase;
  uint32_t fail_address;
  uint32_t expected;
  uint32_t actual;
  uint32_t errors;
} SDRAM_ProfileReport;

static SDRAM_ProfileReport s_profiles[SDRAM_PROFILE_COUNT];
static uint32_t s_active_profile;
static uint32_t s_completed_profiles;
static uint32_t s_profile_start_ms;
static uint8_t s_profile_advance_pending;
static uint8_t s_all_profiles_done;
static uint32_t s_io_comp_ready;

static void OLED_DrawHex32(int16_t x, int16_t y, uint32_t value)
{
  static const char digits[] = "0123456789ABCDEF";
  char text[9];
  uint32_t i;

  for (i = 0U; i < 8U; ++i)
  {
    text[i] = digits[(value >> (28U - i * 4U)) & 0xFU];
  }
  text[8] = '\0';
  OLED_DrawString(x, y, text, 1U);
}

static void OLED_DrawDecimal2(int16_t x, int16_t y, uint32_t value)
{
  char text[3];
  if (value > 99U) value = 99U;
  text[0] = (char)('0' + value / 10U);
  text[1] = (char)('0' + value % 10U);
  text[2] = '\0';
  OLED_DrawString(x, y, text, 1U);
}

static void OLED_DrawDecimal3(int16_t x, int16_t y, uint32_t value)
{
  char text[4];
  if (value > 100U) value = 100U;
  text[0] = (char)('0' + value / 100U);
  text[1] = (char)('0' + (value / 10U) % 10U);
  text[2] = (char)('0' + value % 10U);
  text[3] = '\0';
  OLED_DrawString(x, y, text, 1U);
}

static uint8_t SDRAM_LineScanPassed(void)
{
  return (g_sdram_line_scan_done != 0U &&
          g_sdram_dq_fail_mask == 0U &&
          g_sdram_addr_tested_mask == 0x01FFFFFFUL &&
          g_sdram_addr_fail_mask == 0U &&
          g_sdram_addr_last_ok != 0U &&
          g_sdram_dqm_tested != 0U &&
          g_sdram_dqm_low_ok != 0U &&
          g_sdram_dqm_high_ok != 0U) ? 1U : 0U;
}

static void SDRAM_FinishProfile(void)
{
  SDRAM_ProfileReport *report = &s_profiles[s_active_profile];
  uint32_t i;

  report->passed = (g_sdram_test_phase == 5U) ? 1U : 0U;
  report->init_ok = g_sdram_init_ok;
  report->line_ok = SDRAM_LineScanPassed();
  report->dq_pass_count = g_sdram_dq_pass_count;
  report->dq_data_bad = g_sdram_dq_data_fail_mask;
  report->dq_bias_bad = g_sdram_dq_bias_fail_mask;
  report->dq_bad = g_sdram_dq_fail_mask;
  report->addr_pass_count = g_sdram_addr_pass_count;
  report->addr_tested = g_sdram_addr_tested_mask;
  report->addr_bad = g_sdram_addr_fail_mask;
  report->addr_last_ok = g_sdram_addr_last_ok;
  report->dqm_low_ok = g_sdram_dqm_low_ok;
  report->dqm_high_ok = g_sdram_dqm_high_ok;
  report->dqm_low_read = g_sdram_dqm_low_readback;
  report->dqm_high_read = g_sdram_dqm_high_readback;
  report->sdcr1 = g_sdram_diag_sdcr1;
  report->sdcr2 = g_sdram_diag_sdcr2;
  report->sdtr1 = g_sdram_diag_sdtr1;
  report->sdtr2 = g_sdram_diag_sdtr2;
  report->sdrtr = g_sdram_diag_sdrtr;
  report->data_pass = s_data_pass;
  report->address_pass = s_address_pass;
  report->write_pass = s_write_pass;
  report->verify_pass = s_verify_pass;
  report->stress_sweeps = g_sdram_stress_completed_sweeps;
  report->stress_round = g_sdram_stress_round;
  report->stress_pattern = g_sdram_stress_pattern;
  for (i = 0U; i < SDRAM_STRESS_ROUNDS; ++i)
  {
    report->stress_masks[i] = g_sdram_stress_round_pass_mask[i];
  }
  report->init_ms = s_init_elapsed_ms;
  report->scan_ms = s_scan_elapsed_ms;
  report->write_ms = s_write_elapsed_ms;
  report->verify_ms = s_verify_elapsed_ms;
  report->data_ms = s_data_elapsed_ms;
  report->address_ms = s_address_elapsed_ms;
  report->stress_ms = s_stress_elapsed_ms;
  report->total_ms = HAL_GetTick() - s_profile_start_ms;
  report->fail_phase = s_failed_phase;
  report->fail_address = g_sdram_fail_address;
  report->expected = g_sdram_expected_value;
  report->actual = g_sdram_actual_value;
  report->errors = g_sdram_error_count;
  ++s_completed_profiles;
  if (report->passed != 0U && s_completed_profiles < SDRAM_PROFILE_COUNT)
  {
    ++s_active_profile;
    s_profile_advance_pending = 1U;
  }
  else
  {
    s_all_profiles_done = 1U;
  }
}

static void SDRAM_BeginProfile(void)
{
  SDRAM_ProfileReport *report = &s_profiles[s_active_profile];
  uint32_t i;

  report->clock_mhz = s_profile_clock_mhz[s_active_profile];
  s_profile_start_ms = HAL_GetTick();
  s_init_elapsed_ms = 0U;
  s_scan_elapsed_ms = 0U;
  s_data_elapsed_ms = 0U;
  s_address_elapsed_ms = 0U;
  s_write_elapsed_ms = 0U;
  s_verify_elapsed_ms = 0U;
  s_stress_elapsed_ms = 0U;
  s_data_pass = 0U;
  s_address_pass = 0U;
  s_write_pass = 0U;
  s_verify_pass = 0U;
  s_failed_phase = 0U;
  g_sdram_test_phase = 0U;
  g_sdram_test_done = 0U;
  g_sdram_init_ok = 0U;
  g_sdram_line_scan_done = 0U;
  g_sdram_error_count = 0U;
  g_sdram_fail_address = 0U;
  g_sdram_expected_value = 0U;
  g_sdram_actual_value = 0U;
  g_sdram_diag_sdcr1 = 0U;
  g_sdram_diag_sdcr2 = 0U;
  g_sdram_diag_sdtr1 = 0U;
  g_sdram_diag_sdtr2 = 0U;
  g_sdram_diag_sdrtr = 0U;
  g_sdram_dq_pass_count = 0U;
  g_sdram_dq_data_fail_mask = 0U;
  g_sdram_dq_bias_fail_mask = 0U;
  g_sdram_dq_fail_mask = 0U;
  g_sdram_addr_pass_count = 0U;
  g_sdram_addr_tested_mask = 0U;
  g_sdram_addr_fail_mask = 0U;
  g_sdram_addr_last_ok = 0U;
  g_sdram_dqm_low_ok = 0U;
  g_sdram_dqm_high_ok = 0U;
  g_sdram_dqm_low_readback = 0U;
  g_sdram_dqm_high_readback = 0U;
  g_sdram_stress_started = 0U;
  g_sdram_stress_round = 0U;
  g_sdram_stress_pattern = 0U;
  g_sdram_stress_completed_sweeps = 0U;
  for (i = 0U; i < SDRAM_STRESS_ROUNDS; ++i)
  {
    g_sdram_stress_round_pass_mask[i] = 0U;
  }
  if (SDRAM_PrepareClockMHz(report->clock_mhz) == 0U ||
      SDRAM_Init() == 0U)
  {
    s_init_elapsed_ms = HAL_GetTick() - s_profile_start_ms;
    g_sdram_init_ok = 0U;
    g_sdram_test_phase = 7U;
    g_sdram_test_done = 1U;
    s_failed_phase = 7U;
    SDRAM_FinishProfile();
    return;
  }
  s_init_elapsed_ms = HAL_GetTick() - s_profile_start_ms;
  SDRAM_RunLineScan();
  s_scan_elapsed_ms = HAL_GetTick() - s_profile_start_ms - s_init_elapsed_ms;
  if (SDRAM_LineScanPassed() == 0U)
  {
    g_sdram_test_done = 1U;
    SDRAM_FinishProfile();
    return;
  }
  SDRAM_ReworkTestStart();
  s_test_start_ms = HAL_GetTick();
  s_phase_start_ms = s_test_start_ms;
}

static void USB_LogSdramStatus(void)
{
  static uint32_t stage;
  static uint32_t reported_profile;
  static uint32_t detail;
  static uint32_t overall_stage;
  static uint32_t last_phase = 0xFFFFFFFFUL;
  static uint32_t last_bucket = 0xFFFFFFFFUL;
  static uint32_t last_open_count;
  const SDRAM_ProfileReport *report;
  const char *phase_name;
  char line[64];
  uint32_t phase;
  uint32_t bucket;
  uint32_t rate_x100;

  USB_DeviceApp_Process();
  if (g_usb_device_configured == 0U)
  {
    stage = 0U;
    reported_profile = 0U;
    detail = 0U;
    overall_stage = 0U;
    last_phase = 0xFFFFFFFFUL;
    last_bucket = 0xFFFFFFFFUL;
    last_open_count = g_usb_cdc_open_count;
    return;
  }
  if (last_open_count != g_usb_cdc_open_count)
  {
    stage = 0U;
    reported_profile = 0U;
    detail = 0U;
    overall_stage = 0U;
    last_phase = 0xFFFFFFFFUL;
    last_bucket = 0xFFFFFFFFUL;
    last_open_count = g_usb_cdc_open_count;
  }

  switch (stage)
  {
    case 0U:
      if (USB_DeviceApp_SendText("=== SDRAM TEST REPORT v3 BEGIN ===\r\n")) ++stage;
      return;
    case 1U:
      if (USB_DeviceApp_SendText("PART: IS42S16320F-7BLI; MCU: STM32H747 CM7\r\n")) ++stage;
      return;
    case 2U:
      if (USB_DeviceApp_SendText("FMC: Bank2 base=0xD0000000 size=64MiB bus=16bit\r\n")) ++stage;
      return;
    case 3U:
      if (USB_DeviceApp_SendText("RATED: SDRAM=143MHz; MCU SDCLK=110MHz*\r\n")) ++stage;
      return;
    case 4U:
      if (USB_DeviceApp_SendText("*MCU limit: 3.3V, 20pF load per ST datasheet\r\n")) ++stage;
      return;
    case 5U:
      if (USB_DeviceApp_SendText("RAW PEAK: 110MHz x 16bit = 220 MB/s one-way\r\n")) ++stage;
      return;
    case 6U:
      if (USB_DeviceApp_SendText("PROFILE: 110 MHz only\r\n")) ++stage;
      return;
    case 7U:
      if (USB_DeviceApp_SendText("PATTERNS: 00 FF AA 55 address++ PRNG March C-\r\n")) ++stage;
      return;
    case 8U:
      if (USB_DeviceApp_SendText("MARCH C-: up w0,r0w1,r1w0; down r0w1,r1w0; up r0\r\n")) ++stage;
      return;
    case 9U:
      if (USB_DeviceApp_SendText("RUN: 1 round x 7 full-chip patterns\r\n")) ++stage;
      return;
    case 10U:
      if (USB_DeviceApp_SendText("Refresh dwell: 1000ms before each pattern verify\r\n")) ++stage;
      return;
    case 11U:
      (void)snprintf(line, sizeof(line),
                     "IO_COMP_READY: %lu\r\n",
                     (unsigned long)s_io_comp_ready);
      if (USB_DeviceApp_SendText(line)) ++stage;
      return;
    case 12U:
      if (USB_DeviceApp_SendText("110MHz timing: CAS2 tMRD2 tXSR10 tRAS6 tRC9\r\n")) ++stage;
      return;
    case 13U:
      if (USB_DeviceApp_SendText("110MHz timing: tWR3 tRP3 tRCD3 refresh=839\r\n")) ++stage;
      return;
    default:
      break;
  }

  if (reported_profile < s_completed_profiles)
  {
    report = &s_profiles[reported_profile];
    switch (detail)
    {
      case 0U:
        (void)snprintf(line, sizeof(line),
                       "--- PROFILE %lu/%u: %luMHz %s ---\r\n",
                       (unsigned long)(reported_profile + 1U),
                       SDRAM_PROFILE_COUNT,
                       (unsigned long)report->clock_mhz,
                       report->passed != 0U ? "PASS" : "FAIL");
        break;
      case 1U:
        (void)snprintf(line, sizeof(line),
                       "INIT=%s %lums LINE_SCAN=%s %lums\r\n",
                       report->init_ok != 0U ? "PASS" : "FAIL",
                       (unsigned long)report->init_ms,
                       report->line_ok != 0U ? "PASS" : "FAIL",
                       (unsigned long)report->scan_ms);
        break;
      case 2U:
        (void)snprintf(line, sizeof(line),
                       "DQ: %lu/16 data_bad=%04lX bias_bad=%04lX\r\n",
                       (unsigned long)report->dq_pass_count,
                       (unsigned long)report->dq_data_bad,
                       (unsigned long)report->dq_bias_bad);
        break;
      case 3U:
        (void)snprintf(line, sizeof(line),
                       "DQ: combined_bad=%04lX\r\n",
                       (unsigned long)report->dq_bad);
        break;
      case 4U:
        (void)snprintf(line, sizeof(line),
                       "ADDR: %lu/25 tested=%08lX bad=%08lX\r\n",
                       (unsigned long)report->addr_pass_count,
                       (unsigned long)report->addr_tested,
                       (unsigned long)report->addr_bad);
        break;
      case 5U:
        (void)snprintf(line, sizeof(line),
                       "ADDR: last_address=%s\r\n",
                       report->addr_last_ok != 0U ? "PASS" : "FAIL");
        break;
      case 6U:
        (void)snprintf(line, sizeof(line),
                       "DQM: low=%lu high=%lu read=%04lX,%04lX\r\n",
                       (unsigned long)report->dqm_low_ok,
                       (unsigned long)report->dqm_high_ok,
                       (unsigned long)report->dqm_low_read,
                       (unsigned long)report->dqm_high_read);
        break;
      case 7U:
        (void)snprintf(line, sizeof(line),
                       "FMC: SDCR1=%08lX SDCR2=%08lX\r\n",
                       (unsigned long)report->sdcr1,
                       (unsigned long)report->sdcr2);
        break;
      case 8U:
        (void)snprintf(line, sizeof(line),
                       "FMC: SDTR1=%08lX SDTR2=%08lX\r\n",
                       (unsigned long)report->sdtr1,
                       (unsigned long)report->sdtr2);
        break;
      case 9U:
        (void)snprintf(line, sizeof(line),
                       "FMC: SDRTR=%08lX\r\n",
                       (unsigned long)report->sdrtr);
        break;
      case 10U:
        (void)snprintf(line, sizeof(line),
                       "BASE: data=%lu addr=%lu write=%lu verify=%lu\r\n",
                       (unsigned long)report->data_pass,
                       (unsigned long)report->address_pass,
                       (unsigned long)report->write_pass,
                       (unsigned long)report->verify_pass);
        break;
      case 11U:
        (void)snprintf(line, sizeof(line),
                       "BASE: data_ms=%lu addr_ms=%lu\r\n",
                       (unsigned long)report->data_ms,
                       (unsigned long)report->address_ms);
        break;
      case 12U:
        (void)snprintf(line, sizeof(line),
                       "BASE: write_ms=%lu verify_ms=%lu\r\n",
                       (unsigned long)report->write_ms,
                       (unsigned long)report->verify_ms);
        break;
      case 13U:
        rate_x100 = report->write_ms != 0U ?
                    6400000U / report->write_ms : 0U;
        (void)snprintf(line, sizeof(line),
                       "APP_WRITE: %lu.%02lu MiB/s\r\n",
                       (unsigned long)(rate_x100 / 100U),
                       (unsigned long)(rate_x100 % 100U));
        break;
      case 14U:
        rate_x100 = report->verify_ms != 0U ?
                    6400000U / report->verify_ms : 0U;
        (void)snprintf(line, sizeof(line),
                       "APP_VERIFY: %lu.%02lu MiB/s\r\n",
                       (unsigned long)(rate_x100 / 100U),
                       (unsigned long)(rate_x100 % 100U));
        break;
      case 15U:
        (void)snprintf(line, sizeof(line),
                       "STRESS: sweeps=%lu/%u elapsed_ms=%lu\r\n",
                       (unsigned long)report->stress_sweeps,
                       SDRAM_STRESS_ROUNDS * SDRAM_STRESS_PATTERNS,
                       (unsigned long)report->stress_ms);
        break;
      case (16U + SDRAM_STRESS_ROUNDS):
        (void)snprintf(line, sizeof(line),
                       "PROFILE: elapsed_ms=%lu errors=%lu\r\n",
                       (unsigned long)report->total_ms,
                       (unsigned long)report->errors);
        break;
      case (17U + SDRAM_STRESS_ROUNDS):
        (void)snprintf(line, sizeof(line),
                       "FAIL: stage=%lu round=%lu pattern=%lu\r\n",
                       (unsigned long)report->fail_phase,
                       (unsigned long)report->stress_round,
                       (unsigned long)report->stress_pattern);
        if (report->passed != 0U) { ++detail; return; }
        break;
      case (18U + SDRAM_STRESS_ROUNDS):
        (void)snprintf(line, sizeof(line),
                       "FAIL: addr=%08lX exp=%08lX got=%08lX\r\n",
                       (unsigned long)report->fail_address,
                       (unsigned long)report->expected,
                       (unsigned long)report->actual);
        if (report->passed != 0U) { ++detail; return; }
        break;
      case (19U + SDRAM_STRESS_ROUNDS):
        (void)snprintf(line, sizeof(line),
                       "--- PROFILE END ---\r\n");
        break;
      default:
        if (detail >= 16U && detail < 16U + SDRAM_STRESS_ROUNDS)
        {
          (void)snprintf(line, sizeof(line),
                         "STRESS ROUND %lu/%u mask=%02lX\r\n",
                         (unsigned long)(detail - 15U),
                         SDRAM_STRESS_ROUNDS,
                         (unsigned long)report->stress_masks[detail - 16U]);
        }
        else return;
        break;
    }
    if (USB_DeviceApp_SendText(line))
    {
      ++detail;
      if (detail == 20U + SDRAM_STRESS_ROUNDS)
      {
        detail = 0U;
        ++reported_profile;
      }
    }
    return;
  }

  if (s_all_profiles_done != 0U)
  {
    if (overall_stage == 0U)
    {
      (void)snprintf(line, sizeof(line),
                     "OVERALL: %s profiles_passed=%lu/%u\r\n",
                     s_completed_profiles == SDRAM_PROFILE_COUNT &&
                     s_profiles[SDRAM_PROFILE_COUNT - 1U].passed != 0U ?
                     "PASS" : "FAIL",
                     (unsigned long)(s_completed_profiles -
                     (s_profiles[s_completed_profiles - 1U].passed == 0U)),
                     SDRAM_PROFILE_COUNT);
      if (USB_DeviceApp_SendText(line)) ++overall_stage;
    }
    else if (overall_stage == 1U)
    {
      if (USB_DeviceApp_SendText("=== SDRAM TEST REPORT END ===\r\n"))
        ++overall_stage;
    }
    return;
  }

  phase = g_sdram_test_phase;
  bucket = g_sdram_test_progress_pct / 10U;
  phase_name = (phase == 1U) ? "DATA" :
               (phase == 2U) ? "ADDRESS" :
               (phase == 3U) ? "BASE_WRITE" :
               (phase == 4U) ? "BASE_VERIFY" :
               (phase == 8U) ? "STRESS_WRITE" :
               (phase == 9U) ? "REFRESH_WAIT" :
               (phase == 10U) ? "STRESS_VERIFY" :
               (phase >= 11U && phase <= 16U) ? "MARCH_C_MINUS" :
               "INIT_SCAN";
  if (last_phase != phase || last_bucket != bucket)
  {
    (void)snprintf(line, sizeof(line),
                   "LIVE: %luMHz %s r=%lu p=%lu %lu%%\r\n",
                   (unsigned long)s_profile_clock_mhz[s_active_profile],
                   phase_name,
                   (unsigned long)g_sdram_stress_round,
                   (unsigned long)g_sdram_stress_pattern,
                   (unsigned long)g_sdram_test_progress_pct);
    if (USB_DeviceApp_SendText(line))
    {
      last_phase = phase;
      last_bucket = bucket;
    }
  }
}

static void OLED_DrawSdramStatus(void)
{
  OLED_Clear();
  if (s_all_profiles_done != 0U &&
      s_completed_profiles == SDRAM_PROFILE_COUNT &&
      s_profiles[SDRAM_PROFILE_COUNT - 1U].passed != 0U)
  {
    OLED_DrawString(0, 0, "SDRAM PASS", 1U);
    OLED_DrawString(0, 8, "110 MHZ", 1U);
    OLED_DrawString(0, 16, "STRESS 1X7 PASS", 1U);
    OLED_DrawString(0, 24, "64MB VERIFIED", 1U);
  }
  else if (s_all_profiles_done != 0U)
  {
    OLED_DrawString(0, 0, "SDRAM FAIL", 1U);
    OLED_DrawString(0, 8, "FREQ", 1U);
    OLED_DrawDecimal3(30, 8, s_profiles[s_completed_profiles - 1U].clock_mhz);
    OLED_DrawString(0, 16, "ADDR:", 1U);
    OLED_DrawHex32(36, 16, s_profiles[s_completed_profiles - 1U].fail_address);
    OLED_DrawString(0, 24, "PHASE", 1U);
    OLED_DrawDecimal2(36, 24, s_profiles[s_completed_profiles - 1U].fail_phase);
  }
  else if (g_sdram_init_ok == 0U)
  {
    OLED_DrawString(0, 0, "SDRAM INIT FAIL", 1U);
    OLED_DrawString(0, 8, "CHECK FMC", 1U);
  }
  else if (g_sdram_line_scan_done == 0U)
  {
    OLED_DrawString(0, 0, "SDRAM LINE SCAN", 1U);
    OLED_DrawString(0, 8, "PLEASE WAIT", 1U);
  }
  else if (g_sdram_test_phase == 5U)
  {
    OLED_DrawString(0, 0, "SDRAM PASS", 1U);
    OLED_DrawString(0, 8, "STRESS 1X7 PASS", 1U);
    OLED_DrawString(0, 16, "DQ 16 OF16", 1U);
    OLED_DrawString(0, 24, "ADDR 25 OF25", 1U);
  }
  else if (g_sdram_test_phase == 6U)
  {
    OLED_DrawString(0, 0, "SDRAM FAIL", 1U);
    OLED_DrawString(0, 8, "ADDR:", 1U);
    OLED_DrawHex32(36, 8, g_sdram_fail_address);
    OLED_DrawString(0, 16, "EXP:", 1U);
    OLED_DrawHex32(30, 16, g_sdram_expected_value);
    OLED_DrawString(0, 24, "GOT:", 1U);
    OLED_DrawHex32(30, 24, g_sdram_actual_value);
  }
  else if (g_sdram_test_phase != 0U)
  {
    if (g_sdram_test_phase >= 8U)
    {
      OLED_DrawString(0, 0, "SDRAM STRESS", 1U);
      if (g_sdram_test_phase == 8U) OLED_DrawString(0, 8, "WRITE", 1U);
      else if (g_sdram_test_phase == 9U) OLED_DrawString(0, 8, "REFRESH WAIT", 1U);
      else if (g_sdram_test_phase == 10U) OLED_DrawString(0, 8, "VERIFY", 1U);
      else OLED_DrawString(0, 8, "MARCH C-", 1U);
      OLED_DrawString(0, 16, "ROUND", 1U);
      OLED_DrawDecimal2(36, 16, g_sdram_stress_round);
      OLED_DrawString(48, 16, "OF01", 1U);
      OLED_DrawString(0, 24, "PROGRESS", 1U);
      OLED_DrawDecimal3(54, 24, g_sdram_test_progress_pct);
    }
    else
    {
      OLED_DrawString(0, 0, "SDRAM TEST", 1U);
      if (g_sdram_test_phase == 1U) OLED_DrawString(0, 8, "DATA TEST", 1U);
      else if (g_sdram_test_phase == 2U) OLED_DrawString(0, 8, "ADDRESS TEST", 1U);
      else if (g_sdram_test_phase == 3U) OLED_DrawString(0, 8, "WRITE 64MB", 1U);
      else OLED_DrawString(0, 8, "VERIFY 64MB", 1U);
      OLED_DrawString(0, 16, "PROGRESS", 1U);
      OLED_DrawDecimal3(54, 16, g_sdram_test_progress_pct);
      OLED_DrawString(0, 24, "SDCLK", 1U);
      OLED_DrawDecimal3(36, 24, g_sdram_test_clock_mhz);
      OLED_DrawString(54, 24, "MHZ", 1U);
    }
  }
  else if (g_sdram_dq_fail_mask == 0U && g_sdram_addr_fail_mask == 0U &&
           (g_sdram_dqm_tested == 0U || g_sdram_dqm_low_ok == 0U ||
            g_sdram_dqm_high_ok == 0U))
  {
    OLED_DrawString(0, 0, "DQM FAIL", 1U);
    OLED_DrawString(0, 8, "CHECK BYTE MASK", 1U);
  }
  else
  {
    OLED_DrawString(0, 0, "DQ OK", 1U);
    OLED_DrawDecimal2(36, 0, g_sdram_dq_pass_count);
    OLED_DrawString(48, 0, "OF16", 1U);
    OLED_DrawString(0, 8, "DQ BAD:", 1U);
    OLED_DrawHex32(42, 8, g_sdram_dq_fail_mask);
    if (g_sdram_addr_tested_mask != 0U)
    {
      OLED_DrawString(0, 16, "ADDR OK", 1U);
      OLED_DrawDecimal2(48, 16, g_sdram_addr_pass_count);
      OLED_DrawString(60, 16, "OF25", 1U);
      OLED_DrawString(0, 24, "A BAD:", 1U);
      OLED_DrawHex32(36, 24, g_sdram_addr_fail_mask);
    }
    else
    {
      OLED_DrawString(0, 16, "ADDR NOT TESTED", 1U);
    }
  }
  OLED_RequestRefresh();
}
/* USER CODE END 0 */

volatile uint32_t g_oled_smoke_seconds;

static void OLED_SmokeTestRun(void)
{
  char line[24];
  uint32_t last_frame = HAL_GetTick() - 250U;

  for (;;)
  {
    OLED_Process();
    g_oled_smoke_seconds = HAL_GetTick() / 1000U;
    if (OLED_IsReady() != 0U) g_oled_stage = 5U;
    if ((uint32_t)(HAL_GetTick() - last_frame) >= 250U &&
        OLED_TryBeginFrame() != 0U)
    {
      last_frame = HAL_GetTick();
      OLED_Clear();
      OLED_DrawString(0, 0, "V0 OLED TEST", 1U);
      OLED_DrawString(0, 8, "CM7 RUNNING", 1U);
      (void)snprintf(line, sizeof(line), "TIME %lu S",
                     (unsigned long)g_oled_smoke_seconds);
      OLED_DrawString(0, 16, line, 1U);
      (void)snprintf(line, sizeof(line), "I2C OK %lu",
                     (unsigned long)g_oled_i2c_ok);
      OLED_DrawString(0, 24, line, 1U);
      OLED_RequestRefresh();
    }
  }
}

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */
/* USER CODE BEGIN Boot_Mode_Sequence_0 */
/* USER CODE END Boot_Mode_Sequence_0 */

/* USER CODE BEGIN Boot_Mode_Sequence_1 */
  /* This test runs on CM7; CM4 is intentionally left unstarted/asleep. */
/* USER CODE END Boot_Mode_Sequence_1 */
  /* MCU Configuration--------------------------------------------------------*/

  /* Initialize HAL and clocks. */
  HAL_Init();
  g_oled_stage = 1U;

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();
  g_oled_stage = 2U;
/* USER CODE BEGIN Boot_Mode_Sequence_2 */
  __HAL_RCC_SYSCFG_CLK_ENABLE();
  HAL_EnableCompensationCell();
  {
    uint32_t start = HAL_GetTick();
    while ((SYSCFG->CCCSR & SYSCFG_CCCSR_READY) == 0U &&
           (uint32_t)(HAL_GetTick() - start) < 10U) {}
    s_io_comp_ready = ((SYSCFG->CCCSR & SYSCFG_CCCSR_READY) != 0U) ? 1U : 0U;
  }
  OLED_PortInit();
  g_oled_stage = 3U;
  OLED_Init();
  g_oled_stage = 4U;
  if (ETH_ONLY_MODE != 0U) ETH_PHY_DiagnosticRun();
  if (OLED_ONLY_MODE != 0U) OLED_SmokeTestRun();
  USB_DeviceApp_Init();
#if CAMERA_PROBE_MODE
#if WIFI_ONLY_MODE
  /* Wi-Fi-only isolation run: do not touch the camera/DCMI/DMA path. */
  /* The SDMMC clock is sourced from PLL2, which is prepared by the normal
     SDRAM clock setup. Keep that clock setup but do not start the camera. */
  (void)SDRAM_PrepareClockMHz(100U);
  (void)SDRAM_Init();
  RTL8723BS_SDIO_Probe();
  if (g_rtl8723bs_probe_stage == RTL8723BS_PROBE_CMD53_OK)
  {
    RTL8723BS_MAC_EnableBlocks();
    if (g_rtl8723bs_host_stage == 2U) RTL8723BS_FW_Download();
    if (g_rtl8723bs_fw_stage == 10U) RTL8723BS_MAC_Configure();
    if (g_rtl8723bs_mac_stage == 3U) RTL8723BS_BB_Configure();
    if (g_rtl8723bs_bb_stage == 5U) RTL8723BS_EFUSE_Read();
    if (g_rtl8723bs_efuse_stage == 5U) RTL8723BS_RF_Configure();
    if (g_rtl8723bs_rf_stage == 5U) RTL8723BS_Queue_Configure();
    if (g_rtl8723bs_queue_stage == 5U) RTL8723BS_WPA2_Prepare();
    if (g_rtl8723bs_queue_stage == 5U) RTL8723BS_RX_ConfigureAndListen();
  }
  for (;;)
  {
    static uint8_t auth_attempted;
    USB_DeviceApp_Process();
    Wifi_ProbeReport();
    if (g_rtl8723bs_rx_stage == 4U && g_rtl8723bs_rx_valid_beacon == 0U)
    {
      static uint32_t last_rx_retry;
      if ((uint32_t)(HAL_GetTick() - last_rx_retry) >= 2000U)
      {
        last_rx_retry = HAL_GetTick();
        RTL8723BS_RX_ConfigureAndListen();
      }
    }
    if (g_rtl8723bs_rx_valid_beacon != 0U && auth_attempted == 0U)
    {
      auth_attempted = 1U;
      RTL8723BS_TX_AuthProbe();
      if (g_rtl8723bs_tx_auth_stage == 3U &&
          g_rtl8723bs_tx_auth_status == 0U)
        RTL8723BS_TX_AssocProbe();
      if (g_rtl8723bs_tx_assoc_stage == 3U &&
          g_rtl8723bs_tx_assoc_status == 0U)
        RTL8723BS_RX_WaitEapolM1();
      if (g_rtl8723bs_eapol_stage == 2U && g_rtl8723bs_wpa_stage == 2U)
        RTL8723BS_WPA2_SendM2AndWaitM3();
      if (g_rtl8723bs_wpa_stage == 4U &&
          g_rtl8723bs_wpa_m3_mic_valid != 0U)
        RTL8723BS_WPA2_Complete();
      if (g_rtl8723bs_wpa_stage == 6U)
        RTL8723BS_DHCP_Acquire();
      else
      {
        /* Authentication can miss a response during RF/channel settling.
           Let the existing timed scan run again before the next attempt. */
        g_rtl8723bs_rx_valid_beacon = 0U;
        auth_attempted = 0U;
      }
    }
  }
#else
  Camera_ProbeInit();
  Camera_LiveInit();
#if WIFI_PROBE_MODE
  if (g_live_stage == 5U) RTL8723BS_SDIO_Probe();
#endif
  for (;;)
  {
    OLED_Process();
    USB_DeviceApp_Process();
    Camera_LivePoll();
#if WIFI_PROBE_MODE
    Wifi_ProbeReport();
#endif
  }
#endif
#else
  SDRAM_BeginProfile();

  for (;;)
  {
    OLED_Process();
    USB_LogSdramStatus();
    if (s_profile_advance_pending != 0U)
    {
      s_profile_advance_pending = 0U;
      SDRAM_BeginProfile();
    }
    else if (((g_sdram_test_phase >= 1U && g_sdram_test_phase <= 4U) ||
              (g_sdram_test_phase >= 8U && g_sdram_test_phase <= 16U)) &&
             g_sdram_test_done == 0U)
    {
      uint32_t old_phase = g_sdram_test_phase;
      uint32_t now;
      SDRAM_ReworkTestStep();
      if (g_sdram_test_phase != old_phase)
      {
        now = HAL_GetTick();
        if (old_phase == 1U)
        {
          s_data_elapsed_ms = now - s_phase_start_ms;
          s_data_pass = (g_sdram_test_phase == 2U) ? 1U : 0U;
        }
        else if (old_phase == 2U)
        {
          s_address_elapsed_ms = now - s_phase_start_ms;
          s_address_pass = (g_sdram_test_phase == 3U) ? 1U : 0U;
        }
        else if (old_phase == 3U)
        {
          s_write_elapsed_ms = now - s_phase_start_ms;
          s_write_pass = (g_sdram_test_phase == 4U) ? 1U : 0U;
        }
        else if (old_phase == 4U)
        {
          s_verify_elapsed_ms = now - s_phase_start_ms;
          s_verify_pass = (g_sdram_test_phase == 8U) ? 1U : 0U;
          if (s_verify_pass != 0U) s_stress_start_ms = now;
        }
        if (g_sdram_test_phase == 5U || g_sdram_test_phase == 6U)
        {
          if (g_sdram_test_phase == 6U) s_failed_phase = old_phase;
          if (g_sdram_stress_started != 0U)
          {
            s_stress_elapsed_ms = now - s_stress_start_ms;
          }
          SDRAM_FinishProfile();
        }
        s_phase_start_ms = now;
      }
    }
    if (OLED_TryBeginFrame() != 0U)
    {
      OLED_DrawSdramStatus();
    }
  }
#endif
/* USER CODE END Boot_Mode_Sequence_2 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Supply configuration update enable
  */
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 60;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_3;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
