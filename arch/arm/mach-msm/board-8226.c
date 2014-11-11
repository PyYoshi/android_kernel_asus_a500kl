/* Copyright (c) 2012-2013, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/of_fdt.h>
#include <linux/of_irq.h>
#include <linux/memory.h>
#include <linux/regulator/qpnp-regulator.h>
#include <linux/msm_tsens.h>
#include <asm/mach/map.h>
#include <asm/hardware/gic.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <mach/board.h>
#include <mach/gpiomux.h>
#include <mach/msm_iomap.h>
#include <mach/restart.h>
#ifdef CONFIG_ION_MSM
#include <mach/ion.h>
#endif
#include <mach/msm_memtypes.h>
#include <mach/socinfo.h>
#include <mach/board.h>
#include <mach/clk-provider.h>
#include <mach/msm_smd.h>
#include <mach/rpm-smd.h>
#include <mach/rpm-regulator-smd.h>
#include <mach/msm_smem.h>
#include <linux/msm_thermal.h>
#include "board-dt.h"
#include "clock.h"
#include "platsmp.h"
#include "spm.h"
#include "pm.h"
#include "modem_notifier.h"

//Freeman +++

#define I2C_SURF 1
#define I2C_FFA  (1 << 1)
#define I2C_RUMI (1 << 2)
#define I2C_SIM  (1 << 3)
#define I2C_LIQUID (1 << 4)
#define I2C_MPQ_CDP	BIT(5)
#define I2C_MPQ_HRD	BIT(6)
#define I2C_MPQ_DTV	BIT(7)

struct i2c_registry {
	u8                     machs;
	int                    bus;
	struct i2c_board_info *info;
	int                    len;
};


#include <linux/input/synaptics_dsx.h>
#include <linux/i2c.h>
#define TM1940 (1) /* I2C */
#define TM2448 (2) /* I2C */
#define TM2074 (3) /* SPI */
#define SYNAPTICS_MODULE TM2448

/* Synaptics changes for Panda Board */
static int synaptics_gpio_setup(int gpio, bool configure, int dir, int state);

#if (SYNAPTICS_MODULE == TM2448)
#define SYNAPTICS_I2C_DEVICE
#define DSX_I2C_ADDR 0x20
#define DSX_ATTN_GPIO 17
#define DSX_ATTN_MUX_NAME "gpio_17"
#define DSX_POWER_GPIO -1
//#define DSX_POWER_MUX_NAME "mcspi1_cs3.gpio_140"
#define DSX_POWER_ON_STATE 1
#define DSX_POWER_DELAY_MS 160
#define DSX_RESET_GPIO 16
#define DSX_RESET_ON_STATE 0
#define DSX_RESET_DELAY_MS 100
#define DSX_RESET_ACTIVE_MS 20
#define DSX_IRQ_FLAGS IRQF_TRIGGER_FALLING
static unsigned char regulator_name[] = "";
static unsigned char cap_button_codes[] =
		{};

#elif (SYNAPTICS_MODULE == TM1940)
#define SYNAPTICS_I2C_DEVICE
#define DSX_I2C_ADDR 0x20
#define DSX_ATTN_GPIO 39
#define DSX_ATTN_MUX_NAME "gpmc_ad15.gpio_39"
#define DSX_POWER_GPIO -1
#define DSX_POWER_ON_STATE 1
#define DSX_POWER_DELAY_MS 160
#define DSX_RESET_GPIO -1
#define DSX_RESET_ON_STATE 0
#define DSX_RESET_DELAY_MS 100
#define DSX_RESET_ACTIVE_MS 20
#define DSX_IRQ_FLAGS IRQF_TRIGGER_FALLING
static unsigned char regulator_name[] = "";
static unsigned char cap_button_codes[] =
		{KEY_MENU, KEY_HOME, KEY_BACK, KEY_SEARCH};

