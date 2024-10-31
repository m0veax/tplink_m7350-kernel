/*
 * Copyright (c) 2010-2011 Atheros Communications Inc.
 * Copyright (c) 2011 Qualcomm Atheros, Inc.
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


#include <linux/printk.h>
#include <asm/unaligned.h>

#include "core.h"
#include "cfg80211.h"
#include "debug.h"
#include "hif-ops.h"
#include "testmode.h"
#include "wmiconfig.h"
#include "wmi.h"

#include <net/netlink.h>

enum ath6kl_tm_attr {
	__ATH6KL_TM_ATTR_INVALID	= 0,
	ATH6KL_TM_ATTR_CMD		= 1,
	ATH6KL_TM_ATTR_DATA		= 2,

	/* keep last */
	__ATH6KL_TM_ATTR_AFTER_LAST,
	ATH6KL_TM_ATTR_MAX		= __ATH6KL_TM_ATTR_AFTER_LAST - 1,
};

enum ath6kl_tm_cmd {
	ATH6KL_TM_CMD_TCMD		= 0,
	ATH6KL_TM_CMD_RX_REPORT		= 1,	/* not used anymore */
	ATH6KL_TM_CMD_WMI_CMD		= 0xF000,
};

#define AP_ACS_NONE AP_ACS_POLICY_MAX

#define ATH6KL_WWAN_FDD 0x0F
#define ATH6KL_WWAN_TDD 0xF0
#define ATH6KL_WWAN_BAND 0xFF

struct app_lte_coex_wwan_data_t {
	uint32_t cmd;

	uint32_t band_info_valid;
	uint32_t ul_freq;
	uint32_t ul_bw;
	uint32_t dl_freq;
	uint32_t dl_bw;

	uint32_t tdd_info_valid;
	uint32_t fr_offset;
	uint32_t tdd_cfg;
	uint32_t sub_fr_cfg;
	uint32_t ul_cfg;
	uint32_t dl_cfg;

	uint32_t off_period_valid;
	uint32_t off_period;
};

#define CHAN1 BIT(0)
#define CHAN2 BIT(1)
#define CHAN3 BIT(2)
#define CHAN4 BIT(3)
#define CHAN5 BIT(4)
#define CHAN6 BIT(5)
#define CHAN7 BIT(6)
#define CHAN8 BIT(7)
#define CHAN9 BIT(8)
#define CHAN10 BIT(9)
#define CHAN11 BIT(10)
#define CHAN12 BIT(11)
#define CHAN13 BIT(12)

#define LTE_COEX_REF_TDD_ROWS 4
#define LTE_COEX_REF_FDD_ROWS 20
struct _lte_coex_chk {
	int	wwan_min_freq;
	int	wwan_max_freq;
	int	wwan_bw;
	u32	margin_0_acs_chan_mask;     /* margin 0 4db - 8db */
	u32	margin_1_acs_chan_mask;     /* margin 1 < 4db */
	u32	margin_2_acs_chan_mask;     /* margin 2 > 8db */
};

