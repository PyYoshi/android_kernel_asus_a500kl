#include <linux/kernel.h>
#include <mach/gpiomux.h>

/////////////////////////////////////////////////////////////////////
//includ add asus platform gpio table
#include "a600kl_evb_gpio_pinmux.h"
#include "a600kl_evb2_gpio_pinmux.h"
#include "a500kl_evb1_gpio_pinmux.h"
#include "a500kl_sr_gpio_pinmux.h"
#include "a500kl_sr2_gpio_pinmux.h"
#include "a500kl_er1_gpio_pinmux.h"
#include "a500kl_er2_gpio_pinmux.h"
#include "a500kl_er3_gpio_pinmux.h"
#include "a500kl_mp_gpio_pinmux.h"
#include "a500kl_mp2_gpio_pinmux.h"
/////////////////////////////////////////////////////////////////////
//define asus gpio var.A600KL_EVB

int __init device_gpio_init(void)
{
   	switch (oem_hardware_id())
   	{
	case A600KL_EVB:
		printk("[KERNEL] a600kl gpio config table = EVB\n");
		msm_gpiomux_install(a600kl_evb_msm8926_gpio_configs,
		ARRAY_SIZE(a600kl_evb_msm8926_gpio_configs));
        break;

	case A600KL_EVB2:
		printk("[KERNEL] a600kl gpio config table = EVB2\n");
		msm_gpiomux_install(a600kl_evb2_msm8926_gpio_configs,
		ARRAY_SIZE(a600kl_evb2_msm8926_gpio_configs));
        break;

	case A500KL_EVB1:
		printk("[KERNEL] a500kl gpio config table = EVB1\n");
		msm_gpiomux_install(a500kl_evb1_msm8926_gpio_configs,
		ARRAY_SIZE(a500kl_evb1_msm8926_gpio_configs));
        break;
	case A500KL_SR:
		printk("[KERNEL] a500kl gpio config table = SR\n");
		msm_gpiomux_install(a500kl_sr_msm8926_gpio_configs,
		ARRAY_SIZE(a500kl_sr_msm8926_gpio_configs));
        break;
	case A500KL_SR2:
		printk("[KERNEL] a500kl gpio config table = SR2\n");
		msm_gpiomux_install(a500kl_sr2_msm8926_gpio_configs,
		ARRAY_SIZE(a500kl_sr2_msm8926_gpio_configs));
        break;
	case A500KL_ER1:
		printk("[KERNEL] a500kl gpio config table = ER1\n");
		msm_gpiomux_install(a500kl_er1_msm8926_gpio_configs,
		ARRAY_SIZE(a500kl_er1_msm8926_gpio_configs));
        break;
	case A500KL_ER2:
		printk("[KERNEL] a500kl gpio config table = ER2\n");
		msm_gpiomux_install(a500kl_er2_msm8926_gpio_configs,
		ARRAY_SIZE(a500kl_er2_msm8926_gpio_configs));
        break;
	case A500KL_ER3:
		printk("[KERNEL] a500kl gpio config table = ER3\n");
		msm_gpiomux_install(a500kl_er3_msm8926_gpio_configs,
		ARRAY_SIZE(a500kl_er3_msm8926_gpio_configs));
        break;
	case A500KL_MP:
		printk("[KERNEL] a500kl gpio config table = MP\n");
		msm_gpiomux_install(a500kl_mp_msm8926_gpio_configs,
		ARRAY_SIZE(a500kl_mp_msm8926_gpio_configs));
        break;
	case A500KL_MP2:
		printk("[KERNEL] a500kl gpio config table = MP2\n");
		msm_gpiomux_install(a500kl_mp2_msm8926_gpio_configs,
		ARRAY_SIZE(a500kl_mp2_msm8926_gpio_configs));
        break;
	default:
		printk(KERN_ERR "[ERROR] There is NO valid hardware ID, use A500KL MP2 GPIO TABLE\n");
		msm_gpiomux_install(a500kl_mp2_msm8926_gpio_configs,
		ARRAY_SIZE(a500kl_mp2_msm8926_gpio_configs));
	break;
	}

   return 0;
}