#elif (SYNAPTICS_MODULE == TM2074)
#define SYNAPTICS_SPI_DEVICE
#define DSX_SPI_BUS 1
#define DSX_SPI_CS 0
#define DSX_SPI_CS_MUX_NAME "mcspi1_cs0"
#define DSX_SPI_MAX_SPEED (8 * 1000 * 1000)
#define DSX_SPI_BYTE_DELAY_US 20
#define DSX_SPI_BLOCK_DELAY_US 20
#define DSX_ATTN_GPIO 39
#define DSX_ATTN_MUX_NAME "gpmc_ad15.gpio_39"
#define DSX_POWER_GPIO 140
#define DSX_POWER_MUX_NAME "mcspi1_cs3.gpio_140"
#define DSX_POWER_ON_STATE 1
#define DSX_POWER_DELAY_MS 160
#define DSX_RESET_GPIO -1
#define DSX_RESET_ON_STATE 0
#define DSX_RESET_DELAY_MS 100
#define DSX_RESET_ACTIVE_MS 20
#define DSX_IRQ_FLAGS IRQF_TRIGGER_FALLING
static unsigned char regulator_name[] = "";
static unsigned char cap_button_codes[] =
		{};
#endif

static struct synaptics_dsx_cap_button_map cap_button_map = {
	.nbuttons = ARRAY_SIZE(cap_button_codes),
	.map = cap_button_codes,
};

static struct synaptics_dsx_board_data dsx_board_data = {
	.irq_gpio = DSX_ATTN_GPIO,
	.irq_flags = DSX_IRQ_FLAGS,
	.power_gpio = DSX_POWER_GPIO,
	.power_on_state = DSX_POWER_ON_STATE,
	.power_delay_ms = DSX_POWER_DELAY_MS,
	.reset_gpio = DSX_RESET_GPIO,
	.reset_on_state = DSX_RESET_ON_STATE,
	.reset_delay_ms = DSX_RESET_DELAY_MS,
	.reset_active_ms = DSX_RESET_ACTIVE_MS,
 	.gpio_config = synaptics_gpio_setup,
 	.regulator_name = regulator_name,
 	.cap_button_map = &cap_button_map,
#ifdef SYNAPTICS_SPI_DEVICE
	.byte_delay_us = DSX_SPI_BYTE_DELAY_US,
	.block_delay_us = DSX_SPI_BLOCK_DELAY_US,
#endif
};

#ifdef SYNAPTICS_I2C_DEVICE
static struct i2c_board_info bus5_i2c_devices[] = {
	{
		I2C_BOARD_INFO(I2C_DRIVER_NAME, DSX_I2C_ADDR),
		.platform_data = &dsx_board_data,
	},
};
//static struct spi_board_info spi_devices[] = {};
#endif

#ifdef SYNAPTICS_SPI_DEVICE
static struct spi_board_info spi_devices[] = {
	{
		.modalias = SPI_DRIVER_NAME,
		.bus_num = DSX_SPI_BUS,
		.chip_select = DSX_SPI_CS,
		.mode = SPI_MODE_3,
		.max_speed_hz = DSX_SPI_MAX_SPEED,
		.platform_data = &dsx_board_data,
	},
};
static struct i2c_board_info bus4_i2c_devices[] = {};
#endif

static int synaptics_gpio_setup(int gpio, bool configure, int dir, int state)
{
	int retval = 0;
	unsigned char buf[16];

	if (configure) {
		snprintf(buf, PAGE_SIZE, "dsx_gpio_%u\n", gpio);

		retval = gpio_request(gpio, buf);
		if (retval) {
			pr_err("%s: Failed to get gpio %d (code: %d)",
					__func__, gpio, retval);
			return retval;
		}

		if (dir == 0)
			retval = gpio_direction_input(gpio);
		else
			retval = gpio_direction_output(gpio, state);
		if (retval) {
			pr_err("%s: Failed to set gpio %d direction",
					__func__, gpio);
			return retval;
		}
	} else {
		gpio_free(gpio);
	}

	return retval;
}

