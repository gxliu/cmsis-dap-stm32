/* CMSIS-DAP-STM32 libre using libopencm3 library 
 * 
 * 
 * Copyright (C) 2009 Uwe Hermann <uwe@hermann-uwe.de>,
 * Copyright (C) 2011 Piotr Esden-Tempski <piotr@esden.net>
 * Copyright (C) 2015 Zhiyuan Wan <rgwan@rocaloid.org>
 * 
 * This library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/flash.h>

#include "hwconfig.h"
#include "common.h"

static void rcc_clock_setup_in_hse_8mhz_out_48mhz(void)
{
	/* Enable internal high-speed oscillator. */
	rcc_osc_on(HSI);
	rcc_wait_for_osc_ready(HSI);

	/* Select HSI as SYSCLK source. */
	rcc_set_sysclk_source(RCC_CFGR_SW_SYSCLKSEL_HSICLK);
	
	/* Turn on external clock */
	rcc_osc_on(HSE);
	rcc_wait_for_osc_ready(HSE);
	rcc_set_sysclk_source(RCC_CFGR_SW_SYSCLKSEL_HSECLK);
	/*
	 * Set prescalers for AHB, ADC, ABP1, ABP2.
	 * Do this before touching the PLL (TODO: why?).
	 */
	rcc_set_hpre(RCC_CFGR_HPRE_SYSCLK_NODIV);	/*Set.48MHz Max.72MHz */
	rcc_set_adcpre(RCC_CFGR_ADCPRE_PCLK2_DIV8);	/*Set. 6MHz Max.14MHz */
	rcc_set_ppre1(RCC_CFGR_PPRE1_HCLK_DIV2);	/*Set.24MHz Max.36MHz */
	rcc_set_ppre2(RCC_CFGR_PPRE2_HCLK_NODIV);	/*Set.48MHz Max.72MHz */
	rcc_set_usbpre(RCC_CFGR_USBPRE_PLL_CLK_NODIV);  /*Set.48MHz Max.48MHz */

	/*
	 * Sysclk runs with 48MHz -> 1 waitstates.
	 * 0WS from 0-24MHz
	 * 1WS from 24-48MHz
	 * 2WS from 48-72MHz
	 */
	flash_set_ws(FLASH_ACR_LATENCY_1WS);

	/*
	 * Set the PLL multiplication factor to 6
	 * 8MHz (external) * 6 (multiplier) = 48MHz
	 */
	rcc_set_pll_multiplication_factor(RCC_CFGR_PLLMUL_PLL_CLK_MUL6);

	/* Select HSE as PLL source. */
	rcc_set_pll_source(RCC_CFGR_PLLSRC_HSE_CLK);

	/* Enable PLL oscillator and wait for it to stabilize. */
	rcc_osc_on(PLL);
	rcc_wait_for_osc_ready(PLL);

	/* Select PLL as SYSCLK source. */
	rcc_set_sysclk_source(RCC_CFGR_SW_SYSCLKSEL_PLLCLK);

	/* Set the peripheral clock frequencies used */
	rcc_ahb_frequency = 48000000;
	rcc_apb1_frequency = 24000000;
	rcc_apb2_frequency = 48000000;
}

void clock_setup(void)
{
	rcc_clock_setup_in_hse_8mhz_out_48mhz();

	/* Enable GPIOB clock (for LED GPIOs). */
	rcc_periph_clock_enable(RCC_GPIOB);

	/* Enable clocks for GPIO port A (for GPIO_USART1_TX) and USART1. */
	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_AFIO);
	AFIO_MAPR |= AFIO_MAPR_SWJ_CFG_JTAG_OFF_SW_ON;
	rcc_periph_clock_enable(RCC_USART1);
}


void gpio_setup(void)
{
	led_Off();
	usb_Off();

	gpio_set_mode(LED_PORT, GPIO_MODE_OUTPUT_2_MHZ,
		      GPIO_CNF_OUTPUT_PUSHPULL, LED_BIT);
	
	gpio_set_mode(USB_PULLUP_PORT, GPIO_MODE_OUTPUT_2_MHZ,
		      GPIO_CNF_OUTPUT_PUSHPULL,USB_PULLUP_BIT);
}


#ifdef DEBUG
void hexdump(char *buffer, int len)
{
int     i;

    for(i = 0; i < len; i++){
        if(i != 0){
            if(i % 16 == 0){
                printf("\r\n");
            }else{
                printf(" ");
            }
        }
        printf("0x%02x", buffer[i] & 0xff);
    }
    if(i != 0)
        printf("\r\n");
}
#endif
