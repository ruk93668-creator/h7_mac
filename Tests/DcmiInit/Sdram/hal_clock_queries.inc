/**
  ******************************************************************************
  * @file    stm32h7xx_hal_rcc_ex.c
  * @author  MCD Application Team
  * @brief   Extended RCC HAL module driver.
  *          This file provides firmware functions to manage the following
  *          functionalities RCC extension peripheral:
  *           + Extended Peripheral Control functions
  *
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2017 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file in
  * the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  ******************************************************************************
  */

uint32_t HAL_RCCEx_GetPeriphCLKFreq(uint64_t PeriphClk)
{
  PLL1_ClocksTypeDef pll1_clocks;
  PLL2_ClocksTypeDef pll2_clocks;
  PLL3_ClocksTypeDef pll3_clocks;

  /* This variable is used to store the clock frequency (value in Hz) */
  uint32_t frequency;
  /* This variable is used to store the SAI and CKP clock source */
  uint32_t saiclocksource;
  uint32_t ckpclocksource;
  uint32_t srcclk;

  if (PeriphClk == RCC_PERIPHCLK_SAI1)
  {

    saiclocksource = __HAL_RCC_GET_SAI1_SOURCE();

    switch (saiclocksource)
    {
      case RCC_SAI1CLKSOURCE_PLL: /* PLL1 is the clock source for SAI1 */
      {
        if (HAL_IS_BIT_SET(RCC->CR, RCC_CR_PLL1RDY))
        {
          HAL_RCCEx_GetPLL1ClockFreq(&pll1_clocks);
          frequency = pll1_clocks.PLL1_Q_Frequency;
        }
        else
        {
          frequency = 0;
        }
        break;
      }
      case RCC_SAI1CLKSOURCE_PLL2: /* PLL2 is the clock source for SAI1 */
      {
        if (HAL_IS_BIT_SET(RCC->CR, RCC_CR_PLL2RDY))
        {
          HAL_RCCEx_GetPLL2ClockFreq(&pll2_clocks);
          frequency = pll2_clocks.PLL2_P_Frequency;
        }
        else
        {
          frequency = 0;
        }
        break;
      }

      case RCC_SAI1CLKSOURCE_PLL3: /* PLL3 is the clock source for SAI1 */
      {
        if (HAL_IS_BIT_SET(RCC->CR, RCC_CR_PLL3RDY))
        {
          HAL_RCCEx_GetPLL3ClockFreq(&pll3_clocks);
          frequency = pll3_clocks.PLL3_P_Frequency;
        }
        else
        {
          frequency = 0;
        }
        break;
      }

      case RCC_SAI1CLKSOURCE_CLKP: /* CKPER is the clock source for SAI1*/
      {

        ckpclocksource = __HAL_RCC_GET_CLKP_SOURCE();

        if ((HAL_IS_BIT_SET(RCC->CR, RCC_CR_HSIRDY)) && (ckpclocksource == RCC_CLKPSOURCE_HSI))
        {
          /* In Case the CKPER Source is HSI */
          frequency = (HSI_VALUE >> (__HAL_RCC_GET_HSI_DIVIDER() >> 3));
        }

        else if ((HAL_IS_BIT_SET(RCC->CR, RCC_CR_CSIRDY)) && (ckpclocksource == RCC_CLKPSOURCE_CSI))
        {
          /* In Case the CKPER Source is CSI */
          frequency = CSI_VALUE;
        }

        else if ((HAL_IS_BIT_SET(RCC->CR, RCC_CR_HSERDY)) && (ckpclocksource == RCC_CLKPSOURCE_HSE))
        {
          /* In Case the CKPER Source is HSE */
          frequency = HSE_VALUE;
        }

        else
        {
          /* In Case the CKPER is disabled*/
          frequency = 0;
        }

        break;
      }

      case (RCC_SAI1CLKSOURCE_PIN): /* External clock is the clock source for SAI1 */
      {
        frequency = EXTERNAL_CLOCK_VALUE;
        break;
      }
      default :
      {
        frequency = 0;
        break;
      }
    }
  }

#if defined(SAI3)
  else if (PeriphClk == RCC_PERIPHCLK_SAI23)
  {

    saiclocksource = __HAL_RCC_GET_SAI23_SOURCE();

    switch (saiclocksource)
    {
      case RCC_SAI23CLKSOURCE_PLL: /* PLL1 is the clock source for SAI2/3 */
      {
        if (HAL_IS_BIT_SET(RCC->CR, RCC_CR_PLL1RDY))
        {
          HAL_RCCEx_GetPLL1ClockFreq(&pll1_clocks);
          frequency = pll1_clocks.PLL1_Q_Frequency;
        }
        else
        {
          frequency = 0;
        }
        break;
      }
      case RCC_SAI23CLKSOURCE_PLL2: /* PLL2 is the clock source for SAI2/3 */
      {
        if (HAL_IS_BIT_SET(RCC->CR, RCC_CR_PLL2RDY))
        {
          HAL_RCCEx_GetPLL2ClockFreq(&pll2_clocks);
          frequency = pll2_clocks.PLL2_P_Frequency;
        }
        else
        {
          frequency = 0;
        }
        break;
      }

      case RCC_SAI23CLKSOURCE_PLL3: /* PLL3 is the clock source for SAI2/3 */
      {
        if (HAL_IS_BIT_SET(RCC->CR, RCC_CR_PLL3RDY))
        {
          HAL_RCCEx_GetPLL3ClockFreq(&pll3_clocks);
          frequency = pll3_clocks.PLL3_P_Frequency;
        }
        else
        {
          frequency = 0;
        }
        break;
      }

      case RCC_SAI23CLKSOURCE_CLKP: /* CKPER is the clock source for SAI2/3 */
      {

        ckpclocksource = __HAL_RCC_GET_CLKP_SOURCE();

        if ((HAL_IS_BIT_SET(RCC->CR, RCC_CR_HSIRDY)) && (ckpclocksource == RCC_CLKPSOURCE_HSI))
        {
          /* In Case the CKPER Source is HSI */
          frequency = (HSI_VALUE >> (__HAL_RCC_GET_HSI_DIVIDER() >> 3));
        }

        else if ((HAL_IS_BIT_SET(RCC->CR, RCC_CR_CSIRDY)) && (ckpclocksource == RCC_CLKPSOURCE_CSI))
        {
          /* In Case the CKPER Source is CSI */
          frequency = CSI_VALUE;
        }

        else if ((HAL_IS_BIT_SET(RCC->CR, RCC_CR_HSERDY)) && (ckpclocksource == RCC_CLKPSOURCE_HSE))
        {
          /* In Case the CKPER Source is HSE */
          frequency = HSE_VALUE;
        }

        else
        {
          /* In Case the CKPER is disabled*/
          frequency = 0;
        }

        break;
      }

      case (RCC_SAI23CLKSOURCE_PIN): /* External clock is the clock source for SAI2/3 */
      {
        frequency = EXTERNAL_CLOCK_VALUE;
        break;
      }
      default :
      {
        frequency = 0;
        break;
      }
    }
  }
#endif /* SAI3 */

#if  defined(RCC_CDCCIP1R_SAI2ASEL)

  else if (PeriphClk == RCC_PERIPHCLK_SAI2A)
  {
    saiclocksource = __HAL_RCC_GET_SAI2A_SOURCE();

    switch (saiclocksource)
    {
      case RCC_SAI2ACLKSOURCE_PLL: /* PLL1 is the clock source for SAI2A */
      {
        if (HAL_IS_BIT_SET(RCC->CR, RCC_CR_PLL1RDY))
        {
          HAL_RCCEx_GetPLL1ClockFreq(&pll1_clocks);
          frequency = pll1_clocks.PLL1_Q_Frequency;
        }
        else
        {
          frequency = 0;
        }
        break;
      }
      case RCC_SAI2ACLKSOURCE_PLL2: /* PLLI2 is the clock source for SAI2A */
      {
        if (HAL_IS_BIT_SET(RCC->CR, RCC_CR_PLL2RDY))
        {
          HAL_RCCEx_GetPLL2ClockFreq(&pll2_clocks);
          frequency = pll2_clocks.PLL2_P_Frequency;
        }
        else
        {
          frequency = 0;
        }
        break;
      }

      case RCC_SAI2ACLKSOURCE_PLL3: /* PLLI3 is the clock source for SAI2A  */
      {
        if (HAL_IS_BIT_SET(RCC->CR, RCC_CR_PLL3RDY))
        {
          HAL_RCCEx_GetPLL3ClockFreq(&pll3_clocks);
          frequency = pll3_clocks.PLL3_P_Frequency;
        }
        else
        {
          frequency = 0;
        }
        break;
      }

      case RCC_SAI2ACLKSOURCE_CLKP: /* CKPER is the clock source for SAI2A  */
      {

        ckpclocksource = __HAL_RCC_GET_CLKP_SOURCE();

        if ((HAL_IS_BIT_SET(RCC->CR, RCC_CR_HSIRDY)) && (ckpclocksource == RCC_CLKPSOURCE_HSI))
        {
          /* In Case the CKPER Source is HSI */
          frequency = (HSI_VALUE >> (__HAL_RCC_GET_HSI_DIVIDER() >> 3));
        }

        else if ((HAL_IS_BIT_SET(RCC->CR, RCC_CR_CSIRDY)) && (ckpclocksource == RCC_CLKPSOURCE_CSI))
        {
          /* In Case the CKPER Source is CSI */
          frequency = CSI_VALUE;
        }

        else if ((HAL_IS_BIT_SET(RCC->CR, RCC_CR_HSERDY)) && (ckpclocksource == RCC_CLKPSOURCE_HSE))
        {
          /* In Case the CKPER Source is HSE */
          frequency = HSE_VALUE;
        }

        else
        {
          /* In Case the CKPER is disabled*/
          frequency = 0;
        }

        break;
      }

      case (RCC_SAI2ACLKSOURCE_PIN): /* External clock is the clock source for SAI2A */
      {
        frequency = EXTERNAL_CLOCK_VALUE;
        break;
      }

      default :
      {
        frequency = 0;
        break;
      }
    }

  }
#endif

#if  defined(RCC_CDCCIP1R_SAI2BSEL_0)
  else if (PeriphClk == RCC_PERIPHCLK_SAI2B)
  {

    saiclocksource = __HAL_RCC_GET_SAI2B_SOURCE();

    switch (saiclocksource)
    {
      case RCC_SAI2BCLKSOURCE_PLL: /* PLL1 is the clock source for SAI2B */
      {
        if (HAL_IS_BIT_SET(RCC->CR, RCC_CR_PLL1RDY))
        {
          HAL_RCCEx_GetPLL1ClockFreq(&pll1_clocks);
          frequency = pll1_clocks.PLL1_Q_Frequency;
        }
        else
        {
          frequency = 0;
        }
        break;
      }
      case RCC_SAI2BCLKSOURCE_PLL2: /* PLLI2 is the clock source for SAI2B */
      {
        if (HAL_IS_BIT_SET(RCC->CR, RCC_CR_PLL2RDY))
        {
          HAL_RCCEx_GetPLL2ClockFreq(&pll2_clocks);
          frequency = pll2_clocks.PLL2_P_Frequency;
        }
        else
        {
          frequency = 0;
        }
        break;
      }

      case RCC_SAI2BCLKSOURCE_PLL3: /* PLLI3 is the clock source for SAI2B */
      {
        if (HAL_IS_BIT_SET(RCC->CR, RCC_CR_PLL3RDY))
        {
          HAL_RCCEx_GetPLL3ClockFreq(&pll3_clocks);
          frequency = pll3_clocks.PLL3_P_Frequency;
        }
        else
        {
          frequency = 0;
        }
        break;
      }

      case RCC_SAI2BCLKSOURCE_CLKP: /* CKPER is the clock source for SAI2B*/
      {

        ckpclocksource = __HAL_RCC_GET_CLKP_SOURCE();

        if ((HAL_IS_BIT_SET(RCC->CR, RCC_CR_HSIRDY)) && (ckpclocksource == RCC_CLKPSOURCE_HSI))
        {
          /* In Case the CKPER Source is HSI */
          frequency = (HSI_VALUE >> (__HAL_RCC_GET_HSI_DIVIDER() >> 3));
        }

        else if ((HAL_IS_BIT_SET(RCC->CR, RCC_CR_CSIRDY)) && (ckpclocksource == RCC_CLKPSOURCE_CSI))
        {
          /* In Case the CKPER Source is CSI */
          frequency = CSI_VALUE;
        }

        else if ((HAL_IS_BIT_SET(RCC->CR, RCC_CR_HSERDY)) && (ckpclocksource == RCC_CLKPSOURCE_HSE))
        {
          /* In Case the CKPER Source is HSE */
          frequency = HSE_VALUE;
        }

        else
        {
          /* In Case the CKPER is disabled*/
          frequency = 0;
        }
        break;
      }

      case (RCC_SAI2BCLKSOURCE_PIN): /* External clock is the clock source for SAI2B */
      {
        frequency = EXTERNAL_CLOCK_VALUE;
        break;
      }

      default :
      {
        frequency = 0;
        break;
      }
    }
  }
#endif

#if defined(SAI4)
  else if (PeriphClk == RCC_PERIPHCLK_SAI4A)
  {

    saiclocksource = __HAL_RCC_GET_SAI4A_SOURCE();

    switch (saiclocksource)
    {
      case RCC_SAI4ACLKSOURCE_PLL: /* PLL1 is the clock source for SAI4A */
      {
        if (HAL_IS_BIT_SET(RCC->CR, RCC_CR_PLL1RDY))
        {
          HAL_RCCEx_GetPLL1ClockFreq(&pll1_clocks);
          frequency = pll1_clocks.PLL1_Q_Frequency;
        }
        else
        {
          frequency = 0;
        }
        break;
      }
      case RCC_SAI4ACLKSOURCE_PLL2: /* PLLI2 is the clock source for SAI4A */
      {
        if (HAL_IS_BIT_SET(RCC->CR, RCC_CR_PLL2RDY))
        {
          HAL_RCCEx_GetPLL2ClockFreq(&pll2_clocks);
          frequency = pll2_clocks.PLL2_P_Frequency;
        }
        else
        {
          frequency = 0;
        }
        break;
      }

      case RCC_SAI4ACLKSOURCE_PLL3: /* PLLI3 is the clock source for SAI4A */
      {
        if (HAL_IS_BIT_SET(RCC->CR, RCC_CR_PLL3RDY))
        {
          HAL_RCCEx_GetPLL3ClockFreq(&pll3_clocks);
          frequency = pll3_clocks.PLL3_P_Frequency;
        }
        else
        {
          frequency = 0;
        }
        break;
      }

      case RCC_SAI4ACLKSOURCE_CLKP: /* CKPER is the clock source for SAI4A*/
      {

        ckpclocksource = __HAL_RCC_GET_CLKP_SOURCE();

        if ((HAL_IS_BIT_SET(RCC->CR, RCC_CR_HSIRDY)) && (ckpclocksource == RCC_CLKPSOURCE_HSI))
        {
          /* In Case the CKPER Source is HSI */
          frequency = (HSI_VALUE >> (__HAL_RCC_GET_HSI_DIVIDER() >> 3));
        }

        else if ((HAL_IS_BIT_SET(RCC->CR, RCC_CR_CSIRDY)) && (ckpclocksource == RCC_CLKPSOURCE_CSI))
        {
          /* In Case the CKPER Source is CSI */
          frequency = CSI_VALUE;
        }

        else if ((HAL_IS_BIT_SET(RCC->CR, RCC_CR_HSERDY)) && (ckpclocksource == RCC_CLKPSOURCE_HSE))
        {
          /* In Case the CKPER Source is HSE */
          frequency = HSE_VALUE;
        }

        else
        {
          /* In Case the CKPER is disabled*/
          frequency = 0;
        }

        break;
      }

      case RCC_SAI4ACLKSOURCE_PIN: /* External clock is the clock source for SAI4A */
      {
        frequency = EXTERNAL_CLOCK_VALUE;
        break;
      }

      default :
      {
        frequency = 0;
        break;
      }
    }
  }

  else if (PeriphClk == RCC_PERIPHCLK_SAI4B)
  {

    saiclocksource = __HAL_RCC_GET_SAI4B_SOURCE();

    switch (saiclocksource)
    {
      case RCC_SAI4BCLKSOURCE_PLL: /* PLL1 is the clock source for SAI4B */
      {
        if (HAL_IS_BIT_SET(RCC->CR, RCC_CR_PLL1RDY))
        {
          HAL_RCCEx_GetPLL1ClockFreq(&pll1_clocks);
          frequency = pll1_clocks.PLL1_Q_Frequency;
        }
        else
        {
          frequency = 0;
        }
        break;
      }
      case RCC_SAI4BCLKSOURCE_PLL2: /* PLLI2 is the clock source for SAI4B */
      {
        if (HAL_IS_BIT_SET(RCC->CR, RCC_CR_PLL2RDY))
        {
          HAL_RCCEx_GetPLL2ClockFreq(&pll2_clocks);
          frequency = pll2_clocks.PLL2_P_Frequency;
        }
        else
        {
          frequency = 0;
        }
        break;
      }

      case RCC_SAI4BCLKSOURCE_PLL3: /* PLLI3 is the clock source for SAI4B */
      {
        if (HAL_IS_BIT_SET(RCC->CR, RCC_CR_PLL3RDY))
        {
          HAL_RCCEx_GetPLL3ClockFreq(&pll3_clocks);
          frequency = pll3_clocks.PLL3_P_Frequency;
        }
        else
        {
          frequency = 0;
        }
        break;
      }

      case RCC_SAI4BCLKSOURCE_CLKP: /* CKPER is the clock source for SAI4B*/
      {

        ckpclocksource = __HAL_RCC_GET_CLKP_SOURCE();

        if ((HAL_IS_BIT_SET(RCC->CR, RCC_CR_HSIRDY)) && (ckpclocksource == RCC_CLKPSOURCE_HSI))
        {
          /* In Case the CKPER Source is HSI */
          frequency = (HSI_VALUE >> (__HAL_RCC_GET_HSI_DIVIDER() >> 3));
        }

        else if ((HAL_IS_BIT_SET(RCC->CR, RCC_CR_CSIRDY)) && (ckpclocksource == RCC_CLKPSOURCE_CSI))
        {
          /* In Case the CKPER Source is CSI */
          frequency = CSI_VALUE;
        }

        else if ((HAL_IS_BIT_SET(RCC->CR, RCC_CR_HSERDY)) && (ckpclocksource == RCC_CLKPSOURCE_HSE))
        {
          /* In Case the CKPER Source is HSE */
          frequency = HSE_VALUE;
        }

        else
        {
          /* In Case the CKPER is disabled*/
          frequency = 0;
        }

        break;
      }

      case RCC_SAI4BCLKSOURCE_PIN: /* External clock is the clock source for SAI4B */
      {
        frequency = EXTERNAL_CLOCK_VALUE;
        break;
      }

      default :
      {
        frequency = 0;
        break;
      }
    }
  }
#endif /*SAI4*/
  else if (PeriphClk == RCC_PERIPHCLK_SPI123)
  {
    /* Get SPI1/2/3 clock source */
    srcclk = __HAL_RCC_GET_SPI123_SOURCE();

    switch (srcclk)
    {
      case RCC_SPI123CLKSOURCE_PLL: /* PLL1 is the clock source for SPI123 */
      {
        if (HAL_IS_BIT_SET(RCC->CR, RCC_CR_PLL1RDY))
        {
          HAL_RCCEx_GetPLL1ClockFreq(&pll1_clocks);
          frequency = pll1_clocks.PLL1_Q_Frequency;
        }
        else
        {
          frequency = 0;
        }
        break;
      }
      case RCC_SPI123CLKSOURCE_PLL2: /* PLL2 is the clock source for SPI123 */
      {
        if (HAL_IS_BIT_SET(RCC->CR, RCC_CR_PLL2RDY))
        {
          HAL_RCCEx_GetPLL2ClockFreq(&pll2_clocks);
          frequency = pll2_clocks.PLL2_P_Frequency;
        }
        else
        {
          frequency = 0;
        }
        break;
      }

      case RCC_SPI123CLKSOURCE_PLL3: /* PLL3 is the clock source for SPI123 */
      {
        if (HAL_IS_BIT_SET(RCC->CR, RCC_CR_PLL3RDY))
        {
          HAL_RCCEx_GetPLL3ClockFreq(&pll3_clocks);
          frequency = pll3_clocks.PLL3_P_Frequency;
        }
        else
        {
          frequency = 0;
        }
        break;
      }

      case RCC_SPI123CLKSOURCE_CLKP: /* CKPER is the clock source for SPI123 */
      {

        ckpclocksource = __HAL_RCC_GET_CLKP_SOURCE();

        if ((HAL_IS_BIT_SET(RCC->CR, RCC_CR_HSIRDY)) && (ckpclocksource == RCC_CLKPSOURCE_HSI))
        {
          /* In Case the CKPER Source is HSI */
          frequency = (HSI_VALUE >> (__HAL_RCC_GET_HSI_DIVIDER() >> 3));
        }

        else if ((HAL_IS_BIT_SET(RCC->CR, RCC_CR_CSIRDY)) && (ckpclocksource == RCC_CLKPSOURCE_CSI))
        {
          /* In Case the CKPER Source is CSI */
          frequency = CSI_VALUE;
        }

        else if ((HAL_IS_BIT_SET(RCC->CR, RCC_CR_HSERDY)) && (ckpclocksource == RCC_CLKPSOURCE_HSE))
        {
          /* In Case the CKPER Source is HSE */
          frequency = HSE_VALUE;
        }

        else
        {
          /* In Case the CKPER is disabled*/
          frequency = 0;
        }

        break;
      }

      case (RCC_SPI123CLKSOURCE_PIN): /* External clock is the clock source for I2S */
      {
        frequency = EXTERNAL_CLOCK_VALUE;
        break;
      }
      default :
      {
        frequency = 0;
        break;
      }
    }
  }
  else if (PeriphClk == RCC_PERIPHCLK_SPI45)
  {
    /* Get SPI45 clock source */
    srcclk = __HAL_RCC_GET_SPI45_SOURCE();
    switch (srcclk)
    {
      case RCC_SPI45CLKSOURCE_PCLK2: /* CD/D2 PCLK2 is the clock source for SPI4/5 */
      {
        frequency = HAL_RCC_GetPCLK1Freq();
        break;
      }
      case RCC_SPI45CLKSOURCE_PLL2: /* PLL2 is the clock source for SPI45 */
      {
        if (HAL_IS_BIT_SET(RCC->CR, RCC_CR_PLL2RDY))
        {
          HAL_RCCEx_GetPLL2ClockFreq(&pll2_clocks);
          frequency = pll2_clocks.PLL2_Q_Frequency;
        }
        else
        {
          frequency = 0;
        }
        break;
      }
      case RCC_SPI45CLKSOURCE_PLL3: /* PLL3 is the clock source for SPI45 */
      {
        if (HAL_IS_BIT_SET(RCC->CR, RCC_CR_PLL3RDY))
        {
          HAL_RCCEx_GetPLL3ClockFreq(&pll3_clocks);
          frequency = pll3_clocks.PLL3_Q_Frequency;
        }
        else
        {
          frequency = 0;
        }
        break;
      }
      case RCC_SPI45CLKSOURCE_HSI: /* HSI is the clock source for SPI45 */
      {
        if (HAL_IS_BIT_SET(RCC->CR, RCC_CR_HSIRDY))
        {
          frequency = (HSI_VALUE >> (__HAL_RCC_GET_HSI_DIVIDER() >> 3));
        }
        else
        {
          frequency = 0;
        }
        break;
      }
      case RCC_SPI45CLKSOURCE_CSI: /* CSI is the clock source for SPI45 */
      {
        if (HAL_IS_BIT_SET(RCC->CR, RCC_CR_CSIRDY))
        {
          frequency = CSI_VALUE;
        }
        else
        {
          frequency = 0;
        }
        break;
      }
      case RCC_SPI45CLKSOURCE_HSE: /* HSE is the clock source for SPI45 */
      {
        if (HAL_IS_BIT_SET(RCC->CR, RCC_CR_HSERDY))
        {
          frequency = HSE_VALUE;
        }
        else
        {
          frequency = 0;
        }
        break;
      }
      default :
      {
        frequency = 0;
        break;
      }
    }
  }
  else if (PeriphClk == RCC_PERIPHCLK_ADC)
  {
    /* Get ADC clock source */
    srcclk = __HAL_RCC_GET_ADC_SOURCE();

    switch (srcclk)
    {
      case RCC_ADCCLKSOURCE_PLL2:
      {
        if (HAL_IS_BIT_SET(RCC->CR, RCC_CR_PLL2RDY))
        {
          HAL_RCCEx_GetPLL2ClockFreq(&pll2_clocks);
          frequency = pll2_clocks.PLL2_P_Frequency;
        }
        else
        {
          frequency = 0;
        }
        break;
      }
      case RCC_ADCCLKSOURCE_PLL3:
      {
        if (HAL_IS_BIT_SET(RCC->CR, RCC_CR_PLL3RDY))
        {
          HAL_RCCEx_GetPLL3ClockFreq(&pll3_clocks);
          frequency = pll3_clocks.PLL3_R_Frequency;
        }
        else
        {
          frequency = 0;
        }
        break;
      }

      case RCC_ADCCLKSOURCE_CLKP:
      {

        ckpclocksource = __HAL_RCC_GET_CLKP_SOURCE();

        if ((HAL_IS_BIT_SET(RCC->CR, RCC_CR_HSIRDY)) && (ckpclocksource == RCC_CLKPSOURCE_HSI))
        {
          /* In Case the CKPER Source is HSI */
          frequency = (HSI_VALUE >> (__HAL_RCC_GET_HSI_DIVIDER() >> 3));
        }

        else if ((HAL_IS_BIT_SET(RCC->CR, RCC_CR_CSIRDY)) && (ckpclocksource == RCC_CLKPSOURCE_CSI))
        {
          /* In Case the CKPER Source is CSI */
          frequency = CSI_VALUE;
        }

        else if ((HAL_IS_BIT_SET(RCC->CR, RCC_CR_HSERDY)) && (ckpclocksource == RCC_CLKPSOURCE_HSE))
        {
          /* In Case the CKPER Source is HSE */
          frequency = HSE_VALUE;
        }

        else
        {
          /* In Case the CKPER is disabled*/
          frequency = 0;
        }

        break;
      }

      default :
      {
        frequency = 0;
        break;
      }
    }
  }
  else if (PeriphClk == RCC_PERIPHCLK_SDMMC)
  {
    /* Get SDMMC clock source */
    srcclk = __HAL_RCC_GET_SDMMC_SOURCE();

    switch (srcclk)
    {
      case RCC_SDMMCCLKSOURCE_PLL: /* PLL1 is the clock source for SDMMC */
      {
        if (HAL_IS_BIT_SET(RCC->CR, RCC_CR_PLL1RDY))
        {
          HAL_RCCEx_GetPLL1ClockFreq(&pll1_clocks);
          frequency = pll1_clocks.PLL1_Q_Frequency;
        }
        else
        {
          frequency = 0;
        }
        break;
      }
      case RCC_SDMMCCLKSOURCE_PLL2: /* PLL2 is the clock source for SDMMC */
      {
        if (HAL_IS_BIT_SET(RCC->CR, RCC_CR_PLL2RDY))
        {
          HAL_RCCEx_GetPLL2ClockFreq(&pll2_clocks);
          frequency = pll2_clocks.PLL2_R_Frequency;
        }
        else
        {
          frequency = 0;
        }
        break;
      }

      default :
      {
        frequency = 0;
        break;
      }
    }
  }
  else if (PeriphClk == RCC_PERIPHCLK_SPI6)
  {
    /* Get SPI6 clock source */
    srcclk = __HAL_RCC_GET_SPI6_SOURCE();

    switch (srcclk)
    {
      case RCC_SPI6CLKSOURCE_D3PCLK1: /* D3PCLK1 (PCLK4) is the clock source for SPI6 */
      {
        frequency = HAL_RCCEx_GetD3PCLK1Freq();
        break;
      }
      case RCC_SPI6CLKSOURCE_PLL2: /* PLL2 is the clock source for SPI6 */
      {
        if (HAL_IS_BIT_SET(RCC->CR, RCC_CR_PLL2RDY))
        {
          HAL_RCCEx_GetPLL2ClockFreq(&pll2_clocks);
          frequency = pll2_clocks.PLL2_Q_Frequency;
        }
        else
        {
          frequency = 0;
        }
        break;
      }
      case RCC_SPI6CLKSOURCE_PLL3: /* PLL3 is the clock source for SPI6 */
      {
        if (HAL_IS_BIT_SET(RCC->CR, RCC_CR_PLL3RDY))
        {
          HAL_RCCEx_GetPLL3ClockFreq(&pll3_clocks);
          frequency = pll3_clocks.PLL3_Q_Frequency;
        }
        else
        {
          frequency = 0;
        }
        break;
      }
      case RCC_SPI6CLKSOURCE_HSI: /* HSI is the clock source for SPI6 */
      {
        if (HAL_IS_BIT_SET(RCC->CR, RCC_CR_HSIRDY))
        {
          frequency = (HSI_VALUE >> (__HAL_RCC_GET_HSI_DIVIDER() >> 3));
        }
        else
        {
          frequency = 0;
        }
        break;
      }
      case RCC_SPI6CLKSOURCE_CSI: /* CSI is the clock source for SPI6 */
      {
        if (HAL_IS_BIT_SET(RCC->CR, RCC_CR_CSIRDY))
        {
          frequency = CSI_VALUE;
        }
        else
        {
          frequency = 0;
        }
        break;
      }
      case RCC_SPI6CLKSOURCE_HSE: /* HSE is the clock source for SPI6 */
      {
        if (HAL_IS_BIT_SET(RCC->CR, RCC_CR_HSERDY))
        {
          frequency = HSE_VALUE;
        }
        else
        {
          frequency = 0;
        }
        break;
      }
#if defined(RCC_SPI6CLKSOURCE_PIN)
      case RCC_SPI6CLKSOURCE_PIN: /* External clock is the clock source for SPI6 */
      {
        frequency = EXTERNAL_CLOCK_VALUE;
        break;
      }
#endif /* RCC_SPI6CLKSOURCE_PIN */
      default :
      {
        frequency = 0;
        break;
      }
    }
  }
  else if (PeriphClk == RCC_PERIPHCLK_FDCAN)
  {
    /* Get FDCAN clock source */
    srcclk = __HAL_RCC_GET_FDCAN_SOURCE();

    switch (srcclk)
    {
      case RCC_FDCANCLKSOURCE_HSE: /* HSE is the clock source for FDCAN */
      {
        if (HAL_IS_BIT_SET(RCC->CR, RCC_CR_HSERDY))
        {
          frequency = HSE_VALUE;
        }
        else
        {
          frequency = 0;
        }
        break;
      }
      case RCC_FDCANCLKSOURCE_PLL: /* PLL is the clock source for FDCAN */
      {
        if (HAL_IS_BIT_SET(RCC->CR, RCC_CR_PLL1RDY))
        {
          HAL_RCCEx_GetPLL1ClockFreq(&pll1_clocks);
          frequency = pll1_clocks.PLL1_Q_Frequency;
        }
        else
        {
          frequency = 0;
        }
        break;
      }
      case RCC_FDCANCLKSOURCE_PLL2: /* PLL2 is the clock source for FDCAN */
      {
        if (HAL_IS_BIT_SET(RCC->CR, RCC_CR_PLL2RDY))
        {
          HAL_RCCEx_GetPLL2ClockFreq(&pll2_clocks);
          frequency = pll2_clocks.PLL2_Q_Frequency;
        }
        else
        {
          frequency = 0;
        }
        break;
      }
      default :
      {
        frequency = 0;
        break;
      }
    }
  }
  else
  {
    frequency = 0;
  }

  return frequency;
}

