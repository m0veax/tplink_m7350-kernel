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

#include "testmode.h"
#include "debug.h"
#include "wmiconfig.h"
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
	ATH6KL_TM_CMD_WMI_CMD           = 0xF000,
	ATH6KL_TM_CMD_CFG		= 0xF001,
};

enum ATH6KL_TM_CMD_CFGS {
	ATH6KL_TM_CMD_DRV_CFG_ACS,
	ATH6KL_TM_CMD_DRV_CFG_LTE,
	ATH6kl_TM_CMD_DRV_CFG_INTER_BSS,
	ATH6KL_TM_CMD_DRV_CFG_SCAN_PARAMS,
	ATH6KL_TM_CMD_DRV_CFG_LAST
};

#define ATH6KL_TM_DATA_MAX_LEN		5000

static const struct nla_policy ath6kl_tm_policy[ATH6KL_TM_ATTR_MAX + 1] = {
	[ATH6KL_TM_ATTR_CMD]		= { .type = NLA_U32 },
	[ATH6KL_TM_ATTR_DATA]		= { .type = NLA_BINARY,
					    .len = ATH6KL_TM_DATA_MAX_LEN },
};

int ath6kl_tm_rx_event(struct ath6kl *ar, void *buf, size_t buf_len,
							int evt_id)
{
	struct sk_buff *skb;
	uint8_t *tbuf;
	int ret = 0;

	if (!buf || buf_len == 0)
		return -EINVAL;

	skb = cfg80211_testmode_alloc_event_skb(ar->wiphy,
			buf_len + sizeof(evt_id), GFP_KERNEL);
	if (!skb) {
		ath6kl_warn("failed to allocate testmode rx skb!\n");
		return -ENOMEM;
	}

	if (!test_bit(TESTMODE_TCMD, &ar->flag)) {
		tbuf = kmalloc(buf_len + sizeof(evt_id), GFP_KERNEL);
		if (!tbuf) {
			ath6kl_warn("Failed to allocate testmode rx buf\n");
			ret = -ENOMEM;
			goto nla_put_failure;
		}
		memcpy(tbuf , (char *) &evt_id, sizeof(evt_id));
		memcpy(tbuf + sizeof(evt_id), buf, buf_len);

		ret = nla_put_u32(skb, ATH6KL_TM_ATTR_CMD, ATH6KL_TM_CMD_TCMD)
			|| nla_put(skb, ATH6KL_TM_ATTR_DATA, buf_len +
							sizeof(evt_id), tbuf);
		kfree(tbuf);

		if (ret)
			goto nla_put_failure;
	} else {
		if (nla_put_u32(skb, ATH6KL_TM_ATTR_CMD, ATH6KL_TM_CMD_TCMD) ||
		nla_put(skb, ATH6KL_TM_ATTR_DATA, buf_len, buf))
			goto nla_put_failure;
	}

	cfg80211_testmode_event(skb, GFP_KERNEL);
	return 0;

nla_put_failure:
	kfree_skb(skb);
	ath6kl_warn("nla_put failed on testmode rx skb!\n");
	return ret;
}

