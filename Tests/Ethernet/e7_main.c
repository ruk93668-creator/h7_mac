#include "stm32h7xx_hal.h"
#include "eth_test_board.h"
#include "eth_memory.h"
#include "eth_http.h"
#include "e7_camera_provider.h"
#include "bsp_sdram.h"
#include "bsp_dcmi.h"
#include "camera_mjpeg_fifo.h"
#include "ov5640_device.h"

#define E7_NETWORK_START 0xD07F0000UL
#define E7_AVI_END 0xD05A0000UL
#if E7_NETWORK_START < CAMERA_MJPEG_REGION_END || E7_NETWORK_START < E7_AVI_END || \
	E7_NETWORK_START + E7_JPEG_CAPACITY > BSP_SDRAM_END
#error E7 network window must be outside all existing SDRAM users
#endif

static uint8_t g_e7_jpeg[E7_JPEG_CAPACITY] __attribute__((section(".e7_jpeg"), zero_init, aligned(32), used));
static EthNetwork_t s_network;
static Device_t *s_sensor;
static E7CameraProvider_t s_provider;
static bool s_camera_initialized;
static const uint8_t s_mac[6] = {2U, 0x47U, 0x47U, 0U, 0U, 1U};

volatile SysResult_t g_e7_sdram_result, g_e7_camera_init, g_e7_command_result;
volatile int32_t g_e7_result;
volatile uint32_t g_e7_mode = ETH_ADDRESS_DHCP;
volatile uint32_t g_e7_command; /* 1 Stop, 2 Restart */
volatile uint16_t g_e7_sensor_id;
volatile uint32_t g_e7_heartbeat, g_e7_ccr, g_e7_dcmi_cr, g_e7_dma_cr;
volatile uint32_t g_e7_dcmi_risr, g_e7_dma_lisr, g_e7_submission_fps_milli;
volatile E7CameraProvider_t g_e7_camera;
volatile EthHttpStats_t g_e7_http;
volatile EthTransportStats_t g_e7_transport;
volatile EthNetworkAddressStatus_t g_e7_address;
volatile EthStatistics_t g_e7_ethernet;
const char *volatile g_e7_assert_message;

void eth_lwip_assert(const char *message, const char *file, unsigned line)
{
	(void)file;
	(void)line;
	g_e7_assert_message = message;
	g_e7_result = -100;
	for (;;) { }
}

static void E7Mpu(void)
{
	MPU_Region_InitTypeDef region = {0};
	region.Enable = MPU_REGION_ENABLE;
	region.Number = MPU_REGION_NUMBER2;
	region.BaseAddress = BSP_SDRAM_BASE;
	region.Size = MPU_REGION_SIZE_8MB;
	region.TypeExtField = MPU_TEX_LEVEL1;
	region.AccessPermission = MPU_REGION_FULL_ACCESS;
	region.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
	region.IsShareable = MPU_ACCESS_SHAREABLE;
	region.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
	region.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
	HAL_MPU_ConfigRegion(&region);
}

static SysResult_t E7Stop(void)
{
	SysResult_t result = E7Camera_Stop(&s_provider);
	if (result != SYS_OK)
		return result;
	return s_sensor->state == DEVICE_STATE_RUNNING ? Device_Stop(s_sensor) :
		(s_sensor->state == DEVICE_STATE_READY ? SYS_OK : SYS_ERR_INVALID_STATE);
}

static SysResult_t E7Start(void)
{
	SysResult_t result;
	if (!s_camera_initialized || s_provider.original_loan || s_provider.pinned ||
		s_sensor->state != DEVICE_STATE_READY || !CameraCapture_IsStopped())
		return SYS_ERR_BUSY;
	result = Device_Start(s_sensor);
	if (result != SYS_OK)
		return result;
	result = E7Camera_Start(&s_provider);
	if (result != SYS_OK)
	{
		SysResult_t stop = E7Stop();
		return stop == SYS_OK ? result : stop;
	}
	return SYS_OK;
}