void HAL_RCCEx_GetPLL2ClockFreq(PLL2_ClocksTypeDef *PLL2_Clocks)
{
  uint32_t  pllsource, pll2m,  pll2fracen, hsivalue;
  float_t fracn2, pll2vco;

  /* PLL_VCO = (HSE_VALUE or HSI_VALUE or CSI_VALUE/ PLL2M) * PLL2N
     PLL2xCLK = PLL2_VCO / PLL2x
  */
  pllsource = (RCC->PLLCKSELR & RCC_PLLCKSELR_PLLSRC);
  pll2m = ((RCC->PLLCKSELR & RCC_PLLCKSELR_DIVM2) >> 12);
  pll2fracen = (RCC->PLLCFGR & RCC_PLLCFGR_PLL2FRACEN) >> RCC_PLLCFGR_PLL2FRACEN_Pos;
  fracn2 = (float_t)(uint32_t)(pll2fracen * ((RCC->PLL2FRACR & RCC_PLL2FRACR_FRACN2) >> 3));

  if (pll2m != 0U)
  {
    switch (pllsource)
    {

      case RCC_PLLSOURCE_HSI:  /* HSI used as PLL clock source */

        if (__HAL_RCC_GET_FLAG(RCC_FLAG_HSIDIV) != 0U)
        {
          hsivalue = (HSI_VALUE >> (__HAL_RCC_GET_HSI_DIVIDER() >> 3));
          pll2vco = ((float_t)hsivalue / (float_t)pll2m) * ((float_t)(uint32_t)(RCC->PLL2DIVR & RCC_PLL2DIVR_N2) + (fracn2 / (float_t)0x2000) + (float_t)1);
        }
        else
        {
          pll2vco = ((float_t)HSI_VALUE / (float_t)pll2m) * ((float_t)(uint32_t)(RCC->PLL2DIVR & RCC_PLL2DIVR_N2) + (fracn2 / (float_t)0x2000) + (float_t)1);
        }
        break;

      case RCC_PLLSOURCE_CSI:  /* CSI used as PLL clock source */
        pll2vco = ((float_t)CSI_VALUE / (float_t)pll2m) * ((float_t)(uint32_t)(RCC->PLL2DIVR & RCC_PLL2DIVR_N2) + (fracn2 / (float_t)0x2000) + (float_t)1);
        break;

      case RCC_PLLSOURCE_HSE:  /* HSE used as PLL clock source */
        pll2vco = ((float_t)HSE_VALUE / (float_t)pll2m) * ((float_t)(uint32_t)(RCC->PLL2DIVR & RCC_PLL2DIVR_N2) + (fracn2 / (float_t)0x2000) + (float_t)1);
        break;

      default:
        pll2vco = ((float_t)CSI_VALUE / (float_t)pll2m) * ((float_t)(uint32_t)(RCC->PLL2DIVR & RCC_PLL2DIVR_N2) + (fracn2 / (float_t)0x2000) + (float_t)1);
        break;
    }
    PLL2_Clocks->PLL2_P_Frequency = (uint32_t)(float_t)(pll2vco / ((float_t)(uint32_t)((RCC->PLL2DIVR & RCC_PLL2DIVR_P2) >> 9)  + (float_t)1)) ;
    PLL2_Clocks->PLL2_Q_Frequency = (uint32_t)(float_t)(pll2vco / ((float_t)(uint32_t)((RCC->PLL2DIVR & RCC_PLL2DIVR_Q2) >> 16) + (float_t)1)) ;
    PLL2_Clocks->PLL2_R_Frequency = (uint32_t)(float_t)(pll2vco / ((float_t)(uint32_t)((RCC->PLL2DIVR & RCC_PLL2DIVR_R2) >> 24) + (float_t)1)) ;
  }
  else
  {
    PLL2_Clocks->PLL2_P_Frequency = 0U;
    PLL2_Clocks->PLL2_Q_Frequency = 0U;
    PLL2_Clocks->PLL2_R_Frequency = 0U;
  }
}