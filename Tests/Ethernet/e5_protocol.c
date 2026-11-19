#include "e5_protocol.h"

static uint32_t Get32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static void Put32(uint8_t *p, uint32_t value)
{
	p[0] = (uint8_t)(value >> 24);
	p[1] = (uint8_t)(value >> 16);
	p[2] = (uint8_t)(value >> 8);
	p[3] = (uint8_t)value;
}

static uint32_t Crc32(const uint8_t *data, uint16_t length)
{
	uint32_t crc = UINT32_MAX;
	unsigned bit;
	while (length--)
	{
		crc ^= *data++;
		for (bit = 0; bit < 8; ++bit)
			crc = (crc >> 1) ^ ((crc & 1U) ? UINT32_C(0xEDB88320) : 0U);
	}
	return ~crc;
}

uint16_t e5_frame_length(const uint8_t *header)
{
	uint16_t payload = (uint16_t)(((uint16_t)header[6] << 8) | header[7]);
	if (Get32(header) != UINT32_C(0x48374535) || header[4] != 1U ||
		(header[5] != E5_REQUEST && header[5] != E5_REPLY) || payload > E5_LIMIT - E5_HEADER)
		return 0U;
	return (uint16_t)(payload + E5_HEADER);
}

int e5_frame_valid(const uint8_t *frame, uint16_t length)
{
	return length >= E5_HEADER && e5_frame_length(frame) == length &&
		Get32(frame + 12U) == Crc32(frame + E5_HEADER, (uint16_t)(length - E5_HEADER));
}

uint16_t e5_frame_build(uint8_t *frame, uint8_t type, uint32_t seq, uint16_t payload)
{
	uint16_t i;
	if (payload > E5_LIMIT - E5_HEADER || (type != E5_REQUEST && type != E5_REPLY))
		return 0U;
	Put32(frame, UINT32_C(0x48374535));
	frame[4] = 1U;
	frame[5] = type;
	frame[6] = (uint8_t)(payload >> 8);
	frame[7] = (uint8_t)payload;
	Put32(frame + 8U, seq);
	for (i = 0U; i < payload; ++i)
		frame[E5_HEADER + i] = (uint8_t)(seq + i);
	Put32(frame + 12U, Crc32(frame + E5_HEADER, payload));
	return (uint16_t)(E5_HEADER + payload);
}

uint32_t e5_frame_sequence(const uint8_t *frame)
{
	return Get32(frame + 8U);
}
