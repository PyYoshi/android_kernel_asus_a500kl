/* Copyright (c) 2009-2013, Linux Foundation. All rights reserved.
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

#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/uaccess.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>
#include <linux/dma-mapping.h>

#include <linux/usb.h>
#include <linux/usb/otg.h>
#include <linux/usb/ulpi.h>
#include <linux/usb/gadget.h>
#include <linux/usb/hcd.h>
#include <linux/usb/quirks.h>
#include <linux/usb/msm_hsusb.h>
#include <linux/usb/msm_hsusb_hw.h>
#include <linux/usb/msm_ext_chg.h>
#include <linux/regulator/consumer.h>
#include <linux/mfd/pm8xxx/pm8921-charger.h>
#include <linux/mfd/pm8xxx/misc.h>
#include <linux/mhl_8334.h>

#include <mach/scm.h>
#include <mach/clk.h>
#include <mach/mpm.h>
#include <mach/msm_xo.h>
#include <mach/msm_bus.h>
#include <mach/rpm-regulator.h>

//ASUS_BSP+++ "[USB][NA][Spec] Add ASUS charger mode support"
#ifdef CONFIG_CHARGER_ASUS
#include <linux/asus_chg.h>
#include <linux/asusdebug.h>
static struct delayed_work asus_chg_work;
static struct work_struct asus_usb_work;
static int g_charger_mode = ASUS_CHG_SRC_NONE;
enum msm_otg_usb_boot_state {
	MSM_OTG_USB_BOOT_INIT,
	MSM_OTG_USB_BOOT_IRQ,//check IRQ to make sure USB is ready
	MSM_OTG_USB_BOOT_DOWN,
};
static int g_usb_boot = MSM_OTG_USB_BOOT_INIT;
#endif
static bool msm_otg_bsv = 0;
//ASUS_BSP--- "[USB][NA][Spec] Add ASUS charger mode support"

//ASUS_BSP+++ Eric5_Ou "add usb otg mode switch support"
#include <linux/proc_fs.h>
static int g_host_mode = 0;
static void asus_otg_vbus_out_enable(bool enable, bool force);
static void asus_otg_mode_switch(enum usb_mode_type req_mode);
//ASUS_BSP--- Eric5_Ou "add usb otg mode switch support"

#define MSM_USB_BASE	(motg->regs)
#define DRIVER_NAME	"msm_otg"

#define ID_TIMER_FREQ		(jiffies + msecs_to_jiffies(500))
#define CHG_RECHECK_DELAY	(jiffies + msecs_to_jiffies(2000))
#define ULPI_IO_TIMEOUT_USEC	(10 * 1000)
#define USB_PHY_3P3_VOL_MIN	3050000 /* uV */
#define USB_PHY_3P3_VOL_MAX	3300000 /* uV */
#define USB_PHY_3P3_HPM_LOAD	50000	/* uA */
#define USB_PHY_3P3_LPM_LOAD	4000	/* uA */

#define USB_PHY_1P8_VOL_MIN	1800000 /* uV */
#define USB_PHY_1P8_VOL_MAX	1800000 /* uV */
#define USB_PHY_1P8_HPM_LOAD	50000	/* uA */
#define USB_PHY_1P8_LPM_LOAD	4000	/* uA */

#define USB_PHY_VDD_DIG_VOL_NONE	0 /*uV */
#define USB_PHY_VDD_DIG_VOL_MIN	1045000 /* uV */
#define USB_PHY_VDD_DIG_VOL_MAX	1320000 /* uV */

#define USB_SUSPEND_DELAY_TIME	(500 * HZ/1000) /* 500 msec */

//ASUS_BSP+++ Eric5_Ou "Add monitor to check otg suspend status in suspend mode"
#define MSM_OTG_SUSPEND_CHECK_TIMEOUT 10000L
//ASUS_BSP--- Eric5_Ou "Add monitor to check otg suspend status in suspend mode"

enum msm_otg_phy_reg_mode {
	USB_PHY_REG_OFF,
	USB_PHY_REG_ON,
	USB_PHY_REG_LPM_ON,
	USB_PHY_REG_LPM_OFF,
};

