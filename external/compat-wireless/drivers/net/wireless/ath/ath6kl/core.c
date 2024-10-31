/*
 * Copyright (c) 2004-2011 Atheros Communications Inc.
 * Copyright (c) 2011-2013 Qualcomm Atheros, Inc.
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

#include "core.h"

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/export.h>
#include <linux/vmalloc.h>

#include "debug.h"
#include "hif-ops.h"
#include "htc-ops.h"
#include "cfg80211.h"
#include "wmiconfig.h"

unsigned int debug_mask;
static unsigned int wow_mode;
static unsigned int ath6kl_p2p;
static unsigned int devmode = ATH6KL_DEFAULT_DEV_MODE;
unsigned int debug_quirks = ATH6KL_DEF_DEBUG_QUIRKS;
static unsigned int mcc_adj_ch_spacing = ATH6KL_DEF_MCC_ADJ_CH_SPACING;
static unsigned int heart_beat_poll;
static unsigned int lte_margin = ATH6KL_DEF_LTE_MARGIN;
static unsigned int enable_ani = 0;
static unsigned int reg_hint = 1;

module_param(debug_mask, uint, 0644);
module_param(wow_mode, uint, 0644);
module_param(ath6kl_p2p, uint, 0644);
module_param(debug_quirks, uint, 0644);
module_param(devmode, uint, 0644);
module_param(mcc_adj_ch_spacing, uint, 0644);
module_param(heart_beat_poll, uint, 0644);
module_param(lte_margin, uint, 0644);
module_param(enable_ani, uint, 0644);
module_param(reg_hint, uint, 0644);
EXPORT_SYMBOL(debug_quirks);

struct ath6kl_fw_err_recovery *fw_recovery;

int cfg80211_ap_fw_error_notify(struct wiphy *wy);

int ath6kl_use_regulatory_hint(void)
{
	return !!reg_hint;
}
EXPORT_SYMBOL(ath6kl_use_regulatory_hint);

void ath6kl_core_tx_complete(struct ath6kl *ar, struct sk_buff *skb)
{
	ath6kl_htc_tx_complete(ar, skb);
}
EXPORT_SYMBOL(ath6kl_core_tx_complete);

void ath6kl_core_rx_complete(struct ath6kl *ar, struct sk_buff *skb, u8 pipe)
{
	ath6kl_htc_rx_complete(ar, skb, pipe);
}
EXPORT_SYMBOL(ath6kl_core_rx_complete);

void ath6kl_update_hb_event(struct ath6kl *ar)
{
	if (ar->fw_recovery)
		ar->fw_recovery->hb_pending = false;
}

static void ath6kl_recovery_hb_timer(unsigned long data)
{
	struct ath6kl *ar = (struct ath6kl *) data;
	int err;

	if (ar->fw_recovery->state != ATH6KL_FW_RECOVERY_NONE)
		return;

	if (ar->fw_recovery->hb_pending)
		ar->fw_recovery->hb_misscnt++;
	else
		ar->fw_recovery->hb_misscnt = 0;

	if (ar->fw_recovery->hb_misscnt > ATH6KL_HB_RESP_MISS_THRES) {
		ar->fw_recovery->hb_misscnt = 0;
		ar->fw_recovery->seq_num = 0;
		ar->fw_recovery->hb_pending = false;
		ath6kl_recovery_err_notify(ar, ATH6KL_FW_HB_RESP_FAILURE);
		return;
	}

	ar->fw_recovery->seq_num++;
	ar->fw_recovery->hb_pending = true;

	err = ath6kl_wmi_get_challenge_resp_cmd(ar->wmi,
			ar->fw_recovery->seq_num, 0);

	if (err)
		ath6kl_warn("Failed to send hb challenge request, err : %d\n",
				err);

	mod_timer(&ar->fw_recovery->hb_timer, jiffies +
			ar->fw_recovery->hb_poll);
}

void ath6kl_recovery_work(struct work_struct *work)
{
	struct ath6kl *ar = fw_recovery->ar;

	if (ar->hif_type == ATH6KL_HIF_TYPE_USB) {
		if (BOOTSTRAP_IS_HSIC(ar->bootstrap_mode)) {
			clear_bit(WMI_READY, &ar->flag);
			ath6kl_cfg80211_stop_all(ar);
			ath6kl_hif_restart(ar);
			return;
		} else
			ath6kl_init_hw_cold_restart(ar);
	} else
		ath6kl_init_hw_restart(ar);

	ar->state = ATH6KL_STATE_ON;
	ar->fw_recovery->err_reason = 0;
}

void ath6kl_recovery_err_notify(struct ath6kl *ar, enum ath6kl_fw_err reason)
{
	if (!ath6kl_debug_quirks_any(ar, ATH6KL_MODULE_FW_ERROR_RECOVERY)) {
		ath6kl_info("Fw error detected, reason :: %d notify hostapd\n",
				reason);
		cfg80211_ap_fw_error_notify(ar->wiphy);
		return;
	}

	if (!test_bit(WMI_READY, &ar->flag))
		return;

	ath6kl_info("Fw error detected, reason :: %d trying to recover\n",
			reason);

	if (ar->fw_recovery->state != ATH6KL_FW_RECOVERY_INPROGRESS) {
		ath6kl_recovery_dump_crash_info(ar);
		set_bit(reason, &ar->fw_recovery->err_reason);
		ar->fw_recovery->state = ATH6KL_FW_RECOVERY_INPROGRESS;
		clear_bit(WMI_CTRL_EP_FULL, &ar->flag);
		del_timer_sync(&ar->fw_recovery->hb_timer);
		schedule_work(&fw_recovery->recovery_work);
	}
}

void ath6kl_recovery_init(struct ath6kl *ar)
{
	struct ath6kl_fw_err_recovery *recovery = ar->fw_recovery;

	if (!ath6kl_debug_quirks_any(ar, ATH6KL_MODULE_FW_ERROR_RECOVERY))
		return;

	if (fw_recovery->state != ATH6KL_FW_RECOVERY_INPROGRESS) {
		fw_recovery->state = ATH6KL_FW_RECOVERY_NONE;
		INIT_WORK(&fw_recovery->recovery_work, ath6kl_recovery_work);
		ar->fw_recovery->hb_timer.function = ath6kl_recovery_hb_timer;
		init_timer_deferrable(&ar->fw_recovery->hb_timer);
	}

	ar->fw_recovery->hb_timer.data = (unsigned long)ar;
	recovery->seq_num = 0;
	recovery->hb_misscnt = 0;
	ar->fw_recovery->hb_pending = false;

	if (ar->fw_recovery->hb_poll)
		mod_timer(&ar->fw_recovery->hb_timer, jiffies +
				ar->fw_recovery->hb_poll);
}

void ath6kl_recovery_cleanup(struct ath6kl *ar)
{
	if (!ath6kl_debug_quirks_any(ar, ATH6KL_MODULE_FW_ERROR_RECOVERY))
		return;

	if (fw_recovery->state == ATH6KL_FW_RECOVERY_INPROGRESS)
		return;

	fw_recovery->state = ATH6KL_FW_RECOVERY_CLEANUP;
	del_timer_sync(&ar->fw_recovery->hb_timer);

	cancel_work_sync(&fw_recovery->recovery_work);
}

void ath6kl_recovery_suspend(struct ath6kl *ar)
{
	if (!ath6kl_debug_quirks_any(ar, ATH6KL_MODULE_FW_ERROR_RECOVERY))
		return;

	cancel_work_sync(&fw_recovery->recovery_work);
	if (ar->fw_recovery->hb_poll)
		del_timer_sync(&ar->fw_recovery->hb_timer);

	/* Check for pending fw error detection */
	if (!ar->fw_recovery->err_reason)
		return;

	ar->fw_recovery->err_reason = 0;
	WARN_ON(ar->state != ATH6KL_STATE_ON);
	ar->fw_recovery->state = ATH6KL_FW_RECOVERY_INPROGRESS;

	if (ar->hif_type == ATH6KL_HIF_TYPE_USB) {
		if (BOOTSTRAP_IS_HSIC(ar->bootstrap_mode))
			ath6kl_hif_restart(ar);
		else
			ath6kl_init_hw_cold_restart(ar);
	} else
		ath6kl_init_hw_restart(ar);

	ar->state = ATH6KL_STATE_ON;
}

