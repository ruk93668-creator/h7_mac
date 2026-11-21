/* Legacy wait-only fixture does not link USBD. Fail-only publication adapters.
 * None may run in the original nine cases; the wrapper verifies that independently.
 * Reset success uses the complete AXF/USBD tests, never these adapters.
 */
#include "device/dcd.h"
volatile uint32_t g_reset_r1_unused_hook_calls;
bool dcd_event_reset_try(uint8_t p,bool end,tusb_speed_t speed,bool isr) {
 (void)p;(void)end;(void)speed;(void)isr;g_reset_r1_unused_hook_calls++;return false;
}
bool dcd_event_setup_fence(uint8_t p,bool isr) {
 (void)p;(void)isr;g_reset_r1_unused_hook_calls++;return false;
}
bool dcd_event_setup_try(uint8_t p,uint8_t const* setup,uint8_t tag,bool isr) {
 (void)p;(void)setup;(void)tag;(void)isr;g_reset_r1_unused_hook_calls++;return false;
}