struct _lte_coex_chk lte_coex_tdd_lookup_table[LTE_COEX_REF_TDD_ROWS] = {
/* wwan_min_freq  wwan_max_freq    wwan_bw
 * margin_0_chan_mask
 * margin_1_chan_mask
 * margin_2_chan_mask
 */

/*
 * For TDD bands margin value is ignored, so all the margins should have the
 * same channel lists
 *
 * */
/* band 40 tdd */
{2300,  2350, 0,
	(CHAN5 | CHAN6 | CHAN7 | CHAN8 | CHAN9 | CHAN10 | CHAN11 | CHAN12 | CHAN13),
	(CHAN5 | CHAN6 | CHAN7 | CHAN8 | CHAN9 | CHAN10 | CHAN11 | CHAN12 | CHAN13),
	(CHAN5 | CHAN6 | CHAN7 | CHAN8 | CHAN9 | CHAN10 | CHAN11 | CHAN12 | CHAN13)},

{2350,  2370, 0,
	(CHAN7 | CHAN8 | CHAN9 | CHAN10 | CHAN11 | CHAN12 | CHAN13),
	(CHAN7 | CHAN8 | CHAN9 | CHAN10 | CHAN11 | CHAN12 | CHAN13),
	(CHAN7 | CHAN8 | CHAN9 | CHAN10 | CHAN11 | CHAN12 | CHAN13)},

{2370,  2401, 0,
	(CHAN10 | CHAN11 | CHAN12 | CHAN13),
	(CHAN10 | CHAN11 | CHAN12 | CHAN13),
	(CHAN10 | CHAN11 | CHAN12 | CHAN13)},


/* band 41 tdd */
{2496,  2691, 0,
	(CHAN1 | CHAN2 | CHAN3 | CHAN4 | CHAN5 | CHAN6 | CHAN7 | CHAN8 | CHAN9),
	(CHAN1 | CHAN2 | CHAN3 | CHAN4 | CHAN5 | CHAN6 | CHAN7 | CHAN8 | CHAN9),
	(CHAN1 | CHAN2 | CHAN3 | CHAN4 | CHAN5 | CHAN6 | CHAN7 | CHAN8 | CHAN9)}
};