void ath6kl_recovery_resume(struct ath6kl *ar)
{
	if (!ath6kl_debug_quirks_any(ar, ATH6KL_MODULE_FW_ERROR_RECOVERY))
		return;

	fw_recovery->state = ATH6KL_FW_RECOVERY_CLEANUP;
	if (!ar->fw_recovery->hb_poll)
		return;

	ar->fw_recovery->hb_pending = false;
	ar->fw_recovery->seq_num = 0;
	ar->fw_recovery->hb_misscnt = 0;
	mod_timer(&ar->fw_recovery->hb_timer, jiffies +
			ar->fw_recovery->hb_poll);
}

static int ath6kl_get_bootstrap_mode(struct ath6kl *ar)
{
	u32 address = WLAN_BOOTSTRAP_ADDRESS;
	u32 bootstrap;

	/*
	 * check only for AR6004 to differentiate between USB and HSIC
	 */
	if (ar->target_type != TARGET_TYPE_AR6004) {
		ar->bootstrap_mode = 0;
		return 0;
	}

	if (ath6kl_diag_read32(ar, address, &bootstrap))
		return -EIO;

	ath6kl_info("Target bootstrap: 0x%08x\n", bootstrap);
	ar->bootstrap_mode = bootstrap;

	return 0;
}

int ath6kl_core_init(struct ath6kl *ar, enum ath6kl_htc_type htc_type)
{
	struct ath6kl_bmi_target_info targ_info;
	struct net_device *ndev;
	int ret = 0, i, no_of_vif = 1;

	switch (htc_type) {
	case ATH6KL_HTC_TYPE_MBOX:
		ath6kl_htc_mbox_attach(ar);
		ar->ath6kl_wq = alloc_workqueue("ath6kl",
				WQ_MEM_RECLAIM | WQ_CPU_INTENSIVE, 1);
		if (!ar->ath6kl_wq)
			goto err_wq;
		break;
	case ATH6KL_HTC_TYPE_PIPE:
		ath6kl_htc_pipe_attach(ar);
		ar->ath6kl_wq_tx = alloc_workqueue("ath6kl_tx",
				WQ_MEM_RECLAIM | WQ_CPU_INTENSIVE, 1);
		if (!ar->ath6kl_wq_tx)
			goto err_wq;

		ar->ath6kl_wq_rx = alloc_workqueue("ath6kl_rx",
				WQ_MEM_RECLAIM | WQ_CPU_INTENSIVE, 1);
		if (!ar->ath6kl_wq_rx)
			goto err_wq;
		break;
	default:
		WARN_ON(1);
		return -ENOMEM;
	}

	ret = ath6kl_bmi_init(ar);
	if (ret)
		goto err_wq;

	/*
	 * Turn on power to get hardware (target) version and leave power
	 * on delibrately as we will boot the hardware anyway within few
	 * seconds.
	 */
	ret = ath6kl_hif_power_on(ar);
	if (ret)
		goto err_bmi_cleanup;

	ret = ath6kl_bmi_get_target_info(ar, &targ_info);
	if (ret)
		goto err_power_off;

	ar->version.target_ver = le32_to_cpu(targ_info.version);
	ar->target_type = le32_to_cpu(targ_info.type);
	ar->wiphy->hw_version = le32_to_cpu(targ_info.version);

	ret = ath6kl_get_bootstrap_mode(ar);

	if (ret) {
		ath6kl_err("Can't get bootstrap mode");
		goto err_power_off;
	}

	ret = ath6kl_init_hw_params(ar);
	if (ret)
		goto err_power_off;

	ar->htc_target = ath6kl_htc_create(ar);

	if (!ar->htc_target) {
		ret = -ENOMEM;
		goto err_power_off;
	}

	ret = ath6kl_init_fetch_firmwares(ar);
	if (ret)
		goto err_htc_cleanup;

	/* FIXME: we should free all firmwares in the error cases below */

	if ( test_bit(TESTMODE_EPPING, &ar->flag) ) {
		ath6kl_info("%s: endpoint loopback mode, ignore wmi init!\n", __func__);
	} else {
		/* Indicate that WMI is enabled (although not ready yet) */
		set_bit(WMI_ENABLED, &ar->flag);
		ar->wmi = ath6kl_wmi_init(ar);
		if (!ar->wmi) {
			ath6kl_err("failed to initialize wmi\n");
			ret = -EIO;
			goto err_htc_cleanup;
		}

		ath6kl_dbg(ATH6KL_DBG_TRC, "%s: got wmi @ 0x%p.\n", __func__, ar->wmi);
	}

	/* setup access class priority mappings */
	ar->ac_stream_pri_map[WMM_AC_BK] = 0; /* lowest  */
	ar->ac_stream_pri_map[WMM_AC_BE] = 1;
	ar->ac_stream_pri_map[WMM_AC_VI] = 2;
	ar->ac_stream_pri_map[WMM_AC_VO] = 3; /* highest */

	/* allocate some buffers that handle larger AMSDU frames */
	ath6kl_refill_amsdu_rxbufs(ar, ATH6KL_MAX_AMSDU_RX_BUFFERS);

	ath6kl_cookie_init(ar);
	ar->mcc_adj_ch_spacing = mcc_adj_ch_spacing;
        ar->enable_ani = enable_ani;

	ar->conf_flags = ATH6KL_CONF_IGNORE_ERP_BARKER |
			 ATH6KL_CONF_ENABLE_11N | ATH6KL_CONF_ENABLE_TX_BURST;

	ar->mcc_flowctrl_ctx = ath6kl_mcc_flowctrl_conn_list_init(ar);

	if (ath6kl_debug_quirks(ar, ATH6KL_MODULE_SUSPEND_CUTPOWER))
		ar->suspend_mode = WLAN_POWER_STATE_CUT_PWR;
	else if (ath6kl_debug_quirks(ar, ATH6KL_MODULE_SUSPEND_DEEPSLEEP))
		ar->suspend_mode = WLAN_POWER_STATE_DEEP_SLEEP;
	else if (ath6kl_debug_quirks(ar, ATH6KL_MODULE_SUSPEND_WOW))
		ar->suspend_mode = WLAN_POWER_STATE_WOW;
	else
		ar->suspend_mode = 0;

	if (ar->suspend_mode == WLAN_POWER_STATE_WOW &&
	    (wow_mode == WLAN_POWER_STATE_CUT_PWR ||
	     wow_mode == WLAN_POWER_STATE_DEEP_SLEEP))
		ar->wow_suspend_mode = wow_mode;
	else
		ar->wow_suspend_mode = 0;

	ar->vif_max = 1;
	if (devmode != ATH6KL_SINGLE_DEV_MODE) {
		ar->vif_max = 2;
		if(ar->vif_max > 1 && !ar->p2p) {
			ar->max_norm_iface = 2;
		}
	}

	if (ath6kl_debug_quirks(ar, ATH6KL_MODULE_UART_DEBUG))
		ar->conf_flags |= ATH6KL_CONF_UART_DEBUG;

	set_bit(FIRST_BOOT, &ar->flag);

	ath6kl_debug_init(ar);

	ret = ath6kl_init_hw_start(ar);
	if (ret) {
		ath6kl_err("Failed to start hardware: %d\n", ret);
		goto err_rxbuf_cleanup;
	}

	/* give our connected endpoints some buffers */
	ath6kl_rx_refill(ar->htc_target, ar->ctrl_ep);
	ath6kl_rx_refill(ar->htc_target, ar->ac2ep_map[WMM_AC_BE]);

	if ( test_bit(TESTMODE_EPPING, &ar->flag) ) {
		ath6kl_info("bypass wmi, and post receive buffer for each endpoint here!\n");
		ath6kl_rx_refill(ar->htc_target, ar->ac2ep_map[WMM_AC_BK]);
		ath6kl_rx_refill(ar->htc_target, ar->ac2ep_map[WMM_AC_VI]);
		ath6kl_rx_refill(ar->htc_target, ar->ac2ep_map[WMM_AC_VO]);
	}

	ret = ath6kl_cfg80211_init(ar);
	if (ret)
		goto err_rxbuf_cleanup;

	ret = ath6kl_debug_init_fs(ar);
	if (ret) {
		wiphy_unregister(ar->wiphy);
		goto err_rxbuf_cleanup;
	}

	for (i = 0; i < ar->vif_max; i++)
		ar->avail_idx_map |= BIT(i);

	ret = ath6kl_lte_coex_init(ar);
	if (ret)
		goto err_rxbuf_cleanup;
	ar->lte_margin = lte_margin;

	if (heart_beat_poll && test_bit(ATH6KL_FW_CAPABILITY_HEART_BEAT_POLL,
			ar->fw_capabilities)) {
		/* store the hb timeout value in terms of jiffies
		 * 1 sec = 60 jiffies, user configured value is in seconds
		 * so converting to jiffies
		 */
		ar->fw_recovery->hb_poll = heart_beat_poll * 60;
	}

	ath6kl_recovery_init(ar);


	if (fw_recovery->state  == ATH6KL_FW_RECOVERY_INPROGRESS)
		no_of_vif = ar->vif_max;

	for (i = 0; i < no_of_vif; i++) {
		rtnl_lock();

		/* Add an initial station interface */
		ndev = ath6kl_interface_add(ar, "wlan%d",
				NL80211_IFTYPE_STATION, i, INFRA_NETWORK);

		rtnl_unlock();

		if (!ndev) {
			ath6kl_err("Failed to instantiate a network device\n");
			ret = -ENOMEM;
			wiphy_unregister(ar->wiphy);
			goto err_rxbuf_cleanup;
		}
		ath6kl_dbg(ATH6KL_DBG_TRC, "%s: name=%s dev=0x%p, ar=0x%p\n",
				__func__, ndev->name, ndev, ar);
	}

	fw_recovery->state = ATH6KL_FW_RECOVERY_NONE;

	return ret;

err_rxbuf_cleanup:
	ath6kl_debug_cleanup(ar);
	ath6kl_htc_flush_rx_buf(ar->htc_target);
	ath6kl_cleanup_amsdu_rxbufs(ar);
	ath6kl_wmi_shutdown(ar->wmi);
	clear_bit(WMI_ENABLED, &ar->flag);
	ar->wmi = NULL;
err_htc_cleanup:
	ath6kl_htc_cleanup(ar->htc_target);
err_power_off:
	ath6kl_hif_power_off(ar);
err_bmi_cleanup:
	ath6kl_bmi_cleanup(ar);
err_wq:
	if (ar->ath6kl_wq)
		destroy_workqueue(ar->ath6kl_wq);
	if (ar->ath6kl_wq_tx)
		destroy_workqueue(ar->ath6kl_wq_tx);
	if (ar->ath6kl_wq_rx)
		destroy_workqueue(ar->ath6kl_wq_rx);
	return ret;
}
EXPORT_SYMBOL(ath6kl_core_init);