/*static void synaptics_gpio_init(void)
{
#ifdef DSX_ATTN_MUX_NAME
	omap_mux_init_signal(DSX_ATTN_MUX_NAME, OMAP_PIN_INPUT_PULLUP);
#endif
#ifdef DSX_POWER_MUX_NAME
	omap_mux_init_signal(DSX_POWER_MUX_NAME, OMAP_PIN_OUTPUT);
#endif
#ifdef DSX_RESET_MUX_NAME
	omap_mux_init_signal(DSX_RESET_MUX_NAME, OMAP_PIN_OUTPUT);
#endif
#ifdef SYNAPTICS_SPI_DEVICE
	omap_mux_init_signal(DSX_SPI_CS_MUX_NAME, OMAP_PIN_OUTPUT);
	omap_mux_init_signal("mcspi1_clk", OMAP_PIN_INPUT);
	omap_mux_init_signal("mcspi1_somi", OMAP_PIN_INPUT_PULLUP);
	omap_mux_init_signal("mcspi1_simo", OMAP_PIN_OUTPUT);
#endif
	return;
}*/
/* End of Synaptics changes for Panda Board */
//Freeman ---

static struct memtype_reserve msm8226_reserve_table[] __initdata = {
	[MEMTYPE_SMI] = {
	},
	[MEMTYPE_EBI0] = {
		.flags	=	MEMTYPE_FLAGS_1M_ALIGN,
	},
	[MEMTYPE_EBI1] = {
		.flags	=	MEMTYPE_FLAGS_1M_ALIGN,
	},
};

//++ASUS_BSP : add for miniporting
#include <linux/init.h>
#include <linux/ioport.h>
#include <mach/board.h>
#include <mach/gpio.h>
#include <mach/gpiomux.h>
extern int __init device_gpio_init(void);// asus gpio init
void __init device_gpiomux_init(void)
{
	//~ int rc;

	//~ rc = msm_gpiomux_init_dt();
	//~ if (rc) {
		//~ pr_err("%s failed %d\n", __func__, rc);
		//~ return;
	//~ }

	device_gpio_init();

}
//--ASUS_BSP : add for miniporting

static int msm8226_paddr_to_memtype(unsigned int paddr)
{
	return MEMTYPE_EBI1;
}

static struct of_dev_auxdata msm8226_auxdata_lookup[] __initdata = {
	OF_DEV_AUXDATA("qcom,msm-sdcc", 0xF9824000, \
			"msm_sdcc.1", NULL),
	OF_DEV_AUXDATA("qcom,msm-sdcc", 0xF98A4000, \
			"msm_sdcc.2", NULL),
	OF_DEV_AUXDATA("qcom,msm-sdcc", 0xF9864000, \
			"msm_sdcc.3", NULL),
	OF_DEV_AUXDATA("qcom,sdhci-msm", 0xF9824900, \
			"msm_sdcc.1", NULL),
	OF_DEV_AUXDATA("qcom,sdhci-msm", 0xF98A4900, \
			"msm_sdcc.2", NULL),
	OF_DEV_AUXDATA("qcom,sdhci-msm", 0xF9864900, \
			"msm_sdcc.3", NULL),
	OF_DEV_AUXDATA("qcom,hsic-host", 0xF9A00000, "msm_hsic_host", NULL),

	{}
};

static struct reserve_info msm8226_reserve_info __initdata = {
	.memtype_reserve_table = msm8226_reserve_table,
	.paddr_to_memtype = msm8226_paddr_to_memtype,
};

static void __init msm8226_early_memory(void)
{
	reserve_info = &msm8226_reserve_info;
	of_scan_flat_dt(dt_scan_for_memory_hole, msm8226_reserve_table);
}
//ASUS battery frank_tao porting +++
#ifdef ASUS_A600KL_PROJECT
#ifdef CONFIG_BATTERY_ASUS
static struct platform_device a600kl_asus_bat_device = {
       .name = "asus_bat",
       .id = 0,
};     
#endif  //CONFIG_BATTERY_ASUS
#endif 
//ASUS battery frank_tao porting ---

#if defined(ASUS_A600KL_PROJECT)
static struct platform_device *msm_a600kl_devices[] = {
	//ASUS battery frank_tao porting +++
	#ifdef CONFIG_BATTERY_ASUS
	&a600kl_asus_bat_device,
	#endif
	//ASUS battery frank_tao porting ---

};
#endif

