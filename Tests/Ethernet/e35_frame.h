//独立实板验收协议；长度均不含 FCS，不属于生产驱动接口
#ifndef E35_FRAME_H
#define E35_FRAME_H
#include <stddef.h>
#include <stdint.h>

typedef struct
{
	uint32_t session;
	uint32_t sequence;
	uint16_t length;
	uint8_t vlan;
} EthValidationFrame_t;

int EthValidation_Build(uint8_t *frame, const EthValidationFrame_t *config, int from_board);
//1 有效，0 非本会话/非测试帧，-1 本会话帧损坏
int EthValidation_Check(const uint8_t *frame, size_t length, uint32_t session,
	int from_board, EthValidationFrame_t *decoded);
#endif