int ath6kl_tm_cmd(struct wiphy *wiphy, struct net_device *dev, void *data,
								int len)
{
	struct ath6kl *ar = ath6kl_priv(dev);
	struct ath6kl_vif *vif = netdev_priv(dev);
	struct nlattr *tb[ATH6KL_TM_ATTR_MAX + 1];
	int err, buf_len;
	void *buf;
	u32 wmi_cmd;
	u32 *cfg_cmd;
        struct sk_buff *skb;

	err = nla_parse(tb, ATH6KL_TM_ATTR_MAX, data, len,
			ath6kl_tm_policy);
	if (err)
		return err;

	if (!tb[ATH6KL_TM_ATTR_CMD])
		return -EINVAL;

	switch (nla_get_u32(tb[ATH6KL_TM_ATTR_CMD])) {
	case ATH6KL_TM_CMD_WMI_CMD:
                if (!tb[ATH6KL_TM_ATTR_DATA])
                        return -EINVAL;

                buf = nla_data(tb[ATH6KL_TM_ATTR_DATA]);
                buf_len = nla_len(tb[ATH6KL_TM_ATTR_DATA]);

                /* First four bytes hold the wmi_cmd and the rest is the data */
                skb = ath6kl_wmi_get_buf(buf_len-4);
                if (!skb)
                        return -ENOMEM;

                memcpy(&wmi_cmd, buf, sizeof(wmi_cmd));
		memcpy(skb->data, (u32 *)buf + 1, buf_len - 4);
		ath6kl_wmi_cmd_send(ar->wmi, vif->fw_vif_idx, skb, wmi_cmd,
							NO_SYNC_WMIFLAG);

                return 0;
	case ATH6KL_TM_CMD_CFG:
		buf = nla_data(tb[ATH6KL_TM_ATTR_DATA]);

		cfg_cmd = (u32 *)buf;

		if (*cfg_cmd == ATH6KL_TM_CMD_DRV_CFG_ACS) {
			struct acs_cfg {
				u32 cfg_cmd;
				u32 acs_config;
			};
			struct acs_cfg *acs = (struct acs_cfg *) buf;
			struct ath6kl_vif *vif;

			spin_lock_bh(&ar->list_lock);
			list_for_each_entry(vif, &ar->vif_list, list) {
			if (vif->nw_type == AP_NETWORK) {
				ath6kl_warn("Setup ACS for AP = %d",
							acs->acs_config);
				vif->profile.ch = cpu_to_le16(acs->acs_config);
				ath6kl_wmi_ap_profile_commit(ar->wmi,
						vif->fw_vif_idx, &vif->profile);
				}
			}
			spin_unlock_bh(&ar->list_lock);
			return 0;

		} else if (*cfg_cmd == ATH6KL_TM_CMD_DRV_CFG_LTE) {
			ath6kl_lte_coex_update_wwan_data(ar, buf);
			return 0;
		} else if (*cfg_cmd == ATH6kl_TM_CMD_DRV_CFG_INTER_BSS) {
			struct wmi_setinterbss_cmd *cmd =
				(struct wmi_setinterbss_cmd *)(buf + 4);
			ar->inter_bss = cmd->enable;
			return 0;
		} else if (*cfg_cmd == ATH6KL_TM_CMD_DRV_CFG_SCAN_PARAMS) {
			struct wmi_scan_params_cmd *cmd =
				(struct wmi_scan_params_cmd *)(buf + 4);

			u16 scan_params_mask = *((u16*)(buf + 4 + sizeof(struct wmi_scan_params_cmd)));

			if (scan_params_mask & ATH6KL_FG_START_PERIOD_MASK)
				ar->scan_params.fg_start_period =
					cmd->fg_start_period;

			if (scan_params_mask & ATH6KL_FG_END_PERIOD_MASK)
				ar->scan_params.fg_end_period =
					cmd->fg_end_period;

			if (scan_params_mask & ATH6KL_BG_PERIOD_MASK)
                                ar->scan_params.bg_period = cmd->bg_period;

			if (scan_params_mask & ATH6KL_MAXACT_CHDWELL_TIME_MASK)
                                ar->scan_params.maxact_chdwell_time =
					cmd->maxact_chdwell_time;

			if (scan_params_mask & ATH6KL_PAS_CHDWELL_TIME_MASK)
                                ar->scan_params.pas_chdwell_time =
					cmd->pas_chdwell_time;

			if (scan_params_mask & ATH6KL_SHORT_SCAN_RATIO_MASK)
                                ar->scan_params.short_scan_ratio =
					cmd->short_scan_ratio;

			if (scan_params_mask & ATH6KL_SCAN_CTRL_FLAGS_MASK)
                                ar->scan_params.scan_ctrl_flags =
					cmd->scan_ctrl_flags;

			if (scan_params_mask & ATH6KL_MINACT_CHDWELL_TIME_MASK)
                                ar->scan_params.minact_chdwell_time =
					cmd->minact_chdwell_time;

			if (scan_params_mask & ATH6KL_MAXACT_SCAN_PER_SSID_MASK)
                                ar->scan_params.maxact_scan_per_ssid =
					cmd->maxact_scan_per_ssid;

			if (scan_params_mask & ATH6KL_MAX_DFSCH_ACT_TIME_MASK)
				ar->scan_params.max_dfsch_act_time =
					cmd->max_dfsch_act_time;

			ar->scan_params_mask |= scan_params_mask;

			ath6kl_wmi_scanparams_cmd(ar->wmi,0,
				ar->scan_params.fg_start_period,
				ar->scan_params.fg_end_period,
				ar->scan_params.bg_period,
				ar->scan_params.minact_chdwell_time,
				ar->scan_params.maxact_chdwell_time,
				ar->scan_params.pas_chdwell_time,
				ar->scan_params.short_scan_ratio,
				ar->scan_params.scan_ctrl_flags,
				ar->scan_params.max_dfsch_act_time,
				ar->scan_params.maxact_scan_per_ssid);

			return 0;
		}
		break;

	case ATH6KL_TM_CMD_TCMD:
		if (!tb[ATH6KL_TM_ATTR_DATA])
			return -EINVAL;

		buf = nla_data(tb[ATH6KL_TM_ATTR_DATA]);
		buf_len = nla_len(tb[ATH6KL_TM_ATTR_DATA]);

		ath6kl_wmi_test_cmd(ar->wmi, buf, buf_len);

		return 0;

		break;
	case ATH6KL_TM_CMD_RX_REPORT:
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}