static char *override_phy_init;
module_param(override_phy_init, charp, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(override_phy_init,
	"Override HSUSB PHY Init Settings");

unsigned int lpm_disconnect_thresh = 1000;
module_param(lpm_disconnect_thresh , uint, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(lpm_disconnect_thresh,
	"Delay before entering LPM on USB disconnect");

static bool floated_charger_enable;
module_param(floated_charger_enable , bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(floated_charger_enable,
	"Whether to enable floated charger");

//ASUS_BSP+++ Eric5_Ou "register early suspend notification for none mode switch"
#if defined(CONFIG_HAS_EARLYSUSPEND)
#include <linux/earlysuspend.h>

#elif defined(CONFIG_FB)
#include <linux/notifier.h>
#include <linux/fb.h>

struct notifier_block fb_notif;
#endif

enum host_auto_sw {
	HOST_AUTO_NONE = 0,
	HOST_AUTO_HOST,
};
static int g_host_none_mode = 0;
static int g_keep_power_on = 0;
static int g_suspend_delay_work_run = 0;
const char *usb_device_list[] = {"/Removable/USBdisk1", "/Removable/USBdisk2", "/Removable/SD", "/sys/class/net/eth0"};
static struct workqueue_struct *early_suspend_delay_wq;
static struct delayed_work early_suspend_delay_work;
static struct work_struct late_resume_work;
static struct wake_lock early_suspend_wlock;
//ASUS_BSP--- Eric5_Ou "register early suspend notification for none mode switch"

//ASUS_BSP+++ Eric5_Ou "add mutex to protect suspend/resume function"
static struct mutex msm_otg_mutex;
//ASUS_BSP--- Eric5_Ou "add mutex to protect suspend/resume function"

//ASUS_BSP+++ Eric5_Ou "add otg 5v output debug file"
static int g_otg_5v_output = 0;
//ASUS_BSP--- Eric5_Ou "add otg 5v output debug file"

//ASUS_BSP+++ Eric5_Ou "add dynamic setting support for phy parameters"
//#define A20_PAD_PHY_PARA_B
//#define A20_PAD_PHY_PARA_C
//#define A20_PAD_PHY_PARA_D
static int g_phy_parameter_b = 0;
static int g_phy_parameter_c = 0x28;
static int g_phy_parameter_d = 0;
//ASUS_BSP--- Eric5_Ou "add dynamic setting support for phy parameters"

//ASUS_BSP+++ Eric5_Ou "Add PM usage count debug log and protection mechanism"
static int otg_pm_count = 0;
//ASUS_BSP--- Eric5_Ou "Add PM usage count debug log and protection mechanism"

static DECLARE_COMPLETION(pmic_vbus_init);
static struct msm_otg *the_msm_otg;
static bool debug_aca_enabled;
static bool debug_bus_voting_enabled;
static bool mhl_det_in_progress;

static struct regulator *hsusb_3p3;
static struct regulator *hsusb_1p8;
static struct regulator *hsusb_vdd;
static struct regulator *vbus_otg;
static struct regulator *mhl_usb_hs_switch;
static struct power_supply *psy;

static bool aca_id_turned_on;

//ASUS_BSP+++ Eric5_Ou "get power supply which registered by charger"
static bool legacy_power_supply = false;
//ASUS_BSP--- Eric5_Ou "get power supply which registered by charger"

static inline bool aca_enabled(void)
{
#ifdef CONFIG_USB_MSM_ACA
	return true;
#else
	return debug_aca_enabled;
#endif
}

static int vdd_val[VDD_TYPE_MAX][VDD_VAL_MAX] = {
		{  /* VDD_CX CORNER Voting */
			[VDD_NONE]	= RPM_VREG_CORNER_NONE,
			[VDD_MIN]	= RPM_VREG_CORNER_NOMINAL,
			[VDD_MAX]	= RPM_VREG_CORNER_HIGH,
		},
		{ /* VDD_CX Voltage Voting */
			[VDD_NONE]	= USB_PHY_VDD_DIG_VOL_NONE,
			[VDD_MIN]	= USB_PHY_VDD_DIG_VOL_MIN,
			[VDD_MAX]	= USB_PHY_VDD_DIG_VOL_MAX,
		},
};

//ASUS_BSP+++ Eric5_Ou "Add monitor to check otg suspend status in suspend mode"
static void asus_otg_suspend_check(struct work_struct *work)
{
	struct msm_otg *motg = the_msm_otg;
	struct usb_otg *otg = motg->phy.otg;

	dev_info(motg->phy.dev, "check otg suspend status (%d)\n", pm_runtime_suspended(otg->phy->dev));

	if (!pm_runtime_suspended(otg->phy->dev)) {
		wake_unlock(&motg->wlock);
	}
}

static DECLARE_DELAYED_WORK(asus_otg_suspend_check_work, asus_otg_suspend_check);
//ASUS_BSP--- Eric5_Ou "Add monitor to check otg suspend status in suspend mode"

//ASUS_BSP+++ Eric5_Ou "register early suspend notification for none mode switch"
static bool asus_otg_keep_power_on_check(void)
{
	struct msm_otg *motg = the_msm_otg;
	struct usb_phy *phy = &motg->phy;
	struct file *flp = NULL;
	mm_segment_t oldfs;
	int index = 0, num = 0, ret = 0;

	oldfs = get_fs();
	set_fs(get_ds());

	num = sizeof(usb_device_list)/sizeof(usb_device_list[0]);

	for(index = 0; index < num; index++) {
		flp = filp_open(usb_device_list[index], O_RDONLY, S_IRWXU);
		if(IS_ERR(flp))
			continue;
		else {
			ret = 1;
			filp_close(flp, NULL);
			dev_info(phy->dev, "%s exist\n", usb_device_list[index]);
			break;
		}
	}

	set_fs(oldfs);

	return ret;
}

static void asus_otg_host_auto_switch(enum host_auto_sw req_mode)
{
	struct msm_otg *motg = the_msm_otg;

	switch (req_mode) {
	case HOST_AUTO_NONE:
		printk("[usb_otg] switch to auto none mode\r\n");
		set_bit(ID, &motg->inputs);
		clear_bit(B_SESS_VLD, &motg->inputs);
		g_host_none_mode = 1;
		break;
	case HOST_AUTO_HOST:
		printk("[usb_otg] switch to auto host mode\r\n");
		clear_bit(ID, &motg->inputs);
		g_host_none_mode = 0;
		break;
	default:
		printk("[usb_otg] unknown auto mode!!! (%d)\r\n", req_mode);
		goto out;
	}

	//ASUS_BSP+++ Eric5_Ou "Add pm_suspended judgement to avoid system crash"
	if (atomic_read(&motg->pm_suspended))
		motg->sm_work_pending = true;
	else
		queue_work(system_nrt_wq, &motg->sm_work);
	//ASUS_BSP--- Eric5_Ou "Add pm_suspended judgement to avoid system crash"
out:
	return;
}

void asus_otg_host_power_off(void)
{
	struct msm_otg *motg = the_msm_otg;
	struct usb_phy *phy = &motg->phy;

	if (g_host_mode) {
		dev_info(phy->dev, "%s()+++ (%d)(%d)\n", __func__, g_keep_power_on, g_host_none_mode);
		if (!g_host_none_mode) {
			g_suspend_delay_work_run = 1;

			asus_otg_host_auto_switch(HOST_AUTO_NONE);

//			if (AX_MicroP_IsP01Connected() && pad_exist()) {
				//ASUS_BSP+++ Eric5_Ou "add microp power control function in pad mode"
//				AX_MicroP_setOTGPower(0);
				//ASUS_BSP--- Eric5_Ou "add microp power control function in pad mode"
//				asus_otg_set_microp_mode(MICROP_SLEEP);
//			} else {
				asus_otg_vbus_out_enable(false, 0);
//			}
		}
		dev_info(phy->dev, "%s()---\n", __func__);
	}
}

static void asus_otg_early_suspend_delay_work(struct work_struct *w)
{
	struct msm_otg *motg = the_msm_otg;
	struct usb_phy *phy = &motg->phy;

	dev_info(phy->dev, "%s()+++\n", __func__);

	g_suspend_delay_work_run = 1;

	if (g_host_mode) {
		g_keep_power_on = asus_otg_keep_power_on_check();
		dev_info(phy->dev, "g_keep_power_on (%d)\n", g_keep_power_on);

//		if (AX_MicroP_IsP01Connected() && pad_exist()) {
//			if (!g_keep_power_on) {
//				asus_otg_host_auto_switch(HOST_AUTO_NONE);

				//if (asus_otg_get_pad_cbus_en()) {
					//asus_otg_set_pad_cbus_en(0);
				//}

				//ASUS_BSP+++ Eric5_Ou "add microp power control function in pad mode"
//				AX_MicroP_setOTGPower(0);
				//ASUS_BSP--- Eric5_Ou "add microp power control function in pad mode"

//				asus_otg_set_microp_mode(MICROP_SLEEP);
//			} else {
				/*
				 * If a usb storage is plugged, unlocking lock here to allow the usb storgae enter pm suspend and
				 * turning off the power of hub in the very end of msm_otg_pm_suspend
				 */
				//if (asus_otg_get_pad_cbus_en()) {
					//asus_otg_set_pad_cbus_en(0);
				//}

//				if (asus_otg_get_pad_camera_power()) {
//					asus_otg_set_pad_camera_power(0);
//				}

//				wake_unlock(&motg->wlock);
//			}
//		} else {
			if (!g_keep_power_on) {
				asus_otg_host_auto_switch(HOST_AUTO_NONE);
				asus_otg_vbus_out_enable(false, 0);
			} else {
				/*
				 * If a usb storage is plugged, unlocking lock here to allow the usb storgae enter pm suspend and
				 * turning off the power of hub in the very end of msm_otg_pm_suspend
				 */
				wake_unlock(&motg->wlock);
			}
//		}
	}

	dev_info(phy->dev, "%s()---\n", __func__);
}

static void asus_otg_late_resume_work(struct work_struct *w)
{
	int wait = 0;
	struct msm_otg *motg = the_msm_otg;
	struct usb_otg *otg = motg->phy.otg;

	dev_info(motg->phy.dev, "%s()+++\n", __func__);

	while ((otg->phy->state != OTG_STATE_B_IDLE) && (wait++ < 10)) {
		msleep(100);
	}

	if (wait >= 10) {
		dev_err(motg->phy.dev, "not b_idle state, skip host auto switch (%d)\n", otg->phy->state);
		return;
	}

	asus_otg_host_auto_switch(HOST_AUTO_HOST);

	dev_info(motg->phy.dev, "%s()--- (%d)\n", __func__, wait);
}

#if defined(CONFIG_FB)
static void asus_otg_fb_early_suspend(void)
{
	struct msm_otg *motg = the_msm_otg;
	struct usb_phy *phy = &motg->phy;

	dev_info(phy->dev, "%s()+++\n", __func__);

	if (g_host_mode) {
		wake_lock_timeout(&early_suspend_wlock, 5 * HZ);
		cancel_work_sync(&late_resume_work);
		cancel_delayed_work_sync(&early_suspend_delay_work);
		queue_delayed_work_on(0, early_suspend_delay_wq, &early_suspend_delay_work, 4 * HZ);
	}

	dev_info(phy->dev, "%s()---\n", __func__);
}

static void asus_otg_fb_late_resume(void)
{
	struct msm_otg *motg = the_msm_otg;
	struct usb_phy *phy = &motg->phy;

	dev_info(phy->dev, "%s()+++\n", __func__);

	if (g_host_mode) {
		cancel_delayed_work_sync(&early_suspend_delay_work);
		if (g_suspend_delay_work_run) {
			if (g_host_mode) {
				queue_work(system_nrt_wq, &late_resume_work);
			}
			g_suspend_delay_work_run = 0;
		}

//		if (AX_MicroP_IsP01Connected() && pad_exist()) {
//			asus_otg_set_microp_mode(MICROP_ACTIVE);

			//ASUS_BSP+++ Eric5_Ou "add microp power control function in pad mode"
//			AX_MicroP_setOTGPower(1);
			//ASUS_BSP--- Eric5_Ou "add microp power control function in pad mode"

			//if (!asus_otg_get_pad_cbus_en()) {
				//asus_otg_set_pad_cbus_en(1);
			//}

//			asus_otg_set_pad_hub_power(1);
//			asus_otg_set_pad_camera_power(1);
//		} else {
			asus_otg_vbus_out_enable(true, 0);
//		}
	}

	dev_info(phy->dev, "%s()---\n", __func__);
}

static int asus_otg_fb_notifier_callback(struct notifier_block *self, unsigned long event, void *data)
{
	struct fb_event *evdata = data;
	static int blank_old = 0;
	int *blank;

	if (evdata && evdata->data && event == FB_EVENT_BLANK) {
		blank = evdata->data;
		if (*blank == FB_BLANK_UNBLANK) {
			if (blank_old == FB_BLANK_POWERDOWN) {
				blank_old = FB_BLANK_UNBLANK;
				asus_otg_fb_late_resume();
			}
		} else if (*blank == FB_BLANK_POWERDOWN) {
			if (blank_old == 0 || blank_old == FB_BLANK_UNBLANK) {
				blank_old = FB_BLANK_POWERDOWN;
				asus_otg_fb_early_suspend();
			}
		}
	}

	return 0;
}
#endif
//ASUS_BSP--- Eric5_Ou "register early suspend notification for none mode switch"

//ASUS_BSP+++ Eric5_Ou "add phone mode usb OTG support"
static void asus_otg_set_id_state(int online)
{
	struct msm_otg *motg = the_msm_otg;

	if (USB_AUTO != motg->otg_mode) {
		dev_err(motg->phy.dev, "OTG not support for non AUTO mode!(%d)\n", motg->otg_mode);
		return;
	}

//	if (USB_AUTO == motg->otg_mode && (AX_MicroP_IsP01Connected() || pad_exist())) {
//		dev_err(motg->phy.dev, "ignore ID events in pad for auto mode (%d)\n", online);
//		return;
//	}

	if((online && g_host_mode)||(!online && !g_host_mode)){
		printk("OTG: ID already set to %d\n",online);
		return;
	}

	if (online) {
		printk("[USB] OTG ID set\n");
		asus_otg_mode_switch(USB_HOST);
	} else {
		printk("[USB] OTG ID clear\n");
		asus_otg_mode_switch(USB_PERIPHERAL);
	}
}
//ASUS_BSP--- Eric5_Ou "add phone mode usb OTG support"

static int msm_hsusb_ldo_init(struct msm_otg *motg, int init)
{
	int rc = 0;

	if (init) {
		hsusb_3p3 = devm_regulator_get(motg->phy.dev, "HSUSB_3p3");
		if (IS_ERR(hsusb_3p3)) {
			dev_err(motg->phy.dev, "unable to get hsusb 3p3\n");
			return PTR_ERR(hsusb_3p3);
		}

		rc = regulator_set_voltage(hsusb_3p3, USB_PHY_3P3_VOL_MIN,
				USB_PHY_3P3_VOL_MAX);
		if (rc) {
			dev_err(motg->phy.dev, "unable to set voltage level for"
					"hsusb 3p3\n");
			return rc;
		}
		hsusb_1p8 = devm_regulator_get(motg->phy.dev, "HSUSB_1p8");
		if (IS_ERR(hsusb_1p8)) {
			dev_err(motg->phy.dev, "unable to get hsusb 1p8\n");
			rc = PTR_ERR(hsusb_1p8);
			goto put_3p3_lpm;
		}
		rc = regulator_set_voltage(hsusb_1p8, USB_PHY_1P8_VOL_MIN,
				USB_PHY_1P8_VOL_MAX);
		if (rc) {
			dev_err(motg->phy.dev, "unable to set voltage level "
					"for hsusb 1p8\n");
			goto put_1p8;
		}

		return 0;
	}

put_1p8:
	regulator_set_voltage(hsusb_1p8, 0, USB_PHY_1P8_VOL_MAX);
put_3p3_lpm:
	regulator_set_voltage(hsusb_3p3, 0, USB_PHY_3P3_VOL_MAX);
	return rc;
}

static int msm_hsusb_config_vddcx(int high)
{
	struct msm_otg *motg = the_msm_otg;
	enum usb_vdd_type vdd_type = motg->vdd_type;
	int max_vol = vdd_val[vdd_type][VDD_MAX];
	int min_vol;
	int ret;

	min_vol = vdd_val[vdd_type][!!high];
	ret = regulator_set_voltage(hsusb_vdd, min_vol, max_vol);
	if (ret) {
		pr_err("%s: unable to set the voltage for regulator "
			"HSUSB_VDDCX\n", __func__);
		return ret;
	}

	pr_debug("%s: min_vol:%d max_vol:%d\n", __func__, min_vol, max_vol);

	return ret;
}

static int msm_hsusb_ldo_enable(struct msm_otg *motg,
	enum msm_otg_phy_reg_mode mode)
{
	int ret = 0;

	if (IS_ERR(hsusb_1p8)) {
		pr_err("%s: HSUSB_1p8 is not initialized\n", __func__);
		return -ENODEV;
	}

	if (IS_ERR(hsusb_3p3)) {
		pr_err("%s: HSUSB_3p3 is not initialized\n", __func__);
		return -ENODEV;
	}

	switch (mode) {
	case USB_PHY_REG_ON:
		ret = regulator_set_optimum_mode(hsusb_1p8,
				USB_PHY_1P8_HPM_LOAD);
		if (ret < 0) {
			pr_err("%s: Unable to set HPM of the regulator "
				"HSUSB_1p8\n", __func__);
			return ret;
		}

		ret = regulator_enable(hsusb_1p8);
		if (ret) {
			dev_err(motg->phy.dev, "%s: unable to enable the hsusb 1p8\n",
				__func__);
			regulator_set_optimum_mode(hsusb_1p8, 0);
			return ret;
		}

		ret = regulator_set_optimum_mode(hsusb_3p3,
				USB_PHY_3P3_HPM_LOAD);
		if (ret < 0) {
			pr_err("%s: Unable to set HPM of the regulator "
				"HSUSB_3p3\n", __func__);
			regulator_set_optimum_mode(hsusb_1p8, 0);
			regulator_disable(hsusb_1p8);
			return ret;
		}

		ret = regulator_enable(hsusb_3p3);
		if (ret) {
			dev_err(motg->phy.dev, "%s: unable to enable the hsusb 3p3\n",
				__func__);
			regulator_set_optimum_mode(hsusb_3p3, 0);
			regulator_set_optimum_mode(hsusb_1p8, 0);
			regulator_disable(hsusb_1p8);
			return ret;
		}

		break;

	case USB_PHY_REG_OFF:
		ret = regulator_disable(hsusb_1p8);
		if (ret) {
			dev_err(motg->phy.dev, "%s: unable to disable the hsusb 1p8\n",
				__func__);
			return ret;
		}

		ret = regulator_set_optimum_mode(hsusb_1p8, 0);
		if (ret < 0)
			pr_err("%s: Unable to set LPM of the regulator "
				"HSUSB_1p8\n", __func__);

		ret = regulator_disable(hsusb_3p3);
		if (ret) {
			dev_err(motg->phy.dev, "%s: unable to disable the hsusb 3p3\n",
				 __func__);
			return ret;
		}
		ret = regulator_set_optimum_mode(hsusb_3p3, 0);
		if (ret < 0)
			pr_err("%s: Unable to set LPM of the regulator "
				"HSUSB_3p3\n", __func__);

		break;

	case USB_PHY_REG_LPM_ON:
		ret = regulator_set_optimum_mode(hsusb_1p8,
				USB_PHY_1P8_LPM_LOAD);
		if (ret < 0) {
			pr_err("%s: Unable to set LPM of the regulator: HSUSB_1p8\n",
				__func__);
			return ret;
		}

		ret = regulator_set_optimum_mode(hsusb_3p3,
				USB_PHY_3P3_LPM_LOAD);
		if (ret < 0) {
			pr_err("%s: Unable to set LPM of the regulator: HSUSB_3p3\n",
				__func__);
			regulator_set_optimum_mode(hsusb_1p8, USB_PHY_REG_ON);
			return ret;
		}

		break;

	case USB_PHY_REG_LPM_OFF:
		ret = regulator_set_optimum_mode(hsusb_1p8,
				USB_PHY_1P8_HPM_LOAD);
		if (ret < 0) {
			pr_err("%s: Unable to set HPM of the regulator: HSUSB_1p8\n",
				__func__);
			return ret;
		}

		ret = regulator_set_optimum_mode(hsusb_3p3,
				USB_PHY_3P3_HPM_LOAD);
		if (ret < 0) {
			pr_err("%s: Unable to set HPM of the regulator: HSUSB_3p3\n",
				__func__);
			regulator_set_optimum_mode(hsusb_1p8, USB_PHY_REG_ON);
			return ret;
		}

		break;

	default:
		pr_err("%s: Unsupported mode (%d).", __func__, mode);
		return -ENOTSUPP;
	}

	pr_debug("%s: USB reg mode (%d) (OFF/HPM/LPM)\n", __func__, mode);
	return ret < 0 ? ret : 0;
}

static void msm_hsusb_mhl_switch_enable(struct msm_otg *motg, bool on)
{
	struct msm_otg_platform_data *pdata = motg->pdata;

	if (!pdata->mhl_enable)
		return;

	if (!mhl_usb_hs_switch) {
		pr_err("%s: mhl_usb_hs_switch is NULL.\n", __func__);
		return;
	}

	if (on) {
		if (regulator_enable(mhl_usb_hs_switch))
			pr_err("unable to enable mhl_usb_hs_switch\n");
	} else {
		regulator_disable(mhl_usb_hs_switch);
	}
}

static int ulpi_read(struct usb_phy *phy, u32 reg)
{
	struct msm_otg *motg = container_of(phy, struct msm_otg, phy);
	int cnt = 0;

	/* initiate read operation */
	writel(ULPI_RUN | ULPI_READ | ULPI_ADDR(reg),
	       USB_ULPI_VIEWPORT);

	/* wait for completion */
	while (cnt < ULPI_IO_TIMEOUT_USEC) {
		if (!(readl(USB_ULPI_VIEWPORT) & ULPI_RUN))
			break;
		udelay(1);
		cnt++;
	}

	if (cnt >= ULPI_IO_TIMEOUT_USEC) {
		dev_err(phy->dev, "ulpi_read: timeout %08x\n",
			readl(USB_ULPI_VIEWPORT));
		dev_err(phy->dev, "PORTSC: %08x USBCMD: %08x\n",
			readl_relaxed(USB_PORTSC), readl_relaxed(USB_USBCMD));
		return -ETIMEDOUT;
	}
	return ULPI_DATA_READ(readl(USB_ULPI_VIEWPORT));
}

static int ulpi_write(struct usb_phy *phy, u32 val, u32 reg)
{
	struct msm_otg *motg = container_of(phy, struct msm_otg, phy);
	int cnt = 0;

	/* initiate write operation */
	writel(ULPI_RUN | ULPI_WRITE |
	       ULPI_ADDR(reg) | ULPI_DATA(val),
	       USB_ULPI_VIEWPORT);

	/* wait for completion */
	while (cnt < ULPI_IO_TIMEOUT_USEC) {
		if (!(readl(USB_ULPI_VIEWPORT) & ULPI_RUN))
			break;
		udelay(1);
		cnt++;
	}

	if (cnt >= ULPI_IO_TIMEOUT_USEC) {
		dev_err(phy->dev, "ulpi_write: timeout\n");
		dev_err(phy->dev, "PORTSC: %08x USBCMD: %08x\n",
			readl_relaxed(USB_PORTSC), readl_relaxed(USB_USBCMD));
		return -ETIMEDOUT;
	}
	return 0;
}

static struct usb_phy_io_ops msm_otg_io_ops = {
	.read = ulpi_read,
	.write = ulpi_write,
};

static void ulpi_init(struct msm_otg *motg)
{
	struct msm_otg_platform_data *pdata = motg->pdata;
	int aseq[10];
	int *seq = NULL;
	//ASUS_BSP+++ Eric5_Ou "add dynamic setting support for phy parameters"
	int addr = 0, value = 0;
	//ASUS_BSP--- Eric5_Ou "add dynamic setting support for phy parameters"

	if (override_phy_init) {
		pr_debug("%s(): HUSB PHY Init:%s\n", __func__,
				override_phy_init);
		get_options(override_phy_init, ARRAY_SIZE(aseq), aseq);
		seq = &aseq[1];
	} else {
		seq = pdata->phy_init_seq;
	}

	if (!seq)
		return;

	while (seq[0] >= 0) {
		//ASUS_BSP+++ Eric5_Ou "add dynamic setting support for phy parameters"
		addr = seq [1];
		value = seq[0];
		if (seq[1] == 0x81) {
			if (g_phy_parameter_b > 0) {
				value = g_phy_parameter_b;
			} else {
				//if (g_A68_hwID >= A80_EVB && g_A68_hwID <= A80_SR2) {
				//	if ((AX_MicroP_IsP01Connected() && pad_exist())) {
				//		value = A80_PAD_PHY_PARA_B;
				//	} else {
				//		value = seq[0];
				//	}
				//} else {
					value = seq[0];
				//}
			}
		}

		if (seq[1] == 0x82) {
			if (g_phy_parameter_c > 0) {
				value = g_phy_parameter_c;
			} else {
				//if (g_A68_hwID >= A80_EVB && g_A68_hwID <= A80_SR2) {
				//	if ((AX_MicroP_IsP01Connected() && pad_exist())) {
				//		value = A80_PAD_PHY_PARA_C;
				//	} else {
				//		value = seq[0];
				//	}
				//} else {
					value = seq[0];
				//}
			}
		}

		if (seq[1] == 0x83) {
			if (g_phy_parameter_d > 0) {
				value = g_phy_parameter_d;
			} else {
				//if (g_A68_hwID >= A80_EVB && g_A68_hwID <= A80_SR2) {
				//	if ((AX_MicroP_IsP01Connected() && pad_exist())) {
				//		value = A80_PAD_PHY_PARA_D;
				//	} else {
				//		value = seq[0];
				//	}
				//} else {
					value = seq[0];
				//}
			}
		}

		if (override_phy_init)
			pr_debug("ulpi: write 0x%02x to 0x%02x\n",
					value, addr);

		dev_info(motg->phy.dev, "ulpi: write 0x%02x to 0x%02x\n",
				value, addr);
		ulpi_write(&motg->phy, value, addr);
		//ASUS_BSP--- Eric5_Ou "add dynamic setting support for phy parameters"
		seq += 2;
	}

	//ASUS_BSP+++ Eric5_Ou "add dynamic setting support for phy parameters"
	if (g_phy_parameter_d <= 0) {
		//dev_info(motg->phy.dev, "ulpi: read 0x%02x to 0x83\n", ulpi_read(&motg->phy, 0x83));
	} else {
		dev_info(motg->phy.dev, "ulpi: write 0x%02x to 0x83\n", g_phy_parameter_d);
		ulpi_write(&motg->phy, g_phy_parameter_d, 0x83);
	}
	//ASUS_BSP--- Eric5_Ou "add dynamic setting support for phy parameters"
}

static int msm_otg_link_clk_reset(struct msm_otg *motg, bool assert)
{
	int ret;

	if (assert) {
		if (!IS_ERR(motg->clk)) {
			ret = clk_reset(motg->clk, CLK_RESET_ASSERT);
		} else {
			/* Using asynchronous block reset to the hardware */
			dev_dbg(motg->phy.dev, "block_reset ASSERT\n");
			clk_disable_unprepare(motg->pclk);
			clk_disable_unprepare(motg->core_clk);
			ret = clk_reset(motg->core_clk, CLK_RESET_ASSERT);
		}
		if (ret)
			dev_err(motg->phy.dev, "usb hs_clk assert failed\n");
	} else {
		if (!IS_ERR(motg->clk)) {
			ret = clk_reset(motg->clk, CLK_RESET_DEASSERT);
		} else {
			dev_dbg(motg->phy.dev, "block_reset DEASSERT\n");
			ret = clk_reset(motg->core_clk, CLK_RESET_DEASSERT);
			ndelay(200);
			clk_prepare_enable(motg->core_clk);
			clk_prepare_enable(motg->pclk);
		}
		if (ret)
			dev_err(motg->phy.dev, "usb hs_clk deassert failed\n");
	}
	return ret;
}

static int msm_otg_phy_reset(struct msm_otg *motg)
{
	u32 val;
	int ret;
	struct msm_otg_platform_data *pdata = motg->pdata;

	/*
	 * AHB2AHB Bypass mode shouldn't be enable before doing
	 * async clock reset. If it is enable, disable the same.
	 */
	val = readl_relaxed(USB_AHBMODE);
	if (val & AHB2AHB_BYPASS) {
		pr_err("%s(): AHB2AHB_BYPASS SET: AHBMODE:%x\n",
				__func__, val);
		val &= ~AHB2AHB_BYPASS_BIT_MASK;
		writel_relaxed(val | AHB2AHB_BYPASS_CLEAR, USB_AHBMODE);
		pr_err("%s(): AHBMODE: %x\n", __func__,
				readl_relaxed(USB_AHBMODE));
	}

	ret = msm_otg_link_clk_reset(motg, 1);
	if (ret)
		return ret;

	/* wait for 1ms delay as suggested in HPG. */
	usleep_range(1000, 1200);

	ret = msm_otg_link_clk_reset(motg, 0);
	if (ret)
		return ret;

	if (pdata && pdata->enable_sec_phy)
		writel_relaxed(readl_relaxed(USB_PHY_CTRL2) | (1<<16),
							USB_PHY_CTRL2);
	val = readl(USB_PORTSC) & ~PORTSC_PTS_MASK;
	writel(val | PORTSC_PTS_ULPI, USB_PORTSC);

	dev_info(motg->phy.dev, "phy_reset: success\n");
	return 0;
}

#define LINK_RESET_TIMEOUT_USEC		(250 * 1000)
static int msm_otg_link_reset(struct msm_otg *motg)
{
	int cnt = 0;
	struct msm_otg_platform_data *pdata = motg->pdata;

	writel_relaxed(USBCMD_RESET, USB_USBCMD);
	while (cnt < LINK_RESET_TIMEOUT_USEC) {
		if (!(readl_relaxed(USB_USBCMD) & USBCMD_RESET))
			break;
		udelay(1);
		cnt++;
	}
	if (cnt >= LINK_RESET_TIMEOUT_USEC)
		return -ETIMEDOUT;

	/* select ULPI phy */
	writel_relaxed(0x80000000, USB_PORTSC);
	writel_relaxed(0x0, USB_AHBBURST);
	writel_relaxed(0x08, USB_AHBMODE);

	if (pdata && pdata->enable_sec_phy)
		writel_relaxed(readl_relaxed(USB_PHY_CTRL2) | (1<<16),
								USB_PHY_CTRL2);
	return 0;
}

static void usb_phy_reset(struct msm_otg *motg)
{
	u32 val;

	if (motg->pdata->phy_type != SNPS_28NM_INTEGRATED_PHY)
		return;

	/* Assert USB PHY_PON */
	val =  readl_relaxed(USB_PHY_CTRL);
	val &= ~PHY_POR_BIT_MASK;
	val |= PHY_POR_ASSERT;
	writel_relaxed(val, USB_PHY_CTRL);

	/* wait for minimum 10 microseconds as suggested in HPG. */
	usleep_range(10, 15);

	/* Deassert USB PHY_PON */
	val =  readl_relaxed(USB_PHY_CTRL);
	val &= ~PHY_POR_BIT_MASK;
	val |= PHY_POR_DEASSERT;
	writel_relaxed(val, USB_PHY_CTRL);

	/* Ensure that RESET operation is completed. */
	mb();
}

static int msm_otg_reset(struct usb_phy *phy)
{
	struct msm_otg *motg = container_of(phy, struct msm_otg, phy);
	struct msm_otg_platform_data *pdata = motg->pdata;
	int ret;
	u32 val = 0;
	u32 ulpi_val = 0;

	/*
	 * USB PHY and Link reset also reset the USB BAM.
	 * Thus perform reset operation only once to avoid
	 * USB BAM reset on other cases e.g. USB cable disconnections.
	 */
	if (pdata->disable_reset_on_disconnect) {
		if (motg->reset_counter)
			return 0;
		else
			motg->reset_counter++;
	}

	if (!IS_ERR(motg->clk))
		clk_prepare_enable(motg->clk);
	ret = msm_otg_phy_reset(motg);
	if (ret) {
		dev_err(phy->dev, "phy_reset failed\n");
		return ret;
	}

	aca_id_turned_on = false;
	ret = msm_otg_link_reset(motg);
	if (ret) {
		dev_err(phy->dev, "link reset failed\n");
		return ret;
	}

	msleep(100);

	/* Reset USB PHY after performing USB Link RESET */
	usb_phy_reset(motg);

	/* Program USB PHY Override registers. */
	ulpi_init(motg);

	/*
	 * It is recommended in HPG to reset USB PHY after programming
	 * USB PHY Override registers.
	 */
	usb_phy_reset(motg);

	if (!IS_ERR(motg->clk))
		clk_disable_unprepare(motg->clk);

	if (pdata->otg_control == OTG_PHY_CONTROL) {
		val = readl_relaxed(USB_OTGSC);
		if (pdata->mode == USB_OTG) {
			ulpi_val = ULPI_INT_IDGRD | ULPI_INT_SESS_VALID;
			val |= OTGSC_IDIE | OTGSC_BSVIE;
		} else if (pdata->mode == USB_PERIPHERAL) {
			ulpi_val = ULPI_INT_SESS_VALID;
			val |= OTGSC_BSVIE;
		}
		writel_relaxed(val, USB_OTGSC);
		ulpi_write(phy, ulpi_val, ULPI_USB_INT_EN_RISE);
		ulpi_write(phy, ulpi_val, ULPI_USB_INT_EN_FALL);
	} else if (pdata->otg_control == OTG_PMIC_CONTROL) {
		ulpi_write(phy, OTG_COMP_DISABLE,
			ULPI_SET(ULPI_PWR_CLK_MNG_REG));
		/* Enable PMIC pull-up */
		pm8xxx_usb_id_pullup(1);
	}

	if (motg->caps & ALLOW_VDD_MIN_WITH_RETENTION_DISABLED)
		writel_relaxed(readl_relaxed(USB_OTGSC) & ~(OTGSC_IDPU),
								USB_OTGSC);

	return 0;
}

static const char *timer_string(int bit)
{
	switch (bit) {
	case A_WAIT_VRISE:		return "a_wait_vrise";
	case A_WAIT_VFALL:		return "a_wait_vfall";
	case B_SRP_FAIL:		return "b_srp_fail";
	case A_WAIT_BCON:		return "a_wait_bcon";
	case A_AIDL_BDIS:		return "a_aidl_bdis";
	case A_BIDL_ADIS:		return "a_bidl_adis";
	case B_ASE0_BRST:		return "b_ase0_brst";
	case A_TST_MAINT:		return "a_tst_maint";
	case B_TST_SRP:			return "b_tst_srp";
	case B_TST_CONFIG:		return "b_tst_config";
	default:			return "UNDEFINED";
	}
}

static enum hrtimer_restart msm_otg_timer_func(struct hrtimer *hrtimer)
{
	struct msm_otg *motg = container_of(hrtimer, struct msm_otg, timer);

	switch (motg->active_tmout) {
	case A_WAIT_VRISE:
		/* TODO: use vbus_vld interrupt */
		set_bit(A_VBUS_VLD, &motg->inputs);
		break;
	case A_TST_MAINT:
		/* OTG PET: End session after TA_TST_MAINT */
		set_bit(A_BUS_DROP, &motg->inputs);
		break;
	case B_TST_SRP:
		/*
		 * OTG PET: Initiate SRP after TB_TST_SRP of
		 * previous session end.
		 */
		set_bit(B_BUS_REQ, &motg->inputs);
		break;
	case B_TST_CONFIG:
		clear_bit(A_CONN, &motg->inputs);
		break;
	default:
		set_bit(motg->active_tmout, &motg->tmouts);
	}

	pr_debug("expired %s timer\n", timer_string(motg->active_tmout));
	queue_work(system_nrt_wq, &motg->sm_work);
	return HRTIMER_NORESTART;
}

static void msm_otg_del_timer(struct msm_otg *motg)
{
	int bit = motg->active_tmout;

	pr_debug("deleting %s timer. remaining %lld msec\n", timer_string(bit),
			div_s64(ktime_to_us(hrtimer_get_remaining(
					&motg->timer)), 1000));
	hrtimer_cancel(&motg->timer);
	clear_bit(bit, &motg->tmouts);
}

static void msm_otg_start_timer(struct msm_otg *motg, int time, int bit)
{
	//ASUS_BSP+++ Eric5_Ou "skip otg detect waiting time"
	time = 0;
	//ASUS_BSP--- Eric5_Ou "skip otg detect waiting time"
	clear_bit(bit, &motg->tmouts);
	motg->active_tmout = bit;
	pr_debug("starting %s timer\n", timer_string(bit));
	hrtimer_start(&motg->timer,
			ktime_set(time / 1000, (time % 1000) * 1000000),
			HRTIMER_MODE_REL);
}

static void msm_otg_init_timer(struct msm_otg *motg)
{
	hrtimer_init(&motg->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	motg->timer.function = msm_otg_timer_func;
}

static int msm_otg_start_hnp(struct usb_otg *otg)
{
	struct msm_otg *motg = container_of(otg->phy, struct msm_otg, phy);

	if (otg->phy->state != OTG_STATE_A_HOST) {
		pr_err("HNP can not be initiated in %s state\n",
				otg_state_string(otg->phy->state));
		return -EINVAL;
	}

	pr_debug("A-Host: HNP initiated\n");
	clear_bit(A_BUS_REQ, &motg->inputs);
	queue_work(system_nrt_wq, &motg->sm_work);
	return 0;
}

static int msm_otg_start_srp(struct usb_otg *otg)
{
	struct msm_otg *motg = container_of(otg->phy, struct msm_otg, phy);
	u32 val;
	int ret = 0;

	if (otg->phy->state != OTG_STATE_B_IDLE) {
		pr_err("SRP can not be initiated in %s state\n",
				otg_state_string(otg->phy->state));
		ret = -EINVAL;
		goto out;
	}

	if ((jiffies - motg->b_last_se0_sess) < msecs_to_jiffies(TB_SRP_INIT)) {
		pr_debug("initial conditions of SRP are not met. Try again"
				"after some time\n");
		ret = -EAGAIN;
		goto out;
	}

	pr_debug("B-Device SRP started\n");

	/*
	 * PHY won't pull D+ high unless it detects Vbus valid.
	 * Since by definition, SRP is only done when Vbus is not valid,
	 * software work-around needs to be used to spoof the PHY into
	 * thinking it is valid. This can be done using the VBUSVLDEXTSEL and
	 * VBUSVLDEXT register bits.
	 */
	ulpi_write(otg->phy, 0x03, 0x97);
	/*
	 * Harware auto assist data pulsing: Data pulse is given
	 * for 7msec; wait for vbus
	 */
	val = readl_relaxed(USB_OTGSC);
	writel_relaxed((val & ~OTGSC_INTSTS_MASK) | OTGSC_HADP, USB_OTGSC);

	/* VBUS plusing is obsoleted in OTG 2.0 supplement */
out:
	return ret;
}

static void msm_otg_host_hnp_enable(struct usb_otg *otg, bool enable)
{
	struct usb_hcd *hcd = bus_to_hcd(otg->host);
	struct usb_device *rhub = otg->host->root_hub;

	if (enable) {
		pm_runtime_disable(&rhub->dev);
		rhub->state = USB_STATE_NOTATTACHED;
		hcd->driver->bus_suspend(hcd);
		clear_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);
	} else {
		usb_remove_hcd(hcd);
		msm_otg_reset(otg->phy);
		usb_add_hcd(hcd, hcd->irq, IRQF_SHARED);
	}
}

static int msm_otg_set_suspend(struct usb_phy *phy, int suspend)
{
	struct msm_otg *motg = container_of(phy, struct msm_otg, phy);

	if (aca_enabled())
		return 0;

	/*
	 * UDC and HCD call usb_phy_set_suspend() to enter/exit LPM
	 * during bus suspend/resume.  Update the relevant state
	 * machine inputs and trigger LPM entry/exit.  Checking
	 * in_lpm flag would avoid unnecessary work scheduling.
	 */
	if (suspend) {
		switch (phy->state) {
		case OTG_STATE_A_WAIT_BCON:
			if (TA_WAIT_BCON > 0)
				break;
			/* fall through */
		case OTG_STATE_A_HOST:
			pr_debug("host bus suspend\n");
			clear_bit(A_BUS_REQ, &motg->inputs);
			if (!atomic_read(&motg->in_lpm))
				queue_work(system_nrt_wq, &motg->sm_work);
			break;
		case OTG_STATE_B_PERIPHERAL:
			pr_debug("peripheral bus suspend\n");
			if (!(motg->caps & ALLOW_LPM_ON_DEV_SUSPEND))
				break;
			set_bit(A_BUS_SUSPEND, &motg->inputs);
			if (!atomic_read(&motg->in_lpm))
				queue_delayed_work(system_nrt_wq,
					&motg->suspend_work,
					USB_SUSPEND_DELAY_TIME);
			break;

		default:
			break;
		}
	} else {
		switch (phy->state) {
		case OTG_STATE_A_WAIT_BCON:
			/* Remote wakeup or resume */
			set_bit(A_BUS_REQ, &motg->inputs);
			/* ensure hardware is not in low power mode */
			if (atomic_read(&motg->in_lpm))
				pm_runtime_resume(phy->dev);
			break;
		case OTG_STATE_A_SUSPEND:
			/* Remote wakeup or resume */
			set_bit(A_BUS_REQ, &motg->inputs);
			phy->state = OTG_STATE_A_HOST;

			/* ensure hardware is not in low power mode */
			if (atomic_read(&motg->in_lpm))
				pm_runtime_resume(phy->dev);
			break;
		case OTG_STATE_B_PERIPHERAL:
			pr_debug("peripheral bus resume\n");
			if (!(motg->caps & ALLOW_LPM_ON_DEV_SUSPEND))
				break;
			clear_bit(A_BUS_SUSPEND, &motg->inputs);
			if (atomic_read(&motg->in_lpm))
				queue_work(system_nrt_wq, &motg->sm_work);
			break;
		default:
			break;
		}
	}
	return 0;
}

static void msm_otg_bus_vote(struct msm_otg *motg, enum usb_bus_vote vote)
{
	int ret;
	struct msm_otg_platform_data *pdata = motg->pdata;

	/* Check if target allows min_vote to be same as no_vote */
	if (pdata->bus_scale_table &&
	    vote >= pdata->bus_scale_table->num_usecases)
		vote = USB_NO_PERF_VOTE;

	if (motg->bus_perf_client) {
		ret = msm_bus_scale_client_update_request(
			motg->bus_perf_client, vote);
		if (ret)
			dev_err(motg->phy.dev, "%s: Failed to vote (%d)\n"
				   "for bus bw %d\n", __func__, vote, ret);
	}
}

#define PHY_SUSPEND_TIMEOUT_USEC	(500 * 1000)
#define PHY_RESUME_TIMEOUT_USEC	(100 * 1000)

#ifdef CONFIG_PM_SLEEP
static int msm_otg_suspend(struct msm_otg *motg)
{
	struct usb_phy *phy = &motg->phy;
	struct usb_bus *bus = phy->otg->host;
	struct msm_otg_platform_data *pdata = motg->pdata;
	int cnt = 0;
	bool host_bus_suspend, device_bus_suspend, dcp, prop_charger;
	bool floated_charger;
	u32 phy_ctrl_val = 0, cmd_val;
	unsigned ret;
	u32 portsc, config2;
	u32 func_ctrl;

	if (atomic_read(&motg->in_lpm))
		return 0;

	if (motg->pdata->delay_lpm_hndshk_on_disconnect && !msm_bam_lpm_ok())
		return -EBUSY;

	//ASUS_BSP+++ Eric5_Ou "add mutex to protect suspend/resume function"
	mutex_lock(&msm_otg_mutex);
	//ASUS_BSP--- Eric5_Ou "add mutex to protect suspend/resume function"

	motg->ui_enabled = 0;
	disable_irq(motg->irq);
	host_bus_suspend = !test_bit(MHL, &motg->inputs) && phy->otg->host &&
		!test_bit(ID, &motg->inputs);
	device_bus_suspend = phy->otg->gadget && test_bit(ID, &motg->inputs) &&
		test_bit(A_BUS_SUSPEND, &motg->inputs) &&
		motg->caps & ALLOW_LPM_ON_DEV_SUSPEND;
	dcp = motg->chg_type == USB_DCP_CHARGER;
	prop_charger = motg->chg_type == USB_PROPRIETARY_CHARGER;
	floated_charger = motg->chg_type == USB_FLOATED_CHARGER;

	/* Enable line state difference wakeup fix for only device and host
	 * bus suspend scenarios.  Otherwise PHY can not be suspended when
	 * a charger that pulls DP/DM high is connected.
	 */
	config2 = readl_relaxed(USB_GENCONFIG2);
	if (device_bus_suspend)
		config2 |= GENCFG2_LINESTATE_DIFF_WAKEUP_EN;
	else
		config2 &= ~GENCFG2_LINESTATE_DIFF_WAKEUP_EN;
	writel_relaxed(config2, USB_GENCONFIG2);

	/*
	 * Abort suspend when,
	 * 1. charging detection in progress due to cable plug-in
	 * 2. host mode activation in progress due to Micro-A cable insertion
	 */

	//ASUS_BSP+++ Eric5_Ou "not check vbus state during suspend"
	/*
	if ((test_bit(B_SESS_VLD, &motg->inputs) && !device_bus_suspend &&
		!dcp && !prop_charger && !floated_charger) ||
		test_bit(A_BUS_REQ, &motg->inputs)) {
		motg->ui_enabled = 1;
		enable_irq(motg->irq);
		//ASUS_BSP+++ "add mutex to protect suspend/resume function"
		wake_unlock(&motg->wlock);
		mutex_unlock(&msm_otg_mutex);
		//ASUS_BSP--- "add mutex to protect suspend/resume function"
		return -EBUSY;
	}
	*/
	//ASUS_BSP--- Eric5_Ou "not check vbus state during suspend"

	/*
	 * Chipidea 45-nm PHY suspend sequence:
	 *
	 * Interrupt Latch Register auto-clear feature is not present
	 * in all PHY versions. Latch register is clear on read type.
	 * Clear latch register to avoid spurious wakeup from
	 * low power mode (LPM).
	 *
	 * PHY comparators are disabled when PHY enters into low power
	 * mode (LPM). Keep PHY comparators ON in LPM only when we expect
	 * VBUS/Id notifications from USB PHY. Otherwise turn off USB
	 * PHY comparators. This save significant amount of power.
	 *
	 * PLL is not turned off when PHY enters into low power mode (LPM).
	 * Disable PLL for maximum power savings.
	 */

	if (motg->pdata->phy_type == CI_45NM_INTEGRATED_PHY) {
		ulpi_read(phy, 0x14);
		if (pdata->otg_control == OTG_PHY_CONTROL)
			ulpi_write(phy, 0x01, 0x30);
		ulpi_write(phy, 0x08, 0x09);
	}

	if (motg->caps & ALLOW_VDD_MIN_WITH_RETENTION_DISABLED) {
		/* put the controller in non-driving mode */
		func_ctrl = ulpi_read(phy, ULPI_FUNC_CTRL);
		func_ctrl &= ~ULPI_FUNC_CTRL_OPMODE_MASK;
		func_ctrl |= ULPI_FUNC_CTRL_OPMODE_NONDRIVING;
		ulpi_write(phy, func_ctrl, ULPI_FUNC_CTRL);
		ulpi_write(phy, ULPI_IFC_CTRL_AUTORESUME,
				ULPI_CLR(ULPI_IFC_CTRL));
	}

	/* Set the PHCD bit, only if it is not set by the controller.
	 * PHY may take some time or even fail to enter into low power
	 * mode (LPM). Hence poll for 500 msec and reset the PHY and link
	 * in failure case.
	 */
	portsc = readl_relaxed(USB_PORTSC);
	if (!(portsc & PORTSC_PHCD)) {
		writel_relaxed(portsc | PORTSC_PHCD,
				USB_PORTSC);
		while (cnt < PHY_SUSPEND_TIMEOUT_USEC) {
			if (readl_relaxed(USB_PORTSC) & PORTSC_PHCD)
				break;
			udelay(1);
			cnt++;
		}
	}

	if (cnt >= PHY_SUSPEND_TIMEOUT_USEC) {
		dev_err(phy->dev, "Unable to suspend PHY\n");
		msm_otg_reset(phy);
		motg->ui_enabled = 1;
		enable_irq(motg->irq);
		//ASUS_BSP+++ "add mutex to protect suspend/resume function"
		wake_unlock(&motg->wlock);
		mutex_unlock(&msm_otg_mutex);
		//ASUS_BSP--- "add mutex to protect suspend/resume function"
		return -ETIMEDOUT;
	}

	/*
	 * PHY has capability to generate interrupt asynchronously in low
	 * power mode (LPM). This interrupt is level triggered. So USB IRQ
	 * line must be disabled till async interrupt enable bit is cleared
	 * in USBCMD register. Assert STP (ULPI interface STOP signal) to
	 * block data communication from PHY.
	 *
	 * PHY retention mode is disallowed while entering to LPM with wall
	 * charger connected.  But PHY is put into suspend mode. Hence
	 * enable asynchronous interrupt to detect charger disconnection when
	 * PMIC notifications are unavailable.
	 */
	cmd_val = readl_relaxed(USB_USBCMD);
	if (host_bus_suspend || device_bus_suspend ||
		(motg->pdata->otg_control == OTG_PHY_CONTROL))
		cmd_val |= ASYNC_INTR_CTRL | ULPI_STP_CTRL;
	else
		cmd_val |= ULPI_STP_CTRL;
	writel_relaxed(cmd_val, USB_USBCMD);

	/*
	 * BC1.2 spec mandates PD to enable VDP_SRC when charging from DCP.
	 * PHY retention and collapse can not happen with VDP_SRC enabled.
	 */
	if (motg->caps & ALLOW_PHY_RETENTION && !device_bus_suspend && !dcp &&
		 (!host_bus_suspend || ((motg->caps & ALLOW_HOST_PHY_RETENTION)
		&& (pdata->dpdm_pulldown_added || !(portsc & PORTSC_CCS))))) {
		phy_ctrl_val = readl_relaxed(USB_PHY_CTRL);
		if (motg->pdata->otg_control == OTG_PHY_CONTROL) {
			/* Enable PHY HV interrupts to wake MPM/Link */
			if ((motg->pdata->mode == USB_OTG) ||
					(motg->pdata->mode == USB_HOST))
				phy_ctrl_val |= (PHY_IDHV_INTEN |
							PHY_OTGSESSVLDHV_INTEN);
			else
				phy_ctrl_val |= PHY_OTGSESSVLDHV_INTEN;
		}
		if (host_bus_suspend)
			phy_ctrl_val |= PHY_CLAMP_DPDMSE_EN;

		if (!(motg->caps & ALLOW_VDD_MIN_WITH_RETENTION_DISABLED)) {
			writel_relaxed(phy_ctrl_val & ~PHY_RETEN, USB_PHY_CTRL);
			motg->lpm_flags |= PHY_RETENTIONED;
		} else {
			writel_relaxed(phy_ctrl_val, USB_PHY_CTRL);
		}
	}

	/* Ensure that above operation is completed before turning off clocks */
	mb();
	/* Consider clocks on workaround flag only in case of bus suspend */
	if (!(phy->state == OTG_STATE_B_PERIPHERAL &&
		test_bit(A_BUS_SUSPEND, &motg->inputs)) ||
	    !motg->pdata->core_clk_always_on_workaround) {
		clk_disable_unprepare(motg->pclk);
		clk_disable_unprepare(motg->core_clk);
		motg->lpm_flags |= CLOCKS_DOWN;
	}

	/* usb phy no more require TCXO clock, hence vote for TCXO disable */
	if (!host_bus_suspend || ((motg->caps & ALLOW_HOST_PHY_RETENTION) &&
		(pdata->dpdm_pulldown_added || !(portsc & PORTSC_CCS)))) {
		if (!IS_ERR(motg->xo_clk)) {
			clk_disable_unprepare(motg->xo_clk);
			motg->lpm_flags |= XO_SHUTDOWN;
		} else {
			ret = msm_xo_mode_vote(motg->xo_handle,
							MSM_XO_MODE_OFF);
			if (ret)
				dev_err(phy->dev, "%s fail to devote XO %d\n",
								 __func__, ret);
			else
				motg->lpm_flags |= XO_SHUTDOWN;
		}
	}

	if (motg->caps & ALLOW_PHY_POWER_COLLAPSE &&
			!host_bus_suspend && !dcp) {
		msm_hsusb_ldo_enable(motg, USB_PHY_REG_OFF);
		motg->lpm_flags |= PHY_PWR_COLLAPSED;
	} else if (motg->caps & ALLOW_PHY_REGULATORS_LPM &&
			!host_bus_suspend && !device_bus_suspend && !dcp) {
		msm_hsusb_ldo_enable(motg, USB_PHY_REG_LPM_ON);
		motg->lpm_flags |= PHY_REGULATORS_LPM;
	}

	if (motg->lpm_flags & PHY_RETENTIONED ||
		(motg->caps & ALLOW_VDD_MIN_WITH_RETENTION_DISABLED)) {
		msm_hsusb_config_vddcx(0);
		msm_hsusb_mhl_switch_enable(motg, 0);
	}

	if (device_may_wakeup(phy->dev)) {
		if (motg->async_irq)
			enable_irq_wake(motg->async_irq);
		else
			enable_irq_wake(motg->irq);

		if (motg->pdata->pmic_id_irq)
			enable_irq_wake(motg->pdata->pmic_id_irq);
		if (pdata->otg_control == OTG_PHY_CONTROL &&
			pdata->mpm_otgsessvld_int)
			msm_mpm_set_pin_wake(pdata->mpm_otgsessvld_int, 1);
		if (host_bus_suspend && pdata->mpm_dpshv_int)
			msm_mpm_set_pin_wake(pdata->mpm_dpshv_int, 1);
		if (host_bus_suspend && pdata->mpm_dmshv_int)
			msm_mpm_set_pin_wake(pdata->mpm_dmshv_int, 1);
	}
	if (bus)
		clear_bit(HCD_FLAG_HW_ACCESSIBLE, &(bus_to_hcd(bus))->flags);

	msm_otg_bus_vote(motg, USB_NO_PERF_VOTE);

	motg->host_bus_suspend = host_bus_suspend;
	atomic_set(&motg->in_lpm, 1);
	/* Enable ASYNC IRQ (if present) during LPM */
	if (motg->async_irq)
		enable_irq(motg->async_irq);

	/* XO shutdown during idle , non wakeable irqs must be disabled */
	if (device_bus_suspend || host_bus_suspend || !motg->async_irq) {
		motg->ui_enabled = 1;
		enable_irq(motg->irq);
	}
	wake_unlock(&motg->wlock);

	//ASUS_BSP+++ Eric5_Ou "Add monitor to check otg suspend status in suspend mode"
	if (g_host_mode) {
		cancel_delayed_work(&asus_otg_suspend_check_work);
	}
	//ASUS_BSP--- Eric5_Ou "Add monitor to check otg suspend status in suspend mode"

	//ASUS_BSP+++ Eric5_Ou "add mutex to protect suspend/resume function"
	mutex_unlock(&msm_otg_mutex);
	//ASUS_BSP--- Eric5_Ou "add mutex to protect suspend/resume function"

	dev_info(phy->dev, "USB in low power mode\n");

	return 0;
}

static int msm_otg_resume(struct msm_otg *motg)
{
	struct usb_phy *phy = &motg->phy;
	struct usb_bus *bus = phy->otg->host;
	struct msm_otg_platform_data *pdata = motg->pdata;
	int cnt = 0;
	unsigned temp;
	u32 phy_ctrl_val = 0;
	unsigned ret;
	u32 func_ctrl;

	if (!atomic_read(&motg->in_lpm))
		return 0;

	//ASUS_BSP+++ Eric5_Ou "add mutex to protect suspend/resume function"
	mutex_lock(&msm_otg_mutex);
	//ASUS_BSP--- Eric5_Ou "add mutex to protect suspend/resume function"

	if (motg->pdata->delay_lpm_hndshk_on_disconnect)
		msm_bam_notify_lpm_resume();

	if (motg->ui_enabled) {
		motg->ui_enabled = 0;
		disable_irq(motg->irq);
	}
	wake_lock(&motg->wlock);

	/* Some platforms require BUS vote to enable/disable clocks */
	msm_otg_bus_vote(motg, USB_MIN_PERF_VOTE);

	/* Vote for TCXO when waking up the phy */
	if (motg->lpm_flags & XO_SHUTDOWN) {
		if (!IS_ERR(motg->xo_clk)) {
			clk_prepare_enable(motg->xo_clk);
		} else {
			ret = msm_xo_mode_vote(motg->xo_handle, MSM_XO_MODE_ON);
			if (ret)
				dev_err(phy->dev, "%s fail to vote for XO %d\n",
								__func__, ret);
		}
		motg->lpm_flags &= ~XO_SHUTDOWN;
	}

	if (motg->lpm_flags & CLOCKS_DOWN) {
		clk_prepare_enable(motg->core_clk);
		clk_prepare_enable(motg->pclk);
		motg->lpm_flags &= ~CLOCKS_DOWN;
	}

	if (motg->lpm_flags & PHY_PWR_COLLAPSED) {
		msm_hsusb_ldo_enable(motg, USB_PHY_REG_ON);
		motg->lpm_flags &= ~PHY_PWR_COLLAPSED;
	} else if (motg->lpm_flags & PHY_REGULATORS_LPM) {
		msm_hsusb_ldo_enable(motg, USB_PHY_REG_LPM_OFF);
		motg->lpm_flags &= ~PHY_REGULATORS_LPM;
	}

	if (motg->lpm_flags & PHY_RETENTIONED ||
		(motg->caps & ALLOW_VDD_MIN_WITH_RETENTION_DISABLED)) {
		msm_hsusb_mhl_switch_enable(motg, 1);
		msm_hsusb_config_vddcx(1);
		phy_ctrl_val = readl_relaxed(USB_PHY_CTRL);
		phy_ctrl_val |= PHY_RETEN;
		if (motg->pdata->otg_control == OTG_PHY_CONTROL)
			/* Disable PHY HV interrupts */
			phy_ctrl_val &=
				~(PHY_IDHV_INTEN | PHY_OTGSESSVLDHV_INTEN);
		phy_ctrl_val &= ~(PHY_CLAMP_DPDMSE_EN);
		writel_relaxed(phy_ctrl_val, USB_PHY_CTRL);
		motg->lpm_flags &= ~PHY_RETENTIONED;
	}

	temp = readl(USB_USBCMD);
	temp &= ~ASYNC_INTR_CTRL;
	temp &= ~ULPI_STP_CTRL;
	writel(temp, USB_USBCMD);

	/*
	 * PHY comes out of low power mode (LPM) in case of wakeup
	 * from asynchronous interrupt.
	 */
	if (!(readl(USB_PORTSC) & PORTSC_PHCD))
		goto skip_phy_resume;

	writel(readl(USB_PORTSC) & ~PORTSC_PHCD, USB_PORTSC);
	while (cnt < PHY_RESUME_TIMEOUT_USEC) {
		if (!(readl(USB_PORTSC) & PORTSC_PHCD))
			break;
		udelay(1);
		cnt++;
	}

	if (cnt >= PHY_RESUME_TIMEOUT_USEC) {
		/*
		 * This is a fatal error. Reset the link and
		 * PHY. USB state can not be restored. Re-insertion
		 * of USB cable is the only way to get USB working.
		 */
		dev_err(phy->dev, "Unable to resume USB."
				"Re-plugin the cable\n");
		msm_otg_reset(phy);
	}

skip_phy_resume:
	if (motg->caps & ALLOW_VDD_MIN_WITH_RETENTION_DISABLED) {
		/* put the controller in normal mode */
		func_ctrl = ulpi_read(phy, ULPI_FUNC_CTRL);
		func_ctrl &= ~ULPI_FUNC_CTRL_OPMODE_MASK;
		func_ctrl |= ULPI_FUNC_CTRL_OPMODE_NORMAL;
		ulpi_write(phy, func_ctrl, ULPI_FUNC_CTRL);
	}

	if (device_may_wakeup(phy->dev)) {
		if (motg->async_irq)
			disable_irq_wake(motg->async_irq);
		else
			disable_irq_wake(motg->irq);

		if (motg->pdata->pmic_id_irq)
			disable_irq_wake(motg->pdata->pmic_id_irq);
		if (pdata->otg_control == OTG_PHY_CONTROL &&
			pdata->mpm_otgsessvld_int)
			msm_mpm_set_pin_wake(pdata->mpm_otgsessvld_int, 0);
		if (motg->host_bus_suspend && pdata->mpm_dpshv_int)
			msm_mpm_set_pin_wake(pdata->mpm_dpshv_int, 0);
		if (motg->host_bus_suspend && pdata->mpm_dmshv_int)
			msm_mpm_set_pin_wake(pdata->mpm_dmshv_int, 0);
	}
	if (bus)
		set_bit(HCD_FLAG_HW_ACCESSIBLE, &(bus_to_hcd(bus))->flags);

	atomic_set(&motg->in_lpm, 0);

	if (motg->async_int) {
		/* Match the disable_irq call from ISR */
		enable_irq(motg->async_int);
		motg->async_int = 0;
	}
	motg->ui_enabled = 1;
	enable_irq(motg->irq);

	/* If ASYNC IRQ is present then keep it enabled only during LPM */
	if (motg->async_irq)
		disable_irq(motg->async_irq);

	//ASUS_BSP+++ Eric5_Ou "Add monitor to check otg suspend status in suspend mode"
	if (g_host_mode) {
		schedule_delayed_work(&asus_otg_suspend_check_work,
			msecs_to_jiffies(MSM_OTG_SUSPEND_CHECK_TIMEOUT));
	}
	//ASUS_BSP--- Eric5_Ou "Add monitor to check otg suspend status in suspend mode"

	//ASUS_BSP+++ Eric5_Ou "add mutex to protect suspend/resume function"
	mutex_unlock(&msm_otg_mutex);
	//ASUS_BSP--- Eric5_Ou "add mutex to protect suspend/resume function"

	dev_info(phy->dev, "USB exited from low power mode\n");

	return 0;
}
#endif

static void msm_otg_notify_host_mode(struct msm_otg *motg, bool host_mode)
{
	if (!psy) {
		//ASUS_BSP+++ Eric5_Ou "get power supply which registered by charger"
		psy = power_supply_get_by_name("usb");
		if (!psy) {
			pr_err("No USB power supply registered!\n");
			return;
		}
		//ASUS_BSP--- Eric5_Ou "get power supply which registered by charger"
	}

	if (legacy_power_supply) {
		/* legacy support */
		if (host_mode) {
			power_supply_set_scope(psy, POWER_SUPPLY_SCOPE_SYSTEM);
		} else {
			power_supply_set_scope(psy, POWER_SUPPLY_SCOPE_DEVICE);
			/*
			 * VBUS comparator is disabled by PMIC charging driver
			 * when SYSTEM scope is selected.  For ID_GND->ID_A
			 * transition, give 50 msec delay so that PMIC charger
			 * driver detect the VBUS and ready for accepting
			 * charging current value from USB.
			 */
			if (test_bit(ID_A, &motg->inputs))
				msleep(50);
		}
	} else {
		motg->host_mode = host_mode;
		power_supply_changed(psy);
	}
}
#ifndef CONFIG_BATTERY_ASUS
static int msm_otg_notify_chg_type(struct msm_otg *motg)
{
	static int charger_type;

	/*
	 * TODO
	 * Unify OTG driver charger types and power supply charger types
	 */
	if (charger_type == motg->chg_type)
		return 0;

	if (motg->chg_type == USB_SDP_CHARGER)
		charger_type = POWER_SUPPLY_TYPE_USB;
	else if (motg->chg_type == USB_CDP_CHARGER)
		charger_type = POWER_SUPPLY_TYPE_USB_CDP;
	else if (motg->chg_type == USB_DCP_CHARGER ||
			motg->chg_type == USB_PROPRIETARY_CHARGER ||
			motg->chg_type == USB_FLOATED_CHARGER)
		charger_type = POWER_SUPPLY_TYPE_USB_DCP;
	else if ((motg->chg_type == USB_ACA_DOCK_CHARGER ||
		motg->chg_type == USB_ACA_A_CHARGER ||
		motg->chg_type == USB_ACA_B_CHARGER ||
		motg->chg_type == USB_ACA_C_CHARGER))
		charger_type = POWER_SUPPLY_TYPE_USB_ACA;
	else
		charger_type = POWER_SUPPLY_TYPE_UNKNOWN;

	if (!psy) {
		pr_err("No USB power supply registered!\n");
		return -EINVAL;
	}

	pr_debug("setting usb power supply type %d\n", charger_type);
	power_supply_set_supply_type(psy, charger_type);
	return 0;
}
#endif
#ifndef CONFIG_CHARGER_ASUS
static int msm_otg_notify_power_supply(struct msm_otg *motg, unsigned mA)
{
	if (!psy) {
		dev_dbg(motg->phy.dev, "no usb power supply registered\n");
		goto psy_error;
	}

	if (motg->cur_power == 0 && mA > 2) {
		/* Enable charging */
		if (power_supply_set_online(psy, true))
			goto psy_error;
		if (power_supply_set_current_limit(psy, 1000*mA))
			goto psy_error;
	} else if (motg->cur_power > 0 && (mA == 0 || mA == 2)) {
		/* Disable charging */
		if (power_supply_set_online(psy, false))
			goto psy_error;
		/* Set max current limit */
		if (power_supply_set_current_limit(psy, 0))
			goto psy_error;
	} else {
		if (power_supply_set_online(psy, true))
			goto psy_error;
		/* Current has changed (100/2 --> 500) */
		if (power_supply_set_current_limit(psy, 1000*mA))
			goto psy_error;
	}

	power_supply_changed(psy);
	return 0;

psy_error:
	dev_dbg(motg->phy.dev, "power supply error when setting property\n");
	return -ENXIO;
}
#endif
static void msm_otg_notify_charger(struct msm_otg *motg, unsigned mA)
{
	struct usb_gadget *g = motg->phy.otg->gadget;

	if (g && g->is_a_peripheral)
		return;

	if ((motg->chg_type == USB_ACA_DOCK_CHARGER ||
		motg->chg_type == USB_ACA_A_CHARGER ||
		motg->chg_type == USB_ACA_B_CHARGER ||
		motg->chg_type == USB_ACA_C_CHARGER) &&
			mA > IDEV_ACA_CHG_LIMIT)
		mA = IDEV_ACA_CHG_LIMIT;
//ASUS_BSP+++ "[USB][NA][Spec] Add ASUS charger mode support"
#ifndef CONFIG_BATTERY_ASUS
	if (msm_otg_notify_chg_type(motg))
		dev_err(motg->phy.dev,
			"Failed notifying %d charger type to PMIC\n",
							motg->chg_type);
#endif
//ASUS_BSP--- "[USB][NA][Spec] Add ASUS charger mode support"
	if (motg->cur_power == mA)
		return;
//ASUS_BSP+++ "[USB][NA][Spec] Add ASUS charger mode support"
#ifndef CONFIG_CHARGER_ASUS
	dev_info(motg->phy.dev, "Avail curr from USB = %u\n", mA);

	/*
	 *  Use Power Supply API if supported, otherwise fallback
	 *  to legacy pm8921 API.
	 */
	if (msm_otg_notify_power_supply(motg, mA))
		pm8921_charger_vbus_draw(mA);
#endif
//ASUS_BSP--- "[USB][NA][Spec] Add ASUS charger mode support"
	motg->cur_power = mA;
}

static int msm_otg_set_power(struct usb_phy *phy, unsigned mA)
{
	struct msm_otg *motg = container_of(phy, struct msm_otg, phy);

	/*
	 * Gadget driver uses set_power method to notify about the
	 * available current based on suspend/configured states.
	 *
	 * IDEV_CHG can be drawn irrespective of suspend/un-configured
	 * states when CDP/ACA is connected.
	 */
	if (motg->chg_type == USB_SDP_CHARGER)
		msm_otg_notify_charger(motg, mA);

	return 0;
}

static void msm_otg_start_host(struct usb_otg *otg, int on)
{
	struct msm_otg *motg = container_of(otg->phy, struct msm_otg, phy);
	struct msm_otg_platform_data *pdata = motg->pdata;
	struct usb_hcd *hcd;

	if (!otg->host)
		return;

	hcd = bus_to_hcd(otg->host);

	if (on) {
		dev_dbg(otg->phy->dev, "host on\n");

		if (pdata->otg_control == OTG_PHY_CONTROL)
			ulpi_write(otg->phy, OTG_COMP_DISABLE,
				ULPI_SET(ULPI_PWR_CLK_MNG_REG));

		/*
		 * Some boards have a switch cotrolled by gpio
		 * to enable/disable internal HUB. Enable internal
		 * HUB before kicking the host.
		 */
		if (pdata->setup_gpio)
			pdata->setup_gpio(OTG_STATE_A_HOST);
		usb_add_hcd(hcd, hcd->irq, IRQF_SHARED);
	} else {
		dev_dbg(otg->phy->dev, "host off\n");

		usb_remove_hcd(hcd);
		/* HCD core reset all bits of PORTSC. select ULPI phy */
		writel_relaxed(0x80000000, USB_PORTSC);

		if (pdata->setup_gpio)
			pdata->setup_gpio(OTG_STATE_UNDEFINED);

		if (pdata->otg_control == OTG_PHY_CONTROL)
			ulpi_write(otg->phy, OTG_COMP_DISABLE,
				ULPI_CLR(ULPI_PWR_CLK_MNG_REG));
	}
}

static int msm_otg_usbdev_notify(struct notifier_block *self,
			unsigned long action, void *priv)
{
	struct msm_otg *motg = container_of(self, struct msm_otg, usbdev_nb);
	struct usb_otg *otg = motg->phy.otg;
	struct usb_device *udev = priv;

	if (action == USB_BUS_ADD || action == USB_BUS_REMOVE)
		goto out;

	if (udev->bus != otg->host)
		goto out;
	/*
	 * Interested in devices connected directly to the root hub.
	 * ACA dock can supply IDEV_CHG irrespective devices connected
	 * on the accessory port.
	 */
	if (!udev->parent || udev->parent->parent ||
			motg->chg_type == USB_ACA_DOCK_CHARGER)
		goto out;

	switch (action) {
	case USB_DEVICE_ADD:
		if (aca_enabled())
			usb_disable_autosuspend(udev);
		if (otg->phy->state == OTG_STATE_A_WAIT_BCON) {
			pr_debug("B_CONN set\n");
			set_bit(B_CONN, &motg->inputs);
			msm_otg_del_timer(motg);
			otg->phy->state = OTG_STATE_A_HOST;
			/*
			 * OTG PET: A-device must end session within
			 * 10 sec after PET enumeration.
			 */
			if (udev->quirks & USB_QUIRK_OTG_PET)
				msm_otg_start_timer(motg, TA_TST_MAINT,
						A_TST_MAINT);
		}
		/* fall through */
	case USB_DEVICE_CONFIG:
		if (udev->actconfig)
			motg->mA_port = udev->actconfig->desc.bMaxPower * 2;
		else
			motg->mA_port = IUNIT;
		if (otg->phy->state == OTG_STATE_B_HOST)
			msm_otg_del_timer(motg);
		break;
	case USB_DEVICE_REMOVE:
		if ((otg->phy->state == OTG_STATE_A_HOST) ||
			(otg->phy->state == OTG_STATE_A_SUSPEND)) {
			pr_debug("B_CONN clear\n");
			clear_bit(B_CONN, &motg->inputs);
			/*
			 * OTG PET: A-device must end session after
			 * PET disconnection if it is enumerated
			 * with bcdDevice[0] = 1. USB core sets
			 * bus->otg_vbus_off for us. clear it here.
			 */
			if (udev->bus->otg_vbus_off) {
				udev->bus->otg_vbus_off = 0;
				set_bit(A_BUS_DROP, &motg->inputs);
			}
			queue_work(system_nrt_wq, &motg->sm_work);
		}
	default:
		break;
	}
	if (test_bit(ID_A, &motg->inputs))
		msm_otg_notify_charger(motg, IDEV_ACA_CHG_MAX -
				motg->mA_port);
out:
	return NOTIFY_OK;
}

static void msm_hsusb_vbus_power(struct msm_otg *motg, bool on)
{
	int ret;
	static bool vbus_is_on;

	if (vbus_is_on == on)
		return;

	if (motg->pdata->vbus_power) {
		ret = motg->pdata->vbus_power(on);
		if (!ret)
			vbus_is_on = on;
		return;
	}

	if (!vbus_otg) {
		pr_err("vbus_otg is NULL.");
		return;
	}

	//ASUS_BSP+++ Eric5_Ou "add otg 5v output debug file"
	g_otg_5v_output = on;
	//ASUS_BSP--- Eric5_Ou "add otg 5v output debug file"

	/*
	 * if entering host mode tell the charger to not draw any current
	 * from usb before turning on the boost.
	 * if exiting host mode disable the boost before enabling to draw
	 * current from the source.
	 */
	if (on) {
		msm_otg_notify_host_mode(motg, on);
		ret = regulator_enable(vbus_otg);
		if (ret) {
			pr_err("unable to enable vbus_otg\n");
			return;
		}
		vbus_is_on = true;
	} else {
		ret = regulator_disable(vbus_otg);
		if (ret) {
			pr_err("unable to disable vbus_otg\n");
			return;
		}
		msm_otg_notify_host_mode(motg, on);
		vbus_is_on = false;
	}

	printk("[vbus_otg] vbus power output is %s\n", vbus_is_on ? "enable" : "disable");
}

static int msm_otg_set_host(struct usb_otg *otg, struct usb_bus *host)
{
	struct msm_otg *motg = container_of(otg->phy, struct msm_otg, phy);
	struct usb_hcd *hcd;

	/*
	 * Fail host registration if this board can support
	 * only peripheral configuration.
	 */
	if (motg->pdata->mode == USB_PERIPHERAL) {
		dev_info(otg->phy->dev, "Host mode is not supported\n");
		return -ENODEV;
	}

	if (!motg->pdata->vbus_power && host) {
		vbus_otg = devm_regulator_get(motg->phy.dev, "vbus_otg");
		if (IS_ERR(vbus_otg)) {
			pr_err("Unable to get vbus_otg\n");
			return PTR_ERR(vbus_otg);
		}
	}

	if (!host) {
		if (otg->phy->state == OTG_STATE_A_HOST) {
			pm_runtime_get_sync(otg->phy->dev);
			//ASUS_BSP+++ Eric5_Ou "Add PM usage count debug log and protection mechanism"
			otg_pm_count+= 1;
			printk("[OTG-PM][%s][1] get usage_count:%d, otg_pm_count:%d\n",__func__,atomic_read(&otg->phy->dev->power.usage_count),otg_pm_count);
			//ASUS_BSP--- Eric5_Ou "Add PM usage count debug log and protection mechanism"
			usb_unregister_notify(&motg->usbdev_nb);
			msm_otg_start_host(otg, 0);
			msm_hsusb_vbus_power(motg, 0);
			otg->host = NULL;
			otg->phy->state = OTG_STATE_UNDEFINED;
			queue_work(system_nrt_wq, &motg->sm_work);
		} else {
			otg->host = NULL;
		}

		return 0;
	}

	hcd = bus_to_hcd(host);
	hcd->power_budget = motg->pdata->power_budget;

#ifdef CONFIG_USB_OTG
	host->otg_port = 1;
#endif
	motg->usbdev_nb.notifier_call = msm_otg_usbdev_notify;
	usb_register_notify(&motg->usbdev_nb);
	otg->host = host;
	dev_dbg(otg->phy->dev, "host driver registered w/ tranceiver\n");

	/*
	 * Kick the state machine work, if peripheral is not supported
	 * or peripheral is already registered with us.
	 */
	if (motg->pdata->mode == USB_HOST || otg->gadget) {
		pm_runtime_get_sync(otg->phy->dev);
		//ASUS_BSP+++ Eric5_Ou "Add PM usage count debug log and protection mechanism"
		otg_pm_count+= 1;
		printk("[OTG-PM][%s][2] get usage_count:%d, otg_pm_count:%d\n",__func__, atomic_read(&otg->phy->dev->power.usage_count),otg_pm_count);
		//ASUS_BSP--- Eric5_Ou "Add PM usage count debug log and protection mechanism"
		queue_work(system_nrt_wq, &motg->sm_work);
	}

	return 0;
}

static void msm_otg_start_peripheral(struct usb_otg *otg, int on)
{
	struct msm_otg *motg = container_of(otg->phy, struct msm_otg, phy);
	struct msm_otg_platform_data *pdata = motg->pdata;

	if (!otg->gadget)
		return;

	if (on) {
		dev_dbg(otg->phy->dev, "gadget on\n");
		/*
		 * Some boards have a switch cotrolled by gpio
		 * to enable/disable internal HUB. Disable internal
		 * HUB before kicking the gadget.
		 */
		if (pdata->setup_gpio)
			pdata->setup_gpio(OTG_STATE_B_PERIPHERAL);

		/* Configure BUS performance parameters for MAX bandwidth */
		if (debug_bus_voting_enabled)
			msm_otg_bus_vote(motg, USB_MAX_PERF_VOTE);

		usb_gadget_vbus_connect(otg->gadget);
	} else {
		dev_dbg(otg->phy->dev, "gadget off\n");
		usb_gadget_vbus_disconnect(otg->gadget);
		/* Configure BUS performance parameters to default */
		msm_otg_bus_vote(motg, USB_MIN_PERF_VOTE);
		if (pdata->setup_gpio)
			pdata->setup_gpio(OTG_STATE_UNDEFINED);
	}
}

static int msm_otg_set_peripheral(struct usb_otg *otg,
					struct usb_gadget *gadget)
{
	struct msm_otg *motg = container_of(otg->phy, struct msm_otg, phy);

	/*
	 * Fail peripheral registration if this board can support
	 * only host configuration.
	 */
	if (motg->pdata->mode == USB_HOST) {
		dev_info(otg->phy->dev, "Peripheral mode is not supported\n");
		return -ENODEV;
	}

	if (!gadget) {
		if (otg->phy->state == OTG_STATE_B_PERIPHERAL) {
			pm_runtime_get_sync(otg->phy->dev);
			//ASUS_BSP+++ Eric5_Ou "Add PM usage count debug log and protection mechanism"
			otg_pm_count+= 1;
			printk("[OTG-PM][%s][1] get usage_count:%d, otg_pm_count:%d\n",__func__, atomic_read(&otg->phy->dev->power.usage_count),otg_pm_count);
			//ASUS_BSP--- Eric5_Ou "Add PM usage count debug log and protection mechanism"
			msm_otg_start_peripheral(otg, 0);
			otg->gadget = NULL;
			otg->phy->state = OTG_STATE_UNDEFINED;
			queue_work(system_nrt_wq, &motg->sm_work);
		} else {
			otg->gadget = NULL;
		}

		return 0;
	}
	otg->gadget = gadget;
	dev_dbg(otg->phy->dev, "peripheral driver registered w/ tranceiver\n");

	/*
	 * Kick the state machine work, if host is not supported
	 * or host is already registered with us.
	 */
	if (motg->pdata->mode == USB_PERIPHERAL || otg->host) {
		pm_runtime_get_sync(otg->phy->dev);
		//ASUS_BSP+++ Eric5_Ou "Add PM usage count debug log and protection mechanism"
		otg_pm_count+= 1;
		printk("[OTG-PM][%s][2] get usage_count:%d, otg_pm_count:%d\n",__func__, atomic_read(&otg->phy->dev->power.usage_count),otg_pm_count);
		//ASUS_BSP--- Eric5_Ou "Add PM usage count debug log and protection mechanism"
		queue_work(system_nrt_wq, &motg->sm_work);
	}

	return 0;
}

static bool msm_otg_read_pmic_id_state(struct msm_otg *motg)
{
	unsigned long flags;
	int id;

	if (!motg->pdata->pmic_id_irq)
		return -ENODEV;

	local_irq_save(flags);
	id = irq_read_line(motg->pdata->pmic_id_irq);
	local_irq_restore(flags);

	/*
	 * If we can not read ID line state for some reason, treat
	 * it as float. This would prevent MHL discovery and kicking
	 * host mode unnecessarily.
	 */
	return !!id;
}

static int msm_otg_mhl_register_callback(struct msm_otg *motg,
						void (*callback)(int on))
{
	struct usb_phy *phy = &motg->phy;
	int ret;

	if (!motg->pdata->mhl_enable) {
		dev_dbg(phy->dev, "MHL feature not enabled\n");
		return -ENODEV;
	}

	if (motg->pdata->otg_control != OTG_PMIC_CONTROL ||
			!motg->pdata->pmic_id_irq) {
		dev_dbg(phy->dev, "MHL can not be supported without PMIC Id\n");
		return -ENODEV;
	}

	if (!motg->pdata->mhl_dev_name) {
		dev_dbg(phy->dev, "MHL device name does not exist.\n");
		return -ENODEV;
	}

	if (callback)
		ret = mhl_register_callback(motg->pdata->mhl_dev_name,
								callback);
	else
		ret = mhl_unregister_callback(motg->pdata->mhl_dev_name);

	if (ret)
		dev_dbg(phy->dev, "mhl_register_callback(%s) return error=%d\n",
						motg->pdata->mhl_dev_name, ret);
	else
		motg->mhl_enabled = true;

	return ret;
}

static void msm_otg_mhl_notify_online(int on)
{
	struct msm_otg *motg = the_msm_otg;
	struct usb_phy *phy = &motg->phy;
	bool queue = false;

	dev_dbg(phy->dev, "notify MHL %s%s\n", on ? "" : "dis", "connected");

	if (on) {
		set_bit(MHL, &motg->inputs);
	} else {
		clear_bit(MHL, &motg->inputs);
		queue = true;
	}

	if (queue && phy->state != OTG_STATE_UNDEFINED)
		schedule_work(&motg->sm_work);
}

static bool msm_otg_is_mhl(struct msm_otg *motg)
{
	struct usb_phy *phy = &motg->phy;
	int is_mhl, ret;

	ret = mhl_device_discovery(motg->pdata->mhl_dev_name, &is_mhl);
	if (ret || is_mhl != MHL_DISCOVERY_RESULT_MHL) {
		/*
		 * MHL driver calls our callback saying that MHL connected
		 * if RID_GND is detected.  But at later part of discovery
		 * it may figure out MHL is not connected and returns
		 * false. Hence clear MHL input here.
		 */
		clear_bit(MHL, &motg->inputs);
		dev_dbg(phy->dev, "MHL device not found\n");
		return false;
	}

	set_bit(MHL, &motg->inputs);
	dev_dbg(phy->dev, "MHL device found\n");
	return true;
}

static bool msm_chg_mhl_detect(struct msm_otg *motg)
{
	bool ret, id;

	if (!motg->mhl_enabled)
		return false;

	id = msm_otg_read_pmic_id_state(motg);

	if (id)
		return false;

	mhl_det_in_progress = true;
	ret = msm_otg_is_mhl(motg);
	mhl_det_in_progress = false;

	return ret;
}

static void msm_otg_chg_check_timer_func(unsigned long data)
{
	struct msm_otg *motg = (struct msm_otg *) data;
	struct usb_otg *otg = motg->phy.otg;

	if (atomic_read(&motg->in_lpm) ||
		!test_bit(B_SESS_VLD, &motg->inputs) ||
		otg->phy->state != OTG_STATE_B_PERIPHERAL ||
		otg->gadget->speed != USB_SPEED_UNKNOWN) {
		dev_dbg(otg->phy->dev, "Nothing to do in chg_check_timer\n");
		return;
	}

	if ((readl_relaxed(USB_PORTSC) & PORTSC_LS) == PORTSC_LS) {
		dev_dbg(otg->phy->dev, "DCP is detected as SDP\n");
		set_bit(B_FALSE_SDP, &motg->inputs);
		queue_work(system_nrt_wq, &motg->sm_work);
	}
}

static bool msm_chg_aca_detect(struct msm_otg *motg)
{
	struct usb_phy *phy = &motg->phy;
	u32 int_sts;
	bool ret = false;

	if (!aca_enabled())
		goto out;

	if (motg->pdata->phy_type == CI_45NM_INTEGRATED_PHY)
		goto out;

	int_sts = ulpi_read(phy, 0x87);
	switch (int_sts & 0x1C) {
	case 0x08:
		if (!test_and_set_bit(ID_A, &motg->inputs)) {
			dev_dbg(phy->dev, "ID_A\n");
			motg->chg_type = USB_ACA_A_CHARGER;
			motg->chg_state = USB_CHG_STATE_DETECTED;
			clear_bit(ID_B, &motg->inputs);
			clear_bit(ID_C, &motg->inputs);
			set_bit(ID, &motg->inputs);
			ret = true;
		}
		break;
	case 0x0C:
		if (!test_and_set_bit(ID_B, &motg->inputs)) {
			dev_dbg(phy->dev, "ID_B\n");
			motg->chg_type = USB_ACA_B_CHARGER;
			motg->chg_state = USB_CHG_STATE_DETECTED;
			clear_bit(ID_A, &motg->inputs);
			clear_bit(ID_C, &motg->inputs);
			set_bit(ID, &motg->inputs);
			ret = true;
		}
		break;
	case 0x10:
		if (!test_and_set_bit(ID_C, &motg->inputs)) {
			dev_dbg(phy->dev, "ID_C\n");
			motg->chg_type = USB_ACA_C_CHARGER;
			motg->chg_state = USB_CHG_STATE_DETECTED;
			clear_bit(ID_A, &motg->inputs);
			clear_bit(ID_B, &motg->inputs);
			set_bit(ID, &motg->inputs);
			ret = true;
		}
		break;
	case 0x04:
		if (test_and_clear_bit(ID, &motg->inputs)) {
			dev_dbg(phy->dev, "ID_GND\n");
			motg->chg_type = USB_INVALID_CHARGER;
			motg->chg_state = USB_CHG_STATE_UNDEFINED;
			clear_bit(ID_A, &motg->inputs);
			clear_bit(ID_B, &motg->inputs);
			clear_bit(ID_C, &motg->inputs);
			ret = true;
		}
		break;
	default:
		ret = test_and_clear_bit(ID_A, &motg->inputs) |
			test_and_clear_bit(ID_B, &motg->inputs) |
			test_and_clear_bit(ID_C, &motg->inputs) |
			!test_and_set_bit(ID, &motg->inputs);
		if (ret) {
			dev_dbg(phy->dev, "ID A/B/C/GND is no more\n");
			motg->chg_type = USB_INVALID_CHARGER;
			motg->chg_state = USB_CHG_STATE_UNDEFINED;
		}
	}
out:
	return ret;
}

static void msm_chg_enable_aca_det(struct msm_otg *motg)
{
	struct usb_phy *phy = &motg->phy;

	if (!aca_enabled())
		return;

	switch (motg->pdata->phy_type) {
	case SNPS_28NM_INTEGRATED_PHY:
		/* Disable ID_GND in link and PHY */
		writel_relaxed(readl_relaxed(USB_OTGSC) & ~(OTGSC_IDPU |
				OTGSC_IDIE), USB_OTGSC);
		ulpi_write(phy, 0x01, 0x0C);
		ulpi_write(phy, 0x10, 0x0F);
		ulpi_write(phy, 0x10, 0x12);
		/* Disable PMIC ID pull-up */
		pm8xxx_usb_id_pullup(0);
		/* Enable ACA ID detection */
		ulpi_write(phy, 0x20, 0x85);
		aca_id_turned_on = true;
		break;
	default:
		break;
	}
}

static void msm_chg_enable_aca_intr(struct msm_otg *motg)
{
	struct usb_phy *phy = &motg->phy;

	if (!aca_enabled())
		return;

	switch (motg->pdata->phy_type) {
	case SNPS_28NM_INTEGRATED_PHY:
		/* Enable ACA Detection interrupt (on any RID change) */
		ulpi_write(phy, 0x01, 0x94);
		break;
	default:
		break;
	}
}

static void msm_chg_disable_aca_intr(struct msm_otg *motg)
{
	struct usb_phy *phy = &motg->phy;

	if (!aca_enabled())
		return;

	switch (motg->pdata->phy_type) {
	case SNPS_28NM_INTEGRATED_PHY:
		ulpi_write(phy, 0x01, 0x95);
		break;
	default:
		break;
	}
}

static bool msm_chg_check_aca_intr(struct msm_otg *motg)
{
	struct usb_phy *phy = &motg->phy;
	bool ret = false;

	if (!aca_enabled())
		return ret;

	switch (motg->pdata->phy_type) {
	case SNPS_28NM_INTEGRATED_PHY:
		if (ulpi_read(phy, 0x91) & 1) {
			dev_dbg(phy->dev, "RID change\n");
			ulpi_write(phy, 0x01, 0x92);
			ret = msm_chg_aca_detect(motg);
		}
	default:
		break;
	}
	return ret;
}

static void msm_otg_id_timer_func(unsigned long data)
{
	struct msm_otg *motg = (struct msm_otg *) data;

	if (!aca_enabled())
		return;

	if (atomic_read(&motg->in_lpm)) {
		dev_dbg(motg->phy.dev, "timer: in lpm\n");
		return;
	}

	if (motg->phy.state == OTG_STATE_A_SUSPEND)
		goto out;

	if (msm_chg_check_aca_intr(motg)) {
		dev_dbg(motg->phy.dev, "timer: aca work\n");
		queue_work(system_nrt_wq, &motg->sm_work);
	}

out:
	if (!test_bit(ID, &motg->inputs) || test_bit(ID_A, &motg->inputs))
		mod_timer(&motg->id_timer, ID_TIMER_FREQ);
}

static bool msm_chg_check_secondary_det(struct msm_otg *motg)
{
	struct usb_phy *phy = &motg->phy;
	u32 chg_det;
	bool ret = false;

	switch (motg->pdata->phy_type) {
	case CI_45NM_INTEGRATED_PHY:
		chg_det = ulpi_read(phy, 0x34);
		ret = chg_det & (1 << 4);
		break;
	case SNPS_28NM_INTEGRATED_PHY:
		chg_det = ulpi_read(phy, 0x87);
		ret = chg_det & 1;
		break;
	default:
		break;
	}
	return ret;
}

static void msm_chg_enable_secondary_det(struct msm_otg *motg)
{
	struct usb_phy *phy = &motg->phy;
	u32 chg_det;

	switch (motg->pdata->phy_type) {
	case CI_45NM_INTEGRATED_PHY:
		chg_det = ulpi_read(phy, 0x34);
		/* Turn off charger block */
		chg_det |= ~(1 << 1);
		ulpi_write(phy, chg_det, 0x34);
		udelay(20);
		/* control chg block via ULPI */
		chg_det &= ~(1 << 3);
		ulpi_write(phy, chg_det, 0x34);
		/* put it in host mode for enabling D- source */
		chg_det &= ~(1 << 2);
		ulpi_write(phy, chg_det, 0x34);
		/* Turn on chg detect block */
		chg_det &= ~(1 << 1);
		ulpi_write(phy, chg_det, 0x34);
		udelay(20);
		/* enable chg detection */
		chg_det &= ~(1 << 0);
		ulpi_write(phy, chg_det, 0x34);
		break;
	case SNPS_28NM_INTEGRATED_PHY:
		/*
		 * Configure DM as current source, DP as current sink
		 * and enable battery charging comparators.
		 */
		ulpi_write(phy, 0x8, 0x85);
		ulpi_write(phy, 0x2, 0x85);
		ulpi_write(phy, 0x1, 0x85);
		break;
	default:
		break;
	}
}

static bool msm_chg_check_primary_det(struct msm_otg *motg)
{
	struct usb_phy *phy = &motg->phy;
	u32 chg_det;
	bool ret = false;

	switch (motg->pdata->phy_type) {
	case CI_45NM_INTEGRATED_PHY:
		chg_det = ulpi_read(phy, 0x34);
		ret = chg_det & (1 << 4);
		break;
	case SNPS_28NM_INTEGRATED_PHY:
		chg_det = ulpi_read(phy, 0x87);
		ret = chg_det & 1;
		/* Turn off VDP_SRC */
		ulpi_write(phy, 0x3, 0x86);
		msleep(20);
		break;
	default:
		break;
	}
	return ret;
}

static void msm_chg_enable_primary_det(struct msm_otg *motg)
{
	struct usb_phy *phy = &motg->phy;
	u32 chg_det;

	switch (motg->pdata->phy_type) {
	case CI_45NM_INTEGRATED_PHY:
		chg_det = ulpi_read(phy, 0x34);
		/* enable chg detection */
		chg_det &= ~(1 << 0);
		ulpi_write(phy, chg_det, 0x34);
		break;
	case SNPS_28NM_INTEGRATED_PHY:
		/*
		 * Configure DP as current source, DM as current sink
		 * and enable battery charging comparators.
		 */
		ulpi_write(phy, 0x2, 0x85);
		ulpi_write(phy, 0x1, 0x85);
		break;
	default:
		break;
	}
}

static bool msm_chg_check_dcd(struct msm_otg *motg)
{
	struct usb_phy *phy = &motg->phy;
	u32 line_state;
	bool ret = false;

	switch (motg->pdata->phy_type) {
	case CI_45NM_INTEGRATED_PHY:
		line_state = ulpi_read(phy, 0x15);
		ret = !(line_state & 1);
		break;
	case SNPS_28NM_INTEGRATED_PHY:
		line_state = ulpi_read(phy, 0x87);
		ret = line_state & 2;
		break;
	default:
		break;
	}
	return ret;
}

static void msm_chg_disable_dcd(struct msm_otg *motg)
{
	struct usb_phy *phy = &motg->phy;
	u32 chg_det;

	switch (motg->pdata->phy_type) {
	case CI_45NM_INTEGRATED_PHY:
		chg_det = ulpi_read(phy, 0x34);
		chg_det &= ~(1 << 5);
		ulpi_write(phy, chg_det, 0x34);
		break;
	case SNPS_28NM_INTEGRATED_PHY:
		ulpi_write(phy, 0x10, 0x86);
		break;
	default:
		break;
	}
}

static void msm_chg_enable_dcd(struct msm_otg *motg)
{
	struct usb_phy *phy = &motg->phy;
	u32 chg_det;

	switch (motg->pdata->phy_type) {
	case CI_45NM_INTEGRATED_PHY:
		chg_det = ulpi_read(phy, 0x34);
		/* Turn on D+ current source */
		chg_det |= (1 << 5);
		ulpi_write(phy, chg_det, 0x34);
		break;
	case SNPS_28NM_INTEGRATED_PHY:
		/* Data contact detection enable */
		ulpi_write(phy, 0x10, 0x85);
		break;
	default:
		break;
	}
}

static void msm_chg_block_on(struct msm_otg *motg)
{
	struct usb_phy *phy = &motg->phy;
	u32 func_ctrl, chg_det;

	/* put the controller in non-driving mode */
	func_ctrl = ulpi_read(phy, ULPI_FUNC_CTRL);
	func_ctrl &= ~ULPI_FUNC_CTRL_OPMODE_MASK;
	func_ctrl |= ULPI_FUNC_CTRL_OPMODE_NONDRIVING;
	ulpi_write(phy, func_ctrl, ULPI_FUNC_CTRL);

	switch (motg->pdata->phy_type) {
	case CI_45NM_INTEGRATED_PHY:
		chg_det = ulpi_read(phy, 0x34);
		/* control chg block via ULPI */
		chg_det &= ~(1 << 3);
		ulpi_write(phy, chg_det, 0x34);
		/* Turn on chg detect block */
		chg_det &= ~(1 << 1);
		ulpi_write(phy, chg_det, 0x34);
		udelay(20);
		break;
	case SNPS_28NM_INTEGRATED_PHY:
		/* disable DP and DM pull down resistors */
		ulpi_write(phy, 0x6, 0xC);
		/* Clear charger detecting control bits */
		ulpi_write(phy, 0x1F, 0x86);
		/* Clear alt interrupt latch and enable bits */
		ulpi_write(phy, 0x1F, 0x92);
		ulpi_write(phy, 0x1F, 0x95);
		udelay(100);
		break;
	default:
		break;
	}
}

static void msm_chg_block_off(struct msm_otg *motg)
{
	struct usb_phy *phy = &motg->phy;
	u32 func_ctrl, chg_det;

	switch (motg->pdata->phy_type) {
	case CI_45NM_INTEGRATED_PHY:
		chg_det = ulpi_read(phy, 0x34);
		/* Turn off charger block */
		chg_det |= ~(1 << 1);
		ulpi_write(phy, chg_det, 0x34);
		break;
	case SNPS_28NM_INTEGRATED_PHY:
		/* Clear charger detecting control bits */
		ulpi_write(phy, 0x3F, 0x86);
		/* Clear alt interrupt latch and enable bits */
		ulpi_write(phy, 0x1F, 0x92);
		ulpi_write(phy, 0x1F, 0x95);
		/* re-enable DP and DM pull down resistors */
		ulpi_write(phy, 0x6, 0xB);
		break;
	default:
		break;
	}

	/* put the controller in normal mode */
	func_ctrl = ulpi_read(phy, ULPI_FUNC_CTRL);
	func_ctrl &= ~ULPI_FUNC_CTRL_OPMODE_MASK;
	func_ctrl |= ULPI_FUNC_CTRL_OPMODE_NORMAL;
	ulpi_write(phy, func_ctrl, ULPI_FUNC_CTRL);
}

static const char *chg_to_string(enum usb_chg_type chg_type)
{
	switch (chg_type) {
	case USB_SDP_CHARGER:		return "USB_SDP_CHARGER";
	case USB_DCP_CHARGER:		return "USB_DCP_CHARGER";
	case USB_CDP_CHARGER:		return "USB_CDP_CHARGER";
	case USB_ACA_A_CHARGER:		return "USB_ACA_A_CHARGER";
	case USB_ACA_B_CHARGER:		return "USB_ACA_B_CHARGER";
	case USB_ACA_C_CHARGER:		return "USB_ACA_C_CHARGER";
	case USB_ACA_DOCK_CHARGER:	return "USB_ACA_DOCK_CHARGER";
	case USB_PROPRIETARY_CHARGER:	return "USB_PROPRIETARY_CHARGER";
	case USB_FLOATED_CHARGER:	return "USB_FLOATED_CHARGER";
	default:			return "INVALID_CHARGER";
	}
}

#define MSM_CHG_DCD_TIMEOUT		(750 * HZ/1000) /* 750 msec */
#define MSM_CHG_DCD_POLL_TIME		(50 * HZ/1000) /* 50 msec */
#define MSM_CHG_PRIMARY_DET_TIME	(50 * HZ/1000) /* TVDPSRC_ON */
#define MSM_CHG_SECONDARY_DET_TIME	(50 * HZ/1000) /* TVDMSRC_ON */

//ASUS_BSP+++ "[USB][NA][Spec] Add ASUS charger mode support"
#ifdef CONFIG_CHARGER_ASUS
static void asus_usb_detect_work(struct work_struct *w)
{
	cancel_delayed_work_sync(&asus_chg_work);
	g_charger_mode = ASUS_CHG_SRC_USB;
	asus_chg_set_chg_mode(ASUS_CHG_SRC_USB);
	printk("[USB] set_chg_mode: USB\n");
}
static void asus_chg_detect_work(struct work_struct *w)
{
	struct msm_otg *motg = the_msm_otg;
	if(g_usb_boot == MSM_OTG_USB_BOOT_DOWN){
		if(msm_otg_bsv){
			g_charger_mode = ASUS_CHG_SRC_UNKNOWN;
			asus_chg_set_chg_mode(ASUS_CHG_SRC_UNKNOWN);
			printk("[USB] set_chg_mode: UNKNOWN\n");
		}
		else{
			printk("[USB] asus_chg_detect_work: BSV is not set,need re-check.(%d,%d)\n",msm_otg_bsv,test_bit(B_SESS_VLD, &motg->inputs));
			queue_work(system_nrt_wq, &motg->sm_work);
		}
	}else{
		printk("[USB] asus_chg_detect_work: g_usb_boot is %d , add more 2 sec\n",g_usb_boot);
		if(g_usb_boot == MSM_OTG_USB_BOOT_IRQ){
			g_usb_boot = MSM_OTG_USB_BOOT_DOWN;
		}
		schedule_delayed_work(&asus_chg_work, (2000 * HZ/1000));
	}
}
#endif
//ASUS_BSP--- "[USB][NA][Spec] Add ASUS charger mode support"

static void msm_chg_detect_work(struct work_struct *w)
{
	struct msm_otg *motg = container_of(w, struct msm_otg, chg_work.work);
	struct usb_phy *phy = &motg->phy;
	bool is_dcd = false, tmout, vout, is_aca;
	static bool dcd;
	u32 line_state, dm_vlgc;
	unsigned long delay;

	dev_dbg(phy->dev, "chg detection work\n");

	if (test_bit(MHL, &motg->inputs)) {
		dev_dbg(phy->dev, "detected MHL, escape chg detection work\n");
		return;
	}

	switch (motg->chg_state) {
	case USB_CHG_STATE_UNDEFINED:
		msm_chg_block_on(motg);
		msm_chg_enable_dcd(motg);
		msm_chg_enable_aca_det(motg);
		motg->chg_state = USB_CHG_STATE_WAIT_FOR_DCD;
		motg->dcd_time = 0;
		delay = MSM_CHG_DCD_POLL_TIME;
		break;
	case USB_CHG_STATE_WAIT_FOR_DCD:
		if (msm_chg_mhl_detect(motg)) {
			msm_chg_block_off(motg);
			motg->chg_state = USB_CHG_STATE_DETECTED;
			motg->chg_type = USB_INVALID_CHARGER;
			queue_work(system_nrt_wq, &motg->sm_work);
			return;
		}
		is_aca = msm_chg_aca_detect(motg);
		if (is_aca) {
			/*
			 * ID_A can be ACA dock too. continue
			 * primary detection after DCD.
			 */
			if (test_bit(ID_A, &motg->inputs)) {
				motg->chg_state = USB_CHG_STATE_WAIT_FOR_DCD;
			} else {
				delay = 0;
				break;
			}
		}
		is_dcd = msm_chg_check_dcd(motg);
		motg->dcd_time += MSM_CHG_DCD_POLL_TIME;
		tmout = motg->dcd_time >= MSM_CHG_DCD_TIMEOUT;
		if (is_dcd || tmout) {
			if (is_dcd)
				dcd = true;
			else
				dcd = false;
			msm_chg_disable_dcd(motg);
			msm_chg_enable_primary_det(motg);
			delay = MSM_CHG_PRIMARY_DET_TIME;
			motg->chg_state = USB_CHG_STATE_DCD_DONE;
		} else {
			delay = MSM_CHG_DCD_POLL_TIME;
		}
		break;
	case USB_CHG_STATE_DCD_DONE:
		vout = msm_chg_check_primary_det(motg);
		line_state = readl_relaxed(USB_PORTSC) & PORTSC_LS;
		dm_vlgc = line_state & PORTSC_LS_DM;
		if (vout && !dm_vlgc) { /* VDAT_REF < DM < VLGC */
			if (test_bit(ID_A, &motg->inputs)) {
				motg->chg_type = USB_ACA_DOCK_CHARGER;
				motg->chg_state = USB_CHG_STATE_DETECTED;
				delay = 0;
				break;
			}
			if (line_state) { /* DP > VLGC */
				motg->chg_type = USB_PROPRIETARY_CHARGER;
				motg->chg_state = USB_CHG_STATE_DETECTED;
				delay = 0;
			} else {
				msm_chg_enable_secondary_det(motg);
				delay = MSM_CHG_SECONDARY_DET_TIME;
				motg->chg_state = USB_CHG_STATE_PRIMARY_DONE;
			}
		} else { /* DM < VDAT_REF || DM > VLGC */
			if (test_bit(ID_A, &motg->inputs)) {
				motg->chg_type = USB_ACA_A_CHARGER;
				motg->chg_state = USB_CHG_STATE_DETECTED;
				delay = 0;
				break;
			}

			if (line_state) /* DP > VLGC or/and DM > VLGC */
				motg->chg_type = USB_PROPRIETARY_CHARGER;
			else if (!dcd && floated_charger_enable)
				motg->chg_type = USB_FLOATED_CHARGER;
			else
				motg->chg_type = USB_SDP_CHARGER;

			motg->chg_state = USB_CHG_STATE_DETECTED;
			delay = 0;
		}
		break;
	case USB_CHG_STATE_PRIMARY_DONE:
		vout = msm_chg_check_secondary_det(motg);
		if (vout)
			motg->chg_type = USB_DCP_CHARGER;
		else
			motg->chg_type = USB_CDP_CHARGER;
		motg->chg_state = USB_CHG_STATE_SECONDARY_DONE;
		/* fall through */
	case USB_CHG_STATE_SECONDARY_DONE:
		motg->chg_state = USB_CHG_STATE_DETECTED;
	case USB_CHG_STATE_DETECTED:
		/*
		 * Notify the charger type to power supply
		 * owner as soon as we determine the charger.
		 */
#ifndef CONFIG_BATTERY_ASUS
		msm_otg_notify_chg_type(motg);
#endif
		msm_chg_block_off(motg);
		msm_chg_enable_aca_det(motg);
		/*
		 * Spurious interrupt is seen after enabling ACA detection
		 * due to which charger detection fails in case of PET.
		 * Add delay of 100 microsec to avoid that.
		 */
		udelay(100);
		msm_chg_enable_aca_intr(motg);
		dev_dbg(phy->dev, "chg_type = %s\n",
			chg_to_string(motg->chg_type));
		queue_work(system_nrt_wq, &motg->sm_work);
//ASUS_BSP+++ "[USB][NA][Spec] Add ASUS charger mode support"
#ifdef CONFIG_CHARGER_ASUS
		if(motg->chg_type != USB_SDP_CHARGER){
			asus_chg_set_chg_mode(ASUS_CHG_SRC_DC);
			printk("[USB] set_chg_mode: ASUS AC\n");
		}
		else{
			if(g_usb_boot == MSM_OTG_USB_BOOT_IRQ){
				g_usb_boot = MSM_OTG_USB_BOOT_DOWN;
			}
			//wait 2 sec to check non-asus charger
			printk("[USB] asus_chg_work: wait 2 sec to check non-asus charger\n");
			schedule_delayed_work(&asus_chg_work, (2000 * HZ/1000));
		}
#endif
//ASUS_BSP--- "[USB][NA][Spec] Add ASUS charger mode support"
		return;
	default:
		return;
	}

	queue_delayed_work(system_nrt_wq, &motg->chg_work, delay);
}

/*
 * We support OTG, Peripheral only and Host only configurations. In case
 * of OTG, mode switch (host-->peripheral/peripheral-->host) can happen
 * via Id pin status or user request (debugfs). Id/BSV interrupts are not
 * enabled when switch is controlled by user and default mode is supplied
 * by board file, which can be changed by userspace later.
 */
static void msm_otg_init_sm(struct msm_otg *motg)
{
	struct msm_otg_platform_data *pdata = motg->pdata;
	u32 otgsc = readl(USB_OTGSC);

	switch (pdata->mode) {
	case USB_OTG:
		if (pdata->otg_control == OTG_USER_CONTROL) {
			if (pdata->default_mode == USB_HOST) {
				clear_bit(ID, &motg->inputs);
			} else if (pdata->default_mode == USB_PERIPHERAL) {
				set_bit(ID, &motg->inputs);
				set_bit(B_SESS_VLD, &motg->inputs);
			} else {
				set_bit(ID, &motg->inputs);
				clear_bit(B_SESS_VLD, &motg->inputs);
			}
		} else if (pdata->otg_control == OTG_PHY_CONTROL) {
			if (otgsc & OTGSC_ID) {
				set_bit(ID, &motg->inputs);
			} else {
				clear_bit(ID, &motg->inputs);
				set_bit(A_BUS_REQ, &motg->inputs);
			}
			if (otgsc & OTGSC_BSV)
				set_bit(B_SESS_VLD, &motg->inputs);
			else
				clear_bit(B_SESS_VLD, &motg->inputs);
		} else if (pdata->otg_control == OTG_PMIC_CONTROL) {
			if (pdata->pmic_id_irq) {
				//ASUS_BSP+++ Eric5_Ou "add phone mode usb OTG support at boot"
				if (msm_otg_read_pmic_id_state(motg)) {
					printk("[usb_otg] switch to peripheral mode\r\n");
					set_bit(ID, &motg->inputs);
					g_host_mode = 0;
				} else {
					printk("[usb_otg] switch to host mode\r\n");
					clear_bit(ID, &motg->inputs);
					g_host_mode = 1;
				}
				//ASUS_BSP--- Eric5_Ou "add phone mode usb OTG support at boot"
			}
			/*
			 * VBUS initial state is reported after PMIC
			 * driver initialization. Wait for it.
			 */
			wait_for_completion(&pmic_vbus_init);
		}
		break;
	case USB_HOST:
		clear_bit(ID, &motg->inputs);
		break;
	case USB_PERIPHERAL:
		set_bit(ID, &motg->inputs);
		if (pdata->otg_control == OTG_PHY_CONTROL) {
			if (otgsc & OTGSC_BSV)
				set_bit(B_SESS_VLD, &motg->inputs);
			else
				clear_bit(B_SESS_VLD, &motg->inputs);
		} else if (pdata->otg_control == OTG_PMIC_CONTROL) {
			/*
			 * VBUS initial state is reported after PMIC
			 * driver initialization. Wait for it.
			 */
			wait_for_completion(&pmic_vbus_init);
		}
		break;
	default:
		break;
	}
}

static void msm_otg_wait_for_ext_chg_done(struct msm_otg *motg)
{
	struct usb_phy *phy = &motg->phy;
	unsigned long t;

	/*
	 * Defer next cable connect event till external charger
	 * detection is completed.
	 */

	if (motg->ext_chg_active) {

		pr_debug("before msm_otg ext chg wait\n");

		t = wait_for_completion_timeout(&motg->ext_chg_wait,
				msecs_to_jiffies(3000));
		if (!t)
			pr_err("msm_otg ext chg wait timeout\n");
		else
			pr_debug("msm_otg ext chg wait done\n");
	}

	if (motg->ext_chg_opened) {
		if (phy->flags & ENABLE_DP_MANUAL_PULLUP) {
			ulpi_write(phy, ULPI_MISC_A_VBUSVLDEXT |
					ULPI_MISC_A_VBUSVLDEXTSEL,
					ULPI_CLR(ULPI_MISC_A));
		}
		/* clear charging register bits */
		ulpi_write(phy, 0x3F, 0x86);
		/* re-enable DP and DM pull-down resistors*/
		ulpi_write(phy, 0x6, 0xB);
	}
}

static void msm_otg_sm_work(struct work_struct *w)
{
	struct msm_otg *motg = container_of(w, struct msm_otg, sm_work);
	struct usb_otg *otg = motg->phy.otg;
	bool work = 0, srp_reqd, dcp;

	pm_runtime_resume(otg->phy->dev);
	pr_debug("%s work\n", otg_state_string(otg->phy->state));
	switch (otg->phy->state) {
	case OTG_STATE_UNDEFINED:
		msm_otg_reset(otg->phy);
		msm_otg_init_sm(motg);
		if (!psy && legacy_power_supply) {
			psy = power_supply_get_by_name("usb");

			if (!psy)
				pr_err("couldn't get usb power supply\n");
		}

		otg->phy->state = OTG_STATE_B_IDLE;
		if (!test_bit(B_SESS_VLD, &motg->inputs) &&
				test_bit(ID, &motg->inputs)) {
			pm_runtime_put_noidle(otg->phy->dev);
			//ASUS_BSP+++ Eric5_Ou "Add PM usage count debug log and protection mechanism"
			otg_pm_count-= 1;
			printk("[OTG-PM][%s][1] put usage_count:%d, otg_pm_count:%d\n",__func__, atomic_read(&otg->phy->dev->power.usage_count),otg_pm_count);
			//ASUS_BSP--- Eric5_Ou "Add PM usage count debug log and protection mechanism"
			pm_runtime_suspend(otg->phy->dev);
			break;
		}
		/* FALL THROUGH */
	case OTG_STATE_B_IDLE:
		if (test_bit(MHL, &motg->inputs)) {
			/* allow LPM */
			pm_runtime_put_noidle(otg->phy->dev);
			//ASUS_BSP+++ Eric5_Ou "Add PM usage count debug log and protection mechanism"
			otg_pm_count-= 1;
			printk("[OTG-PM][%s][2] put usage_count:%d, otg_pm_count:%d\n",__func__, atomic_read(&otg->phy->dev->power.usage_count),otg_pm_count);
			//ASUS_BSP--- Eric5_Ou "Add PM usage count debug log and protection mechanism"
			pm_runtime_suspend(otg->phy->dev);
		} else if ((!test_bit(ID, &motg->inputs) ||
				test_bit(ID_A, &motg->inputs)) && otg->host) {
			pr_debug("!id || id_A\n");
			if (msm_chg_mhl_detect(motg)) {
				work = 1;
				break;
			}
			clear_bit(B_BUS_REQ, &motg->inputs);
			set_bit(A_BUS_REQ, &motg->inputs);
			otg->phy->state = OTG_STATE_A_IDLE;
			work = 1;
//ASUS_BSP+++ "[USB][NA][Spec] Add ASUS charger mode support"
#ifdef CONFIG_CHARGER_ASUS
			cancel_delayed_work_sync(&asus_chg_work);
#endif
//ASUS_BSP--- "[USB][NA][Spec] Add ASUS charger mode support"
		} else if (test_bit(B_SESS_VLD, &motg->inputs)) {
			pr_debug("b_sess_vld\n");
			switch (motg->chg_state) {
			case USB_CHG_STATE_UNDEFINED:
				//ASUS_BSP+++ Eric5_Ou "Add PM usage count debug log and protection mechanism"
				pm_runtime_get(otg->phy->dev);
				otg_pm_count+= 1;
				printk("[OTG-PM][USB_CHG_STATE_UNDEFINED] get usage_count:%d, otg_pm_count:%d\n", atomic_read(&motg->phy.dev->power.usage_count),otg_pm_count);
				//ASUS_BSP--- Eric5_Ou "Add PM usage count debug log and protection mechanism"
				//msm_chg_detect_work(&motg->chg_work.work);
				queue_delayed_work(system_nrt_wq, &motg->chg_work, (500 * HZ/1000));//for smd charger detect
				break;
			case USB_CHG_STATE_DETECTED:
				switch (motg->chg_type) {
				case USB_DCP_CHARGER:
					/* Enable VDP_SRC */
					ulpi_write(otg->phy, 0x2, 0x85);
					if (motg->ext_chg_opened) {
						init_completion(
							&motg->ext_chg_wait);
						motg->ext_chg_active = true;
					}
					/* fall through */
				case USB_PROPRIETARY_CHARGER:
					msm_otg_notify_charger(motg,
							IDEV_CHG_MAX);
					//ASUS_BSP+++ Eric5_Ou "Add PM usage count debug log and protection mechanism"
					while(otg_pm_count>0){
						pm_runtime_put_sync(otg->phy->dev);
						otg_pm_count-= 1;
						printk("[OTG-PM][%s][USB_PROPRIETARY_CHARGER] put usage_count:%d, otg_pm_count:%d\n",__func__, atomic_read(&otg->phy->dev->power.usage_count),otg_pm_count);
					}
					//ASUS_BSP--- Eric5_Ou "Add PM usage count debug log and protection mechanism"
					break;
				case USB_FLOATED_CHARGER:
					msm_otg_notify_charger(motg,
							IDEV_CHG_MAX);
					pm_runtime_put_noidle(otg->phy->dev);
					//ASUS_BSP+++ Eric5_Ou "Add PM usage count debug log and protection mechanism"
					otg_pm_count-= 1;
					printk("[OTG-PM][%s][4] put usage_count:%d, otg_pm_count:%d\n",__func__, atomic_read(&otg->phy->dev->power.usage_count),otg_pm_count);
					//ASUS_BSP--- Eric5_Ou "Add PM usage count debug log and protection mechanism"
					pm_runtime_suspend(otg->phy->dev);
					break;
				case USB_ACA_B_CHARGER:
					msm_otg_notify_charger(motg,
							IDEV_ACA_CHG_MAX);
					/*
					 * (ID_B --> ID_C) PHY_ALT interrupt can
					 * not be detected in LPM.
					 */
					break;
				case USB_CDP_CHARGER:
					msm_otg_notify_charger(motg,
							IDEV_CHG_MAX);
					msm_otg_start_peripheral(otg, 1);
					otg->phy->state =
						OTG_STATE_B_PERIPHERAL;
					break;
				case USB_ACA_C_CHARGER:
					msm_otg_notify_charger(motg,
							IDEV_ACA_CHG_MAX);
					msm_otg_start_peripheral(otg, 1);
					otg->phy->state =
						OTG_STATE_B_PERIPHERAL;
					break;
				case USB_SDP_CHARGER:
					msm_otg_start_peripheral(otg, 1);
					otg->phy->state =
						OTG_STATE_B_PERIPHERAL;
					mod_timer(&motg->chg_check_timer,
							CHG_RECHECK_DELAY);
					break;
				default:
					break;
				}
				break;
			default:
				break;
			}
		} else if (test_bit(B_BUS_REQ, &motg->inputs)) {
			pr_debug("b_sess_end && b_bus_req\n");
			if (msm_otg_start_srp(otg) < 0) {
				clear_bit(B_BUS_REQ, &motg->inputs);
				work = 1;
				break;
			}
			otg->phy->state = OTG_STATE_B_SRP_INIT;
			msm_otg_start_timer(motg, TB_SRP_FAIL, B_SRP_FAIL);
			break;
		} else {
			pr_debug("chg_work cancel");
			del_timer_sync(&motg->chg_check_timer);
			clear_bit(B_FALSE_SDP, &motg->inputs);
			clear_bit(A_BUS_REQ, &motg->inputs);
			cancel_delayed_work_sync(&motg->chg_work);
			dcp = (motg->chg_type == USB_DCP_CHARGER);
//ASUS_BSP+++ "[USB][NA][Spec] Add ASUS charger mode support"
#ifdef CONFIG_CHARGER_ASUS
			cancel_delayed_work_sync(&asus_chg_work);
			g_charger_mode = ASUS_CHG_SRC_NONE;
			asus_chg_set_chg_mode(ASUS_CHG_SRC_NONE);
			printk("[USB] set_chg_mode: None\n");
#endif
//ASUS_BSP--- "[USB][NA][Spec] Add ASUS charger mode support"
			motg->chg_state = USB_CHG_STATE_UNDEFINED;
			motg->chg_type = USB_INVALID_CHARGER;
			msm_otg_notify_charger(motg, 0);

			//ASUS_BSP+++ Eric5_Ou "Add PM usage count debug log and protection mechanism"
			if(otg_pm_count > 0) {
				if (dcp) {
					msm_otg_wait_for_ext_chg_done(motg);
					/* Turn off VDP_SRC */
					ulpi_write(otg->phy, 0x2, 0x86);
				}
				msm_otg_reset(otg->phy);
			} else {
				printk("[OTG-PM][%s] chg_work has canceled before, put usage_count:%d, otg_pm_count:%d\n",__func__, atomic_read(&otg->phy->dev->power.usage_count),otg_pm_count);
			}
			//ASUS_BSP--- Eric5_Ou "Add PM usage count debug log and protection mechanism"

			/*
			 * There is a small window where ID interrupt
			 * is not monitored during ID detection circuit
			 * switch from ACA to PMIC.  Check ID state
			 * before entering into low power mode.
			 */
			//ASUS_BSP+++ Eric5_Ou "not check ID state during suspend"
			/*
			if (!msm_otg_read_pmic_id_state(motg)) {
				pr_debug("process missed ID intr\n");
				clear_bit(ID, &motg->inputs);
				work = 1;
				break;
			}
			*/
			//ASUS_BSP--- Eric5_Ou "not check ID state during suspend"

			//ASUS_BSP+++ Eric5_Ou "Add PM usage count debug log and protection mechanism"
			while(otg_pm_count>0){
				pm_runtime_put_noidle(otg->phy->dev);
				otg_pm_count-= 1;
				printk("[OTG-PM][%s][USB_UNPLUG] put usage_count:%d, otg_pm_count:%d\n",__func__, atomic_read(&otg->phy->dev->power.usage_count),otg_pm_count);
			}
			//ASUS_BSP--- Eric5_Ou "Add PM usage count debug log and protection mechanism"

			/*
			 * Only if autosuspend was enabled in probe, it will be
			 * used here. Otherwise, no delay will be used.
			 */
			pm_runtime_mark_last_busy(otg->phy->dev);
			pm_runtime_autosuspend(otg->phy->dev);
		}
		break;
	case OTG_STATE_B_SRP_INIT:
		if (!test_bit(ID, &motg->inputs) ||
				test_bit(ID_A, &motg->inputs) ||
				test_bit(ID_C, &motg->inputs) ||
				(test_bit(B_SESS_VLD, &motg->inputs) &&
				!test_bit(ID_B, &motg->inputs))) {
			pr_debug("!id || id_a/c || b_sess_vld+!id_b\n");
			msm_otg_del_timer(motg);
			otg->phy->state = OTG_STATE_B_IDLE;
			/*
			 * clear VBUSVLDEXTSEL and VBUSVLDEXT register
			 * bits after SRP initiation.
			 */
			ulpi_write(otg->phy, 0x0, 0x98);
			work = 1;
		} else if (test_bit(B_SRP_FAIL, &motg->tmouts)) {
			pr_debug("b_srp_fail\n");
			pr_info("A-device did not respond to SRP\n");
			clear_bit(B_BUS_REQ, &motg->inputs);
			clear_bit(B_SRP_FAIL, &motg->tmouts);
			otg_send_event(OTG_EVENT_NO_RESP_FOR_SRP);
			ulpi_write(otg->phy, 0x0, 0x98);
			otg->phy->state = OTG_STATE_B_IDLE;
			motg->b_last_se0_sess = jiffies;
			work = 1;
		}
		break;
	case OTG_STATE_B_PERIPHERAL:
		if (test_bit(B_SESS_VLD, &motg->inputs) &&
				test_bit(B_FALSE_SDP, &motg->inputs)) {
			pr_debug("B_FALSE_SDP\n");
			msm_otg_start_peripheral(otg, 0);
#ifdef CONFIG_CHARGER_ASUS
			cancel_delayed_work_sync(&asus_chg_work);
			asus_chg_set_chg_mode(ASUS_CHG_SRC_DC);
			printk("[USB] B_FALSE_SDP set_chg_mode: ASUS AC\n");
#endif
			motg->chg_type = USB_DCP_CHARGER;
			clear_bit(B_FALSE_SDP, &motg->inputs);
			otg->phy->state = OTG_STATE_B_IDLE;
			work = 1;
		} else if (!test_bit(ID, &motg->inputs) ||
				test_bit(ID_A, &motg->inputs) ||
				test_bit(ID_B, &motg->inputs) ||
				!test_bit(B_SESS_VLD, &motg->inputs)) {
			pr_debug("!id  || id_a/b || !b_sess_vld\n");
			motg->chg_state = USB_CHG_STATE_UNDEFINED;
			motg->chg_type = USB_INVALID_CHARGER;
			msm_otg_notify_charger(motg, 0);
			srp_reqd = otg->gadget->otg_srp_reqd;
			msm_otg_start_peripheral(otg, 0);
			if (test_bit(ID_B, &motg->inputs))
				clear_bit(ID_B, &motg->inputs);
			clear_bit(B_BUS_REQ, &motg->inputs);
			otg->phy->state = OTG_STATE_B_IDLE;
			motg->b_last_se0_sess = jiffies;
			if (srp_reqd)
				msm_otg_start_timer(motg,
					TB_TST_SRP, B_TST_SRP);
			else
				work = 1;
		} else if (test_bit(B_BUS_REQ, &motg->inputs) &&
				otg->gadget->b_hnp_enable &&
				test_bit(A_BUS_SUSPEND, &motg->inputs)) {
			pr_debug("b_bus_req && b_hnp_en && a_bus_suspend\n");
			msm_otg_start_timer(motg, TB_ASE0_BRST, B_ASE0_BRST);
			/* D+ pullup should not be disconnected within 4msec
			 * after A device suspends the bus. Otherwise PET will
			 * fail the compliance test.
			 */
			udelay(1000);
			msm_otg_start_peripheral(otg, 0);
			otg->phy->state = OTG_STATE_B_WAIT_ACON;
			/*
			 * start HCD even before A-device enable
			 * pull-up to meet HNP timings.
			 */
			otg->host->is_b_host = 1;
			msm_otg_start_host(otg, 1);
		} else if (test_bit(A_BUS_SUSPEND, &motg->inputs) &&
				   test_bit(B_SESS_VLD, &motg->inputs)) {
			pr_debug("a_bus_suspend && b_sess_vld\n");
			if (motg->caps & ALLOW_LPM_ON_DEV_SUSPEND) {
				pm_runtime_put_noidle(otg->phy->dev);
				//ASUS_BSP+++ Eric5_Ou "Add PM usage count debug log and protection mechanism"
				otg_pm_count-= 1;
				printk("[OTG-PM][%s][6] put usage_count:%d, otg_pm_count:%d\n",__func__, atomic_read(&otg->phy->dev->power.usage_count),otg_pm_count);
				//ASUS_BSP--- Eric5_Ou "Add PM usage count debug log and protection mechanism"
				pm_runtime_suspend(otg->phy->dev);
			}
		} else if (test_bit(ID_C, &motg->inputs)) {
			msm_otg_notify_charger(motg, IDEV_ACA_CHG_MAX);
		}
		break;
	case OTG_STATE_B_WAIT_ACON:
		if (!test_bit(ID, &motg->inputs) ||
				test_bit(ID_A, &motg->inputs) ||
				test_bit(ID_B, &motg->inputs) ||
				!test_bit(B_SESS_VLD, &motg->inputs)) {
			pr_debug("!id || id_a/b || !b_sess_vld\n");
			msm_otg_del_timer(motg);
			/*
			 * A-device is physically disconnected during
			 * HNP. Remove HCD.
			 */
			msm_otg_start_host(otg, 0);
			otg->host->is_b_host = 0;

			clear_bit(B_BUS_REQ, &motg->inputs);
			clear_bit(A_BUS_SUSPEND, &motg->inputs);
			motg->b_last_se0_sess = jiffies;
			otg->phy->state = OTG_STATE_B_IDLE;
			msm_otg_reset(otg->phy);
			work = 1;
		} else if (test_bit(A_CONN, &motg->inputs)) {
			pr_debug("a_conn\n");
			clear_bit(A_BUS_SUSPEND, &motg->inputs);
			otg->phy->state = OTG_STATE_B_HOST;
			/*
			 * PET disconnects D+ pullup after reset is generated
			 * by B device in B_HOST role which is not detected by
			 * B device. As workaorund , start timer of 300msec
			 * and stop timer if A device is enumerated else clear
			 * A_CONN.
			 */
			msm_otg_start_timer(motg, TB_TST_CONFIG,
						B_TST_CONFIG);
		} else if (test_bit(B_ASE0_BRST, &motg->tmouts)) {
			pr_debug("b_ase0_brst_tmout\n");
			pr_info("B HNP fail:No response from A device\n");
			msm_otg_start_host(otg, 0);
			msm_otg_reset(otg->phy);
			otg->host->is_b_host = 0;
			clear_bit(B_ASE0_BRST, &motg->tmouts);
			clear_bit(A_BUS_SUSPEND, &motg->inputs);
			clear_bit(B_BUS_REQ, &motg->inputs);
			otg_send_event(OTG_EVENT_HNP_FAILED);
			otg->phy->state = OTG_STATE_B_IDLE;
			work = 1;
		} else if (test_bit(ID_C, &motg->inputs)) {
			msm_otg_notify_charger(motg, IDEV_ACA_CHG_MAX);
		}
		break;
	case OTG_STATE_B_HOST:
		if (!test_bit(B_BUS_REQ, &motg->inputs) ||
				!test_bit(A_CONN, &motg->inputs) ||
				!test_bit(B_SESS_VLD, &motg->inputs)) {
			pr_debug("!b_bus_req || !a_conn || !b_sess_vld\n");
			clear_bit(A_CONN, &motg->inputs);
			clear_bit(B_BUS_REQ, &motg->inputs);
			msm_otg_start_host(otg, 0);
			otg->host->is_b_host = 0;
			otg->phy->state = OTG_STATE_B_IDLE;
			msm_otg_reset(otg->phy);
			work = 1;
		} else if (test_bit(ID_C, &motg->inputs)) {
			msm_otg_notify_charger(motg, IDEV_ACA_CHG_MAX);
		}
		break;
	case OTG_STATE_A_IDLE:
		otg->default_a = 1;
		if (test_bit(ID, &motg->inputs) &&
			!test_bit(ID_A, &motg->inputs)) {
			pr_debug("id && !id_a\n");
			otg->default_a = 0;
			clear_bit(A_BUS_DROP, &motg->inputs);
			otg->phy->state = OTG_STATE_B_IDLE;
			del_timer_sync(&motg->id_timer);
			msm_otg_link_reset(motg);
			msm_chg_enable_aca_intr(motg);
			msm_otg_notify_charger(motg, 0);
			work = 1;
		} else if (!test_bit(A_BUS_DROP, &motg->inputs) &&
				(test_bit(A_SRP_DET, &motg->inputs) ||
				 test_bit(A_BUS_REQ, &motg->inputs))) {
			pr_debug("!a_bus_drop && (a_srp_det || a_bus_req)\n");

			clear_bit(A_SRP_DET, &motg->inputs);
			/* Disable SRP detection */
			writel_relaxed((readl_relaxed(USB_OTGSC) &
					~OTGSC_INTSTS_MASK) &
					~OTGSC_DPIE, USB_OTGSC);

			otg->phy->state = OTG_STATE_A_WAIT_VRISE;
			/* VBUS should not be supplied before end of SRP pulse
			 * generated by PET, if not complaince test fail.
			 */
			usleep_range(10000, 12000);
			/* ACA: ID_A: Stop charging untill enumeration */
			if (test_bit(ID_A, &motg->inputs))
				msm_otg_notify_charger(motg, 0);
			else
				msm_hsusb_vbus_power(motg, 1);
			msm_otg_start_timer(motg, TA_WAIT_VRISE, A_WAIT_VRISE);
		} else {
			pr_debug("No session requested\n");
			clear_bit(A_BUS_DROP, &motg->inputs);
			if (test_bit(ID_A, &motg->inputs)) {
					msm_otg_notify_charger(motg,
							IDEV_ACA_CHG_MAX);
			} else if (!test_bit(ID, &motg->inputs)) {
				msm_otg_notify_charger(motg, 0);
				/*
				 * A-device is not providing power on VBUS.
				 * Enable SRP detection.
				 */
				writel_relaxed(0x13, USB_USBMODE);
				writel_relaxed((readl_relaxed(USB_OTGSC) &
						~OTGSC_INTSTS_MASK) |
						OTGSC_DPIE, USB_OTGSC);
				mb();
			}
		}
		break;
	case OTG_STATE_A_WAIT_VRISE:
		if ((test_bit(ID, &motg->inputs) &&
				!test_bit(ID_A, &motg->inputs)) ||
				test_bit(A_BUS_DROP, &motg->inputs) ||
				test_bit(A_WAIT_VRISE, &motg->tmouts)) {
			pr_debug("id || a_bus_drop || a_wait_vrise_tmout\n");
			clear_bit(A_BUS_REQ, &motg->inputs);
			msm_otg_del_timer(motg);
			msm_hsusb_vbus_power(motg, 0);
			otg->phy->state = OTG_STATE_A_WAIT_VFALL;
			msm_otg_start_timer(motg, TA_WAIT_VFALL, A_WAIT_VFALL);
		} else if (test_bit(A_VBUS_VLD, &motg->inputs)) {
			pr_debug("a_vbus_vld\n");
			otg->phy->state = OTG_STATE_A_WAIT_BCON;
			if (TA_WAIT_BCON > 0)
				msm_otg_start_timer(motg, TA_WAIT_BCON,
					A_WAIT_BCON);

			/* Clear BSV in host mode */
			clear_bit(B_SESS_VLD, &motg->inputs);
			msm_otg_start_host(otg, 1);
			msm_chg_enable_aca_det(motg);
			msm_chg_disable_aca_intr(motg);
			mod_timer(&motg->id_timer, ID_TIMER_FREQ);
			if (msm_chg_check_aca_intr(motg))
				work = 1;
		}
		break;
	case OTG_STATE_A_WAIT_BCON:
		if ((test_bit(ID, &motg->inputs) &&
				!test_bit(ID_A, &motg->inputs)) ||
				test_bit(A_BUS_DROP, &motg->inputs) ||
				test_bit(A_WAIT_BCON, &motg->tmouts)) {
			pr_debug("(id && id_a/b/c) || a_bus_drop ||"
					"a_wait_bcon_tmout\n");
			if (test_bit(A_WAIT_BCON, &motg->tmouts)) {
				pr_info("Device No Response\n");
				otg_send_event(OTG_EVENT_DEV_CONN_TMOUT);
			}
			msm_otg_del_timer(motg);
			clear_bit(A_BUS_REQ, &motg->inputs);
			clear_bit(B_CONN, &motg->inputs);
			msm_otg_start_host(otg, 0);
			/*
			 * ACA: ID_A with NO accessory, just the A plug is
			 * attached to ACA: Use IDCHG_MAX for charging
			 */
			if (test_bit(ID_A, &motg->inputs))
				msm_otg_notify_charger(motg, IDEV_CHG_MIN);
			else
				msm_hsusb_vbus_power(motg, 0);
			otg->phy->state = OTG_STATE_A_WAIT_VFALL;
			msm_otg_start_timer(motg, TA_WAIT_VFALL, A_WAIT_VFALL);
		} else if (!test_bit(A_VBUS_VLD, &motg->inputs)) {
			pr_debug("!a_vbus_vld\n");
			clear_bit(B_CONN, &motg->inputs);
			msm_otg_del_timer(motg);
			msm_otg_start_host(otg, 0);
			otg->phy->state = OTG_STATE_A_VBUS_ERR;
			msm_otg_reset(otg->phy);
		} else if (test_bit(ID_A, &motg->inputs)) {
			msm_hsusb_vbus_power(motg, 0);
		} else if (!test_bit(A_BUS_REQ, &motg->inputs)) {
			/*
			 * If TA_WAIT_BCON is infinite, we don;t
			 * turn off VBUS. Enter low power mode.
			 */
			if (TA_WAIT_BCON < 0) {
				//ASUS_BSP+++ Eric5_Ou "Add PM usage count debug log and protection mechanism"
				while (otg_pm_count > 0) {
					pm_runtime_put_sync(otg->phy->dev);
					otg_pm_count-= 1;
					printk("[OTG-PM][%s][OTG_STATE_A_WAIT_BCON] put usage_count:%d, otg_pm_count:%d\n",__func__, atomic_read(&otg->phy->dev->power.usage_count),otg_pm_count);
				}
				//ASUS_BSP--- Eric5_Ou "Add PM usage count debug log and protection mechanism"
			}
		} else if (!test_bit(ID, &motg->inputs)) {
			msm_hsusb_vbus_power(motg, 1);
		}
		break;
	case OTG_STATE_A_HOST:
		if ((test_bit(ID, &motg->inputs) &&
				!test_bit(ID_A, &motg->inputs)) ||
				test_bit(A_BUS_DROP, &motg->inputs)) {
			pr_debug("id_a/b/c || a_bus_drop\n");
			clear_bit(B_CONN, &motg->inputs);
			clear_bit(A_BUS_REQ, &motg->inputs);
			msm_otg_del_timer(motg);
			otg->phy->state = OTG_STATE_A_WAIT_VFALL;
			msm_otg_start_host(otg, 0);
			if (!test_bit(ID_A, &motg->inputs))
				msm_hsusb_vbus_power(motg, 0);
			msm_otg_start_timer(motg, TA_WAIT_VFALL, A_WAIT_VFALL);
		} else if (!test_bit(A_VBUS_VLD, &motg->inputs)) {
			pr_debug("!a_vbus_vld\n");
			clear_bit(B_CONN, &motg->inputs);
			msm_otg_del_timer(motg);
			otg->phy->state = OTG_STATE_A_VBUS_ERR;
			msm_otg_start_host(otg, 0);
			msm_otg_reset(otg->phy);
		} else if (!test_bit(A_BUS_REQ, &motg->inputs)) {
			/*
			 * a_bus_req is de-asserted when root hub is
			 * suspended or HNP is in progress.
			 */
			pr_debug("!a_bus_req\n");
			msm_otg_del_timer(motg);
			otg->phy->state = OTG_STATE_A_SUSPEND;
			if (otg->host->b_hnp_enable)
				msm_otg_start_timer(motg, TA_AIDL_BDIS,
						A_AIDL_BDIS);
			else {
				pm_runtime_put_sync(otg->phy->dev);
				//ASUS_BSP+++ Eric5_Ou "Add PM usage count debug log and protection mechanism"
				otg_pm_count-= 1;
				printk("[OTG-PM][%s][8] put usage_count:%d, otg_pm_count:%d\n",__func__, atomic_read(&otg->phy->dev->power.usage_count),otg_pm_count);
				//ASUS_BSP--- Eric5_Ou "Add PM usage count debug log and protection mechanism"
			}
		} else if (!test_bit(B_CONN, &motg->inputs)) {
			pr_debug("!b_conn\n");
			msm_otg_del_timer(motg);
			otg->phy->state = OTG_STATE_A_WAIT_BCON;
			if (TA_WAIT_BCON > 0)
				msm_otg_start_timer(motg, TA_WAIT_BCON,
					A_WAIT_BCON);
			if (msm_chg_check_aca_intr(motg))
				work = 1;
		} else if (test_bit(ID_A, &motg->inputs)) {
			msm_otg_del_timer(motg);
			msm_hsusb_vbus_power(motg, 0);
			if (motg->chg_type == USB_ACA_DOCK_CHARGER)
				msm_otg_notify_charger(motg,
						IDEV_ACA_CHG_MAX);
			else
				msm_otg_notify_charger(motg,
						IDEV_CHG_MIN - motg->mA_port);
		} else if (!test_bit(ID, &motg->inputs)) {
			motg->chg_state = USB_CHG_STATE_UNDEFINED;
			motg->chg_type = USB_INVALID_CHARGER;
			msm_otg_notify_charger(motg, 0);
			msm_hsusb_vbus_power(motg, 1);
		}
		break;
	case OTG_STATE_A_SUSPEND:
		if ((test_bit(ID, &motg->inputs) &&
				!test_bit(ID_A, &motg->inputs)) ||
				test_bit(A_BUS_DROP, &motg->inputs) ||
				test_bit(A_AIDL_BDIS, &motg->tmouts)) {
			pr_debug("id_a/b/c || a_bus_drop ||"
					"a_aidl_bdis_tmout\n");
			msm_otg_del_timer(motg);
			clear_bit(B_CONN, &motg->inputs);
			otg->phy->state = OTG_STATE_A_WAIT_VFALL;
			msm_otg_start_host(otg, 0);
			msm_otg_reset(otg->phy);
			if (!test_bit(ID_A, &motg->inputs))
				msm_hsusb_vbus_power(motg, 0);
			msm_otg_start_timer(motg, TA_WAIT_VFALL, A_WAIT_VFALL);
		} else if (!test_bit(A_VBUS_VLD, &motg->inputs)) {
			pr_debug("!a_vbus_vld\n");
			msm_otg_del_timer(motg);
			clear_bit(B_CONN, &motg->inputs);
			otg->phy->state = OTG_STATE_A_VBUS_ERR;
			msm_otg_start_host(otg, 0);
			msm_otg_reset(otg->phy);
		} else if (!test_bit(B_CONN, &motg->inputs) &&
				otg->host->b_hnp_enable) {
			pr_debug("!b_conn && b_hnp_enable");
			otg->phy->state = OTG_STATE_A_PERIPHERAL;
			msm_otg_host_hnp_enable(otg, 1);
			otg->gadget->is_a_peripheral = 1;
			msm_otg_start_peripheral(otg, 1);
		} else if (!test_bit(B_CONN, &motg->inputs) &&
				!otg->host->b_hnp_enable) {
			pr_debug("!b_conn && !b_hnp_enable");
			/*
			 * bus request is dropped during suspend.
			 * acquire again for next device.
			 */
			set_bit(A_BUS_REQ, &motg->inputs);
			otg->phy->state = OTG_STATE_A_WAIT_BCON;
			if (TA_WAIT_BCON > 0)
				msm_otg_start_timer(motg, TA_WAIT_BCON,
					A_WAIT_BCON);
		} else if (test_bit(ID_A, &motg->inputs)) {
			msm_hsusb_vbus_power(motg, 0);
			msm_otg_notify_charger(motg,
					IDEV_CHG_MIN - motg->mA_port);
		} else if (!test_bit(ID, &motg->inputs)) {
			msm_otg_notify_charger(motg, 0);
			msm_hsusb_vbus_power(motg, 1);
		}
		break;
	case OTG_STATE_A_PERIPHERAL:
		if ((test_bit(ID, &motg->inputs) &&
				!test_bit(ID_A, &motg->inputs)) ||
				test_bit(A_BUS_DROP, &motg->inputs)) {
			pr_debug("id _f/b/c || a_bus_drop\n");
			/* Clear BIDL_ADIS timer */
			msm_otg_del_timer(motg);
			otg->phy->state = OTG_STATE_A_WAIT_VFALL;
			msm_otg_start_peripheral(otg, 0);
			otg->gadget->is_a_peripheral = 0;
			msm_otg_start_host(otg, 0);
			msm_otg_reset(otg->phy);
			if (!test_bit(ID_A, &motg->inputs))
				msm_hsusb_vbus_power(motg, 0);
			msm_otg_start_timer(motg, TA_WAIT_VFALL, A_WAIT_VFALL);
		} else if (!test_bit(A_VBUS_VLD, &motg->inputs)) {
			pr_debug("!a_vbus_vld\n");
			/* Clear BIDL_ADIS timer */
			msm_otg_del_timer(motg);
			otg->phy->state = OTG_STATE_A_VBUS_ERR;
			msm_otg_start_peripheral(otg, 0);
			otg->gadget->is_a_peripheral = 0;
			msm_otg_start_host(otg, 0);
		} else if (test_bit(A_BIDL_ADIS, &motg->tmouts)) {
			pr_debug("a_bidl_adis_tmout\n");
			msm_otg_start_peripheral(otg, 0);
			otg->gadget->is_a_peripheral = 0;
			otg->phy->state = OTG_STATE_A_WAIT_BCON;
			set_bit(A_BUS_REQ, &motg->inputs);
			msm_otg_host_hnp_enable(otg, 0);
			if (TA_WAIT_BCON > 0)
				msm_otg_start_timer(motg, TA_WAIT_BCON,
					A_WAIT_BCON);
		} else if (test_bit(ID_A, &motg->inputs)) {
			msm_hsusb_vbus_power(motg, 0);
			msm_otg_notify_charger(motg,
					IDEV_CHG_MIN - motg->mA_port);
		} else if (!test_bit(ID, &motg->inputs)) {
			msm_otg_notify_charger(motg, 0);
			msm_hsusb_vbus_power(motg, 1);
		}
		break;
	case OTG_STATE_A_WAIT_VFALL:
		if (test_bit(A_WAIT_VFALL, &motg->tmouts)) {
			clear_bit(A_VBUS_VLD, &motg->inputs);
			otg->phy->state = OTG_STATE_A_IDLE;
			work = 1;
		}
		break;
	case OTG_STATE_A_VBUS_ERR:
		if ((test_bit(ID, &motg->inputs) &&
				!test_bit(ID_A, &motg->inputs)) ||
				test_bit(A_BUS_DROP, &motg->inputs) ||
				test_bit(A_CLR_ERR, &motg->inputs)) {
			otg->phy->state = OTG_STATE_A_WAIT_VFALL;
			if (!test_bit(ID_A, &motg->inputs))
				msm_hsusb_vbus_power(motg, 0);
			msm_otg_start_timer(motg, TA_WAIT_VFALL, A_WAIT_VFALL);
			motg->chg_state = USB_CHG_STATE_UNDEFINED;
			motg->chg_type = USB_INVALID_CHARGER;
			msm_otg_notify_charger(motg, 0);
		}
		break;
	default:
		break;
	}
	if (work)
		queue_work(system_nrt_wq, &motg->sm_work);
}

static void msm_otg_suspend_work(struct work_struct *w)
{
	struct msm_otg *motg =
		container_of(w, struct msm_otg, suspend_work.work);

	/* This work is only for device bus suspend */
	if (test_bit(A_BUS_SUSPEND, &motg->inputs))
		msm_otg_sm_work(&motg->sm_work);
}

static irqreturn_t msm_otg_irq(int irq, void *data)
{
	struct msm_otg *motg = data;
	struct usb_otg *otg = motg->phy.otg;
	u32 otgsc = 0, usbsts, pc;
	bool work = 0;
	irqreturn_t ret = IRQ_HANDLED;

	if (atomic_read(&motg->in_lpm)) {
		pr_debug("OTG IRQ: %d in LPM\n", irq);
		disable_irq_nosync(irq);
		motg->async_int = irq;
		if (!atomic_read(&motg->pm_suspended))
			pm_request_resume(otg->phy->dev);
		return IRQ_HANDLED;
	}

	usbsts = readl(USB_USBSTS);
//ASUS_BSP+++ "[USB][NA][Spec] Add ASUS charger mode support"
#ifdef CONFIG_CHARGER_ASUS
	if(usbsts & (1<<6)){//check usb reset
		if((g_charger_mode!=ASUS_CHG_SRC_USB) && (motg->chg_type != USB_CDP_CHARGER)){
			schedule_work(&asus_usb_work);
		}
	}
	if(g_usb_boot == MSM_OTG_USB_BOOT_INIT){
		g_usb_boot = MSM_OTG_USB_BOOT_IRQ;
	}
#endif
//ASUS_BSP--- "[USB][NA][Spec] Add ASUS charger mode support"
	otgsc = readl(USB_OTGSC);

	if (!(otgsc & OTG_OTGSTS_MASK) && !(usbsts & OTG_USBSTS_MASK))
		return IRQ_NONE;

	if ((otgsc & OTGSC_IDIS) && (otgsc & OTGSC_IDIE)) {
		if (otgsc & OTGSC_ID) {
			dev_dbg(otg->phy->dev, "ID set\n");
			set_bit(ID, &motg->inputs);
		} else {
			dev_dbg(otg->phy->dev, "ID clear\n");
			/*
			 * Assert a_bus_req to supply power on
			 * VBUS when Micro/Mini-A cable is connected
			 * with out user intervention.
			 */
			set_bit(A_BUS_REQ, &motg->inputs);
			clear_bit(ID, &motg->inputs);
			msm_chg_enable_aca_det(motg);
		}
		writel_relaxed(otgsc, USB_OTGSC);
		work = 1;
	} else if (otgsc & OTGSC_DPIS) {
		pr_debug("DPIS detected\n");
		writel_relaxed(otgsc, USB_OTGSC);
		set_bit(A_SRP_DET, &motg->inputs);
		set_bit(A_BUS_REQ, &motg->inputs);
		work = 1;
	} else if ((otgsc & OTGSC_BSVIE) && (otgsc & OTGSC_BSVIS)) {
		writel_relaxed(otgsc, USB_OTGSC);
//ASUS_BSP+++ "[USB][NA][Fix] Ignore BSVIS when OTG_PMIC_CONTROL"
		if (motg->pdata->otg_control == OTG_PMIC_CONTROL){
			return IRQ_HANDLED;
		}
//ASUS_BSP--- "[USB][NA][Fix] Ignore BSVIS when OTG_PMIC_CONTROL"
		/*
		 * BSV interrupt comes when operating as an A-device
		 * (VBUS on/off).
		 * But, handle BSV when charger is removed from ACA in ID_A
		 */
		if ((otg->phy->state >= OTG_STATE_A_IDLE) &&
			!test_bit(ID_A, &motg->inputs))
			return IRQ_HANDLED;
		if (otgsc & OTGSC_BSV) {
			dev_dbg(otg->phy->dev, "BSV set\n");
			set_bit(B_SESS_VLD, &motg->inputs);
		} else {
			dev_dbg(otg->phy->dev, "BSV clear\n");
			clear_bit(B_SESS_VLD, &motg->inputs);
			clear_bit(A_BUS_SUSPEND, &motg->inputs);

			msm_chg_check_aca_intr(motg);
		}
		work = 1;
	} else if (usbsts & STS_PCI) {
		pc = readl_relaxed(USB_PORTSC);
		pr_debug("portsc = %x\n", pc);
		ret = IRQ_NONE;
		/*
		 * HCD Acks PCI interrupt. We use this to switch
		 * between different OTG states.
		 */
		work = 1;
		switch (otg->phy->state) {
		case OTG_STATE_A_SUSPEND:
			if (otg->host->b_hnp_enable && (pc & PORTSC_CSC) &&
					!(pc & PORTSC_CCS)) {
				pr_debug("B_CONN clear\n");
				clear_bit(B_CONN, &motg->inputs);
				msm_otg_del_timer(motg);
			}
			break;
		case OTG_STATE_A_PERIPHERAL:
			/*
			 * A-peripheral observed activity on bus.
			 * clear A_BIDL_ADIS timer.
			 */
			msm_otg_del_timer(motg);
			work = 0;
			break;
		case OTG_STATE_B_WAIT_ACON:
			if ((pc & PORTSC_CSC) && (pc & PORTSC_CCS)) {
				pr_debug("A_CONN set\n");
				set_bit(A_CONN, &motg->inputs);
				/* Clear ASE0_BRST timer */
				msm_otg_del_timer(motg);
			}
			break;
		case OTG_STATE_B_HOST:
			if ((pc & PORTSC_CSC) && !(pc & PORTSC_CCS)) {
				pr_debug("A_CONN clear\n");
				clear_bit(A_CONN, &motg->inputs);
				msm_otg_del_timer(motg);
			}
			break;
		case OTG_STATE_A_WAIT_BCON:
			if (TA_WAIT_BCON < 0)
				set_bit(A_BUS_REQ, &motg->inputs);
		default:
			work = 0;
			break;
		}
	} else if (usbsts & STS_URI) {
		ret = IRQ_NONE;
		switch (otg->phy->state) {
		case OTG_STATE_A_PERIPHERAL:
			/*
			 * A-peripheral observed activity on bus.
			 * clear A_BIDL_ADIS timer.
			 */
			msm_otg_del_timer(motg);
			work = 0;
			break;
		default:
			work = 0;
			break;
		}
	} else if (usbsts & STS_SLI) {
		ret = IRQ_NONE;
		work = 0;
		switch (otg->phy->state) {
		case OTG_STATE_B_PERIPHERAL:
			if (otg->gadget->b_hnp_enable) {
				set_bit(A_BUS_SUSPEND, &motg->inputs);
				set_bit(B_BUS_REQ, &motg->inputs);
				work = 1;
			}
			break;
		case OTG_STATE_A_PERIPHERAL:
			msm_otg_start_timer(motg, TA_BIDL_ADIS,
					A_BIDL_ADIS);
			break;
		default:
			break;
		}
	} else if ((usbsts & PHY_ALT_INT)) {
		writel_relaxed(PHY_ALT_INT, USB_USBSTS);
		if (msm_chg_check_aca_intr(motg))
			work = 1;
		ret = IRQ_HANDLED;
	}
	if (work)
		queue_work(system_nrt_wq, &motg->sm_work);

	return ret;
}

static void msm_otg_set_vbus_state(int online)
{
	static bool init;
	struct msm_otg *motg = the_msm_otg;
	msm_otg_bsv = online;

	//ASUS_BSP+++ Eric5_Ou "ignore bsv events in host mode"
//	if (g_host_mode || (USB_AUTO == motg->otg_mode && (AX_MicroP_IsP01Connected() || pad_exist()))) {
	if (g_host_mode ) {
		dev_err(motg->phy.dev, "PMIC: ignore BSV events in host mode (%d)(%d)\n",
			online, motg->otg_mode);
		if (!init) {
			init = true;
			complete(&pmic_vbus_init);
			pr_debug("PMIC: BSV init complete\n");
		}
		return;
	}
	//ASUS_BSP--- Eric5_Ou "ignore bsv events in host mode"

//ASUS_BSP+++ "[USB][NA][Spec] Add ASUS charger mode support"
	if((test_bit(B_SESS_VLD, &motg->inputs) && online) ||
		(!test_bit(B_SESS_VLD, &motg->inputs) && !online)){
		if(init){
			pr_debug("PMIC: BSV already set to %d\n",online);
			return;
		}
	}
//ASUS_BSP--- "[USB][NA][Spec] Add ASUS charger mode support"
	if (online) {
		pr_debug("PMIC: BSV set\n");
		printk("[USB] plugin\n");
		set_bit(B_SESS_VLD, &motg->inputs);
	} else {
		pr_debug("PMIC: BSV clear\n");
		printk("[USB] unplug\n");
		clear_bit(B_SESS_VLD, &motg->inputs);
	}

	/* do not queue state m/c work if id is grounded */
	if (!test_bit(ID, &motg->inputs)) {
		/*
		 * state machine work waits for initial VBUS
		 * completion in UNDEFINED state.  Process
		 * the initial VBUS event in ID_GND state.
		 */
		if (init)
			return;
	}

	if (!init) {
		init = true;
		complete(&pmic_vbus_init);
		pr_debug("PMIC: BSV init complete\n");
		return;
	}

	if (test_bit(MHL, &motg->inputs) ||
			mhl_det_in_progress) {
		pr_debug("PMIC: BSV interrupt ignored in MHL\n");
		return;
	}

	if (atomic_read(&motg->pm_suspended))
		motg->sm_work_pending = true;
	else
		queue_work(system_nrt_wq, &motg->sm_work);
}

static void msm_pmic_id_status_w(struct work_struct *w)
{
	struct msm_otg *motg = container_of(w, struct msm_otg,
						pmic_id_status_work.work);
	//ASUS_BSP+++ Eric5_Ou "add phone mode usb OTG support"
	/*
	int work = 0;

	if (msm_otg_read_pmic_id_state(motg)) {
		if (!test_and_set_bit(ID, &motg->inputs)) {
			pr_debug("PMIC: ID set\n");
			work = 1;
		}
	} else {
		if (test_and_clear_bit(ID, &motg->inputs)) {
			pr_debug("PMIC: ID clear\n");
			set_bit(A_BUS_REQ, &motg->inputs);
			work = 1;
		}
	}

	if (work && (motg->phy.state != OTG_STATE_UNDEFINED)) {
		if (atomic_read(&motg->pm_suspended))
			motg->sm_work_pending = true;
		else
			queue_work(system_nrt_wq, &motg->sm_work);
	}
	*/

	asus_otg_set_id_state(!msm_otg_read_pmic_id_state(motg));
	//ASUS_BSP+++ Eric5_Ou "add phone mode usb OTG support"

}

#define MSM_PMIC_ID_STATUS_DELAY	5 /* 5msec */
static irqreturn_t msm_pmic_id_irq(int irq, void *data)
{
	struct msm_otg *motg = data;

	if (test_bit(MHL, &motg->inputs) ||
			mhl_det_in_progress) {
		pr_debug("PMIC: Id interrupt ignored in MHL\n");
		return IRQ_HANDLED;
	}

	if (!aca_id_turned_on)
		/*schedule delayed work for 5msec for ID line state to settle*/
		queue_delayed_work(system_nrt_wq, &motg->pmic_id_status_work,
				msecs_to_jiffies(MSM_PMIC_ID_STATUS_DELAY));

	return IRQ_HANDLED;
}

//ASUS_BSP+++ Eric5_Ou "add usb otg mode switch support"
#if 0
static int msm_otg_mode_show(struct seq_file *s, void *unused)
{
	struct msm_otg *motg = s->private;
	struct usb_otg *otg = motg->phy.otg;

	switch (otg->phy->state) {
	case OTG_STATE_A_HOST:
		seq_printf(s, "host\n");
		break;
	case OTG_STATE_B_PERIPHERAL:
		seq_printf(s, "peripheral\n");
		break;
	default:
		seq_printf(s, "none\n");
		break;
	}

	return 0;
}

static int msm_otg_mode_open(struct inode *inode, struct file *file)
{
	return single_open(file, msm_otg_mode_show, inode->i_private);
}

static ssize_t msm_otg_mode_write(struct file *file, const char __user *ubuf,
				size_t count, loff_t *ppos)
{
	struct seq_file *s = file->private_data;
	struct msm_otg *motg = s->private;
	char buf[16];
	struct usb_phy *phy = &motg->phy;
	int status = count;
	enum usb_mode_type req_mode;

	memset(buf, 0x00, sizeof(buf));

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count))) {
		status = -EFAULT;
		goto out;
	}

	if (!strncmp(buf, "host", 4)) {
		req_mode = USB_HOST;
	} else if (!strncmp(buf, "peripheral", 10)) {
		req_mode = USB_PERIPHERAL;
	} else if (!strncmp(buf, "none", 4)) {
		req_mode = USB_NONE;
	} else {
		status = -EINVAL;
		goto out;
	}

	switch (req_mode) {
	case USB_NONE:
		switch (phy->state) {
		case OTG_STATE_A_HOST:
		case OTG_STATE_B_PERIPHERAL:
			set_bit(ID, &motg->inputs);
			clear_bit(B_SESS_VLD, &motg->inputs);
			break;
		default:
			goto out;
		}
		break;
	case USB_PERIPHERAL:
		switch (phy->state) {
		case OTG_STATE_B_IDLE:
		case OTG_STATE_A_HOST:
			set_bit(ID, &motg->inputs);
			set_bit(B_SESS_VLD, &motg->inputs);
			break;
		default:
			goto out;
		}
		break;
	case USB_HOST:
		switch (phy->state) {
		case OTG_STATE_B_IDLE:
		case OTG_STATE_B_PERIPHERAL:
			clear_bit(ID, &motg->inputs);
			break;
		default:
			goto out;
		}
		break;
	default:
		goto out;
	}

	pm_runtime_resume(phy->dev);
	queue_work(system_nrt_wq, &motg->sm_work);
out:
	return status;
}

