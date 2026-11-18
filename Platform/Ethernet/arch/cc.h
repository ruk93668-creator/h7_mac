//Keil ARMCC 与主机验证的 lwIP 编译器适配
#ifndef ETH_LWIP_ARCH_CC_H
#define ETH_LWIP_ARCH_CC_H

#if defined(__CC_ARM)
#define PACK_STRUCT_BEGIN __packed
#elif defined(_MSC_VER) && !defined(__clang__)
#define PACK_STRUCT_USE_INCLUDES
#endif

void eth_lwip_assert(const char *message, const char *file, unsigned line);
#define LWIP_PLATFORM_ASSERT(message) eth_lwip_assert((message), __FILE__, __LINE__)
#define LWIP_PLATFORM_DIAG(message) do { } while (0)

#endif
