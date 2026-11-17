#include "e35_frame.h"
#include <string.h>

static const uint8_t s_board_mac[6] = {2U, 0x47U, 0x47U, 0U, 0U, 1U};
static const uint8_t s_peer_mac[6] = {0xECU, 0x1AU, 0xC3U, 0x3EU, 0xDBU, 0xB2U};

static void EthValidation_Write32(uint8_t *data, uint32_t value)
{
	data[0] = (uint8_t)(value >> 24);
	data[1] = (uint8_t)(value >> 16);
	data[2] = (uint8_t)(value >> 8);
	data[3] = (uint8_t)value;
}

static uint32_t EthValidation_Read32(const uint8_t *data)
{
	return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
		((uint32_t)data[2] << 8) | data[3];
}

int EthValidation_Build(uint8_t *frame, const EthValidationFrame_t *config, int from_board)
{
	size_t offset = config->vlan ? 18U : 14U;
	size_t index;
	if (config->length < 60U || config->length > 1522U || config->session == 0U ||
		config->sequence == 0U)
		return 0;
	memcpy(frame, from_board ? s_peer_mac : s_board_mac, 6U);
	memcpy(frame + 6, from_board ? s_board_mac : s_peer_mac, 6U);
	if (config->vlan)
	{
		frame[12] = 0x81U;
		frame[13] = 0U;
		frame[14] = 0U;
		frame[15] = 7U;
	}
	frame[offset - 2U] = 0x88U;
	frame[offset - 1U] = 0xB5U;
	memcpy(frame + offset, "E35!", 4U);
	EthValidation_Write32(frame + offset + 4U, config->session);
	EthValidation_Write32(frame + offset + 8U, config->sequence);
	frame[offset + 12U] = (uint8_t)(config->length >> 8);
	frame[offset + 13U] = (uint8_t)config->length;
	for (index = offset + 14U;
		 index < config->length;
		 ++index)
		frame[index] = (uint8_t)(index ^ config->sequence ^ (config->sequence >> 8) ^ config->session);
	return 1;
}

int EthValidation_Check(const uint8_t *frame, size_t length, uint32_t session,
	int from_board, EthValidationFrame_t *decoded)
{
	size_t offset;
	size_t index;
	if (length < 60U || length > 1522U)
		return 0;
	offset = frame[12] == 0x81U && frame[13] == 0U ? 18U : 14U;
	if (frame[offset - 2U] != 0x88U || frame[offset - 1U] != 0xB5U ||
		memcmp(frame + offset, "E35!", 4U) != 0 || EthValidation_Read32(frame + offset + 4U) != session)
		return 0;
	decoded->session = session;
	decoded->sequence = EthValidation_Read32(frame + offset + 8U);
	decoded->length = (uint16_t)(((uint16_t)frame[offset + 12U] << 8) | frame[offset + 13U]);
	decoded->vlan = offset == 18U;
	if (decoded->sequence == 0U || decoded->length != length ||
		memcmp(frame, from_board ? s_peer_mac : s_board_mac, 6U) != 0 ||
		memcmp(frame + 6, from_board ? s_board_mac : s_peer_mac, 6U) != 0 ||
		(decoded->vlan && (frame[14] != 0U || frame[15] != 7U)))
		return -1;
	for (index = offset + 14U;
		 index < length;
		 ++index)
		if (frame[index] != (uint8_t)(index ^ decoded->sequence ^ (decoded->sequence >> 8) ^ session))
			return -1;
	return 1;
}