const struct file_operations msm_otg_mode_fops = {
	.open = msm_otg_mode_open,
	.read = seq_read,
	.write = msm_otg_mode_write,
	.llseek = seq_lseek,
	.release = single_release,
};
#endif

static void asus_otg_vbus_out_enable(bool enable, bool force)
{
	struct msm_otg *motg = the_msm_otg;

	if (force) {
		msm_hsusb_vbus_power(motg, enable);
	} else {
//		if (!AX_MicroP_IsP01Connected() || !pad_exist()) {
			msm_hsusb_vbus_power(motg, enable);
//		} else {
//			dev_info(motg->phy.dev, "ignore to set vbus enable in pad (%d)\n", enable);
//		}
	}
}

//ASUS_BSP+++ Eric5_Ou "init usb host parameters"
static void asus_otg_host_mode_prepare(void) {
	g_suspend_delay_work_run = 0;
	g_keep_power_on = 0;
	g_host_none_mode = 0;
}
//ASUS_BSP--- Eric5_Ou "init usb host parameters"

static void asus_otg_mode_switch(enum usb_mode_type req_mode)
{
	struct msm_otg *motg = the_msm_otg;

	switch (req_mode) {
	case USB_NONE:
		printk("[usb_otg] switch to none mode\r\n");
		set_bit(ID, &motg->inputs);
		clear_bit(B_SESS_VLD, &motg->inputs);
		g_host_mode = 0;
		break;
	case USB_PERIPHERAL:
		printk("[usb_otg] switch to peripheral mode\r\n");
		set_bit(ID, &motg->inputs);
		g_host_mode = 0;
		break;
	case USB_HOST:
		printk("[usb_otg] switch to host mode\r\n");
		//ASUS_BSP+++ Eric5_Ou "init usb host parameters"
		asus_otg_host_mode_prepare();
		//ASUS_BSP--- Eric5_Ou "init usb host parameters"
		clear_bit(ID, &motg->inputs);
		g_host_mode = 1;
		break;
	case USB_AUTO:
//		if (AX_MicroP_IsP01Connected() && pad_exist()) {
//			printk("[usb_otg] switch to host mode (auto)\r\n");
			//ASUS_BSP+++ Eric5_Ou "init usb host parameters"
//			asus_otg_host_mode_prepare();
			//ASUS_BSP--- Eric5_Ou "init usb host parameters"
//			clear_bit(ID, &motg->inputs);
//			g_host_mode = 1;
//		} else {
			printk("[usb_otg] switch to peripheral mode (auto)\r\n");
			set_bit(ID, &motg->inputs);
			g_host_mode = 0;
//		}
		break;
	default:
		printk("[usb_otg] unknown mode!!! (%d)\r\n", req_mode);
		goto out;
	}

	//ASUS_BSP+++ Eric5_Ou "Add pm_suspended judgement to avoid system crash"
	if (atomic_read(&motg->pm_suspended))
		motg->sm_work_pending = true;
	else
		queue_work(system_nrt_wq, &motg->sm_work);
	//ASUS_BSP--- Eric5_Ou "Add pm_suspended judgement to avoid system crash"
out:
	return;
}