/* Coex mode same for FDD B20 and B7 */
struct _lte_coex_chk lte_coex_fdd_lookup_table[LTE_COEX_REF_FDD_ROWS] = {
/* band 7 10 MHz */
{2500,  2525, 10,
	/* margin 0 4db - 8db channel list */
        (CHAN2 | CHAN3 | CHAN4| CHAN5 | CHAN6 | CHAN7 | CHAN8 | CHAN9 | CHAN10 | CHAN11 | CHAN12 | CHAN13),
	/* margin 1 < 4db  channel list */
        (CHAN1 | CHAN2 | CHAN3 | CHAN4| CHAN5 | CHAN6 | CHAN7 | CHAN8 | CHAN9 | CHAN10 | CHAN11 | CHAN12 | CHAN13),
	/* margin 2 > 8db channel list*/
        (CHAN4 | CHAN5 | CHAN6 | CHAN7 | CHAN8 | CHAN9 | CHAN10 | CHAN11 | CHAN12 | CHAN13)},

{2525,  2552, 10,
        (CHAN7 | CHAN8 | CHAN9 | CHAN10 | CHAN11 | CHAN12 | CHAN13), /* margin 0 4db - 8db channel list */
        (CHAN6 | CHAN7 | CHAN8 | CHAN9 | CHAN10 | CHAN11 | CHAN12 | CHAN13), /* margin 1 < 4db  channel list */
        (CHAN9 | CHAN10 | CHAN11 | CHAN12 | CHAN13)}, /* margin 2 > 8db channel list*/

{2552,  2562, 10,
        (CHAN9 | CHAN10 | CHAN11 | CHAN12 | CHAN13), /* margin 0 4db - 8db channel list */
        (CHAN8 | CHAN9 | CHAN10 | CHAN11 | CHAN12 | CHAN13), /* margin 1 < 4db  channel list */
        (CHAN11 | CHAN12 | CHAN13)}, /* margin 2 > 8db channel list*/

{2562,  2568, 10,
        (CHAN1 | CHAN2 | CHAN3 | CHAN4| CHAN5 | CHAN11 | CHAN12 | CHAN13), /* margin 0 4db - 8db channel list */
        (CHAN1 | CHAN2 | CHAN3 | CHAN4| CHAN5 | CHAN6 | CHAN10 | CHAN11 | CHAN12 | CHAN13), /* margin 1 < 4db  channel list */
        (CHAN1 | CHAN2 | CHAN3 | CHAN13)}, /* margin 2 > 8db channel list*/

{2568,  2570, 10,
        (CHAN1 | CHAN2 | CHAN3 | CHAN4| CHAN5 | CHAN6), /* margin 0 4db - 8db channel list */
        (CHAN1 | CHAN2 | CHAN3 | CHAN4| CHAN5 | CHAN6 | CHAN7), /* margin 1 < 4db  channel list */
        (CHAN1 | CHAN2 | CHAN3 | CHAN4)}, /* margin 2 > 8db channel list*/

/* band 7 20 MHz */
{2500,  2525, 20,
        (CHAN4 | CHAN5 | CHAN6 | CHAN7 | CHAN8 | CHAN9 | CHAN10 | CHAN11 | CHAN12 | CHAN13), /* margin 0 4db - 8db channel list */
        (CHAN3 | CHAN4 | CHAN5 | CHAN6 | CHAN7 | CHAN8 | CHAN9 | CHAN10 | CHAN11 | CHAN12 | CHAN13), /* margin 1 < 4db channel list */
        (CHAN6 | CHAN7 | CHAN8 | CHAN9 | CHAN10 | CHAN11 | CHAN12 | CHAN13)}, /* margin 2 > 8db channel list */

{2525,  2552, 20,
        (CHAN9 | CHAN10 | CHAN11 | CHAN12 | CHAN13),/* margin 0 4db - 8db channel list */
        (CHAN8 | CHAN9 | CHAN10 | CHAN11 | CHAN12 | CHAN13), /* margin 1 < 4db channel list */
        (CHAN11 | CHAN12 | CHAN13)}, /* margin 2 > 8db channel list */

{2552,  2562, 20,
        (CHAN11 | CHAN12 | CHAN13), /* margin 0 4db - 8db channel list */
        (CHAN10 | CHAN11 | CHAN12 | CHAN13), /* margin 1 < 4db channel list */
        (CHAN13)}, /* margin 2 > 8db channel list */

{2562,  2568, 20,
        (CHAN1 | CHAN2 | CHAN3 | CHAN13), /* margin 0 4db - 8db channel list */
        (CHAN1 | CHAN2 | CHAN3 | CHAN4 | CHAN12 | CHAN13), /* margin 1 < 4db channel list */
        (CHAN1 | CHAN13)}, /* margin 2 > 8db channel list */

{2568,  2570, 20,
        (CHAN1 | CHAN2 | CHAN3 | CHAN4), /* margin 0 4db - 8db channel list */
        (CHAN1 | CHAN2 | CHAN3 | CHAN4 | CHAN5), /* margin 1 < 4db channel list */
        (CHAN1 | CHAN2)}, /* margin 2 > 8db channel list */

/* band 20 10 MHz */
{832,  837, 10,
        (CHAN1 | CHAN2 | CHAN3 | CHAN4 | CHAN5 | CHAN6), /* margin 0 4db - 8db channel list */
        (CHAN1 | CHAN2 | CHAN3 | CHAN4 | CHAN5 | CHAN6 | CHAN7 | CHAN10 | CHAN13), /* margin 1 < 4db channel list */
        (CHAN1 | CHAN2 | CHAN3 | CHAN4)}, /* margin 2 > 8db channel list */

{837,  844, 10,
        (CHAN1 | CHAN2 | CHAN3 | CHAN4 | CHAN5 | CHAN6 | CHAN7 | CHAN8 | CHAN9), /* margin 0 4db - 8db channel list */
        (CHAN1 | CHAN2 | CHAN3 | CHAN4 | CHAN5 | CHAN6 | CHAN7 | CHAN8 | CHAN9 | CHAN10), /* margin 1 < 4db channel list */
        (CHAN1 | CHAN2 | CHAN3 | CHAN4 | CHAN5 | CHAN6 | CHAN7)}, /* margin 2 > 8db channel list */

{845,  852, 10,
        (CHAN6 | CHAN7 | CHAN8 | CHAN9 | CHAN10 | CHAN11 | CHAN12 | CHAN13), /* margin 0 4db - 8db channel list */
        (CHAN6 | CHAN7 | CHAN8 | CHAN9 | CHAN10 | CHAN11 | CHAN12 | CHAN13), /* margin 1 < 4db channel list */
        (CHAN6 | CHAN7 | CHAN8 | CHAN9 | CHAN10 | CHAN11 | CHAN12)}, /* margin 2 > 8db channel list */

{852,  857, 10,
        (CHAN1 | CHAN2 | CHAN3 | CHAN10 | CHAN11 | CHAN12 | CHAN13), /* margin 0 4db - 8db channel list */
        (CHAN1 | CHAN2 | CHAN3 | CHAN4 | CHAN9 | CHAN10 | CHAN11 | CHAN12 | CHAN13), /* margin 1 < 4db channel list */
        (CHAN1 | CHAN2 | CHAN11 | CHAN12 | CHAN13)}, /* margin 2 > 8db channel list */

{857,  862, 10,
        (CHAN1 | CHAN2 | CHAN3 | CHAN4 | CHAN5 | CHAN6 | CHAN7 | CHAN12 | CHAN13), /* margin 0 4db - 8db channel list */
        (CHAN1 | CHAN2 | CHAN3 | CHAN4 | CHAN5 | CHAN6 | CHAN7 | CHAN12 | CHAN13), /* margin 1 < 4db channel list */
        (CHAN1 | CHAN2 | CHAN3 | CHAN4 | CHAN5 | CHAN6 | CHAN7 | CHAN12 | CHAN13)}, /* margin 2 > 8db channel list */

/* band 20 20 MHz */
{832,  837, 20,
        (CHAN1 | CHAN2 | CHAN3 | CHAN4), /* margin 0 4db - 8db channel list */
        (CHAN1 | CHAN2 | CHAN3 | CHAN4 | CHAN5),  /* margin 1 < 4db channel list */
        (CHAN1 | CHAN2)}, /* margin 2 > 8db channel list */

{837,  844, 20,
        (CHAN2 | CHAN3 | CHAN4 | CHAN5 | CHAN6 | CHAN7), /* margin 0 4db - 8db channel list */
        (CHAN2 | CHAN3 | CHAN4 | CHAN5 | CHAN6 | CHAN7 | CHAN8), /* margin 1 < 4db channel list */
        (CHAN2 | CHAN3 | CHAN4 | CHAN5)}, /* margin 2 > 8db channel list */

{845,  852, 20,
        (CHAN7 | CHAN8 | CHAN9 | CHAN10 | CHAN11 | CHAN12), /* margin 0 4db - 8db channel list */
        (CHAN7 | CHAN8 | CHAN9 | CHAN10 | CHAN11 | CHAN12 | CHAN13), /* margin 1 < 4db channel list */
        (CHAN7 | CHAN8 | CHAN9 | CHAN10)}, /* margin 2 > 8db channel list */

{852,  857, 20,
        (CHAN1 | CHAN2 | CHAN10 | CHAN11 | CHAN12 | CHAN13), /* margin 0 4db - 8db channel list */
        (CHAN1 | CHAN2 | CHAN3 | CHAN7 | CHAN8 | CHAN9 | CHAN10 | CHAN11 | CHAN12 | CHAN13), /* margin 1 < 4db channel list */
        (CHAN1 | CHAN2 | CHAN10 | CHAN11 | CHAN12 | CHAN13)}, /* margin 2 > 8db channel list */

{857,  862, 20,
        (CHAN1 | CHAN2 | CHAN3 | CHAN4 | CHAN5 | CHAN6), /* margin 0 4db - 8db channel list */
        (CHAN1 | CHAN2 | CHAN3 | CHAN4 | CHAN5 | CHAN6), /* margin 1 < 4db channel list */
        (CHAN1 | CHAN2 | CHAN3 | CHAN4 | CHAN5 | CHAN6)}, /* margin 2 > 8db channel list */

/* No lte_coex needed for TDD B38
 *{ATH6KL_WWAN_FREQ_2570, ATH6KL_WWAN_FREQ_2620, CH1, CH14,
 *                                              LTE_COEX_MODE_DISABLED,
 *      LTE_COEX_MODE_DISABLED,            AP_ACS_NONE,
 *                                              ATH6KL_WWAN_B38},
 */
};