struct ath6kl *ath6kl_core_create(struct device *dev)
{
	struct ath6kl *ar;
	u8 ctr;

	ar = ath6kl_cfg80211_create();
	if (!ar)
		return NULL;

	ath6kl_dbg(ATH6KL_DBG_BOOT,
			"Module param: debug_quirks set to : %x \n",
			debug_quirks);

	ar->debug_quirks = debug_quirks;

	ar->p2p = !!ath6kl_p2p;
	ar->dev = dev;
	ar->vif_max = 1;
	ar->num_vif = 0;
	ar->inter_bss = true;
	ar->max_norm_iface = 1;
	ar->sta_bh_override = 0;
	ar->acs_in_prog = 0;

	spin_lock_init(&ar->lock);
	spin_lock_init(&ar->mcastpsq_lock);
	spin_lock_init(&ar->list_lock);

	init_waitqueue_head(&ar->event_wq);
	sema_init(&ar->sem, 1);

	INIT_LIST_HEAD(&ar->amsdu_rx_buffer_queue);
	INIT_LIST_HEAD(&ar->vif_list);

	clear_bit(WMI_ENABLED, &ar->flag);
	clear_bit(SKIP_SCAN, &ar->flag);
	clear_bit(DESTROY_IN_PROGRESS, &ar->flag);