//ASUS battery frank_tao porting +++
#ifdef ASUS_A500KL_PROJECT
#ifdef CONFIG_BATTERY_ASUS
static struct platform_device a500kl_asus_bat_device = {
       .name = "asus_bat",
       .id = 0,
};     
#endif  //CONFIG_BATTERY_ASUS
#endif 
//ASUS battery frank_tao porting ---

#if defined(ASUS_A500KL_PROJECT)
static struct platform_device *msm_a500kl_devices[] = {
	//ASUS battery frank_tao porting +++
	#ifdef CONFIG_BATTERY_ASUS
	&a500kl_asus_bat_device,
	#endif
	//ASUS battery frank_tao porting ---

};
#endif
static void __init msm8226_reserve(void)
{
	reserve_info = &msm8226_reserve_info;
	of_scan_flat_dt(dt_scan_for_memory_reserve, msm8226_reserve_table);
	msm_reserve();
}

/*
 * Used to satisfy dependencies for devices that need to be
 * run early or in a particular order. Most likely your device doesn't fall
 * into this category, and thus the driver should not be added here. The
 * EPROBE_DEFER can satisfy most dependency problems.
 */
void __init msm8226_add_drivers(void)
{
	msm_smem_init();
	msm_init_modem_notifier_list();
	msm_smd_init();
	msm_rpm_driver_init();
	msm_spm_device_init();
	msm_pm_sleep_status_init();
	rpm_regulator_smd_driver_init();
	qpnp_regulator_init();
	if (of_board_is_rumi())
		msm_clock_init(&msm8226_rumi_clock_init_data);
	else
		msm_clock_init(&msm8226_clock_init_data);
	tsens_tm_init_driver();
	msm_thermal_device_init();
//ASUS battery frank_tao porting +++
	#if defined(ASUS_A600KL_PROJECT)
	platform_add_devices(msm_a600kl_devices, ARRAY_SIZE(msm_a600kl_devices));
	#elif defined(ASUS_A500KL_PROJECT)
	platform_add_devices(msm_a500kl_devices, ARRAY_SIZE(msm_a500kl_devices));
	#endif
//ASUS battery frank_tao porting ---
}

//Freeman +++
static struct i2c_registry i2c_devices[] __initdata = {
	{
		I2C_SURF | I2C_FFA | I2C_RUMI,
		5,
		bus5_i2c_devices,
		ARRAY_SIZE(bus5_i2c_devices),
	},
};
static void __init register_i2c_devices(void)
{
	//u8 mach_mask = 0;
	int i;
		for (i = 0; i < ARRAY_SIZE(i2c_devices); ++i) {
			if (i2c_devices[i].machs)
				i2c_register_board_info(i2c_devices[i].bus,i2c_devices[i].info,i2c_devices[i].len);
	}
}
//Freeman ---

void __init msm8226_init(void)
{
	struct of_dev_auxdata *adata = msm8226_auxdata_lookup;

	if (socinfo_init() < 0)
		pr_err("%s: socinfo_init() failed\n", __func__);

//+++ASUS_BSP : add for miniporting
	msm8226_init_gpiomux();
	device_gpiomux_init();
//---ASUS_BSP : add for miniporting
	//synaptics_gpio_init();

//Freeman +++
	register_i2c_devices();
//Freeman ---

	board_dt_populate(adata);
	msm8226_add_drivers();
}

static const char *msm8226_dt_match[] __initconst = {
	"qcom,msm8226",
	"qcom,msm8926",
	"qcom,apq8026",
	NULL
};

DT_MACHINE_START(MSM8226_DT, "Qualcomm MSM 8226 (Flattened Device Tree)")
	.map_io = msm_map_msm8226_io,
	.init_irq = msm_dt_init_irq,
	.init_machine = msm8226_init,
	.handle_irq = gic_handle_irq,
	.timer = &msm_dt_timer,
	.dt_compat = msm8226_dt_match,
	.reserve = msm8226_reserve,
	.init_very_early = msm8226_early_memory,
	.restart = msm_restart,
	.smp = &arm_smp_ops,
MACHINE_END