struct sk_buff *ath6kl_wmi_get_buf(u32 size)
{
	struct sk_buff *skb;

	skb = ath6kl_buf_alloc(size);
	if (!skb)
		return NULL;

	skb_put(skb, size);
	if (size)
		memset(skb->data, 0, size);

	return skb;
}

static u32 ath6kl_get_lte_acs_chan_mask(struct ath6kl *ar, u8 index,
		struct _lte_coex_chk lte_coex_chk[])
{
	if (ar->lte_margin == 0)
		return lte_coex_chk[index].margin_0_acs_chan_mask;
	else if (ar->lte_margin == 1)
		return lte_coex_chk[index].margin_1_acs_chan_mask;
	else if (ar->lte_margin == 2)
		return lte_coex_chk[index].margin_2_acs_chan_mask;
	return 0;
}

static void ath6kl_lte_coex_config_acs_mask(struct ath6kl_vif *vif,
		uint32_t ap_oper_chan_mask, uint8_t index,
		struct _lte_coex_chk lte_coex_chk[])
{
	struct ath6kl *ar = vif->ar;
	uint8_t vif_idx  = vif->fw_vif_idx;

	if (!(ar->lte_coex->acs_chan_mask & ap_oper_chan_mask)) {
		/* AP up */
		if (ar->acs_in_prog) {
			vif->ap_hold_conn = 1;
			mod_timer(&vif->ap_restart_timer, jiffies +
				msecs_to_jiffies(1 * AP_RESTART_TIMER_INVAL));
		} else {
			ar->acs_in_prog = 1;
			ath6kl_wmi_ap_profile_commit(ar->wmi, vif_idx,
					&vif->profile);
		}
	}
}

