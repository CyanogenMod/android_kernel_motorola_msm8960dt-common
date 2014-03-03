/* Copyright (c) 2012, Motorola Mobility, Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <asm/mach-types.h>
#include <linux/dma-mapping.h>
#include <linux/mfd/pm8xxx/pm8921-charger.h>
#include <linux/persistent_ram.h>
#include <linux/platform_device.h>
#include <linux/w1-gpio.h>
#include <linux/platform_data/ram_console.h>
#include <linux/power/bq5101xb_charger.h>
#include <linux/leds-pwm-gpio.h>

#include <mach/board.h>
#include <mach/dma.h>
#include <mach/irqs-8960.h>
#include <mach/msm_iomap-8960-mmi.h>

#include "board-8960.h"

static struct resource resources_uart_gsbi2[] = {
	{
		.start	= MSM8960_GSBI2_UARTDM_IRQ,
		.end	= MSM8960_GSBI2_UARTDM_IRQ,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= MSM_UART2DM_PHYS,
		.end	= MSM_UART2DM_PHYS + PAGE_SIZE - 1,
		.name	= "uartdm_resource",
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= MSM_GSBI2_PHYS,
		.end	= MSM_GSBI2_PHYS + PAGE_SIZE - 1,
		.name	= "gsbi_resource",
		.flags	= IORESOURCE_MEM,
	},
};

struct platform_device mmi_msm8960_device_uart_gsbi2 = {
	.name	= "msm_serial_hsl",
	.id	= 2,
	.num_resources	= ARRAY_SIZE(resources_uart_gsbi2),
	.resource	= resources_uart_gsbi2,
};

static struct resource resources_qup_i2c_gsbi4[] = {
	{
		.name	= "gsbi_qup_i2c_addr",
		.start	= MSM_GSBI4_PHYS,
		.end	= MSM_GSBI4_PHYS + 4 - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "qup_phys_addr",
		.start	= MSM_GSBI4_QUP_PHYS,
		.end	= MSM_GSBI4_QUP_PHYS + MSM_QUP_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "qup_err_intr",
		.start	= GSBI4_QUP_IRQ,
		.end	= GSBI4_QUP_IRQ,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.name	= "i2c_sda",
		.start	= MSM_GSBI4_I2C_SDA,
		.end	= MSM_GSBI4_I2C_SDA,
		.flags	= IORESOURCE_IO,
	},
	{
		.name	= "i2c_clk",
		.start	= MSM_GSBI4_I2C_CLK,
		.end	= MSM_GSBI4_I2C_CLK,
		.flags	= IORESOURCE_IO,
	},
};

struct platform_device mmi_msm8960_device_qup_i2c_gsbi4 = {
	.name		= "qup_i2c",
	.id		= 4,
	.num_resources	= ARRAY_SIZE(resources_qup_i2c_gsbi4),
	.resource	= resources_qup_i2c_gsbi4,
};

static struct resource resources_uart_gsbi5[] = {
	{
		.start	= GSBI5_UARTDM_IRQ,
		.end	= GSBI5_UARTDM_IRQ,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= MSM_UART5DM_PHYS,
		.end	= MSM_UART5DM_PHYS + PAGE_SIZE - 1,
		.name	= "uartdm_resource",
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= MSM_GSBI5_PHYS,
		.end	= MSM_GSBI5_PHYS + PAGE_SIZE - 1,
		.name	= "gsbi_resource",
		.flags	= IORESOURCE_MEM,
	},
};

struct platform_device mmi_msm8960_device_uart_gsbi5 = {
	.name	= "msm_serial_hsl",
	.id	= 5,
	.num_resources	= ARRAY_SIZE(resources_uart_gsbi5),
	.resource	= resources_uart_gsbi5,
};

static struct resource resources_qup_i2c_gsbi8[] = {
	{
		.name	= "gsbi_qup_i2c_addr",
		.start	= MSM_GSBI8_PHYS,
		.end	= MSM_GSBI8_PHYS + 4 - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "qup_phys_addr",
		.start	= MSM_GSBI8_QUP_PHYS,
		.end	= MSM_GSBI8_QUP_PHYS + MSM_QUP_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "qup_err_intr",
		.start	= GSBI8_QUP_IRQ,
		.end	= GSBI8_QUP_IRQ,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.name	= "i2c_sda",
		.start	= 36,
		.end	= 36,
		.flags	= IORESOURCE_IO,
	},
	{
		.name	= "i2c_clk",
		.start	= 37,
		.end	= 37,
		.flags	= IORESOURCE_IO,
	},
};

struct platform_device mmi_msm8960_device_qup_i2c_gsbi8 = {
	.name		= "qup_i2c",
	.id		= 8,
	.num_resources	= ARRAY_SIZE(resources_qup_i2c_gsbi8),
	.resource	= resources_qup_i2c_gsbi8,
};

static struct resource resources_uart_gsbi8[] = {
	{
		.start	= GSBI8_UARTDM_IRQ,
		.end	= GSBI8_UARTDM_IRQ,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= MSM_UART8DM_PHYS,
		.end	= MSM_UART8DM_PHYS + PAGE_SIZE - 1,
		.name	= "uartdm_resource",
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= MSM_GSBI8_PHYS,
		.end	= MSM_GSBI8_PHYS + PAGE_SIZE - 1,
		.name	= "gsbi_resource",
		.flags	= IORESOURCE_MEM,
	},
};

struct platform_device mmi_msm8960_device_uart_gsbi8 = {
	.name	= "msm_serial_hsl",
	.id	= 8,
	.num_resources	= ARRAY_SIZE(resources_uart_gsbi8),
	.resource	= resources_uart_gsbi8,
};

/* GSBI 5 used into UARTDM Mode */
static struct resource msm_uart_dm5_resources[] = {
	{
		.start	= MSM_UART5DM_PHYS,
		.end	= MSM_UART5DM_PHYS + PAGE_SIZE - 1,
		.name	= "uartdm_resource",
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= GSBI5_UARTDM_IRQ,
		.end	= GSBI5_UARTDM_IRQ,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= MSM_GSBI5_PHYS,
		.end	= MSM_GSBI5_PHYS + 4 - 1,
		.name	= "gsbi_resource",
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= DMOV_HSUART_GSBI5_TX_CRCI,
		.end	= DMOV_HSUART_GSBI5_RX_CRCI,
		.name	= "uartdm_crci",
		.flags	= IORESOURCE_DMA,
	},
};

