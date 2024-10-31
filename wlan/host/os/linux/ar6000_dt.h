/*
 * ISC License (ISC)
 *
 * Copyright (c) 2013, The Linux Foundation
 * All rights reserved.
 * Software was previously licensed under ISC license by Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

struct ar6000_reg_data {
	/* Regulator Name */
	const char *name;

	/* voltage level at which AR chip can operate */
	u32 low_vol_level;
	u32 high_vol_level;

	/* Current which will be drawn from regulator (Worst case( */
	u32 load_uA;

	/* Time for this operation to next */
	int delay_mT;

	/* Is this regulator required */
	bool is_required;

	/* Voltage regulator handle */
	struct regulator *reg;

};

struct ar6000_gpio_data {
	int chip_pwd_l_gpio;
	int pm_enable_gpio;
	int wlan_clk_req_gpio;
};

struct ar6000_dt_data {
	struct platform_device *pdev;
	struct ar6000_gpio_data gpio_data;
	struct ar6000_reg_data *reg_table;
	u32 reg_table_size;
};


int ar6000_dt_power(struct ar6000_dt_data *pdata, int on);
int ar6000_dt_probe(struct platform_device *pdev);
int ar6000_dt_remove(struct platform_device *pdev);