static void ath6kl_lte_coex_set_ap_mode(struct ath6kl *ar, uint8_t index,
		struct _lte_coex_chk lte_coex_chk[])
{
	struct ath6kl_vif *vif;
	uint32_t ap_oper_chan_mask;
	uint8_t vif_idx = 0;
	spin_lock_bh(&ar->list_lock);
	list_for_each_entry(vif, &ar->vif_list, list) {
		ar->lte_coex->acs_chan_mask =
			ath6kl_get_lte_acs_chan_mask(ar, index,
					lte_coex_chk);;
		if (vif->nw_type != AP_NETWORK ||
				!test_bit(CONNECTED, &vif->flags) )
			continue;
		vif_idx = vif->fw_vif_idx;
		ap_oper_chan_mask = ar->lte_coex->dev_ctx[vif_idx].ap_oper_chan_mask;
		/* select wlan band */
		if (ap_oper_chan_mask && !ar->sta_bh_override &&
				vif->phy_mode != WMI_11A_MODE) {
			ath6kl_lte_coex_config_acs_mask(vif, ap_oper_chan_mask, index,
					lte_coex_chk);
		}
	}
	spin_unlock_bh(&ar->list_lock);
}

static void ath6kl_setup_wlan_ap_lte_coex_mode(struct ath6kl *ar,
		int table_look_up_entries,
		struct _lte_coex_chk lte_coex_chk[])
{
	int i;

	if (ar->lte_coex->wwan_operational == 0)
		return ;

	ar->lte_coex->acs_chan_mask = AP_ACS_NONE;
	/* Select wwan band */
	for (i = 0; i < table_look_up_entries; i++) {
		if (ar->lte_coex->wwan_freq >= lte_coex_chk[i].wwan_min_freq &&
		ar->lte_coex->wwan_freq < lte_coex_chk[i].wwan_max_freq) {
			if (ar->lte_coex->wwan_band == ATH6KL_WWAN_FDD &&
				(ar->lte_coex->wwan_bw != lte_coex_chk[i].wwan_bw))
				continue;

			ath6kl_lte_coex_set_ap_mode(ar, i, lte_coex_chk);
			break;
		}
	}
}