	ar->tx_pwr = 0;
	ar->lrssi_roam_threshold = DEF_LRSSI_ROAM_THRESHOLD;
	ar->tx_psq_threshold = ATH6KL_CONN_TX_PSQ_MAX_LEN;

	ar->state = ATH6KL_STATE_OFF;

	memset((u8 *)ar->sta_list, 0,
	       NUM_CONN * sizeof(struct ath6kl_sta));

	/* Init the PS queues */
	for (ctr = 0; ctr < NUM_CONN; ctr++) {
		spin_lock_init(&ar->sta_list[ctr].psq_lock);
		skb_queue_head_init(&ar->sta_list[ctr].psq);
		skb_queue_head_init(&ar->sta_list[ctr].apsdq);
		ar->sta_list[ctr].mgmt_psq_len = 0;
		INIT_LIST_HEAD(&ar->sta_list[ctr].mgmt_psq);
		ar->sta_list[ctr].aggr_conn =
			kzalloc(sizeof(struct aggr_info_conn), GFP_KERNEL);
		if (!ar->sta_list[ctr].aggr_conn) {
			ath6kl_err("Failed to allocate memory for sta aggregation information\n");
			ath6kl_core_destroy(ar);
			return NULL;
		}
	}

	skb_queue_head_init(&ar->mcastpsq);