static int asus_otg_mode_show(struct seq_file *s, void *unused)
{
	struct msm_otg *motg = s->private;

	if (USB_AUTO == motg->otg_mode) {
		if(!test_bit(ID, &motg->inputs)) {
			seq_printf(s, "host (auto)\n");
		} else {
			seq_printf(s, "peripheral (auto)\n");
		}
	} else {
		if(!test_bit(ID, &motg->inputs)) {
			seq_printf(s, "host\n");
		} else {
			seq_printf(s, "peripheral\n");
		}
	}

	return 0;
}

static int asus_otg_mode_open(struct inode *inode, struct file *file)
{
	return single_open(file, asus_otg_mode_show, inode->i_private);
}

static ssize_t asus_otg_mode_write(struct file *file, const char __user *ubuf,
				size_t count, loff_t *ppos)
{
	struct seq_file *s = file->private_data;
	struct msm_otg *motg = s->private;
	char buf[16];
	int status = count;
	enum usb_mode_type req_mode;

	memset(buf, 0x00, sizeof(buf));

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count))) {
		status = -EFAULT;
		goto out;
	}

	if (!strncmp(buf, "host", 4)) {
		req_mode = USB_HOST;
		asus_otg_vbus_out_enable(true, 0);
	} else if (!strncmp(buf, "peripheral", 10)) {
		req_mode = USB_PERIPHERAL;
		asus_otg_vbus_out_enable(false, 0);
	} else if (!strncmp(buf, "none", 4)) {
		req_mode = USB_NONE;
	} else if (!strncmp(buf, "auto", 4)) {
		asus_otg_vbus_out_enable(false, 0);
		req_mode = USB_AUTO;
	} else {
		status = -EINVAL;
		goto out;
	}

	motg->otg_mode = req_mode;
	asus_otg_mode_switch(req_mode);
