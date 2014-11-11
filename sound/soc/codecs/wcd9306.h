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
#ifndef WCD9306_H
#define WCD9306_H

#include <sound/soc.h>
#include <sound/jack.h>
#include <sound/apr_audio-v2.h>
#include <linux/mfd/wcd9xxx/wcd9xxx-slimslave.h>
#include "wcd9xxx-mbhc.h"
#include "wcd9xxx-resmgr.h"
#include "wcd9xxx-common.h"

// wendy4_Wang@asus.com
#include <linux/switch.h>
// wendy4_Wang@asus.com

#define TAPAN_NUM_REGISTERS 0x400
#define TAPAN_MAX_REGISTER (TAPAN_NUM_REGISTERS-1)
#define TAPAN_CACHE_SIZE TAPAN_NUM_REGISTERS

#define TAPAN_REG_VAL(reg, val)		{reg, 0, val}

#define TAPAN_CDC_ZDET_SUPPORTED  true

extern const u8 tapan_reg_readable[TAPAN_CACHE_SIZE];
extern const u8 tapan_reset_reg_defaults[TAPAN_CACHE_SIZE];
struct tapan_codec_dai_data {
	u32 rate;
	u32 *ch_num;
	u32 ch_act;
	u32 ch_tot;
};

enum tapan_pid_current {
	TAPAN_PID_MIC_2P5_UA,
	TAPAN_PID_MIC_5_UA,
	TAPAN_PID_MIC_10_UA,
	TAPAN_PID_MIC_20_UA,
};

// wendy4_Wang@asus.com
#define TABLA_JACK_BUTTON_MASK (SND_JACK_BTN_0 | SND_JACK_BTN_1 | \
			SND_JACK_BTN_2 | SND_JACK_BTN_3 | \
			SND_JACK_BTN_4 | SND_JACK_BTN_5 | \
			SND_JACK_BTN_6 | SND_JACK_BTN_7)

struct gpio_switch_data {
	struct switch_dev sdev;
	unsigned gpio;
	const char *name_on;
	const char *name_off;
	const char *state_on;
	const char *state_off;
	int irq;
	struct work_struct work;
};

struct wcd9306_hs_struct {
	int hs_path_en;
	int hsmic_bias;
	int button_gpio;
	int jack_gpio;
	int button_irq;
	int jack_irq;
};
// wendy4_Wang@asus.com

struct tapan_reg_mask_val {
	u16	reg;
	u8	mask;
	u8	val;
};

enum tapan_mbhc_analog_pwr_cfg {
	TAPAN_ANALOG_PWR_COLLAPSED = 0,
	TAPAN_ANALOG_PWR_ON,
	TAPAN_NUM_ANALOG_PWR_CONFIGS,
};

/* Number of input and output Slimbus port */
enum {
	TAPAN_RX1 = 0,
	TAPAN_RX2,
	TAPAN_RX3,
	TAPAN_RX4,
	TAPAN_RX5,
	TAPAN_RX_MAX,
};

enum {
	TAPAN_TX1 = 0,
	TAPAN_TX2,
	TAPAN_TX3,
	TAPAN_TX4,
	TAPAN_TX5,
	TAPAN_TX_MAX,
};

extern int tapan_mclk_enable(struct snd_soc_codec *codec, int mclk_enable,
			     bool dapm);
extern int tapan_hs_detect(struct snd_soc_codec *codec,
			   struct wcd9xxx_mbhc_config *mbhc_cfg);
extern void *tapan_get_afe_config(struct snd_soc_codec *codec,
				  enum afe_config_type config_type);
extern void tapan_event_register(
	int (*machine_event_cb)(struct snd_soc_codec *codec,
				 enum wcd9xxx_codec_event),
	struct snd_soc_codec *codec);
void apply_wcd9306_spk_gain(void);  //mei_huang@asus.com
#endif