static SysResult_t E7CameraInit(void)
{
	const CameraConfig_t config = {.width = 160U, .height = 120U, .format = CAMERA_PIXEL_FORMAT_JPEG};
	Ov5640Device_t *camera;
	SysResult_t result;
	uint16_t id = 0U;
	g_e7_sdram_result = BSP_SDRAM_Init();
	if (g_e7_sdram_result != SYS_OK || !BSP_SDRAM_IsReady())
		return SYS_ERR_IO;
	result = BSP_DCMI_Init();
	if (result != SYS_OK)
		return result;
	/* This execution region contains exactly the OV5640 device export. */
	result = Device_RegisterAll();
	if (result != SYS_OK)
		return result;
	s_sensor = Device_Find("ov5640");
	if (!s_sensor)
		return SYS_ERR_NOT_FOUND;
	result = Device_Init(s_sensor);
	if (result != SYS_OK)
		return result;
	camera = FRAMEWORK_CONTAINER_OF(s_sensor, Ov5640Device_t, base);
	result = camera->ops->read_id(camera, &id);
	g_e7_sensor_id = id;
	if (result != SYS_OK || id != OV5640_CHIP_ID)
		return SYS_ERR_IO;
	result = camera->ops->configure(camera, &config);
	if (result != SYS_OK)
		return result;
	result = CameraCapture_Init(&config);
	if (result != SYS_OK)
		return result;
	s_provider.buffer = g_e7_jpeg;
	s_provider.fifo = (const uint8_t *)CAMERA_MJPEG_FIFO_BASE;
	s_provider.fifo_size = CAMERA_MJPEG_FIFO_BYTES;
	s_camera_initialized = true;
	return E7Start();
}

int main(void)
{
	EthNetworkAddressConfig_t config = {.mode = ETH_ADDRESS_DHCP};
	const EthMjpegSource_t source = {&s_provider, E7Camera_Acquire, E7Camera_Release};
	uint32_t last = 0U;
	uint32_t fps_tick = 0U, prior_submitted = 0U;
	if (!eth_test_board_init_with_mpu(E7Mpu) || !eth_memory_valid() ||
		(uintptr_t)g_e7_jpeg != E7_NETWORK_START || g_e7_mode > ETH_ADDRESS_DHCP)
		eth_lwip_assert("E7 setup", __FILE__, __LINE__);
	config.mode = (EthNetworkAddressMode_t)g_e7_mode;
	config.timeout_ms = 30000U;
	IP4_ADDR(&config.ip, 192, 168, 50, 2);
	IP4_ADDR(&config.mask, 255, 255, 255, 0);
	g_e7_camera_init = E7CameraInit();
	g_e7_result = eth_network_init_address(&s_network, s_mac, &config);
	if (g_e7_result == ERR_OK)
		g_e7_result = eth_http_init_source(&s_network, &source, 200U);
	if (g_e7_result != ERR_OK)
	{
		if (s_camera_initialized)
			g_e7_command_result = E7Stop();
		for (;;) { }
	}
	for (;;)
	{
		uint32_t now = HAL_GetTick();
		uint32_t command = g_e7_command;
		++g_e7_heartbeat;
		if (command)
		{
			g_e7_command = 0U;
			g_e7_command_result = !s_camera_initialized ? SYS_ERR_INVALID_STATE :
				command == 1U ? E7Stop() : command == 2U ? E7Start() : SYS_ERR_INVALID_ARG;
		}
		if (s_camera_initialized)
			E7Camera_Poll(&s_provider, now);
		eth_network_poll(&s_network);
		eth_transport_poll();
		eth_http_poll();
		if (now - last >= 100U)
		{
			EthHttpStats_t http;
			EthTransportStats_t transport;
			EthStatistics_t eth;
			last = now;
			g_e7_ccr = SCB->CCR;
			g_e7_dcmi_cr = DCMI->CR;
			g_e7_dma_cr = DMA1_Stream0->CR;
			g_e7_dcmi_risr = DCMI->RISR;
			g_e7_dma_lisr = DMA1->LISR;
			g_e7_camera = s_provider;
			g_e7_address = s_network.address;
			eth_http_get_statistics(&http);
			if (now - fps_tick >= 1000U)
			{
				g_e7_submission_fps_milli = (http.stream.frames_submitted - prior_submitted) * 1000000U /
					(now - fps_tick);
				prior_submitted = http.stream.frames_submitted;
				fps_tick = now;
			}
			g_e7_http = http;
			eth_transport_get_statistics(&transport);
			g_e7_transport = transport;
			if (eth_mac_get_statistics(&eth) == ETH_OK)
				g_e7_ethernet = eth;
		}
	}
}