void ath6kl_lte_coex_update_wwan_data(struct ath6kl *ar, void *wmi_buf)
{
	int table_look_up_entries = 0;
	struct app_lte_coex_wwan_data_t *wwan =
				(struct app_lte_coex_wwan_data_t *)wmi_buf;

	ath6kl_info("LTE_COEX: QMI LTE band info:%d %d %d",
			wwan->band_info_valid, wwan->ul_freq, wwan->dl_freq);
	ath6kl_info("LTE_COEX: QMI TDD CFG:%d %d",
					wwan->tdd_info_valid, wwan->tdd_cfg);
	ath6kl_info("LTE_COEX: QMI off period:%d %d",
				wwan->off_period_valid, wwan->off_period);

	if (!(wwan->band_info_valid == 1 || wwan->tdd_info_valid == 1 ||
		wwan->off_period_valid == 1))
		return;

	ar->lte_coex->wwan_operational = 1;

	if (wwan->band_info_valid == 1) {
		ar->lte_coex->wwan_freq = wwan->ul_freq;
		ar->lte_coex->wwan_bw = wwan->ul_bw;

		if (wwan->ul_freq == 0) {
			if (wwan->dl_freq == 0) {
				ath6kl_dbg(ATH6KL_DBG_LTE_COEX,
				"LTE_COEX: LTE deactivated. Disable lte_coex");
				ar->lte_coex->wwan_state =
					LTE_COEX_WWAN_STATE_DEACTIVATED;
				ar->lte_coex->wwan_band = 0;
			} else {
				ath6kl_dbg(ATH6KL_DBG_LTE_COEX,
						"LTE_COEX: LTE Idle Rx");
				ar->lte_coex->wwan_freq = wwan->dl_freq;
				ar->lte_coex->wwan_state =
						LTE_COEX_WWAN_STATE_IDLE;
				ar->lte_coex->wwan_band = ATH6KL_WWAN_BAND;
			}
		} else {
			ath6kl_dbg(ATH6KL_DBG_LTE_COEX, "LTE_COEX: LTE Active");
			if (wwan->ul_freq == wwan->dl_freq) {
				ar->lte_coex->wwan_band = ATH6KL_WWAN_TDD;
				table_look_up_entries = LTE_COEX_REF_TDD_ROWS;
			} else {
				ar->lte_coex->wwan_band = ATH6KL_WWAN_FDD;
				table_look_up_entries = LTE_COEX_REF_FDD_ROWS;
			}
			ar->lte_coex->wwan_state =
					LTE_COEX_WWAN_STATE_CONNECTED;
		}
	}

	if (ar->lte_coex->wwan_state != LTE_COEX_WWAN_STATE_IDLE) {
		ath6kl_info("wwan state=%d, wwan_band=%d wwan_freq=%d\n",
				ar->lte_coex->wwan_state,
				ar->lte_coex->wwan_band,
				ar->lte_coex->wwan_freq);
		ath6kl_setup_wlan_ap_lte_coex_mode(ar, table_look_up_entries,
				(ar->lte_coex->wwan_band & ATH6KL_WWAN_TDD) ?
				lte_coex_tdd_lookup_table :
				lte_coex_fdd_lookup_table);
	}
}

u32 ath6kl_set_ap_operating_chan_mask(uint32_t chan)
{
	u32 chan_mask = 0, bit = 0;

	bit = (chan - 2412) / 5;
	chan_mask = BIT(bit);

	if (!chan || chan_mask > 0x1fff)
		chan_mask = 0;

	return chan_mask;
}

void ath6kl_lte_coex_update_wlan_data(struct ath6kl_vif *vif, uint32_t chan)
{
	struct ath6kl *ar = vif->ar;
	int table_look_up_entries = (ar->lte_coex->wwan_band & ATH6KL_WWAN_TDD) ?
					LTE_COEX_REF_TDD_ROWS :
					LTE_COEX_REF_FDD_ROWS;

	if (vif->nw_type == AP_NETWORK) {
		if (chan && vif->phy_mode != WMI_11A_MODE) {
			ar->lte_coex->dev_ctx[vif->fw_vif_idx].ap_oper_chan_mask =
				ath6kl_set_ap_operating_chan_mask(chan);

			ath6kl_setup_wlan_ap_lte_coex_mode(ar,
				table_look_up_entries,
				(ar->lte_coex->wwan_band & ATH6KL_WWAN_TDD) ?
				lte_coex_tdd_lookup_table :
				lte_coex_fdd_lookup_table);
		} else
			ar->lte_coex->dev_ctx[vif->fw_vif_idx].ap_oper_chan_mask = 0;
	}
}