out:
	return status;
}

const struct file_operations asus_otg_mode_fops = {
	.open = asus_otg_mode_open,
	.read = seq_read,
	.write = asus_otg_mode_write,
	.llseek = seq_lseek,
	.release = single_release,
};
//ASUS_BSP--- Eric5_Ou  "add usb otg mode switch support"

//ASUS_BSP+++ Eric5_Ou "add dynamic setting support for phy parameters"
static int myxtoi(const char *name)
{
	int val = 0;

	for (;; name++) {
		switch (*name) {
		case '0' ... '9':
			val = 16*val+(*name-'0');
			break;
		case 'A' ... 'F':
			val = 16*val+(*name-'A'+10);
			break;
		case 'a' ... 'f':
			val = 16*val+(*name-'a'+10);
			break;
		default:
			return val;
		}
	}
}

static int msm_otg_phy_parameter_b_show(struct seq_file *s, void *unused)
{
	seq_printf(s, "reg: 0x81, value: 0x%X\n", g_phy_parameter_b);
	return 0;
}

static int msm_otg_phy_parameter_b_open(struct inode *inode, struct file *file)
{
	return single_open(file, msm_otg_phy_parameter_b_show, inode->i_private);
}

static ssize_t msm_otg_phy_parameter_b_write(struct file *file, const char __user *ubuf,
				size_t count, loff_t *ppos)
{
	char buf[16];
	int status = count;

	memset(buf, 0x00, sizeof(buf));

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count))) {
		status = -EFAULT;
		goto out;
	}

	g_phy_parameter_b = myxtoi(buf);

