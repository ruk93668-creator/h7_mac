#include "tusb.h"
static tusb_desc_device_t const device = {
    .bLength=sizeof(tusb_desc_device_t), .bDescriptorType=TUSB_DESC_DEVICE,
    .bcdUSB=0x0200, .bMaxPacketSize0=64, .idVendor=0x1209, .idProduct=0x0001,
    .bcdDevice=0x0001, .bNumConfigurations=1
};
static uint8_t const config[] = {
    TUD_CONFIG_DESCRIPTOR(1,1,0,TUD_CONFIG_DESC_LEN+TUD_MSC_DESC_LEN,0,100),
    TUD_MSC_DESCRIPTOR(0,0,0x02,0x82,64)
};
uint8_t const *tud_descriptor_device_cb(void) { return (uint8_t const *)&device; }
uint8_t const *tud_descriptor_configuration_cb(uint8_t index) { return index==0U ? config : NULL; }
uint16_t const *tud_descriptor_string_cb(uint8_t index,uint16_t language) { (void)index;(void)language;return NULL; }