bool ath6kl_check_lte_coex_acs(struct ath6kl *ar, uint16_t *ap_acs_ch,
		struct ath6kl_vif *cur_vif)
{
	struct ath6kl_vif *tmp_vif;
	bool ret = false;

	if (ar->lte_coex && ar->lte_coex->acs_chan_mask != AP_ACS_NONE) {
		list_for_each_entry(tmp_vif, &ar->vif_list, list) {
			if (tmp_vif->nw_type == AP_NETWORK) {
				if (test_bit(CONNECTED, &tmp_vif->flags)) {
					if (tmp_vif->fw_vif_idx != cur_vif->fw_vif_idx &&
						(tmp_vif->phy_mode != WMI_11A_MODE)) {
						if (ar->lte_coex->acs_chan_mask &
							ar->lte_coex->dev_ctx[tmp_vif->fw_vif_idx].ap_oper_chan_mask)
							*ap_acs_ch =
								tmp_vif->bss_ch;
					}
				}
			}
		}

		if (!*ap_acs_ch) {
			*ap_acs_ch = cpu_to_le16(AP_ACS_USER_DEFINED);
			cur_vif->acs_chan_mask = ar->lte_coex->acs_chan_mask;
		}
		ar->lte_coex->dev_ctx[cur_vif->fw_vif_idx].ap_oper_chan_mask = 0;
		ret = true;
	}
	return ret;
}

int ath6kl_lte_coex_init(struct ath6kl *ar)
{
	ath6kl_dbg(ATH6KL_DBG_LTE_COEX, "LTE_COEX: WWAN Coex Module Init");
	ar->lte_coex = (struct ath6kl_lte_coex_priv *)
		kzalloc(sizeof(struct ath6kl_lte_coex_priv), GFP_KERNEL);
	if (!ar->lte_coex)
		return -ENOMEM;

	ar->lte_coex->ar = ar;
	ar->lte_coex->acs_chan_mask = AP_ACS_NONE;

	return 0;
}

void ath6kl_lte_coex_deinit(struct ath6kl *ar)
{
	ath6kl_dbg(ATH6KL_DBG_LTE_COEX, "LTE_COEX: WWAN Coex Module Deinit");
	kfree(ar->lte_coex);
	ar->lte_coex = NULL;
}

void ath6kl_tm_rx_wmi_event(struct ath6kl *ar, void *buf, size_t buf_len)
{
	struct sk_buff *skb;


	if (!buf || buf_len == 0)
		return;

	skb = cfg80211_testmode_alloc_event_skb(ar->wiphy, buf_len, GFP_KERNEL);
	if (!skb) {
		ath6kl_warn("failed to allocate testmode rx skb!\n");
		return;
	}
	NLA_PUT_U32(skb, ATH6KL_TM_ATTR_CMD, ATH6KL_TM_CMD_WMI_CMD);
	NLA_PUT(skb, ATH6KL_TM_ATTR_DATA, buf_len, buf);
	cfg80211_testmode_event(skb, GFP_KERNEL);
	return;

nla_put_failure:
	kfree_skb(skb);
	ath6kl_warn("nla_put failed on testmode rx skb!\n");
}

void ath6kl_wmicfg_send_stats(struct ath6kl_vif *vif,
			      struct target_stats *stats)
{
	u32 *buff = kzalloc(sizeof(*stats) + 4, GFP_KERNEL);
	if (buff == NULL)
		return;

	buff[0] = WMI_REPORT_STATISTICS_EVENTID;
	memcpy(buff+1, stats, sizeof(struct target_stats));
	ath6kl_tm_rx_wmi_event(vif->ar->wmi->parent_dev, buff,
			       sizeof(struct target_stats)+4);
	kfree(buff);
}
