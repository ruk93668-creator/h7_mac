#ifndef E5_TEST_PROTOCOL_H
#define E5_TEST_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#define E5_HEADER 16U
#define E5_LIMIT 1472U
#define E5_REQUEST 1U
#define E5_REPLY 2U

uint16_t e5_frame_length(const uint8_t *header);
int e5_frame_valid(const uint8_t *frame, uint16_t length);
uint16_t e5_frame_build(uint8_t *frame, uint8_t type, uint32_t seq, uint16_t payload);
uint32_t e5_frame_sequence(const uint8_t *frame);

#endif
