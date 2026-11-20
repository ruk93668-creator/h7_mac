#include "tusb.h"
extern volatile uint32_t g_video_result[8];
void tud_video_frame_xfer_complete_cb(uint_fast8_t a,uint_fast8_t b) { (void)a;(void)b;++g_video_result[5]; }
void tud_video_frame_xfer_abort_cb(uint_fast8_t a,uint_fast8_t b) { (void)a;(void)b;++g_video_result[6]; }
bool usbd_edpt_claim(uint8_t p,uint8_t a) { (void)p;(void)a;++g_video_result[7];return false; }
bool usbd_edpt_release(uint8_t p,uint8_t a) { (void)p;(void)a;++g_video_result[7];return false; }
bool usbd_edpt_xfer(uint8_t p,uint8_t a,uint8_t *b,uint16_t n,bool i) { (void)p;(void)a;(void)b;(void)n;(void)i;++g_video_result[7];return false; }
bool usbd_edpt_busy(uint8_t p,uint8_t a) { (void)p;(void)a;++g_video_result[7];return true; }
bool usbd_edpt_stalled(uint8_t p,uint8_t a) { (void)p;(void)a;++g_video_result[7];return true; }
void usbd_edpt_abort(uint8_t p,uint8_t a) { (void)p;(void)a;++g_video_result[7]; }
bool usbd_edpt_open(uint8_t p,tusb_desc_endpoint_t const *d) { (void)p;(void)d;++g_video_result[7];return false; }
void usbd_edpt_close(uint8_t p,uint8_t a) { (void)p;(void)a;++g_video_result[7]; }
bool usbd_edpt_iso_alloc(uint8_t p,uint8_t a,uint16_t n) { (void)p;(void)a;(void)n;++g_video_result[7];return false; }
bool usbd_edpt_iso_activate(uint8_t p,tusb_desc_endpoint_t const *d) { (void)p;(void)d;++g_video_result[7];return false; }
bool tud_control_xfer(uint8_t p,tusb_control_request_t const *r,void *b,uint16_t n) { (void)p;(void)r;(void)b;(void)n;++g_video_result[7];return false; }
bool tud_control_status(uint8_t p,tusb_control_request_t const *r) { (void)p;(void)r;++g_video_result[7];return false; }
