/* Numeric register constants expanded from this repo's STM32H747 CMSIS.
 * Test-only register model; does not emulate bus timing or hardware DMA. */
#ifndef ETH_TEST_FAKE_HAL_H
#define ETH_TEST_FAKE_HAL_H
#include <stdint.h>
#include <stddef.h>
#define ETH_DMACCR_DSL_128BIT (0x00100000U)
#define ETH_DMACIER_AIE (0x1UL << (14U))
#define ETH_DMACIER_CDEE (0x1UL << (13U))
#define ETH_DMACIER_FBEE (0x1UL << (12U))
#define ETH_DMACIER_NIE (0x1UL << (15U))
#define ETH_DMACIER_RBUE (0x1UL << (7U))
#define ETH_DMACIER_RIE (0x1UL << (6U))
#define ETH_DMACIER_RSE (0x1UL << (8U))
#define ETH_DMACIER_TBUE (0x1UL << (2U))
#define ETH_DMACIER_TIE (0x1UL << (0U))
#define ETH_DMACIER_TXSE (0x1UL << (1U))
#define ETH_DMACRCR_RPBL_Pos (16U)
#define ETH_DMACRCR_SR (0x1UL << (0U))
#define ETH_DMACSR_AIS (0x1UL << (14U))
#define ETH_DMACSR_CDE (0x1UL << (13U))
#define ETH_DMACSR_FBE (0x1UL << (12U))
#define ETH_DMACSR_NIS (0x1UL << (15U))
#define ETH_DMACSR_RBU (0x1UL << (7U))
#define ETH_DMACSR_RI (0x1UL << (6U))
#define ETH_DMACSR_RPS (0x1UL << (8U))
#define ETH_DMACSR_TBU (0x1UL << (2U))
#define ETH_DMACSR_TI (0x1UL << (0U))
#define ETH_DMACSR_TPS (0x1UL << (1U))
#define ETH_DMACTCR_ST (0x1UL << (0U))
#define ETH_DMACTCR_TPBL_Pos (16U)
#define ETH_DMAMR_SWR (0x1UL << (0U))
#define ETH_DMASBMR_AAL (0x1UL << (12U))
#define ETH_DMASBMR_FB (0x1UL << (0U))
#define ETH_MACCR_CST (0x1UL << (21U))
#define ETH_MACCR_ACS (0x1UL << (20U))
#define ETH_MACCR_DM (0x1UL << (13U))
#define ETH_MACCR_FES (0x1UL << (14U))
#define ETH_MACCR_RE (0x1UL << (0U))
#define ETH_MACCR_TE (0x1UL << (1U))
#define ETH_MACIER_RXSTSIE (0x1UL << (14U))
#define ETH_MACIER_TXSTSIE (0x1UL << (13U))
#define ETH_MACISR_RXSTSIS (0x1UL << (14U))
#define ETH_MACISR_TXSTSIS (0x1UL << (13U))
#define ETH_MACMDIOAR_CR_DIV102 (0x1UL << (10U))
#define ETH_MACMDIOAR_CR_DIV124 (0x5UL << (8U))
#define ETH_MACMDIOAR_CR_DIV16 (0x1UL << (9U))
#define ETH_MACMDIOAR_CR_DIV26 (0x3UL << (8U))
#define ETH_MACMDIOAR_CR_DIV42 (0U)
#define ETH_MACMDIOAR_CR_DIV62 (0x1UL << (8U))
#define ETH_MACMDIOAR_MB (0x1UL << (0U))
#define ETH_MACMDIOAR_MOC_RD (0x3UL << (2U))
#define ETH_MACMDIOAR_MOC_WR (0x1UL << (2U))
#define ETH_MTLRQOMR_RSF (0x1UL << (5U))
#define ETH_MTLTQOMR_FTQ (0x1UL << (0U))
#define ETH_MTLTQOMR_TSF (0x1UL << (1U))
#define GPIO_AF11_ETH ((uint8_t)0x0B)
#define GPIO_MODE_AF_PP ((0x2uL << 0u) | (0x0uL << 4u))
#define GPIO_MODE_OUTPUT_PP ((0x1uL << 0u) | (0x0uL << 4u))
#define GPIO_NOPULL (0x00000000U)
#define GPIO_PIN_1 ((uint16_t)0x0002)
#define GPIO_PIN_11 ((uint16_t)0x0800)
#define GPIO_PIN_13 ((uint16_t)0x2000)
#define GPIO_PIN_14 ((uint16_t)0x4000)
#define GPIO_PIN_2 ((uint16_t)0x0004)
#define GPIO_PIN_4 ((uint16_t)0x0010)
#define GPIO_PIN_5 ((uint16_t)0x0020)
#define GPIO_PIN_7 ((uint16_t)0x0080)
#define GPIO_PIN_9 ((uint16_t)0x0200)
#define GPIO_SPEED_FREQ_LOW (0x00000000U)
#define GPIO_SPEED_FREQ_VERY_HIGH (0x00000003U)
#define SYSCFG_ETH_RMII (0x4UL << (21U))
typedef struct
{
	volatile uint32_t DMACCR;
	volatile uint32_t DMACIER;
	volatile uint32_t DMACRCR;
	volatile uint32_t DMACRDLAR;
	volatile uint32_t DMACRDRLR;
	volatile uint32_t DMACRDTPR;
	volatile uint32_t DMACSR;
	volatile uint32_t DMACTCR;
	volatile uint32_t DMACTDLAR;
	volatile uint32_t DMACTDRLR;
	volatile uint32_t DMACTDTPR;
	volatile uint32_t DMAMR;
	volatile uint32_t DMASBMR;
	volatile uint32_t MACA0HR;
	volatile uint32_t MACA0LR;
	volatile uint32_t MACCR;
	volatile uint32_t MACIER;
	volatile uint32_t MACISR;
	volatile uint32_t MACMDIOAR;
	volatile uint32_t MACMDIODR;
	volatile uint32_t MACPFR;
	volatile uint32_t MACRXTXSR;
	volatile uint32_t MMCRIMR;
	volatile uint32_t MMCTIMR;
	volatile uint32_t MTLRQOMR;
	volatile uint32_t MTLTQOMR;
} FakeEth;
extern FakeEth fake_eth;
#define ETH (&fake_eth)
typedef struct
{
	volatile uint32_t AHB1RSTR;
} FakeRcc;
extern FakeRcc fake_rcc;
#define RCC (&fake_rcc)
typedef struct
{
	volatile uint32_t PMCR;
} FakeSyscfg;
extern FakeSyscfg fake_syscfg;
#define SYSCFG (&fake_syscfg)
#define GPIOA ((void *)1)
#define GPIOC ((void *)2)
#define GPIOG ((void *)3)
#define ETH_IRQn 61
#define RESET 0U
#define GPIO_PIN_RESET 0U
#define GPIO_PIN_SET 1U
typedef struct
{
	uint32_t Pin, Mode, Pull, Speed, Alternate;
} GPIO_InitTypeDef;
uint32_t HAL_GetTick(void);
uint32_t HAL_RCC_GetHCLKFreq(void);
void HAL_Delay(uint32_t);
void HAL_GPIO_WritePin(void *, uint32_t, uint32_t);
void HAL_GPIO_Init(void *, GPIO_InitTypeDef *);
void HAL_SYSCFG_ETHInterfaceSelect(uint32_t);
void HAL_NVIC_DisableIRQ(int);
void HAL_NVIC_EnableIRQ(int);
void HAL_NVIC_ClearPendingIRQ(int);
void fake_reset(void);
#define __DSB() ((void)0)
#define __get_PRIMASK() 0U
#define __disable_irq() ((void)0)
#define __set_PRIMASK(x) ((void)(x))
#define __HAL_RCC_ETH1MAC_CLK_ENABLE() ((void)0)
#define __HAL_RCC_ETH1MAC_FORCE_RESET() fake_reset()
#define __HAL_RCC_ETH1MAC_RELEASE_RESET() ((void)0)
#define __HAL_RCC_ETH1RX_CLK_ENABLE() ((void)0)
#define __HAL_RCC_ETH1TX_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOA_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOC_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOG_CLK_ENABLE() ((void)0)
#define __HAL_RCC_SYSCFG_CLK_ENABLE() ((void)0)
#endif