static u64 msm_uart_dm5_dma_mask = DMA_BIT_MASK(32);
struct platform_device mmi_msm8960_device_uart_dm5 = {
	.name	= "msm_serial_hs",
	.id	= 0,
	.num_resources	= ARRAY_SIZE(msm_uart_dm5_resources),
	.resource	= msm_uart_dm5_resources,
	.dev	= {
		.dma_mask		= &msm_uart_dm5_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
};

static struct w1_gpio_platform_data mmi_w1_gpio_device_pdata = {
	.pin = -1,
	.is_open_drain = 0,
};

struct platform_device mmi_w1_gpio_device = {
	.name	= "w1-gpio",
	.dev	= {
		.platform_data = &mmi_w1_gpio_device_pdata,
	},
};

static char *bq5101xb_supplied_to[] = {
	"battery",
};

static struct bq5101xb_charger_platform_data bq5101xb_pdata = {
	.chrg_b_pin = -1,
	.ts_ctrl_term_b_pin = -1,
	.ts_ctrl_fault_pin = -1,
	.en1_pin = -1,
	.en2_pin = -1,
	.hot_temp = 45,
	.cold_temp = -20,
	.resume_soc = 99,
	.resume_vbatt = 0,
	.supply_list = bq5101xb_supplied_to,
	.num_supplies = ARRAY_SIZE(bq5101xb_supplied_to),
	.priority = BQ5101XB_WIRED,
	.check_powered = pm8921_is_dc_chg_plugged_in,
	.check_wired = pm8921_is_usb_chg_plugged_in,
};

struct platform_device mmi_bq5101xb_device = {
	.name	= BQ5101XB_DRV_NAME,
	.dev	= {
		.platform_data = &bq5101xb_pdata,
	},
};

static struct led_pwm_gpio pm8xxx_pwm_gpio_leds[] = {
	[0] = {
		.name			= "red",
		.default_trigger	= "none",
		.pwm_id = 0,
		.gpio = PM8921_GPIO_PM_TO_SYS(24),
		.active_low = 0,
		.retain_state_suspended = 1,
		.default_state = 0,
	},
	[1] = {
		.name			= "green",
		.default_trigger	= "none",
		.pwm_id = 1,
		.gpio = PM8921_GPIO_PM_TO_SYS(25),
		.active_low = 0,
		.retain_state_suspended = 1,
		.default_state = 0,
	},
	[2] = {
		.name			= "blue",
		.default_trigger	= "none",
		.pwm_id = 2,
		.gpio = PM8921_GPIO_PM_TO_SYS(26),
		.active_low = 0,
		.retain_state_suspended = 1,
		.default_state = 0,
	},
};

static struct led_pwm_gpio_platform_data pm8xxx_rgb_leds_pdata = {
	.num_leds = ARRAY_SIZE(pm8xxx_pwm_gpio_leds),
	.leds = pm8xxx_pwm_gpio_leds,
	.max_brightness = LED_FULL,
};

struct platform_device mmi_pm8xxx_rgb_leds_device = {
	.name	= "pm8xxx_rgb_leds",
	.id	= -1,
	.dev	= {
		.platform_data = &pm8xxx_rgb_leds_pdata,
	},
};

#ifdef CONFIG_ANDROID_RAM_CONSOLE

/* Put RAM Console adjacent to Device Tree at 0x88100000 */
#define MSM_RAM_CONSOLE_SIZE    128 * SZ_1K
#define MSM_RAM_CONSOLE_START   0x880E0000

struct platform_device mmi_ram_console_device = {
	.name = "ram_console",
	.id = -1,
};

static struct persistent_ram_descriptor ram_console_pram_desc[] = {
	{
		.name = "ram_console",
		.size = MSM_RAM_CONSOLE_SIZE,
	}
};

struct persistent_ram mmi_ram_console_pram = {
	.start = MSM_RAM_CONSOLE_START,
	.size = MSM_RAM_CONSOLE_SIZE,
	.num_descs = ARRAY_SIZE(ram_console_pram_desc),
	.descs = ram_console_pram_desc,
};

#endif

struct platform_device mmi_device_ext_5v_vreg __devinitdata = {
	.name	= GPIO_REGULATOR_DEV_NAME,
	.id	= PM8921_MPP_PM_TO_SYS(7),
	.dev	= {
		.platform_data = &msm_gpio_regulator_pdata[GPIO_VREG_ID_EXT_5V],
	},
};

struct platform_device mmi_alsa_to_h2w_hs_device = {
	.name	= "alsa-to-h2w",
};