out:
	return status;
}

const struct file_operations msm_otg_phy_parameter_b_fops = {
	.open = msm_otg_phy_parameter_b_open,
	.read = seq_read,
	.write = msm_otg_phy_parameter_b_write,
	.llseek = seq_lseek,
	.release = single_release,
};

static int msm_otg_phy_parameter_c_show(struct seq_file *s, void *unused)
{
	seq_printf(s, "reg: 0x82, value: 0x%X\n", g_phy_parameter_c);
	return 0;
}

static int msm_otg_phy_parameter_c_open(struct inode *inode, struct file *file)
{
	return single_open(file, msm_otg_phy_parameter_c_show, inode->i_private);
}

static ssize_t msm_otg_phy_parameter_c_write(struct file *file, const char __user *ubuf,
				size_t count, loff_t *ppos)
{
	char buf[16];
	int status = count;

	memset(buf, 0x00, sizeof(buf));

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count))) {
		status = -EFAULT;
		goto out;
	}

	g_phy_parameter_c = myxtoi(buf);

out:
	return status;
}

const struct file_operations msm_otg_phy_parameter_c_fops = {
	.open = msm_otg_phy_parameter_c_open,
	.read = seq_read,
	.write = msm_otg_phy_parameter_c_write,
	.llseek = seq_lseek,
	.release = single_release,
};

static int msm_otg_phy_parameter_d_show(struct seq_file *s, void *unused)
{
	seq_printf(s, "reg: 0x83, value: 0x%X\n", g_phy_parameter_d);
	return 0;
}

static int msm_otg_phy_parameter_d_open(struct inode *inode, struct file *file)
{
	return single_open(file, msm_otg_phy_parameter_d_show, inode->i_private);
}

static ssize_t msm_otg_phy_parameter_d_write(struct file *file, const char __user *ubuf,
				size_t count, loff_t *ppos)
{
	char buf[16];
	int status = count;

	memset(buf, 0x00, sizeof(buf));

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count))) {
		status = -EFAULT;
		goto out;
	}

	g_phy_parameter_d = myxtoi(buf);

out:
	return status;
}

const struct file_operations msm_otg_phy_parameter_d_fops = {
	.open = msm_otg_phy_parameter_d_open,
	.read = seq_read,
	.write = msm_otg_phy_parameter_d_write,
	.llseek = seq_lseek,
	.release = single_release,
};
//ASUS_BSP--- Eric5_Ou "add dynamic setting support for phy parameters"

//ASUS_BSP+++ Eric5_Ou "add otg 5v output debug file"
static int asus_otg_5v_output_show(struct seq_file *s, void *unused)
{
	if (g_otg_5v_output) {
		seq_printf(s, "enable\n");
	} else {
		seq_printf(s, "disable\n");
	}

	return 0;
}

static int asus_otg_5v_output_open(struct inode *inode, struct file *file)
{
	return single_open(file, asus_otg_5v_output_show, inode->i_private);
}

static ssize_t asus_otg_5v_output_write(struct file *file, const char __user *ubuf,
				size_t count, loff_t *ppos)
{
	char buf[16];
	int status = count;

	memset(buf, 0x00, sizeof(buf));

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count))) {
		status = -EFAULT;
		goto out;
	}

	if (!strncmp(buf, "enable", 6)) {
		asus_otg_vbus_out_enable(true, 1);
	} else if (!strncmp(buf, "disable", 7)) {
		asus_otg_vbus_out_enable(false, 1);
	} else {
		status = -EINVAL;
		goto out;
	}
out:
	return status;
}

const struct file_operations asus_otg_5v_output_fops = {
	.open = asus_otg_5v_output_open,
	.read = seq_read,
	.write = asus_otg_5v_output_write,
	.llseek = seq_lseek,
	.release = single_release,
};
//ASUS_BSP--- Eric5_Ou "add otg 5v output debug file"

//ASUS_BSP+++ Eric5_Ou "add reset on disconnect flag debug file"
static int asus_otg_reset_on_disconnect_show(struct seq_file *s, void *unused)
{
	struct msm_otg *motg = the_msm_otg;
	struct msm_otg_platform_data *pdata = motg->pdata;

	if (pdata->disable_reset_on_disconnect) {
		seq_printf(s, "1\n");
	} else {
		seq_printf(s, "0\n");
	}

	return 0;
}

static int asus_otg_reset_on_disconnect_open(struct inode *inode, struct file *file)
{
	return single_open(file, asus_otg_reset_on_disconnect_show, inode->i_private);
}

static ssize_t asus_otg_reset_on_disconnect_write(struct file *file, const char __user *ubuf,
				size_t count, loff_t *ppos)
{
	struct msm_otg *motg = the_msm_otg;
	struct msm_otg_platform_data *pdata = motg->pdata;
	char buf[16];
	int status = count;

	memset(buf, 0x00, sizeof(buf));

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count))) {
		status = -EFAULT;
		goto out;
	}

	if (!strncmp(buf, "1", 1)) {
		pdata->disable_reset_on_disconnect = 1;
	} else if (!strncmp(buf, "0", 1)) {
		pdata->disable_reset_on_disconnect = 0;
	} else {
		status = -EINVAL;
		goto out;
	}
out:
	return status;
}

const struct file_operations asus_otg_reset_on_disconnect_fops = {
	.open = asus_otg_reset_on_disconnect_open,
	.read = seq_read,
	.write = asus_otg_reset_on_disconnect_write,
	.llseek = seq_lseek,
	.release = single_release,
};
//ASUS_BSP--- Eric5_Ou "add reset on disconnect flag debug file"

static int msm_otg_show_otg_state(struct seq_file *s, void *unused)
{
	struct msm_otg *motg = s->private;
	struct usb_phy *phy = &motg->phy;

	seq_printf(s, "%s\n", otg_state_string(phy->state));
	return 0;
}

static int msm_otg_otg_state_open(struct inode *inode, struct file *file)
{
	return single_open(file, msm_otg_show_otg_state, inode->i_private);
}

const struct file_operations msm_otg_state_fops = {
	.open = msm_otg_otg_state_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int msm_otg_show_chg_type(struct seq_file *s, void *unused)
{
	struct msm_otg *motg = s->private;

	seq_printf(s, "%s\n", chg_to_string(motg->chg_type));
	return 0;
}

static int msm_otg_chg_open(struct inode *inode, struct file *file)
{
	return single_open(file, msm_otg_show_chg_type, inode->i_private);
}

const struct file_operations msm_otg_chg_fops = {
	.open = msm_otg_chg_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int msm_otg_aca_show(struct seq_file *s, void *unused)
{
	if (debug_aca_enabled)
		seq_printf(s, "enabled\n");
	else
		seq_printf(s, "disabled\n");

	return 0;
}

static int msm_otg_aca_open(struct inode *inode, struct file *file)
{
	return single_open(file, msm_otg_aca_show, inode->i_private);
}

static ssize_t msm_otg_aca_write(struct file *file, const char __user *ubuf,
				size_t count, loff_t *ppos)
{
	char buf[8];

	memset(buf, 0x00, sizeof(buf));

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count)))
		return -EFAULT;

	if (!strncmp(buf, "enable", 6))
		debug_aca_enabled = true;
	else
		debug_aca_enabled = false;

	return count;
}

const struct file_operations msm_otg_aca_fops = {
	.open = msm_otg_aca_open,
	.read = seq_read,
	.write = msm_otg_aca_write,
	.llseek = seq_lseek,
	.release = single_release,
};

static int msm_otg_bus_show(struct seq_file *s, void *unused)
{
	if (debug_bus_voting_enabled)
		seq_printf(s, "enabled\n");
	else
		seq_printf(s, "disabled\n");

	return 0;
}

static int msm_otg_bus_open(struct inode *inode, struct file *file)
{
	return single_open(file, msm_otg_bus_show, inode->i_private);
}

