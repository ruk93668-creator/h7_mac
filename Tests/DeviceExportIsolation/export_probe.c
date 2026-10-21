/* Link-only probe: actual CM4 OLED source and production scatter file.
 * Missing OLED operations are defined only here as inert test stubs.
 * This probe is not part of either Keil Target and is never flashed.
 */
#include "framework_init.h"
#include "../../CM4/Device/Src/oled_device.c"

static SysResult_t Oled_ClearImpl(OledDevice_t *oled)
{
    (void)oled;
    return SYS_ERR_NOT_SUPPORTED;
}
static SysResult_t Oled_DrawPixelImpl(OledDevice_t *oled, uint16_t x, uint16_t y, bool on)
{
    (void)oled; (void)x; (void)y; (void)on;
    return SYS_ERR_NOT_SUPPORTED;
}
static SysResult_t Oled_RefreshImpl(OledDevice_t *oled)
{
    (void)oled;
    return SYS_ERR_NOT_SUPPORTED;
}

/* Satisfy the other selectors in the unchanged production scatter file. */
__attribute__((used,section("RESET")))
static const uint32_t probe_vectors[2] = { 0x10048000U, 0U };
__attribute__((used,section(".shared")))
static uint32_t probe_shared;
static SysResult_t Probe_Init(void) { return SYS_OK; }
FRAMEWORK_INIT_REGISTER(Probe_Init, INIT_LEVEL_DEVICE);

uint32_t Export_ProbeEntry(void)
{
    return g_device_export_s_oled.device == &s_oled.base ? 0U : 1U;
}
