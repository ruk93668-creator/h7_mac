#include "e35_frame.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
	static const uint16_t lengths[] = {60U, 512U, 1514U, 1518U, 1522U};
	uint8_t frame[1522];
	EthValidationFrame_t config = {0x12345678U, 257U, 60U, 0U};
	EthValidationFrame_t decoded;
	size_t index;
	int direction;
	int vectors = argc == 2 && strcmp(argv[1], "--vectors") == 0;
	for (index = 0U;
		 index < sizeof(lengths) / sizeof(lengths[0]);
		 ++index)
	{
		config.length = lengths[index];
		config.vlan = index >= 3U;
		for (direction = 0;
			 direction <= 1;
			 ++direction)
		{
			assert(EthValidation_Build(frame, &config, direction));
			if (vectors)
			{
				size_t byte;
				printf("%u %d ", (unsigned)config.length, direction);
				for (byte = 0U;
					 byte < config.length;
					 ++byte)
					printf("%02x", (unsigned)frame[byte]);
				puts("");
			}
			assert(EthValidation_Check(frame, config.length, config.session, direction, &decoded) == 1);
			assert(decoded.sequence == 257U && decoded.vlan == config.vlan);
			assert(EthValidation_Check(frame, config.length, config.session + 1U, direction, &decoded) == 0);
			assert(EthValidation_Check(frame, config.length, config.session, !direction, &decoded) == -1);
			frame[config.length - 1U] ^= 1U;
			assert(EthValidation_Check(frame, config.length, config.session, direction, &decoded) == -1);
		}
	}
	config.length = 1523U;
	assert(!EthValidation_Build(frame, &config, 0));
	if (!vectors)
		puts("PASS E3.5 frame lengths/session/direction/corruption");
	return 0;
}