static ssize_t msm_otg_bus_write(struct file *file, const char __user *ubuf,
				size_t count, loff_t *ppos)
{
	char buf[8];
	struct seq_file *s = file->private_data;
	struct msm_otg *motg = s->private;

	memset(buf, 0x00, sizeof(buf));

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count)))
		return -EFAULT;

	if (!strncmp(buf, "enable", 6)) {
		/* Do not vote here. Let OTG statemachine decide when to vote */
		debug_bus_voting_enabled = true;
	} else {
		debug_bus_voting_enabled = false;
		msm_otg_bus_vote(motg, USB_MIN_PERF_VOTE);
	}

	return count;
}
#ifndef CONFIG_CHARGER_ASUS
static int otg_power_get_property_usb(struct power_supply *psy,
				  enum power_supply_property psp,
				  union power_supply_propval *val)
{
	struct msm_otg *motg = container_of(psy, struct msm_otg, usb_psy);
	switch (psp) {
	case POWER_SUPPLY_PROP_SCOPE:
		if (motg->host_mode)
			val->intval = POWER_SUPPLY_SCOPE_SYSTEM;
		else
			val->intval = POWER_SUPPLY_SCOPE_DEVICE;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		val->intval = motg->voltage_max;
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		val->intval = motg->current_max;
		break;
	/* Reflect USB enumeration */
	case POWER_SUPPLY_PROP_PRESENT:
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = motg->online;
		break;
	case POWER_SUPPLY_PROP_TYPE:
		val->intval = psy->type;
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = motg->usbin_health;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int otg_power_set_property_usb(struct power_supply *psy,
				  enum power_supply_property psp,
				  const union power_supply_propval *val)
{
	struct msm_otg *motg = container_of(psy, struct msm_otg, usb_psy);

	switch (psp) {
	/* Process PMIC notification in PRESENT prop */
	case POWER_SUPPLY_PROP_PRESENT:
		msm_otg_set_vbus_state(val->intval);
		break;
	/* The ONLINE property reflects if usb has enumerated */
	case POWER_SUPPLY_PROP_ONLINE:
		motg->online = val->intval;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		motg->voltage_max = val->intval;
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		motg->current_max = val->intval;
		break;
	case POWER_SUPPLY_PROP_TYPE:
		psy->type = val->intval;
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		motg->usbin_health = val->intval;
		break;
	default:
		return -EINVAL;
	}

	power_supply_changed(&motg->usb_psy);
	return 0;
}

static int otg_power_property_is_writeable_usb(struct power_supply *psy,
						enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_HEALTH:
	case POWER_SUPPLY_PROP_PRESENT:
	case POWER_SUPPLY_PROP_ONLINE:
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		return 1;
	default:
		break;
	}

	return 0;
}

static char *otg_pm_power_supplied_to[] = {
	"battery",
};

static enum power_supply_property otg_pm_power_props_usb[] = {
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_SCOPE,
	POWER_SUPPLY_PROP_TYPE,
};
#endif

//ASUS_BSP+++ Eric5_Ou "add usb otg mode switch support"
static int asus_otg_proc_mode_open(struct inode *inode, struct file *file)
{
	return single_open(file, asus_otg_mode_show, PDE(inode)->data);
}

const struct file_operations asus_otg_proc_mode_fops = {
	.open = asus_otg_proc_mode_open,
	.read = seq_read,
//	.write = asus_otg_mode_write,
	.llseek = seq_lseek,
	.release = single_release,
};
//ASUS_BSP--- Eric5_Ou "add usb otg mode switch support"

//ASUS_BSP+++ "[USB][NA][Fix] reset usb when TD_STATUS_HALTED"
static struct proc_dir_entry *asus_otg_proc_root;
static int asus_halted_reset_count = 0;
static int asus_reset_plugout = 0;
static struct delayed_work asus_halted_work;
static void asus_halted_reset_work(struct work_struct *w)
{
	struct msm_otg *motg = the_msm_otg;

	if(test_bit(B_SESS_VLD, &motg->inputs)&&!asus_reset_plugout){
		asus_reset_plugout = 1;
		clear_bit(B_SESS_VLD, &motg->inputs);
		queue_work(system_nrt_wq, &motg->sm_work);
		schedule_delayed_work(&asus_halted_work, (1000 * HZ/1000));
	}
	else{
		if(!test_bit(B_SESS_VLD, &motg->inputs) && msm_otg_bsv && asus_reset_plugout){
			asus_reset_plugout = 0;
			set_bit(B_SESS_VLD, &motg->inputs);
			queue_work(system_nrt_wq, &motg->sm_work);
		}
	}
}

void asus_otg_halted_reset(void){
	printk("trigger halted reset from callback!!!\n");
	asus_halted_reset_count+=1;
	schedule_delayed_work(&asus_halted_work, (1 * HZ/1000));
}

static int asus_otg_proc_halted_reset_show(struct seq_file *s, void *unused)
{
	seq_printf(s, "%d\n", asus_halted_reset_count);
	return 0;
}

static int asus_otg_proc_halted_reset_open(struct inode *inode, struct file *file)
{
	return single_open(file, asus_otg_proc_halted_reset_show, PDE(inode)->data);
}

static ssize_t asus_otg_proc_halted_reset_write(struct file *file, const char __user *ubuf,
				size_t count, loff_t *ppos)
{
	char buf[16];
	int status = count;

	memset(buf, 0x00, sizeof(buf));
	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count))) {
		status = -EFAULT;
		goto out;
	}
	if (!strncmp(buf, "1", 1)) {
		printk("trigger halted reset from proc!!!\n");
		asus_halted_reset_count+=1;
		schedule_delayed_work(&asus_halted_work, (1 * HZ/1000));
	} else {
		status = -EINVAL;
		goto out;
	}
out:
	return status;
}

const struct file_operations asus_otg_proc_halted_reset_fops = {
	.open = asus_otg_proc_halted_reset_open,
	.read = seq_read,
	.write = asus_otg_proc_halted_reset_write,
	.llseek = seq_lseek,
	.release = single_release,
};

static int asus_otg_proc_init(struct msm_otg *motg)
{
	struct proc_dir_entry *proc_entry;

	asus_otg_proc_root = proc_mkdir("msm_otg", NULL);
	if (!asus_otg_proc_root) {
		return -ENODEV;
	}

	//ASUS_BSP+++ Eric5_Ou "add usb otg mode switch support"
	proc_entry = proc_create_data("mode", S_IRUGO |S_IWUSR, asus_otg_proc_root,
			&asus_otg_proc_mode_fops, motg);
	if (!proc_entry) {
		remove_proc_entry("mode", asus_otg_proc_root);
		asus_otg_proc_root = NULL;
		return -ENODEV;
	}
	//ASUS_BSP--- Eric5_Ou "add usb otg mode switch support"

	proc_entry = proc_create_data("halted_reset", S_IRUGO |S_IWUSR, asus_otg_proc_root,
			&asus_otg_proc_halted_reset_fops, motg);
	if (!proc_entry) {
		remove_proc_entry("halted_reset", asus_otg_proc_root);
		asus_otg_proc_root = NULL;
		return -ENODEV;
	}


	return 0;
}
//ASUS_BSP--- "[USB][NA][Fix] reset usb when TD_STATUS_HALTED"

//ASUS_BSP+++ Eric5_Ou "add usb otg mode switch support"
static void asus_otg_proc_cleanup(void)
{
	remove_proc_entry("mode", asus_otg_proc_root);
	remove_proc_entry("msm_otg", NULL);
}
//ASUS_BSP--- Eric5_Ou "add usb otg mode switch support"

const struct file_operations msm_otg_bus_fops = {
	.open = msm_otg_bus_open,
	.read = seq_read,
	.write = msm_otg_bus_write,
	.llseek = seq_lseek,
	.release = single_release,
};

static struct dentry *msm_otg_dbg_root;

static int msm_otg_debugfs_init(struct msm_otg *motg)
{
	struct dentry *msm_otg_dentry;

	msm_otg_dbg_root = debugfs_create_dir("msm_otg", NULL);

	if (!msm_otg_dbg_root || IS_ERR(msm_otg_dbg_root))
		return -ENODEV;

	//ASUS_BSP+++ Eric5_Ou "enable otg driver debugfs"
	if (motg->pdata->mode == USB_OTG) {
	//ASUS_BSP--- Eric5_Ou "enable otg driver debugfs"
		//ASUS_BSP+++ Eric5_Ou "add usb otg mode switch support"
		msm_otg_dentry = debugfs_create_file("mode", S_IRUGO |
			S_IWUSR, msm_otg_dbg_root, motg,
			&asus_otg_mode_fops);
		//ASUS_BSP--- Eric5_Ou "add usb otg mode switch support"

		if (!msm_otg_dentry) {
			debugfs_remove(msm_otg_dbg_root);
			msm_otg_dbg_root = NULL;
			return -ENODEV;
		}
	}

	msm_otg_dentry = debugfs_create_file("chg_type", S_IRUGO,
		msm_otg_dbg_root, motg,
		&msm_otg_chg_fops);

	if (!msm_otg_dentry) {
		debugfs_remove_recursive(msm_otg_dbg_root);
		return -ENODEV;
	}

	msm_otg_dentry = debugfs_create_file("aca", S_IRUGO | S_IWUSR,
		msm_otg_dbg_root, motg,
		&msm_otg_aca_fops);

	if (!msm_otg_dentry) {
		debugfs_remove_recursive(msm_otg_dbg_root);
		return -ENODEV;
	}

	msm_otg_dentry = debugfs_create_file("bus_voting", S_IRUGO | S_IWUSR,
		msm_otg_dbg_root, motg,
		&msm_otg_bus_fops);

	if (!msm_otg_dentry) {
		debugfs_remove_recursive(msm_otg_dbg_root);
		return -ENODEV;
	}

	msm_otg_dentry = debugfs_create_file("otg_state", S_IRUGO,
				msm_otg_dbg_root, motg, &msm_otg_state_fops);

	if (!msm_otg_dentry) {
		debugfs_remove_recursive(msm_otg_dbg_root);
		return -ENODEV;
	}

	//ASUS_BSP+++ Eric5_Ou "add dynamic setting support for phy parameters"
	msm_otg_dentry = debugfs_create_file("phy_parameter_b", S_IRUGO |
		S_IWUSR, msm_otg_dbg_root, motg,
		&msm_otg_phy_parameter_b_fops);

	if (!msm_otg_dentry) {
		debugfs_remove_recursive(msm_otg_dbg_root);
		return -ENODEV;
	}

	msm_otg_dentry = debugfs_create_file("phy_parameter_c", S_IRUGO |
		S_IWUSR, msm_otg_dbg_root, motg,
		&msm_otg_phy_parameter_c_fops);

	if (!msm_otg_dentry) {
		debugfs_remove_recursive(msm_otg_dbg_root);
		return -ENODEV;
	}

	msm_otg_dentry = debugfs_create_file("phy_parameter_d", S_IRUGO |
		S_IWUSR, msm_otg_dbg_root, motg,
		&msm_otg_phy_parameter_d_fops);

	if (!msm_otg_dentry) {
		debugfs_remove_recursive(msm_otg_dbg_root);
		return -ENODEV;
	}
	//ASUS_BSP--- Eric5_Ou "add dynamic setting support for phy parameters"

	//ASUS_BSP+++ Eric5_Ou "add otg 5v output debug file"
	msm_otg_dentry = debugfs_create_file("otg_5v_output", S_IRUGO |
		S_IWUSR, msm_otg_dbg_root, motg,
		&asus_otg_5v_output_fops);

	if (!msm_otg_dentry) {
		debugfs_remove_recursive(msm_otg_dbg_root);
		return -ENODEV;
	}
	//ASUS_BSP--- Eric5_Ou "add otg 5v output debug file"

	//ASUS_BSP+++ Eric5_Ou "add reset on disconnect flag debug file"
	msm_otg_dentry = debugfs_create_file("disable_reset_on_disconnect", S_IRUGO |
		S_IWUSR, msm_otg_dbg_root, motg,
		&asus_otg_reset_on_disconnect_fops);

	if (!msm_otg_dentry) {
		debugfs_remove_recursive(msm_otg_dbg_root);
		return -ENODEV;
	}
	//ASUS_BSP--- Eric5_Ou "add reset on disconnect flag debug file"
	return 0;
}

static void msm_otg_debugfs_cleanup(void)
{
	debugfs_remove_recursive(msm_otg_dbg_root);
}

#define MSM_OTG_CMD_ID		0x09
#define MSM_OTG_DEVICE_ID	0x04
#define MSM_OTG_VMID_IDX	0xFF
#define MSM_OTG_MEM_TYPE	0x02
struct msm_otg_scm_cmd_buf {
	unsigned int device_id;
	unsigned int vmid_idx;
	unsigned int mem_type;
} __attribute__ ((__packed__));

static void msm_otg_pnoc_errata_fix(struct msm_otg *motg)
{
	int ret;
	struct msm_otg_platform_data *pdata = motg->pdata;
	struct msm_otg_scm_cmd_buf cmd_buf;

	if (!pdata->pnoc_errata_fix)
		return;

	dev_dbg(motg->phy.dev, "applying fix for pnoc h/w issue\n");

	cmd_buf.device_id = MSM_OTG_DEVICE_ID;
	cmd_buf.vmid_idx = MSM_OTG_VMID_IDX;
	cmd_buf.mem_type = MSM_OTG_MEM_TYPE;

	ret = scm_call(SCM_SVC_MP, MSM_OTG_CMD_ID, &cmd_buf,
				sizeof(cmd_buf), NULL, 0);

	if (ret)
		dev_err(motg->phy.dev, "scm command failed to update VMIDMT\n");
}

static u64 msm_otg_dma_mask = DMA_BIT_MASK(64);
static struct platform_device *msm_otg_add_pdev(
		struct platform_device *ofdev, const char *name)
{
	struct platform_device *pdev;
	const struct resource *res = ofdev->resource;
	unsigned int num = ofdev->num_resources;
	int retval;
	struct ci13xxx_platform_data ci_pdata;
	struct msm_otg_platform_data *otg_pdata;

	pdev = platform_device_alloc(name, -1);
	if (!pdev) {
		retval = -ENOMEM;
		goto error;
	}

	pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);
	pdev->dev.dma_mask = &msm_otg_dma_mask;

	if (num) {
		retval = platform_device_add_resources(pdev, res, num);
		if (retval)
			goto error;
	}

	if (!strcmp(name, "msm_hsusb")) {
		otg_pdata =
			(struct msm_otg_platform_data *)
				ofdev->dev.platform_data;
		ci_pdata.log2_itc = otg_pdata->log2_itc;
		ci_pdata.usb_core_id = 0;
		ci_pdata.l1_supported = otg_pdata->l1_supported;
		ci_pdata.enable_ahb2ahb_bypass =
				otg_pdata->enable_ahb2ahb_bypass;
		retval = platform_device_add_data(pdev, &ci_pdata,
			sizeof(ci_pdata));
		if (retval)
			goto error;
	}

	retval = platform_device_add(pdev);
	if (retval)
		goto error;

	return pdev;

error:
	platform_device_put(pdev);
	return ERR_PTR(retval);
}

static int msm_otg_setup_devices(struct platform_device *ofdev,
		enum usb_mode_type mode, bool init)
{
	const char *gadget_name = "msm_hsusb";
	const char *host_name = "msm_hsusb_host";
	static struct platform_device *gadget_pdev;
	static struct platform_device *host_pdev;
	int retval = 0;

	if (!init) {
		if (gadget_pdev)
			platform_device_unregister(gadget_pdev);
		if (host_pdev)
			platform_device_unregister(host_pdev);
		return 0;
	}

	switch (mode) {
	case USB_OTG:
		/* fall through */
	case USB_PERIPHERAL:
		gadget_pdev = msm_otg_add_pdev(ofdev, gadget_name);
		if (IS_ERR(gadget_pdev)) {
			retval = PTR_ERR(gadget_pdev);
			break;
		}
		if (mode == USB_PERIPHERAL)
			break;
		/* fall through */
	case USB_HOST:
		host_pdev = msm_otg_add_pdev(ofdev, host_name);
		if (IS_ERR(host_pdev)) {
			retval = PTR_ERR(host_pdev);
			if (mode == USB_OTG)
				platform_device_unregister(gadget_pdev);
		}
		break;
	default:
		break;
	}

	return retval;
}
#ifndef CONFIG_CHARGER_ASUS
static int msm_otg_register_power_supply(struct platform_device *pdev,
					struct msm_otg *motg)
{
	int ret;

	ret = power_supply_register(&pdev->dev, &motg->usb_psy);
	if (ret < 0) {
		dev_err(motg->phy.dev,
			"%s:power_supply_register usb failed\n",
			__func__);
		return ret;
	}

	legacy_power_supply = false;
	return 0;
}
#endif
static int msm_otg_ext_chg_open(struct inode *inode, struct file *file)
{
	struct msm_otg *motg = the_msm_otg;

	pr_debug("msm_otg ext chg open\n");

	motg->ext_chg_opened = true;
	file->private_data = (void *)motg;
	return 0;
}

static long
msm_otg_ext_chg_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct msm_otg *motg = file->private_data;
	struct msm_usb_chg_info info = {0};
	int ret = 0, val;

	switch (cmd) {
	case MSM_USB_EXT_CHG_INFO:
		info.chg_block_type = USB_CHG_BLOCK_ULPI;
		info.page_offset = motg->io_res->start & ~PAGE_MASK;
		/* mmap() works on PAGE granularity */
		info.length = PAGE_SIZE;

		if (copy_to_user((void __user *)arg, &info, sizeof(info))) {
			pr_err("%s: copy to user failed\n\n", __func__);
			ret = -EFAULT;
		}
		break;
	case MSM_USB_EXT_CHG_BLOCK_LPM:
		if (get_user(val, (int __user *)arg)) {
			pr_err("%s: get_user failed\n\n", __func__);
			ret = -EFAULT;
			break;
		}
		pr_debug("%s: LPM block request %d\n", __func__, val);
		if (val) { /* block LPM */
			if (motg->chg_type == USB_DCP_CHARGER) {
				/*
				 * If device is already suspended, resume it.
				 * The PM usage counter is incremented in
				 * runtime resume method.  if device is not
				 * suspended, cancel the scheduled suspend
				 * and increment the PM usage counter.
				 */
				if (pm_runtime_suspended(motg->phy.dev))
					pm_runtime_resume(motg->phy.dev);
				else {
					pm_runtime_get_sync(motg->phy.dev);
					//ASUS_BSP+++ Eric5_Ou "Add PM usage count debug log and protection mechanism"
					otg_pm_count+= 1;
					printk("[OTG-PM][%s] get usage_count:%d, otg_pm_count:%d\n",__func__, atomic_read(&motg->phy.dev->power.usage_count),otg_pm_count);
					//ASUS_BSP--- Eric5_Ou "Add PM usage count debug log and protection mechanism"
				}
			} else {
				motg->ext_chg_active = false;
				complete(&motg->ext_chg_wait);
				ret = -ENODEV;
			}
		} else {
			motg->ext_chg_active = false;
			complete(&motg->ext_chg_wait);
			pm_runtime_put(motg->phy.dev);
			//ASUS_BSP+++ Eric5_Ou "Add PM usage count debug log and protection mechanism"
			otg_pm_count-= 1;
			printk("[OTG-PM][%s] put usage_count:%d, otg_pm_count:%d\n",__func__, atomic_read(&motg->phy.dev->power.usage_count),otg_pm_count);
			//ASUS_BSP--- Eric5_Ou "Add PM usage count debug log and protection mechanism"
		}
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int msm_otg_ext_chg_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct msm_otg *motg = file->private_data;
	unsigned long vsize = vma->vm_end - vma->vm_start;
	int ret;

	if (vma->vm_pgoff || vsize > PAGE_SIZE)
		return -EINVAL;

	vma->vm_pgoff = __phys_to_pfn(motg->io_res->start);
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	ret = io_remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
				 vsize, vma->vm_page_prot);
	if (ret < 0) {
		pr_err("%s: failed with return val %d\n", __func__, ret);
		return ret;
	}

	return 0;
}

static int msm_otg_ext_chg_release(struct inode *inode, struct file *file)
{
	struct msm_otg *motg = file->private_data;

	pr_debug("msm_otg ext chg release\n");

	motg->ext_chg_opened = false;

	return 0;
}

static const struct file_operations msm_otg_ext_chg_fops = {
	.owner = THIS_MODULE,
	.open = msm_otg_ext_chg_open,
	.unlocked_ioctl = msm_otg_ext_chg_ioctl,
	.mmap = msm_otg_ext_chg_mmap,
	.release = msm_otg_ext_chg_release,
};

static int msm_otg_setup_ext_chg_cdev(struct msm_otg *motg)
{
	int ret;

	if (motg->pdata->enable_sec_phy || motg->pdata->mode == USB_HOST ||
			motg->pdata->otg_control != OTG_PMIC_CONTROL ||
			psy != &motg->usb_psy) {
		pr_debug("usb ext chg is not supported by msm otg\n");
		return -ENODEV;
	}

	ret = alloc_chrdev_region(&motg->ext_chg_dev, 0, 1, "usb_ext_chg");
	if (ret < 0) {
		pr_err("Fail to allocate usb ext char dev region\n");
		return ret;
	}
	motg->ext_chg_class = class_create(THIS_MODULE, "msm_ext_chg");
	if (ret < 0) {
		pr_err("Fail to create usb ext chg class\n");
		goto unreg_chrdev;
	}
	cdev_init(&motg->ext_chg_cdev, &msm_otg_ext_chg_fops);
	motg->ext_chg_cdev.owner = THIS_MODULE;

	ret = cdev_add(&motg->ext_chg_cdev, motg->ext_chg_dev, 1);
	if (ret < 0) {
		pr_err("Fail to add usb ext chg cdev\n");
		goto destroy_class;
	}
	motg->ext_chg_device = device_create(motg->ext_chg_class,
					NULL, motg->ext_chg_dev, NULL,
					"usb_ext_chg");
	if (IS_ERR(motg->ext_chg_device)) {
		pr_err("Fail to create usb ext chg device\n");
		ret = PTR_ERR(motg->ext_chg_device);
		motg->ext_chg_device = NULL;
		goto del_cdev;
	}

	init_completion(&motg->ext_chg_wait);
	pr_debug("msm otg ext chg cdev setup success\n");
	return 0;

del_cdev:
	cdev_del(&motg->ext_chg_cdev);
destroy_class:
	class_destroy(motg->ext_chg_class);
unreg_chrdev:
	unregister_chrdev_region(motg->ext_chg_dev, 1);

	return ret;
}

static ssize_t dpdm_pulldown_enable_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct msm_otg *motg = the_msm_otg;
	struct msm_otg_platform_data *pdata = motg->pdata;

	return snprintf(buf, PAGE_SIZE, "%s\n", pdata->dpdm_pulldown_added ?
							"enabled" : "disabled");
}

static ssize_t dpdm_pulldown_enable_store(struct device *dev,
		struct device_attribute *attr, const char
		*buf, size_t size)
{
	struct msm_otg *motg = the_msm_otg;
	struct msm_otg_platform_data *pdata = motg->pdata;

	if (!strnicmp(buf, "enable", 6)) {
		pdata->dpdm_pulldown_added = true;
		return size;
	} else if (!strnicmp(buf, "disable", 7)) {
		pdata->dpdm_pulldown_added = false;
		return size;
	}

	return -EINVAL;
}

static DEVICE_ATTR(dpdm_pulldown_enable, S_IRUGO | S_IWUSR,
		dpdm_pulldown_enable_show, dpdm_pulldown_enable_store);

struct msm_otg_platform_data *msm_otg_dt_to_pdata(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct msm_otg_platform_data *pdata;
	int len = 0;

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata) {
		pr_err("unable to allocate platform data\n");
		return NULL;
	}
	of_get_property(node, "qcom,hsusb-otg-phy-init-seq", &len);
	if (len) {
		pdata->phy_init_seq = devm_kzalloc(&pdev->dev, len, GFP_KERNEL);
		if (!pdata->phy_init_seq)
			return NULL;
		of_property_read_u32_array(node, "qcom,hsusb-otg-phy-init-seq",
				pdata->phy_init_seq,
				len/sizeof(*pdata->phy_init_seq));
	}
	of_property_read_u32(node, "qcom,hsusb-otg-power-budget",
				&pdata->power_budget);
	of_property_read_u32(node, "qcom,hsusb-otg-mode",
				&pdata->mode);
	of_property_read_u32(node, "qcom,hsusb-otg-otg-control",
				&pdata->otg_control);
	of_property_read_u32(node, "qcom,hsusb-otg-default-mode",
				&pdata->default_mode);
	of_property_read_u32(node, "qcom,hsusb-otg-phy-type",
				&pdata->phy_type);
	pdata->disable_reset_on_disconnect = of_property_read_bool(node,
				"qcom,hsusb-otg-disable-reset");
	pdata->pnoc_errata_fix = of_property_read_bool(node,
				"qcom,hsusb-otg-pnoc-errata-fix");
	pdata->enable_lpm_on_dev_suspend = of_property_read_bool(node,
				"qcom,hsusb-otg-lpm-on-dev-suspend");
	pdata->core_clk_always_on_workaround = of_property_read_bool(node,
				"qcom,hsusb-otg-clk-always-on-workaround");
	pdata->delay_lpm_on_disconnect = of_property_read_bool(node,
				"qcom,hsusb-otg-delay-lpm");
	pdata->delay_lpm_hndshk_on_disconnect = of_property_read_bool(node,
				"qcom,hsusb-otg-delay-lpm-hndshk-on-disconnect");
	pdata->dp_manual_pullup = of_property_read_bool(node,
				"qcom,dp-manual-pullup");
	pdata->enable_sec_phy = of_property_read_bool(node,
					"qcom,usb2-enable-hsphy2");
	of_property_read_u32(node, "qcom,hsusb-log2-itc",
				&pdata->log2_itc);

	of_property_read_u32(node, "qcom,hsusb-otg-mpm-dpsehv-int",
				&pdata->mpm_dpshv_int);
	of_property_read_u32(node, "qcom,hsusb-otg-mpm-dmsehv-int",
				&pdata->mpm_dmshv_int);
	pdata->pmic_id_irq = platform_get_irq_byname(pdev, "pmic_id_irq");
	if (pdata->pmic_id_irq < 0)
		pdata->pmic_id_irq = 0;

	pdata->l1_supported = of_property_read_bool(node,
				"qcom,hsusb-l1-supported");
	pdata->enable_ahb2ahb_bypass = of_property_read_bool(node,
				"qcom,ahb-async-bridge-bypass");
	pdata->disable_retention_with_vdd_min = of_property_read_bool(node,
				"qcom,disable-retention-with-vdd-min");
#ifdef CONFIG_CHARGER_ASUS
	pdata->otg_control = OTG_PMIC_CONTROL;
#endif

	//ASUS_BSP+++ Eric5_Ou "init otg driver parameters"
	pdata->mode = USB_OTG;

	dev_info(&pdev->dev, "power_budget: %d\n", pdata->power_budget);
	dev_info(&pdev->dev, "mode: %d\n", pdata->mode);
	dev_info(&pdev->dev, "otg_control: %d\n", pdata->otg_control);
	dev_info(&pdev->dev, "default_mode: %d\n", pdata->default_mode);
	dev_info(&pdev->dev, "phy_type: %d\n", pdata->phy_type);
	dev_info(&pdev->dev, "pmic_id_irq: %d\n", pdata->pmic_id_irq);
	dev_info(&pdev->dev, "disable_reset_on_disconnect: %d\n", pdata->disable_reset_on_disconnect);
	dev_info(&pdev->dev, "pnoc_errata_fix: %d\n", pdata->pnoc_errata_fix);
	//ASUS_BSP--- Eric5_Ou "init otg driver parameters"

	return pdata;
}