	memcpy(ar->ap_country_code, DEF_AP_COUNTRY_CODE, 3);

	memset(&ar->scan_params, 0, sizeof(struct wmi_scan_params_cmd));
	ar->scan_params.scan_ctrl_flags = DEFAULT_SCAN_CTRL_FLAGS;
	ar->scan_params.short_scan_ratio = WMI_SHORTSCANRATIO_DEFAULT;

	ar->scan_params_mask = 0;
	if (!fw_recovery) {
		fw_recovery = kmalloc(sizeof(struct ath6kl_fw_err_recovery),
				GFP_KERNEL);

		if (!fw_recovery) {
			ath6kl_err("Failed to allocate memory for"
					" fw_recovery!\n");
			ath6kl_core_destroy(ar);
			return NULL;
		}

		memset(fw_recovery, 0, sizeof(struct ath6kl_fw_err_recovery));

		ar->fw_recovery = fw_recovery;
		fw_recovery->ar = ar;
	} else {
		ar->fw_recovery = fw_recovery;
		fw_recovery->ar = ar;
	}

	return ar;
}
EXPORT_SYMBOL(ath6kl_core_create);

void ath6kl_core_cleanup(struct ath6kl *ar)
{
	ath6kl_hif_power_off(ar);
	ath6kl_recovery_cleanup(ar);

	if (ar->ath6kl_wq)
		destroy_workqueue(ar->ath6kl_wq);
	if (ar->ath6kl_wq_tx)
		destroy_workqueue(ar->ath6kl_wq_tx);
	if (ar->ath6kl_wq_rx)
		destroy_workqueue(ar->ath6kl_wq_rx);

	if (ar->htc_target)
		ath6kl_htc_cleanup(ar->htc_target);

	ath6kl_cookie_cleanup(ar);

	ath6kl_cleanup_amsdu_rxbufs(ar);

	ath6kl_bmi_cleanup(ar);

	ath6kl_debug_cleanup(ar);

	ath6kl_mcc_flowctrl_conn_list_deinit(ar);
	ath6kl_lte_coex_deinit(ar);

	kfree(ar->fw_board);
	kfree(ar->fw_otp);
	if (ath6kl_debug_quirks_any(ar, ATH6KL_MODULE_TESTMODE_TCMD |
				ATH6KL_MODULE_TESTMODE_UTF |
				ATH6KL_MODULE_ENABLE_EPPING))
		kfree(ar->fw);
	else
		vfree(ar->fw);

	kfree(ar->fw_patch);
	kfree(ar->fw_testscript);

	ath6kl_cfg80211_cleanup(ar);
}
EXPORT_SYMBOL(ath6kl_core_cleanup);

void ath6kl_core_destroy(struct ath6kl *ar)
{
	if (fw_recovery &&
			fw_recovery->state != ATH6KL_FW_RECOVERY_INPROGRESS) {
		kfree(fw_recovery);
		fw_recovery = NULL;
	}

	ath6kl_cfg80211_destroy(ar);
}
EXPORT_SYMBOL(ath6kl_core_destroy);

MODULE_AUTHOR("Qualcomm Atheros");
MODULE_DESCRIPTION("Core module for AR600x SDIO and USB devices.");
MODULE_LICENSE("Dual BSD/GPL");