static int __init msm_otg_probe(struct platform_device *pdev)
{
	int ret = 0;
	int len = 0;
	u32 tmp[3];
	struct resource *res;
	struct msm_otg *motg;
	struct usb_phy *phy;
	struct msm_otg_platform_data *pdata;

	dev_info(&pdev->dev, "msm_otg probe\n");

	if (pdev->dev.of_node) {
		dev_dbg(&pdev->dev, "device tree enabled\n");
		pdata = msm_otg_dt_to_pdata(pdev);
		if (!pdata)
			return -ENOMEM;

		pdata->bus_scale_table = msm_bus_cl_get_pdata(pdev);
		if (!pdata->bus_scale_table)
			dev_dbg(&pdev->dev, "bus scaling is disabled\n");

		pdev->dev.platform_data = pdata;
		ret = msm_otg_setup_devices(pdev, pdata->mode, true);
		if (ret) {
			dev_err(&pdev->dev, "devices setup failed\n");
			return ret;
		}
	} else if (!pdev->dev.platform_data) {
		dev_err(&pdev->dev, "No platform data given. Bailing out\n");
		return -ENODEV;
	} else {
		pdata = pdev->dev.platform_data;
	}

	motg = devm_kzalloc(&pdev->dev, sizeof(struct msm_otg), GFP_KERNEL);
	if (!motg) {
		dev_err(&pdev->dev, "unable to allocate msm_otg\n");
		return -ENOMEM;
	}

	motg->phy.otg = devm_kzalloc(&pdev->dev, sizeof(struct usb_otg),
							GFP_KERNEL);
	if (!motg->phy.otg) {
		dev_err(&pdev->dev, "unable to allocate usb_otg\n");
		return -ENOMEM;
	}

	the_msm_otg = motg;
	motg->pdata = pdata;
	phy = &motg->phy;
	phy->dev = &pdev->dev;

	if (motg->pdata->bus_scale_table) {
		motg->bus_perf_client =
		    msm_bus_scale_register_client(motg->pdata->bus_scale_table);
		if (!motg->bus_perf_client) {
			dev_err(motg->phy.dev, "%s: Failed to register BUS\n"
						"scaling client!!\n", __func__);
		} else {
			debug_bus_voting_enabled = true;
			/* Some platforms require BUS vote to control clocks */
			msm_otg_bus_vote(motg, USB_MIN_PERF_VOTE);
		}
	}

	/*
	 * ACA ID_GND threshold range is overlapped with OTG ID_FLOAT.  Hence
	 * PHY treat ACA ID_GND as float and no interrupt is generated.  But
	 * PMIC can detect ACA ID_GND and generate an interrupt.
	 */
	if (aca_enabled() && motg->pdata->otg_control != OTG_PMIC_CONTROL) {
		dev_err(&pdev->dev, "ACA can not be enabled without PMIC\n");
		ret = -EINVAL;
		goto devote_bus_bw;
	}

	/* initialize reset counter */
	motg->reset_counter = 0;

	/*
	 * Targets on which link uses asynchronous reset methodology,
	 * free running clock is not required during the reset.
	 */
	motg->clk = clk_get(&pdev->dev, "alt_core_clk");
	if (IS_ERR(motg->clk))
		dev_dbg(&pdev->dev, "alt_core_clk is not present\n");
	else
		clk_set_rate(motg->clk, 60000000);

	/*
	 * USB Core is running its protocol engine based on CORE CLK,
	 * CORE CLK  must be running at >55Mhz for correct HSUSB
	 * operation and USB core cannot tolerate frequency changes on
	 * CORE CLK. For such USB cores, vote for maximum clk frequency
	 * on pclk source
	 */
	motg->core_clk = clk_get(&pdev->dev, "core_clk");
	if (IS_ERR(motg->core_clk)) {
		motg->core_clk = NULL;
		dev_err(&pdev->dev, "failed to get core_clk\n");
		ret = PTR_ERR(motg->core_clk);
		goto put_clk;
	}

	/*
	 * Get Max supported clk frequency for USB Core CLK and request
	 * to set the same.
	 */
	motg->core_clk_rate = clk_round_rate(motg->core_clk, LONG_MAX);
	if (IS_ERR_VALUE(motg->core_clk_rate)) {
		dev_err(&pdev->dev, "fail to get core clk max freq.\n");
	} else {
		ret = clk_set_rate(motg->core_clk, motg->core_clk_rate);
		if (ret)
			dev_err(&pdev->dev, "fail to set core_clk freq:%d\n",
									ret);
	}

	motg->pclk = clk_get(&pdev->dev, "iface_clk");
	if (IS_ERR(motg->pclk)) {
		dev_err(&pdev->dev, "failed to get iface_clk\n");
		ret = PTR_ERR(motg->pclk);
		goto put_core_clk;
	}

	/*
	 * On few platforms USB PHY is fed with sleep clk.
	 * Hence don't fail probe.
	 */
	motg->sleep_clk = devm_clk_get(&pdev->dev, "sleep_clk");
	if (IS_ERR(motg->sleep_clk)) {
		dev_dbg(&pdev->dev, "failed to get sleep_clk\n");
	} else {
		ret = clk_prepare_enable(motg->sleep_clk);
		if (ret) {
			dev_err(&pdev->dev, "%s failed to vote sleep_clk%d\n",
							__func__, ret);
			goto put_pclk;
		}
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "failed to get platform resource mem\n");
		ret = -ENODEV;
		goto disable_sleep_clk;
	}

	motg->io_res = res;
	motg->regs = ioremap(res->start, resource_size(res));
	if (!motg->regs) {
		dev_err(&pdev->dev, "ioremap failed\n");
		ret = -ENOMEM;
		goto disable_sleep_clk;
	}
	dev_info(&pdev->dev, "OTG regs = %p\n", motg->regs);

	motg->irq = platform_get_irq(pdev, 0);
	if (!motg->irq) {
		dev_err(&pdev->dev, "platform_get_irq failed\n");
		ret = -ENODEV;
		goto free_regs;
	}

	motg->async_irq = platform_get_irq_byname(pdev, "async_irq");
	if (motg->async_irq < 0) {
		dev_dbg(&pdev->dev, "platform_get_irq for async_int failed\n");
		motg->async_irq = 0;
	}

	motg->xo_clk = clk_get(&pdev->dev, "xo");
	if (IS_ERR(motg->xo_clk)) {
		motg->xo_handle = msm_xo_get(MSM_XO_TCXO_D0, "usb");
		if (IS_ERR(motg->xo_handle)) {
			dev_err(&pdev->dev, "%s fail to get handle for TCXO\n",
								__func__);
			ret = PTR_ERR(motg->xo_handle);
			goto free_regs;
		} else {
			ret = msm_xo_mode_vote(motg->xo_handle, MSM_XO_MODE_ON);
			if (ret) {
				dev_err(&pdev->dev, "%s XO voting failed %d\n",
								__func__, ret);
				goto free_xo_handle;
			}
		}
	} else {
		ret = clk_prepare_enable(motg->xo_clk);
		if (ret) {
			dev_err(&pdev->dev, "%s failed to vote for TCXO %d\n",
							__func__, ret);
			goto free_xo_handle;
		}
	}


	clk_prepare_enable(motg->pclk);

	motg->vdd_type = VDDCX_CORNER;
	hsusb_vdd = devm_regulator_get(motg->phy.dev, "hsusb_vdd_dig");
	if (IS_ERR(hsusb_vdd)) {
		hsusb_vdd = devm_regulator_get(motg->phy.dev, "HSUSB_VDDCX");
		if (IS_ERR(hsusb_vdd)) {
			dev_err(motg->phy.dev, "unable to get hsusb vddcx\n");
			ret = PTR_ERR(hsusb_vdd);
			goto devote_xo_handle;
		}
		motg->vdd_type = VDDCX;
	}

	if (pdev->dev.of_node) {
		of_get_property(pdev->dev.of_node,
				"qcom,vdd-voltage-level",
				&len);
		if (len == sizeof(tmp)) {
			of_property_read_u32_array(pdev->dev.of_node,
					"qcom,vdd-voltage-level",
					tmp, len/sizeof(*tmp));
			vdd_val[motg->vdd_type][0] = tmp[0];
			vdd_val[motg->vdd_type][1] = tmp[1];
			vdd_val[motg->vdd_type][2] = tmp[2];
		} else {
			dev_dbg(&pdev->dev, "Using default hsusb vdd config.\n");
		}
	}

	ret = msm_hsusb_config_vddcx(1);
	if (ret) {
		dev_err(&pdev->dev, "hsusb vddcx configuration failed\n");
		goto devote_xo_handle;
	}

	ret = regulator_enable(hsusb_vdd);
	if (ret) {
		dev_err(&pdev->dev, "unable to enable the hsusb vddcx\n");
		goto free_config_vddcx;
	}

	ret = msm_hsusb_ldo_init(motg, 1);
	if (ret) {
		dev_err(&pdev->dev, "hsusb vreg configuration failed\n");
		goto free_hsusb_vdd;
	}

	if (pdata->mhl_enable) {
		mhl_usb_hs_switch = devm_regulator_get(motg->phy.dev,
							"mhl_usb_hs_switch");
		if (IS_ERR(mhl_usb_hs_switch)) {
			dev_err(&pdev->dev, "Unable to get mhl_usb_hs_switch\n");
			ret = PTR_ERR(mhl_usb_hs_switch);
			goto free_ldo_init;
		}
	}

	ret = msm_hsusb_ldo_enable(motg, USB_PHY_REG_ON);
	if (ret) {
		dev_err(&pdev->dev, "hsusb vreg enable failed\n");
		goto free_ldo_init;
	}
	clk_prepare_enable(motg->core_clk);

	/* Check if USB mem_type change is needed to workaround PNOC hw issue */
	msm_otg_pnoc_errata_fix(motg);

	writel(0, USB_USBINTR);
	writel(0, USB_OTGSC);
	/* Ensure that above STOREs are completed before enabling interrupts */
	mb();

	ret = msm_otg_mhl_register_callback(motg, msm_otg_mhl_notify_online);
	if (ret)
		dev_dbg(&pdev->dev, "MHL can not be supported\n");
	wake_lock_init(&motg->wlock, WAKE_LOCK_SUSPEND, "msm_otg");
	msm_otg_init_timer(motg);
	INIT_WORK(&motg->sm_work, msm_otg_sm_work);
	INIT_DELAYED_WORK(&motg->chg_work, msm_chg_detect_work);
	INIT_DELAYED_WORK(&motg->pmic_id_status_work, msm_pmic_id_status_w);
	INIT_DELAYED_WORK(&motg->suspend_work, msm_otg_suspend_work);

//ASUS_BSP+++ "[USB][NA][Spec] Add ASUS charger mode support"
#ifdef CONFIG_CHARGER_ASUS
	INIT_WORK(&asus_usb_work, asus_usb_detect_work);
	INIT_DELAYED_WORK(&asus_chg_work, asus_chg_detect_work);
#endif
//ASUS_BSP--- "[USB][NA][Spec] Add ASUS charger mode support

//ASUS_BSP+++ "[USB][NA][Fix] reset usb when TD_STATUS_HALTED"
	INIT_DELAYED_WORK(&asus_halted_work, asus_halted_reset_work);
//ASUS_BSP--- "[USB][NA][Fix] reset usb when TD_STATUS_HALTED"

	//ASUS_BSP+++ Eric5_Ou "register early suspend notification for none mode switch"
	wake_lock_init(&early_suspend_wlock, WAKE_LOCK_SUSPEND, "asus_otg_early_suspend_wlock");
	if (!early_suspend_delay_wq)
		early_suspend_delay_wq = create_singlethread_workqueue("asus_otg_early_suspend_delay_wq");
	INIT_DELAYED_WORK_DEFERRABLE(&early_suspend_delay_work, asus_otg_early_suspend_delay_work);
	INIT_WORK(&late_resume_work, asus_otg_late_resume_work);
#if defined(CONFIG_HAS_EARLYSUSPEND)
	register_early_suspend(&asus_otg_early_suspend_handler);
#elif defined(CONFIG_FB)
	fb_notif.notifier_call = asus_otg_fb_notifier_callback;
	ret = fb_register_client(&fb_notif);
	if (ret)
		dev_err(&pdev->dev, "Unable to register fb_notifier: %d\n", ret);
#endif
	//ASUS_BSP--- Eric5_Ou "register early suspend notification for none mode switch"

	//ASUS_BSP+++ Eric5_Ou "add mutex to protect suspend/resume function"
	mutex_init(&msm_otg_mutex);
	//ASUS_BSP--- Eric5_Ou "add mutex to protect suspend/resume function"
	setup_timer(&motg->id_timer, msm_otg_id_timer_func,
				(unsigned long) motg);
	setup_timer(&motg->chg_check_timer, msm_otg_chg_check_timer_func,
				(unsigned long) motg);
	ret = request_irq(motg->irq, msm_otg_irq, IRQF_SHARED,
					"msm_otg", motg);
	if (ret) {
		dev_err(&pdev->dev, "request irq failed\n");
		goto destroy_wlock;
	}

	if (motg->async_irq) {
		ret = request_irq(motg->async_irq, msm_otg_irq,
					IRQF_TRIGGER_RISING, "msm_otg", motg);
		if (ret) {
			dev_err(&pdev->dev, "request irq failed (ASYNC INT)\n");
			goto free_irq;
		}
		disable_irq(motg->async_irq);
	}

	if (pdata->otg_control == OTG_PHY_CONTROL && pdata->mpm_otgsessvld_int)
		msm_mpm_enable_pin(pdata->mpm_otgsessvld_int, 1);

	if (pdata->mpm_dpshv_int)
		msm_mpm_enable_pin(pdata->mpm_dpshv_int, 1);
	if (pdata->mpm_dmshv_int)
		msm_mpm_enable_pin(pdata->mpm_dmshv_int, 1);

	phy->init = msm_otg_reset;
	phy->set_power = msm_otg_set_power;
	phy->set_suspend = msm_otg_set_suspend;

	phy->io_ops = &msm_otg_io_ops;

	phy->otg->phy = &motg->phy;
	phy->otg->set_host = msm_otg_set_host;
	phy->otg->set_peripheral = msm_otg_set_peripheral;
	phy->otg->start_hnp = msm_otg_start_hnp;
	phy->otg->start_srp = msm_otg_start_srp;
	if (pdata->dp_manual_pullup)
		phy->flags |= ENABLE_DP_MANUAL_PULLUP;

	if (pdata->enable_sec_phy)
		phy->flags |= ENABLE_SECONDARY_PHY;

	ret = usb_set_transceiver(&motg->phy);
	if (ret) {
		dev_err(&pdev->dev, "usb_set_transceiver failed\n");
		goto free_async_irq;
	}

	if (motg->pdata->mode == USB_OTG &&
		motg->pdata->otg_control == OTG_PMIC_CONTROL) {
		if (motg->pdata->pmic_id_irq) {
			ret = request_irq(motg->pdata->pmic_id_irq,
						msm_pmic_id_irq,
						IRQF_TRIGGER_RISING |
						IRQF_TRIGGER_FALLING,
						"msm_otg", motg);
			if (ret) {
				dev_err(&pdev->dev, "request irq failed for PMIC ID\n");
				goto remove_phy;
			}
		} else {
			ret = -ENODEV;
			dev_err(&pdev->dev, "PMIC IRQ for ID notifications doesn't exist\n");
			goto remove_phy;
		}
	}

	msm_hsusb_mhl_switch_enable(motg, 1);

	platform_set_drvdata(pdev, motg);
	device_init_wakeup(&pdev->dev, 1);
	motg->mA_port = IUNIT;

	//ASUS_BSP+++ Eric5_Ou "add usb otg mode switch support"
	motg->otg_mode = USB_AUTO;
	//ASUS_BSP--- Eric5_Ou "add usb otg mode switch support"

//ASUS_BSP+++ "[USB][NA][Fix] reset usb when TD_STATUS_HALTED"
	ret = asus_otg_proc_init(motg);
	if (ret) {
		dev_err(&pdev->dev, "proc file init fail (%d)\n", ret);
	}
//ASUS_BSP--- "[USB][NA][Fix] reset usb when TD_STATUS_HALTED"

	ret = msm_otg_debugfs_init(motg);
	if (ret)
		dev_dbg(&pdev->dev, "mode debugfs file is"
			"not available\n");

	if (motg->pdata->phy_type == SNPS_28NM_INTEGRATED_PHY) {
		if (motg->pdata->otg_control == OTG_PMIC_CONTROL)
			motg->caps = ALLOW_PHY_POWER_COLLAPSE |
				ALLOW_PHY_RETENTION;

		if (motg->pdata->otg_control == OTG_PHY_CONTROL)
			motg->caps = ALLOW_PHY_RETENTION |
				ALLOW_PHY_REGULATORS_LPM;

		if (motg->pdata->mpm_dpshv_int || motg->pdata->mpm_dmshv_int)
			motg->caps |= ALLOW_HOST_PHY_RETENTION;
			device_create_file(&pdev->dev,
					&dev_attr_dpdm_pulldown_enable);
	}

	if (motg->pdata->enable_lpm_on_dev_suspend)
		motg->caps |= ALLOW_LPM_ON_DEV_SUSPEND;

	if (motg->pdata->disable_retention_with_vdd_min)
		motg->caps |= ALLOW_VDD_MIN_WITH_RETENTION_DISABLED;

	wake_lock(&motg->wlock);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	if (motg->pdata->delay_lpm_on_disconnect) {
		pm_runtime_set_autosuspend_delay(&pdev->dev,
			lpm_disconnect_thresh);
		pm_runtime_use_autosuspend(&pdev->dev);
	}
//ASUS_BSP+++ "[USB][NA][Spec] Add ASUS charger mode support"
#ifdef CONFIG_CHARGER_ASUS
	if (motg->pdata->otg_control == OTG_PMIC_CONTROL)
		registerChargerInOutNotificaition(&msm_otg_set_vbus_state);
#else
	motg->usb_psy.name = "usb";
	motg->usb_psy.type = POWER_SUPPLY_TYPE_USB;
	motg->usb_psy.supplied_to = otg_pm_power_supplied_to;
	motg->usb_psy.num_supplicants = ARRAY_SIZE(otg_pm_power_supplied_to);
	motg->usb_psy.properties = otg_pm_power_props_usb;
	motg->usb_psy.num_properties = ARRAY_SIZE(otg_pm_power_props_usb);
	motg->usb_psy.get_property = otg_power_get_property_usb;
	motg->usb_psy.set_property = otg_power_set_property_usb;
	motg->usb_psy.property_is_writeable
		= otg_power_property_is_writeable_usb;

	if (!pm8921_charger_register_vbus_sn(NULL)) {
		/* if pm8921 use legacy implementation */
		dev_dbg(motg->phy.dev, "%s: legacy support\n", __func__);
		legacy_power_supply = true;
	} else {
		/* otherwise register our own power supply */
		if (!msm_otg_register_power_supply(pdev, motg))
			psy = &motg->usb_psy;
	}

	if (legacy_power_supply && pdata->otg_control == OTG_PMIC_CONTROL)
		pm8921_charger_register_vbus_sn(&msm_otg_set_vbus_state);
#endif
//ASUS_BSP--- "[USB][NA][Spec] Add ASUS charger mode support"

	ret = msm_otg_setup_ext_chg_cdev(motg);
	if (ret)
		dev_dbg(&pdev->dev, "fail to setup cdev\n");

	return 0;

remove_phy:
	usb_set_transceiver(NULL);
free_async_irq:
	if (motg->async_irq)
		free_irq(motg->async_irq, motg);
free_irq:
	free_irq(motg->irq, motg);
destroy_wlock:
	wake_lock_destroy(&motg->wlock);
	clk_disable_unprepare(motg->core_clk);
	msm_hsusb_ldo_enable(motg, USB_PHY_REG_OFF);
free_ldo_init:
	msm_hsusb_ldo_init(motg, 0);
free_hsusb_vdd:
	regulator_disable(hsusb_vdd);
free_config_vddcx:
	regulator_set_voltage(hsusb_vdd,
		vdd_val[motg->vdd_type][VDD_NONE],
		vdd_val[motg->vdd_type][VDD_MAX]);
devote_xo_handle:
	clk_disable_unprepare(motg->pclk);
	if (!IS_ERR(motg->xo_clk))
		clk_disable_unprepare(motg->xo_clk);
	else
		msm_xo_mode_vote(motg->xo_handle, MSM_XO_MODE_OFF);
free_xo_handle:
	if (!IS_ERR(motg->xo_clk))
		clk_put(motg->xo_clk);
	else
		msm_xo_put(motg->xo_handle);
free_regs:
	iounmap(motg->regs);
disable_sleep_clk:
	if (!IS_ERR(motg->sleep_clk))
		clk_disable_unprepare(motg->sleep_clk);
put_pclk:
	clk_put(motg->pclk);
put_core_clk:
	clk_put(motg->core_clk);
put_clk:
	if (!IS_ERR(motg->clk))
		clk_put(motg->clk);
devote_bus_bw:
	if (motg->bus_perf_client) {
		msm_otg_bus_vote(motg, USB_NO_PERF_VOTE);
		msm_bus_scale_unregister_client(motg->bus_perf_client);
	}

	return ret;
}

static int __devexit msm_otg_remove(struct platform_device *pdev)
{
	struct msm_otg *motg = platform_get_drvdata(pdev);
	struct usb_phy *phy = &motg->phy;
	int cnt = 0;

	if (phy->otg->host || phy->otg->gadget)
		return -EBUSY;

	if (!motg->ext_chg_device) {
		device_destroy(motg->ext_chg_class, motg->ext_chg_dev);
		cdev_del(&motg->ext_chg_cdev);
		class_destroy(motg->ext_chg_class);
		unregister_chrdev_region(motg->ext_chg_dev, 1);
	}

	if (pdev->dev.of_node)
		msm_otg_setup_devices(pdev, motg->pdata->mode, false);
	if (motg->pdata->otg_control == OTG_PMIC_CONTROL)
		pm8921_charger_unregister_vbus_sn(0);
	msm_otg_mhl_register_callback(motg, NULL);
	//ASUS_BSP+++ Eric5_Ou "register early suspend notification for none mode switch"
	cancel_delayed_work_sync(&early_suspend_delay_work);
	cancel_work_sync(&late_resume_work);
	destroy_workqueue(early_suspend_delay_wq);
	wake_lock_destroy(&early_suspend_wlock);
#if defined(CONFIG_HAS_EARLYSUSPEND)
	unregister_early_suspend(&asus_otg_early_suspend_handler);
#elif defined(CONFIG_FB)
	fb_unregister_client(&fb_notif);
#endif
	//ASUS_BSP--- Eric5_Ou "register early suspend notification for none mode switch"
	//ASUS_BSP+++ Eric5_Ou "add mutex to protect suspend/resume function"
	mutex_destroy(&msm_otg_mutex);
	//ASUS_BSP--- Eric5_Ou "add mutex to protect suspend/resume function"
	//ASUS_BSP+++ Eric5_Ou "add usb otg mode switch support"
	asus_otg_proc_cleanup();
	//ASUS_BSP--- Eric5_Ou "add usb otg mode switch support"
	msm_otg_debugfs_cleanup();
	cancel_delayed_work_sync(&motg->chg_work);
	cancel_delayed_work_sync(&motg->pmic_id_status_work);

//ASUS_BSP+++ "[USB][NA][Spec] Add ASUS charger mode support"
#ifdef CONFIG_CHARGER_ASUS
	cancel_delayed_work_sync(&asus_chg_work);
#endif
//ASUS_BSP--- "[USB][NA][Spec] Add ASUS charger mode support"

	cancel_delayed_work_sync(&motg->suspend_work);
	cancel_work_sync(&motg->sm_work);

	pm_runtime_resume(&pdev->dev);

	device_init_wakeup(&pdev->dev, 0);
	pm_runtime_disable(&pdev->dev);
	wake_lock_destroy(&motg->wlock);

	msm_hsusb_mhl_switch_enable(motg, 0);
	if (motg->pdata->pmic_id_irq)
		free_irq(motg->pdata->pmic_id_irq, motg);
	usb_set_transceiver(NULL);
	free_irq(motg->irq, motg);

	if ((motg->pdata->phy_type == SNPS_28NM_INTEGRATED_PHY) &&
		(motg->pdata->mpm_dpshv_int || motg->pdata->mpm_dmshv_int))
			device_remove_file(&pdev->dev,
					&dev_attr_dpdm_pulldown_enable);
	if (motg->pdata->otg_control == OTG_PHY_CONTROL &&
		motg->pdata->mpm_otgsessvld_int)
		msm_mpm_enable_pin(motg->pdata->mpm_otgsessvld_int, 0);

	if (motg->pdata->mpm_dpshv_int)
		msm_mpm_enable_pin(motg->pdata->mpm_dpshv_int, 0);
	if (motg->pdata->mpm_dmshv_int)
		msm_mpm_enable_pin(motg->pdata->mpm_dmshv_int, 0);

	/*
	 * Put PHY in low power mode.
	 */
	ulpi_read(phy, 0x14);
	ulpi_write(phy, 0x08, 0x09);

	writel(readl(USB_PORTSC) | PORTSC_PHCD, USB_PORTSC);
	while (cnt < PHY_SUSPEND_TIMEOUT_USEC) {
		if (readl(USB_PORTSC) & PORTSC_PHCD)
			break;
		udelay(1);
		cnt++;
	}
	if (cnt >= PHY_SUSPEND_TIMEOUT_USEC)
		dev_err(phy->dev, "Unable to suspend PHY\n");

	clk_disable_unprepare(motg->pclk);
	clk_disable_unprepare(motg->core_clk);
	if (!IS_ERR(motg->xo_clk)) {
		clk_disable_unprepare(motg->xo_clk);
		clk_put(motg->xo_clk);
	} else {
		msm_xo_put(motg->xo_handle);
	}

	if (!IS_ERR(motg->sleep_clk))
		clk_disable_unprepare(motg->sleep_clk);

	msm_hsusb_ldo_enable(motg, USB_PHY_REG_OFF);
	msm_hsusb_ldo_init(motg, 0);
	regulator_disable(hsusb_vdd);
	regulator_set_voltage(hsusb_vdd,
		vdd_val[motg->vdd_type][VDD_NONE],
		vdd_val[motg->vdd_type][VDD_MAX]);

	iounmap(motg->regs);
	pm_runtime_set_suspended(&pdev->dev);

	clk_put(motg->pclk);
	if (!IS_ERR(motg->clk))
		clk_put(motg->clk);
	clk_put(motg->core_clk);

	if (motg->bus_perf_client) {
		msm_otg_bus_vote(motg, USB_NO_PERF_VOTE);
		msm_bus_scale_unregister_client(motg->bus_perf_client);
	}

	return 0;
}

#ifdef CONFIG_PM_RUNTIME
static int msm_otg_runtime_idle(struct device *dev)
{
	struct msm_otg *motg = dev_get_drvdata(dev);
	struct usb_phy *phy = &motg->phy;

	dev_dbg(dev, "OTG runtime idle\n");

	if (phy->state == OTG_STATE_UNDEFINED)
		return -EAGAIN;

	if (motg->ext_chg_active) {
		dev_dbg(dev, "Deferring LPM\n");
		/*
		 * Charger detection may happen in user space.
		 * Delay entering LPM by 3 sec.  Otherwise we
		 * have to exit LPM when user space begins
		 * charger detection.
		 *
		 * This timer will be canceled when user space
		 * votes against LPM by incrementing PM usage
		 * counter.  We enter low power mode when
		 * PM usage counter is decremented.
		 */
		pm_schedule_suspend(dev, 3000);
		return -EAGAIN;
	}

	return 0;
}

static int msm_otg_runtime_suspend(struct device *dev)
{
	struct msm_otg *motg = dev_get_drvdata(dev);

	dev_dbg(dev, "OTG runtime suspend\n");
	return msm_otg_suspend(motg);
}

static int msm_otg_runtime_resume(struct device *dev)
{
	struct msm_otg *motg = dev_get_drvdata(dev);

	dev_dbg(dev, "OTG runtime resume\n");
	pm_runtime_get_noresume(dev);
	//ASUS_BSP+++ Eric5_Ou "Add PM usage count debug log and protection mechanism"
	otg_pm_count+= 1;
	printk("[OTG-PM][%s] get usage_count:%d, otg_pm_count:%d\n",__func__, atomic_read(&dev->power.usage_count),otg_pm_count);
	//ASUS_BSP--- Eric5_Ou "Add PM usage count debug log and protection mechanism"
	return msm_otg_resume(motg);
}
#endif

#ifdef CONFIG_PM_SLEEP
static int msm_otg_pm_suspend(struct device *dev)
{
	int ret = 0;
	struct msm_otg *motg = dev_get_drvdata(dev);

	dev_dbg(dev, "OTG PM suspend\n");

	//ASUS_BSP+++ Eric5_Ou "Add judgement to avoid msm_otg pm suspend work before runtime suspend"
	if (!atomic_read(&motg->in_lpm)) {
		dev_err(dev, "Abort PM suspend!! (USB is outside LPM)\n");
		return -EBUSY;
	}
	//ASUS_BSP+++ Eric5_Ou "Add judgement to avoid msm_otg pm suspend work before runtime suspend"

	atomic_set(&motg->pm_suspended, 1);
	ret = msm_otg_suspend(motg);
	if (ret)
		atomic_set(&motg->pm_suspended, 0);

	return ret;
}

static int msm_otg_pm_resume(struct device *dev)
{
	int ret = 0;
	struct msm_otg *motg = dev_get_drvdata(dev);

	dev_dbg(dev, "OTG PM resume\n");

	atomic_set(&motg->pm_suspended, 0);
	if (motg->async_int || motg->sm_work_pending) {
		pm_runtime_get_noresume(dev);
		//ASUS_BSP+++ Eric5_Ou "Add PM usage count debug log and protection mechanism"
		otg_pm_count+= 1;
		printk("[OTG-PM][%s] get usage_count:%d, otg_pm_count:%d\n",__func__, atomic_read(&dev->power.usage_count),otg_pm_count);
		//ASUS_BSP--- Eric5_Ou "Add PM usage count debug log and protection mechanism"
		ret = msm_otg_resume(motg);

		/* Update runtime PM status */
		pm_runtime_disable(dev);
		pm_runtime_set_active(dev);
		pm_runtime_enable(dev);

		if (motg->sm_work_pending) {
			motg->sm_work_pending = false;
			queue_work(system_nrt_wq, &motg->sm_work);
		}
	}

	return ret;
}
#endif

#ifdef CONFIG_PM
static const struct dev_pm_ops msm_otg_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(msm_otg_pm_suspend, msm_otg_pm_resume)
	SET_RUNTIME_PM_OPS(msm_otg_runtime_suspend, msm_otg_runtime_resume,
				msm_otg_runtime_idle)
};
#endif

static struct of_device_id msm_otg_dt_match[] = {
	{	.compatible = "qcom,hsusb-otg",
	},
	{}
};

static struct platform_driver msm_otg_driver = {
	.remove = __devexit_p(msm_otg_remove),
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
#ifdef CONFIG_PM
		.pm = &msm_otg_dev_pm_ops,
#endif
		.of_match_table = msm_otg_dt_match,
	},
};

static int __init msm_otg_init(void)
{
	return platform_driver_probe(&msm_otg_driver, msm_otg_probe);
}

static void __exit msm_otg_exit(void)
{
	platform_driver_unregister(&msm_otg_driver);
}

module_init(msm_otg_init);
module_exit(msm_otg_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("MSM USB transceiver driver");
