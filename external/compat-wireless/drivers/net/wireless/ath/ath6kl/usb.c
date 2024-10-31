/*
 * Copyright (c) 2007-2011 Atheros Communications Inc.
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
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/atomic.h>

#ifdef CONFIG_ATH6KL_BAM2BAM
#include <linux/usb/hbm.h>
#endif

#include "debug.h"
#include "core.h"
#include "cfg80211.h"
#include "platform.h"
#include "hif-ops.h"

/* constants */
#define TX_URB_COUNT 		32
#define RX_URB_COUNT            32
#define ATH6KL_USB_RX_BUFFER_SIZE  1700

extern unsigned int debug_quirks;

/* tx/rx pipes for usb */
enum ATH6KL_USB_PIPE_ID {
	ATH6KL_USB_PIPE_TX_CTRL = 0,
	ATH6KL_USB_PIPE_TX_DATA_LP,
	ATH6KL_USB_PIPE_TX_DATA_MP,
	ATH6KL_USB_PIPE_TX_DATA_HP,
	ATH6KL_USB_PIPE_TX_DATA_VHP,
	ATH6KL_USB_PIPE_RX_CTRL,
	ATH6KL_USB_PIPE_RX_DATA,
	ATH6KL_USB_PIPE_RX_DATA2,
	ATH6KL_USB_PIPE_RX_INT,
	ATH6KL_USB_PIPE_MAX
};

#ifdef CONFIG_ATH6KL_AUTO_PM
enum ATH6KL_USB_AUTOPM_STATE {
	ATH6KL_USB_AUTOPM_STATE_ON = 0, /* Port is ON */
	ATH6KL_USB_AUTOPM_STATE_INPROGRESS, /* Suspend/resume is in progress */
	ATH6KL_USB_AUTOPM_STATE_SUSPENDED, /* Port is Suspended */
};
#endif

#define ATH6KL_USB_PIPE_INVALID ATH6KL_USB_PIPE_MAX

#ifdef CONFIG_ATH6KL_BAM2BAM

/* Structure decl for BAM pipes */
struct ath6kl_usb_bam_pipe {
	u8 connected;
	u32 src_pipe;
	u32 dst_pipe;
	struct urb *urb;
	struct usb_bam_connect_ipa_params ipa_params;
};
#endif

struct ath6kl_usb_pipe_stats {
	u32 num_tx;
	u32 num_tx_err;
	u32 num_tx_err_others;

	u32 num_tx_comp;
	u32 num_tx_comp_err;
	u32 num_tx_io_comp;
	u32 num_tx_wq_sched;
	u32 num_max_tx;

	u32 num_rx_comp;
	u32 num_rx_comp_err;
	u32 num_rx_io_comp;
	u32 num_rx_wq_sched;
	u32 num_max_rx;

#ifdef CONFIG_ATH6KL_BAM2BAM
	u32 num_ipa_tx;
	u32 num_ipa_tx_err;
	u32 num_ipa_tx_comp;

	u32 num_ipa_rx;
#endif /* CONFIG_ATH6KL_BAM2BAM */
};

struct ath6kl_usb_pipe {
	struct list_head urb_list_head;
	struct usb_anchor urb_submitted;
	u32 urb_alloc;
	u32 urb_cnt;
	u32 urb_cnt_thresh;
	unsigned int usb_pipe_handle;
	u32 flags;
	u8 ep_address;
	u8 logical_pipe_num;
	struct ath6kl_usb *ar_usb;
	u16 max_packet_size;
	struct work_struct tx_io_complete_work;
	struct work_struct rx_io_complete_work;
	struct sk_buff_head tx_io_comp_queue;
	struct sk_buff_head rx_io_comp_queue;
	struct usb_endpoint_descriptor *ep_desc;
	struct ath6kl_usb_pipe_stats stats;

#ifdef CONFIG_ATH6KL_BAM2BAM
	struct ath6kl_usb_bam_pipe bam_pipe;
#endif
};

#define ATH6KL_USB_PIPE_FLAG_TX    (1 << 0)
#define ATH6KL_USB_PIPE_FLAG_RX    (1 << 1)

#ifdef CONFIG_ATH6KL_AUTO_PM
struct ath6kl_usb_pm_stats {
	u32 suspended;
	u32 suspend_err;
	u32 resumed;
	u32 reset_resume;
	u32 tx_processed;
	u32 tx_queued;
	u32 max_queue_len;
	u32 bam_activity;
	u32 bam_inactivity;
	u32 disable;
	u32 enable;
};
#endif

/* usb device object */
struct ath6kl_usb {
	/* protects pipe->urb_list_head and  pipe->urb_cnt */
	spinlock_t cs_lock;

	struct usb_device *udev;
	struct usb_interface *interface;
	struct ath6kl_usb_pipe pipes[ATH6KL_USB_PIPE_MAX];
	u8 *diag_cmd_buffer;
	u8 *diag_resp_buffer;
	struct ath6kl *ar;
	u32 rxq_threshold;
#ifdef CONFIG_ATH6KL_BAM2BAM
	u32 bam_pipe_mask;
#endif
#ifdef CONFIG_ATH6KL_AUTO_PM
	atomic_t autopm_state;
	struct list_head pm_q;
	spinlock_t pm_lock;
	struct work_struct pm_resume_work;
	struct ath6kl_usb_pm_stats pm_stats;
#endif
};

/* usb urb object */
struct ath6kl_urb_context {
	struct list_head link;
	struct ath6kl_usb_pipe *pipe;
	struct sk_buff *skb;
	struct ath6kl *ar;
#ifdef CONFIG_ATH6KL_AUTO_PM
	int autopm; /* If set, put_interface_async */
#endif
};

/* USB endpoint definitions */
#define ATH6KL_USB_EP_ADDR_APP_CTRL_IN          0x81
#define ATH6KL_USB_EP_ADDR_APP_DATA_IN          0x82
#define ATH6KL_USB_EP_ADDR_APP_DATA2_IN         0x83
#define ATH6KL_USB_EP_ADDR_APP_INT_IN           0x84

#define ATH6KL_USB_EP_ADDR_APP_CTRL_OUT         0x01
#define ATH6KL_USB_EP_ADDR_APP_DATA_LP_OUT      0x02
#define ATH6KL_USB_EP_ADDR_APP_DATA_MP_OUT      0x03
#define ATH6KL_USB_EP_ADDR_APP_DATA_HP_OUT      0x04
#define ATH6KL_USB_EP_ADDR_APP_DATA_VHP_OUT     0x05

/* diagnostic command defnitions */
#define ATH6KL_USB_CONTROL_REQ_SEND_BMI_CMD        1
#define ATH6KL_USB_CONTROL_REQ_RECV_BMI_RESP       2
#define ATH6KL_USB_CONTROL_REQ_DIAG_CMD            3
#define ATH6KL_USB_CONTROL_REQ_DIAG_RESP           4

#define ATH6KL_USB_CTRL_DIAG_CC_READ               0
#define ATH6KL_USB_CTRL_DIAG_CC_WRITE              1

#define HIF_USB_RX_QUEUE_THRESHOLD          256

struct ath6kl_usb_ctrl_diag_cmd_write {
	__le32 cmd;
	__le32 address;
	__le32 value;
	__le32 _pad[1];
} __packed;

struct ath6kl_usb_ctrl_diag_cmd_read {
	__le32 cmd;
	__le32 address;
} __packed;

struct ath6kl_usb_ctrl_diag_resp_read {
	__le32 value;
} __packed;

/* function declarations */
static void ath6kl_usb_recv_complete(struct urb *urb);
#ifdef CONFIG_PM
static int ath6kl_usb_pm_suspend(struct usb_interface *interface,
		pm_message_t message);
static int ath6kl_usb_pm_resume(struct usb_interface *interface);
static int ath6kl_usb_pm_reset_resume(struct usb_interface *intf);
#endif

#define ATH6KL_USB_IS_BULK_EP(attr) (((attr) & 3) == 0x02)
#define ATH6KL_USB_IS_INT_EP(attr)  (((attr) & 3) == 0x03)
#define ATH6KL_USB_IS_ISOC_EP(attr)  (((attr) & 3) == 0x01)
#define ATH6KL_USB_IS_DIR_IN(addr)  ((addr) & 0x80)


#ifdef CONFIG_ATH6KL_BAM2BAM

void ath6kl_usb_bam_transmit_complete(struct ath6kl_urb_context *urb_context);

static struct bam_inf {
	enum 	ipa_client_type client;
} bam_info[ATH6KL_USB_PIPE_MAX] = {
	/* TX Pipes */
	/* non-bam pipe */
	{0},
	/* BK traffic */
	{IPA_CLIENT_HSIC1_CONS},
	/* BE traffic */
	{IPA_CLIENT_HSIC2_CONS},
	/* VI traffic */
	{IPA_CLIENT_HSIC3_CONS},
	/* VO traffic */
	{IPA_CLIENT_HSIC4_CONS},
	/* RX Pipes */
	/* non-bam pipe */
	{0},
	/* non-bam pipe */
	{0},
	/* RX - WLAN/WWAN traffic */
	{IPA_CLIENT_HSIC1_PROD},
	/* non-bam pipe */
	{0}
};

void ath6kl_hsic_restart(struct ath6kl *ar)
{
	mdelay(500);
	ath6kl_recover_firmware();
}

static inline void ath6kl_put_context(struct sk_buff *skb,
		struct ath6kl_urb_context *context)
{
	*((unsigned long *)(skb->cb+32)) = (unsigned long)context;
}

static inline void *ath6kl_get_context(struct sk_buff *skb)
{
	return (void *)(*((unsigned long  *)(skb->cb+32)));
}

#ifdef CONFIG_ATH6KL_BAM2BAM
static void ath6kl_usb_bam_set_pipe_mask(struct ath6kl_usb *ar_usb)
{
	if (!(!!(debug_quirks & ATH6KL_MODULE_BAM2BAM)))
		return;

	if (!(!!(debug_quirks & ATH6KL_MODULE_BAM_RX_SW_PATH)))
		ar_usb->bam_pipe_mask |= BIT(ATH6KL_USB_PIPE_RX_DATA2);

	if (!(!!(debug_quirks & ATH6KL_MODULE_BAM_TX_SW_PATH))) {
		ar_usb->bam_pipe_mask |= BIT(ATH6KL_USB_PIPE_TX_DATA_LP);
		ar_usb->bam_pipe_mask |= BIT(ATH6KL_USB_PIPE_TX_DATA_MP);
		ar_usb->bam_pipe_mask |= BIT(ATH6KL_USB_PIPE_TX_DATA_HP);
		ar_usb->bam_pipe_mask |= BIT(ATH6KL_USB_PIPE_TX_DATA_VHP);
	}
}

static inline bool ath6kl_is_bam_pipe(struct ath6kl_usb_pipe *pipe)
{
	return pipe->ar_usb->bam_pipe_mask & BIT(pipe->logical_pipe_num);
}
#endif

static void ath6kl_usb_bam_free_urb(struct urb *urb)
{
	if (urb == NULL) {
		ath6kl_err("BAM-CM: URB is NULL, can not free!\n");
		return;
	}

	if (urb->priv_data)
		kfree(urb->priv_data);

	usb_free_urb(urb);
}

static void ath6kl_kill_bam_urbs(struct ath6kl_usb *ar_usb)
{
	int i;
	struct ath6kl_usb_bam_pipe *bam_pipe;

	for (i = 0; i < ATH6KL_USB_PIPE_MAX; i++) {

		bam_pipe = &ar_usb->pipes[i].bam_pipe;

		/* If bam pipe not connected then dont kill dummy urbs attached
		 * to bam pipes */
		if (!bam_pipe->connected)
			continue;

		/* kill the dummy urbs attached to bam pipe during error
		 * condition. If we are not clearing here, while unloading the
		 * driver usb will try to clear these urbs and the callback
		 * registered will be invalid at that time */
		usb_kill_urb(bam_pipe->urb);
	}
}

/* Disconnects all the bam pipes Tx-4, Rx-1 */
static void ath6kl_disconnect_bam_pipes(struct ath6kl_usb *ar_usb)
{
	int i;
	struct ath6kl_usb_bam_pipe *bam_pipe;

	for (i = 0; i < ATH6KL_USB_PIPE_MAX; i++) {

		bam_pipe = &ar_usb->pipes[i].bam_pipe;

		/* If bam pipe not connected then dont delete */
		if (!bam_pipe->connected)
			continue;

		bam_pipe->connected = 0;
		usb_bam_disconnect_ipa(&bam_pipe->ipa_params);

		ath6kl_usb_bam_free_urb(bam_pipe->urb);
	}
}

/* BAM PIPE Callback function , called while Tx complete and Rx Receive */
static void ath6kl_ipa_data_callback(void *priv, enum ipa_dp_evt_type evt,
		unsigned long data)
{
	struct ath6kl_urb_context *urb_context;
	struct ath6kl_usb_pipe *pipe;
	struct sk_buff *skb;
	struct ath6kl_usb *ar_usb = NULL;

	/* typecast the skb buffer pointer */
	skb = (struct sk_buff *) data;

	/* check the callback event type */
	switch (evt)
	{
		/* IPA sends data to WLAN class driver */
	case IPA_RECEIVE:
		/* Since we have only 1 Rx pipe, priv always points to that */
		pipe = (struct ath6kl_usb_pipe *)priv;
		ar_usb = pipe->ar_usb;
		/* Queue the skb and wake-up the worker thread. Please note
		 * that non-bam2bam pipe (usb.c) also uses the same worker
		 * thread after queuing the skb in Rx-Event pipe */
		ath6kl_dbg(ATH6KL_DBG_BAM2BAM,
			"BAM-CM: %s: pipe: %d, Received skb from IPA module\n",
		       	__func__, pipe->logical_pipe_num);
		skb_queue_tail(&pipe->rx_io_comp_queue, skb);
		pipe->stats.num_ipa_rx++;
		queue_work(ar_usb->ar->ath6kl_wq_rx, &pipe->rx_io_complete_work);
		break;

		/* IPA sends Tx complete Event to WLAN */
	case IPA_WRITE_DONE:
		urb_context=(struct ath6kl_urb_context *)ath6kl_get_context(skb);
		switch (urb_context->pipe->logical_pipe_num)
		{
		case	ATH6KL_USB_PIPE_TX_DATA_LP:
		case	ATH6KL_USB_PIPE_TX_DATA_MP:
		case	ATH6KL_USB_PIPE_TX_DATA_HP:
		case	ATH6KL_USB_PIPE_TX_DATA_VHP:
			ath6kl_dbg(ATH6KL_DBG_BAM2BAM,
				"BAM-CM: Tx Complete : Write done from IPA module "
				"%s , pipe no: %d\n", __func__,
				urb_context->pipe->logical_pipe_num);
			ath6kl_usb_bam_transmit_complete(urb_context);
			break;
		default:
			ath6kl_err("BAM-CM: IPA Tx complete callback not "
					"matching with Tx-pipe: %d\n",
					urb_context->pipe->logical_pipe_num);
			break;
		}
		break; /* IPA WRITE DONE */
	default :
		ath6kl_err("BAM-CM: IPA Tx complete callback received "
				"wrong event type : %d\n", evt);
		break;
	}
}

#ifdef CONFIG_ATH6KL_AUTO_PM
int ath6kl_usb_bam_activity_cb(void *priv)
{
	struct ath6kl_usb_pipe *pipe = (struct ath6kl_usb_pipe *) priv;
	struct ath6kl_usb *ar_usb = pipe->ar_usb;
	int autopm_state;

	ath6kl_dbg(ATH6KL_DBG_SUSPEND,
			"BAM Activity indication callback, pipe_num: %d\n",
			pipe->logical_pipe_num);

	spin_lock_bh(&ar_usb->pm_lock);

	autopm_state = atomic_read(&ar_usb->autopm_state);

	if(autopm_state != ATH6KL_USB_AUTOPM_STATE_ON ||
		(atomic_read(&ar_usb->interface->pm_usage_cnt) == 0))
		usb_autopm_get_interface_async(ar_usb->interface);

	spin_unlock_bh(&ar_usb->pm_lock);

	ar_usb->pm_stats.bam_activity++;

	return 0;
}

int ath6kl_usb_bam_inactivity_cb(void *priv)
{
	struct ath6kl_usb_pipe *pipe = (struct ath6kl_usb_pipe *) priv;
	struct ath6kl_usb *ar_usb = pipe->ar_usb;
	int autopm_state;

	ath6kl_dbg(ATH6KL_DBG_SUSPEND,
			"BAM Inactivity indication callback, pipe_num: %d\n",
			pipe->logical_pipe_num);

	spin_lock_bh(&ar_usb->pm_lock);

	autopm_state = atomic_read(&ar_usb->autopm_state);

	if(autopm_state == ATH6KL_USB_AUTOPM_STATE_ON &&
		(atomic_read(&ar_usb->interface->pm_usage_cnt)>0))
		usb_autopm_put_interface_async(ar_usb->interface);

	spin_unlock_bh(&ar_usb->pm_lock);

	ar_usb->pm_stats.bam_inactivity++;

	return 0;
}
#endif /* CONFIG_ATH6KL_AUTO_PM */

/* Create BAM pipes, this function will be called from usb.c file
 * whilte iterating each pipe */
/* returns 0 - On Success, -1 on Error */
static int ath6kl_create_bam_pipe(struct ath6kl_usb_pipe *pipe)
{
	int status = 0;
	int conn_idx;
	int pipe_num = pipe->logical_pipe_num;
	struct ath6kl_usb_bam_pipe *bam_pipe = &pipe->bam_pipe;


	switch(pipe_num) {
	case ATH6KL_USB_PIPE_TX_DATA_LP:
	case ATH6KL_USB_PIPE_TX_DATA_MP:
	case ATH6KL_USB_PIPE_TX_DATA_HP:
	case ATH6KL_USB_PIPE_TX_DATA_VHP:
		bam_pipe->connected = 0;
		ath6kl_dbg(ATH6KL_DBG_BAM2BAM,
			"BAM-CM: Creating bam pipe for TX MP %s, %d\n",
				__func__, __LINE__);
		conn_idx = usb_bam_get_connection_idx("hsic", IPA_P_BAM,
				PEER_PERIPHERAL_TO_USB, pipe_num - 1);

		if (conn_idx < 0) {
			ath6kl_err("TX: Peer Peripheral to USB  (dst) failed "
					"%d: %d\n", pipe_num - 1, conn_idx);
			return conn_idx;
		}

		bam_pipe->ipa_params.dst_idx = conn_idx;
		bam_pipe->ipa_params.src_idx = 0;

		bam_pipe->ipa_params.dir = PEER_PERIPHERAL_TO_USB;
		bam_pipe->ipa_params.dst_client = bam_info[pipe_num].client;
		bam_pipe->ipa_params.src_pipe = NULL;
		bam_pipe->ipa_params.dst_pipe = &(bam_pipe->dst_pipe);
		/* Fill the Tx pipe ep confg from IPA Config module */
		ath6kl_ipacm_get_ep_config_info(bam_pipe->ipa_params.dst_client,
				&(bam_pipe->ipa_params.ipa_ep_cfg));
		bam_pipe->ipa_params.priv = pipe;
		bam_pipe->ipa_params.notify = ath6kl_ipa_data_callback;
#ifdef CONFIG_ATH6KL_AUTO_PM
		bam_pipe->ipa_params.activity_notify =
			ath6kl_usb_bam_activity_cb;
		bam_pipe->ipa_params.inactivity_notify =
			ath6kl_usb_bam_inactivity_cb;
#endif

		if((status = usb_bam_connect_ipa(&bam_pipe->ipa_params))) {
			ath6kl_err ("BAM-CM: Error while creating BAM "
					"Tx pipe num %d, status: %d\n",
					pipe_num, status);
			return status;
		}
		bam_pipe->connected = 1;
		ath6kl_dbg(ATH6KL_DBG_BAM2BAM,
			"BAM-CM: Successfully created bam pipe for TX %d : "
			"conn_idx: %d\n", pipe_num, conn_idx);
		break;

	case ATH6KL_USB_PIPE_RX_DATA2:
		bam_pipe->connected = 0;

		/* Configure Rx Pipes */

		conn_idx = usb_bam_get_connection_idx("hsic", IPA_P_BAM,
				USB_TO_PEER_PERIPHERAL, 0);

		if (conn_idx < 0) {
			ath6kl_err("BAM-CM: RX DATA2(src)failed getting"
					"index: %d src_idx: %d\n", 0,
					conn_idx);
			return conn_idx;
		}
		ath6kl_dbg(ATH6KL_DBG_BAM2BAM,
				"BAM-CM: RX DATA2 (src) success getting index: "
				"%d src_idx: %d\n", 0, conn_idx);
		bam_pipe->ipa_params.src_idx = conn_idx;
		bam_pipe->ipa_params.dst_idx = 0;

		bam_pipe->ipa_params.dir = USB_TO_PEER_PERIPHERAL;
		bam_pipe->ipa_params.src_client = bam_info[pipe_num].client;
		bam_pipe->ipa_params.src_pipe = &(bam_pipe->src_pipe);
		bam_pipe->ipa_params.dst_pipe = NULL;
		/* Fill the Tx pipe ep conf from IPA Config module */
		ath6kl_ipacm_get_ep_config_info(bam_pipe->ipa_params.src_client,
				&(bam_pipe->ipa_params.ipa_ep_cfg));
		bam_pipe->ipa_params.priv = (void *)pipe;
		bam_pipe->ipa_params.notify = ath6kl_ipa_data_callback;
#ifdef CONFIG_ATH6KL_AUTO_PM
		bam_pipe->ipa_params.activity_notify =
			ath6kl_usb_bam_activity_cb;
		bam_pipe->ipa_params.inactivity_notify =
			ath6kl_usb_bam_inactivity_cb;
#endif

		if((status = usb_bam_connect_ipa(&bam_pipe->ipa_params))) {
			ath6kl_err ("BAM-CM: Error while creating "
					"BAM Rx pipe num %d\n", pipe_num);
			return status;
		}

		bam_pipe->connected = 1;

		/* Add the Excep Flt Rule for the HSIC1_PROD-RX Pipe */
		/* Set the Rule for Exception packets to route to A5,
		 * since we cannot provide this as part of Rx properties
		 * */
		status = ath6kl_ipa_add_flt_rule(pipe->ar_usb->ar,
				bam_pipe->ipa_params.src_client);
		if (status) {
			ath6kl_dbg(ATH6KL_DBG_BAM2BAM,
					"BAM-CM: Failed to add filter rule for "
					"RX pipe DATA2(HSIC1_PROD) : status: "
					"%d\n", status);
			return status;
		}
		break;

	default:
		bam_pipe->connected = 0;
	}

	return 0;
}

/* Callback function used while intializing the BAM pipe,
 * This is required only to stop/resume the BAM pipe data,
 * not used now. Also this callback will be called while driver unloading */
void ath6kl_bam_pipe_callback(struct urb *urb)
{
	struct ath6kl_usb_pipe *pipe;

	pipe = (struct ath6kl_usb_pipe *) urb->context;

	ath6kl_dbg(ATH6KL_DBG_BAM2BAM, "BAM-CM: Callback recv, pipe_num: %d\n",
			pipe->logical_pipe_num);

}

static int ath6kl_usb_post_bam_transfers(struct ath6kl_usb_pipe *pipe)
{
#define INTERRUPT_RATE 	1
	struct urb *urb = NULL;
	struct usb_host_bam_type *bam_type = NULL;
	int status = 0, usb_status;
	int length = 0; /* This is just a dummy urb so length is 0 */

	urb = usb_alloc_urb(0, GFP_KERNEL);

	if (urb == NULL) {
		ath6kl_err ("BAM-CM: URB allocation failed for BAM pipe: %d\n",
				pipe->logical_pipe_num);
		status = -ENOMEM;
		goto err_cleanup;
	}

	usb_fill_bulk_urb(urb,
			pipe->ar_usb->udev,
			pipe->usb_pipe_handle,
			NULL,
			length,
			ath6kl_bam_pipe_callback,
			pipe);

	ath6kl_dbg(ATH6KL_DBG_BAM2BAM, "BAM-CM: bulk urb submit: %d, 0x%X "
			"(ep:0x%2.2X, %d bytes\n", pipe->logical_pipe_num,
			pipe->usb_pipe_handle, pipe->ep_address, length);

	urb->transfer_flags = URB_NO_TRANSFER_DMA_MAP;

	if (usb_pipein(pipe->usb_pipe_handle))
		urb->transfer_flags |= URB_SHORT_NOT_OK;

	bam_type = kzalloc(sizeof(struct usb_host_bam_type), GFP_KERNEL);

	if (bam_type == NULL) {
		ath6kl_err("BAM-CM: Failed to allocate memory for BAM type\n");
		status = -ENOMEM;
		goto err_cleanup;
	}

	bam_type->dir = pipe->bam_pipe.ipa_params.dir;

	if (bam_type->dir == USB_TO_PEER_PERIPHERAL)
		bam_type->pipe_num = *(pipe->bam_pipe.ipa_params.src_pipe);
	else
		bam_type->pipe_num = *(pipe->bam_pipe.ipa_params.dst_pipe);


	urb->priv_data = bam_type;

	pipe->bam_pipe.urb = urb;

	usb_status = usb_submit_urb(urb, GFP_KERNEL);

	if (usb_status) {
		ath6kl_err("BAM-CM: Failed to submit URB for BAM pipe: %d\n",
				pipe->logical_pipe_num);
		status = -EINVAL;
		goto err_cleanup;
	}

	return 0;

err_cleanup:
	if (bam_type)
		kfree(bam_type);
	if (urb)
		usb_free_urb(urb);

	return status;
}

/* Setup BAM pipe, returns 0 -On Success ,-ENOMEM - on Failure*/
int ath6kl_usb_setup_bam_pipe(struct ath6kl_usb_pipe *pipe)
{
	int retval;

	ath6kl_dbg(ATH6KL_DBG_BAM2BAM,
			"BAM-CM: usb create bam pipe number = %d\n",
					pipe->logical_pipe_num);
	retval = ath6kl_create_bam_pipe(pipe);

	if (retval != 0) {
		ath6kl_err("BAM-CM: Failed to create bam pipe pipe_num: %d\n",
				pipe->logical_pipe_num);
		return retval;
	}

	retval = ath6kl_usb_post_bam_transfers(pipe);

	return retval;
}

static int ath6kl_usb_bam_resubmit_urbs(struct ath6kl_usb *ar_usb)
{
	struct ath6kl_usb_pipe *pipe;
	int usb_status = 0;
	int i;
	int autopm_state;

	if (!ath6kl_debug_quirks(ar_usb->ar, ATH6KL_MODULE_BAM2BAM))
		return 0;

	for (i = 0; i < ATH6KL_USB_PIPE_MAX; i++) {
		pipe = &ar_usb->pipes[i];

		/* Nothing allocated for this pipe */
		if (pipe->ar_usb == NULL) {
			continue;
		}

		/* Check if it is BAM pipe */
		if (!ath6kl_is_bam_pipe(pipe)) {
			continue;
		}

		/* Submit the URB which is already initilized during probe */
		usb_status = usb_submit_urb(pipe->bam_pipe.urb, GFP_KERNEL);

		if (usb_status) {
			ath6kl_err("Failed to submit URB for BAM pipe: %d\n",
					pipe->logical_pipe_num);
		}

	}

#ifdef CONFIG_ATH6KL_AUTO_PM
	spin_lock_bh(&ar_usb->pm_lock);

	autopm_state = atomic_read(&ar_usb->autopm_state);

	if(autopm_state != ATH6KL_USB_AUTOPM_STATE_ON)
		usb_autopm_get_interface_async(ar_usb->interface);

	spin_unlock_bh(&ar_usb->pm_lock);
#endif

	return 0;
}

#endif

/* pipe/urb operations */
static struct ath6kl_urb_context *
ath6kl_usb_alloc_urb_from_pipe(struct ath6kl_usb_pipe *pipe)
{
	struct ath6kl_urb_context *urb_context = NULL;
	unsigned long flags;

	spin_lock_irqsave(&pipe->ar_usb->cs_lock, flags);
	if (!list_empty(&pipe->urb_list_head)) {
		urb_context =
			list_first_entry(&pipe->urb_list_head,
					struct ath6kl_urb_context, link);
		list_del(&urb_context->link);
		pipe->urb_cnt--;
	}
	spin_unlock_irqrestore(&pipe->ar_usb->cs_lock, flags);

	return urb_context;
}

static void ath6kl_usb_free_urb_to_pipe(struct ath6kl_usb_pipe *pipe,
		struct ath6kl_urb_context *urb_context)
{
	unsigned long flags;

	spin_lock_irqsave(&pipe->ar_usb->cs_lock, flags);
	pipe->urb_cnt++;

	list_add(&urb_context->link, &pipe->urb_list_head);
	spin_unlock_irqrestore(&pipe->ar_usb->cs_lock, flags);
}

static void ath6kl_usb_cleanup_urb_context(
		struct ath6kl_urb_context *urb_context)
{
	if (urb_context->skb != NULL) {
		dev_kfree_skb(urb_context->skb);
		urb_context->skb = NULL;
	}

	ath6kl_usb_free_urb_to_pipe(urb_context->pipe, urb_context);
}

static inline struct ath6kl_usb *ath6kl_usb_priv(struct ath6kl *ar)
{
	return ar->hif_priv;
}

/* pipe resource allocation/cleanup */
static int ath6kl_usb_alloc_pipe_resources(struct ath6kl_usb_pipe *pipe,
		int urb_cnt)
{
	struct ath6kl_urb_context *urb_context;
	int status = 0, i;

	INIT_LIST_HEAD(&pipe->urb_list_head);
	init_usb_anchor(&pipe->urb_submitted);

	for (i = 0; i < urb_cnt; i++) {
		urb_context = kzalloc(sizeof(struct ath6kl_urb_context),
				GFP_KERNEL);
		if (urb_context == NULL)
			/* FIXME: set status to -ENOMEM */
			break;

		urb_context->pipe = pipe;

		/*
		 * we are only allocate the urb contexts here, the actual URB
		 * is allocated from the kernel as needed to do a transaction
		 */
		pipe->urb_alloc++;
		ath6kl_usb_free_urb_to_pipe(pipe, urb_context);
	}

	ath6kl_dbg(ATH6KL_DBG_USB,
		"ath6kl usb: alloc resources lpipe:%d hpipe:0x%X urbs:%d\n",
		pipe->logical_pipe_num, pipe->usb_pipe_handle,
		pipe->urb_alloc);

	return status;
}

static void ath6kl_usb_free_pipe_resources(struct ath6kl_usb_pipe *pipe)
{
	struct ath6kl_urb_context *urb_context;

	if (pipe->ar_usb == NULL) {
		/* nothing allocated for this pipe */
		return;
	}

	ath6kl_dbg(ATH6KL_DBG_USB,
			"ath6kl usb: free resources lpipe:%d"
			"hpipe:0x%X urbs:%d avail:%d\n",
			pipe->logical_pipe_num, pipe->usb_pipe_handle,
			pipe->urb_alloc, pipe->urb_cnt);

	if (pipe->urb_alloc != pipe->urb_cnt) {
		ath6kl_err("ath6kl usb: urb leak! lpipe:%d "
				"hpipe:0x%X urbs:%d avail:%d\n",
				pipe->logical_pipe_num, pipe->usb_pipe_handle,
				pipe->urb_alloc, pipe->urb_cnt);
	}

	while (true) {
		urb_context = ath6kl_usb_alloc_urb_from_pipe(pipe);
		if (urb_context == NULL)
			break;
		kfree(urb_context);
	}

}

static void ath6kl_usb_cleanup_pipe_resources(struct ath6kl_usb *ar_usb)
{
	int i;

	for (i = 0; i < ATH6KL_USB_PIPE_MAX; i++)
		ath6kl_usb_free_pipe_resources(&ar_usb->pipes[i]);

}

static u8 ath6kl_usb_get_logical_pipe_num(struct ath6kl_usb *ar_usb,
		u8 ep_address, int *urb_count)
{
	u8 pipe_num = ATH6KL_USB_PIPE_INVALID;

	switch (ep_address) {
	case ATH6KL_USB_EP_ADDR_APP_CTRL_IN:
		pipe_num = ATH6KL_USB_PIPE_RX_CTRL;
		*urb_count = RX_URB_COUNT;
		break;
	case ATH6KL_USB_EP_ADDR_APP_DATA_IN:
		pipe_num = ATH6KL_USB_PIPE_RX_DATA;
		*urb_count = RX_URB_COUNT;
		break;
	case ATH6KL_USB_EP_ADDR_APP_INT_IN:
		pipe_num = ATH6KL_USB_PIPE_RX_INT;
		*urb_count = RX_URB_COUNT;
		break;
	case ATH6KL_USB_EP_ADDR_APP_DATA2_IN:
		pipe_num = ATH6KL_USB_PIPE_RX_DATA2;
		*urb_count = RX_URB_COUNT;
		break;
	case ATH6KL_USB_EP_ADDR_APP_CTRL_OUT:
		pipe_num = ATH6KL_USB_PIPE_TX_CTRL;
		*urb_count = TX_URB_COUNT;
		break;
	case ATH6KL_USB_EP_ADDR_APP_DATA_LP_OUT:
		pipe_num = ATH6KL_USB_PIPE_TX_DATA_LP;
		*urb_count = TX_URB_COUNT;
		break;
	case ATH6KL_USB_EP_ADDR_APP_DATA_MP_OUT:
		pipe_num = ATH6KL_USB_PIPE_TX_DATA_MP;
		*urb_count = TX_URB_COUNT;
		break;
	case ATH6KL_USB_EP_ADDR_APP_DATA_HP_OUT:
		pipe_num = ATH6KL_USB_PIPE_TX_DATA_HP;
		*urb_count = TX_URB_COUNT;
		break;
	case ATH6KL_USB_EP_ADDR_APP_DATA_VHP_OUT:
		pipe_num = ATH6KL_USB_PIPE_TX_DATA_VHP;
		*urb_count = TX_URB_COUNT;
		break;
	default:
		/* note: there may be endpoints not currently used */
		break;
	}

	return pipe_num;
}

#ifdef CONFIG_ATH6KL_BAM2BAM
static int ath6kl_usb_setup_bampipe_resources(struct ath6kl_usb *ar_usb)
{
	int i;
	int status = 0;
	struct ath6kl_usb_pipe *pipe;

	if (!(!!(debug_quirks & ATH6KL_MODULE_BAM2BAM))) {
		ath6kl_dbg(ATH6KL_DBG_BAM2BAM, "BAM2BAM mode is not"
				" enabled!\n");
		return 0;
	}

	for (i = 0; i < ATH6KL_USB_PIPE_MAX; i++) {
		pipe = &ar_usb->pipes[i];

		/* Nothing allocated for this pipe */
		if (pipe->ar_usb == NULL) {
			continue;
		}

		/* Check if we need to setup BAM pipe */
		if (!ath6kl_is_bam_pipe(pipe)) {
			continue;
		}

		status = ath6kl_usb_setup_bam_pipe(pipe);

		if (status) {
			ath6kl_err("BAM-CM: Failed to setup BAM pipe: %d,"
					" status: %d", pipe->logical_pipe_num,
					status);
			goto cleanup_bam_pipe;
		}
	}

	/* Crate the SYS BAM pipe for WLAN AMPDU Flushing */
	status = ath6kl_usb_create_sysbam_pipes(ar_usb->ar);
	if (status) {
		ath6kl_err("BAM-CM: Failed to create sysbam pipe: %d,",
				status);
		goto cleanup_bam_pipe;
	}

#ifdef CONFIG_ATH6KL_AUTO_PM
	/* Get operation done here for BAM pipes since an URB will be always
	 * submitted so making sure Suspend doesn't happen. Put operation will
	 * be done in inactivity handler when HSIC BAM driver calls the call
	 * back after the inactivity timeout.
	 */
	usb_autopm_get_interface_async(ar_usb->interface);
#endif


	return 0;

cleanup_bam_pipe:
	ath6kl_kill_bam_urbs(ar_usb);
	ath6kl_disconnect_bam_pipes(ar_usb);

	return status;
}
#endif

static int ath6kl_usb_setup_pipe_resources(struct ath6kl_usb *ar_usb)
{
	struct usb_interface *interface = ar_usb->interface;
	struct usb_host_interface *iface_desc = interface->cur_altsetting;
	struct usb_endpoint_descriptor *endpoint;
	struct ath6kl_usb_pipe *pipe;
	int i, urbcount, status = 0;
	u8 pipe_num;

	ath6kl_dbg(ATH6KL_DBG_USB, "setting up USB Pipes using interface\n");

	/* walk decriptors and setup pipes */
	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		endpoint = &iface_desc->endpoint[i].desc;

		if (ATH6KL_USB_IS_BULK_EP(endpoint->bmAttributes)) {
			ath6kl_dbg(ATH6KL_DBG_USB,
				"%s Bulk Ep:0x%2.2X maxpktsz:%d\n",
				ATH6KL_USB_IS_DIR_IN
				(endpoint->bEndpointAddress) ?
				"RX" : "TX", endpoint->bEndpointAddress,
				le16_to_cpu(endpoint->wMaxPacketSize));
		} else if (ATH6KL_USB_IS_INT_EP(endpoint->bmAttributes)) {
			ath6kl_dbg(ATH6KL_DBG_USB,
				"%s Int Ep:0x%2.2X maxpktsz:%d interval:%d\n",
				ATH6KL_USB_IS_DIR_IN
				(endpoint->bEndpointAddress) ?
				"RX" : "TX", endpoint->bEndpointAddress,
				le16_to_cpu(endpoint->wMaxPacketSize),
				endpoint->bInterval);
		} else if (ATH6KL_USB_IS_ISOC_EP(endpoint->bmAttributes)) {
			/* TODO for ISO */
			ath6kl_dbg(ATH6KL_DBG_USB,
				"%s ISOC Ep:0x%2.2X maxpktsz:%d interval:%d\n",
				ATH6KL_USB_IS_DIR_IN
				(endpoint->bEndpointAddress) ?
				"RX" : "TX", endpoint->bEndpointAddress,
				le16_to_cpu(endpoint->wMaxPacketSize),
				endpoint->bInterval);
		}
		urbcount = 0;

		pipe_num =
			ath6kl_usb_get_logical_pipe_num(ar_usb,
					endpoint->bEndpointAddress,
					&urbcount);
		if (pipe_num == ATH6KL_USB_PIPE_INVALID)
			continue;

		pipe = &ar_usb->pipes[pipe_num];
		if (pipe->ar_usb != NULL) {
			/* hmmm..pipe was already setup */
			continue;
		}

		pipe->ar_usb = ar_usb;
		pipe->logical_pipe_num = pipe_num;
		pipe->ep_address = endpoint->bEndpointAddress;
		pipe->max_packet_size = le16_to_cpu(endpoint->wMaxPacketSize);

		if (ATH6KL_USB_IS_BULK_EP(endpoint->bmAttributes)) {
			if (ATH6KL_USB_IS_DIR_IN(pipe->ep_address)) {
				pipe->usb_pipe_handle =
					usb_rcvbulkpipe(ar_usb->udev,
							pipe->ep_address);
			} else {
				pipe->usb_pipe_handle =
					usb_sndbulkpipe(ar_usb->udev,
							pipe->ep_address);
			}
		} else if (ATH6KL_USB_IS_INT_EP(endpoint->bmAttributes)) {
			if (ATH6KL_USB_IS_DIR_IN(pipe->ep_address)) {
				pipe->usb_pipe_handle =
					usb_rcvintpipe(ar_usb->udev,
							pipe->ep_address);
			} else {
				pipe->usb_pipe_handle =
					usb_sndintpipe(ar_usb->udev,
							pipe->ep_address);
			}
		} else if (ATH6KL_USB_IS_ISOC_EP(endpoint->bmAttributes)) {
			/* TODO for ISO */
			if (ATH6KL_USB_IS_DIR_IN(pipe->ep_address)) {
				pipe->usb_pipe_handle =
					usb_rcvisocpipe(ar_usb->udev,
							pipe->ep_address);
			} else {
				pipe->usb_pipe_handle =
					usb_sndisocpipe(ar_usb->udev,
							pipe->ep_address);
			}
		}

		pipe->ep_desc = endpoint;

		if (ATH6KL_USB_IS_DIR_IN(pipe->ep_address))
			pipe->flags |= ATH6KL_USB_PIPE_FLAG_RX;
		else
			pipe->flags |= ATH6KL_USB_PIPE_FLAG_TX;

		status = ath6kl_usb_alloc_pipe_resources(pipe, urbcount);
		if (status != 0)
			break;

	} /* for loop */

	return status;
}

/* pipe operations */
static void ath6kl_usb_post_recv_transfers(struct ath6kl_usb_pipe *recv_pipe,
		int buffer_length)
{
	struct ath6kl_urb_context *urb_context;
	struct urb *urb;
	int usb_status;

#ifdef CONFIG_ATH6KL_BAM2BAM
	if(ath6kl_is_bam_pipe(recv_pipe))
		return;
#endif

	while (true) {
		urb_context = ath6kl_usb_alloc_urb_from_pipe(recv_pipe);
		if (urb_context == NULL)
			break;

		urb_context->skb = dev_alloc_skb(buffer_length);
		if (urb_context->skb == NULL)
			goto err_cleanup_urb;

		urb = usb_alloc_urb(0, GFP_ATOMIC);
		if (urb == NULL)
			goto err_cleanup_urb;

		usb_fill_bulk_urb(urb,
				recv_pipe->ar_usb->udev,
				recv_pipe->usb_pipe_handle,
				urb_context->skb->data,
				buffer_length,
				ath6kl_usb_recv_complete, urb_context);

		ath6kl_dbg(ATH6KL_DBG_USB_BULK,
			"ath6kl usb: bulk recv submit:%d, 0x%X (ep:0x%2.2X), "
			"%d bytes buf:0x%p\n",
			recv_pipe->logical_pipe_num,
			recv_pipe->usb_pipe_handle, recv_pipe->ep_address,
			buffer_length, urb_context->skb);

		usb_anchor_urb(urb, &recv_pipe->urb_submitted);
		usb_status = usb_submit_urb(urb, GFP_ATOMIC);

		if (usb_status) {
			ath6kl_dbg(ATH6KL_DBG_USB_BULK,
				"ath6kl usb : usb bulk recv failed %d\n",
				usb_status);
			usb_unanchor_urb(urb);
			usb_free_urb(urb);
			goto err_cleanup_urb;
		}
		usb_free_urb(urb);
	}
	return;

err_cleanup_urb:
	ath6kl_usb_cleanup_urb_context(urb_context);
	return;
}

static void ath6kl_usb_flush_all(struct ath6kl_usb *ar_usb)
{
	int i;
	struct ath6kl_usb_pipe *pipe;

	for (i = 0; i < ATH6KL_USB_PIPE_MAX; i++) {
		pipe = &ar_usb->pipes[i].ar_usb->pipes[i];
		if (!pipe->ar_usb)
			continue;
		flush_work(&pipe->tx_io_complete_work);
		flush_work(&pipe->rx_io_complete_work);
		usb_kill_anchored_urbs(&pipe->urb_submitted);

#ifdef CONFIG_ATH6KL_BAM2BAM
		if (!pipe->bam_pipe.connected)
			continue;

		usb_kill_urb(pipe->bam_pipe.urb);
#endif

	}
#ifdef CONFIG_ATH6KL_AUTO_PM
	flush_work(&ar_usb->pm_resume_work);
#endif
}

static void ath6kl_usb_suspend_flush_all(struct ath6kl_usb *ar_usb)
{
	int i;
	struct ath6kl_usb_pipe *pipe;

	for (i = 0; i < ATH6KL_USB_PIPE_MAX; i++) {
		pipe = &ar_usb->pipes[i].ar_usb->pipes[i];
		if (!pipe->ar_usb)
			continue;
		flush_work(&pipe->tx_io_complete_work);
		flush_work(&pipe->rx_io_complete_work);
		usb_kill_anchored_urbs(&pipe->urb_submitted);

	}
#ifdef CONFIG_ATH6KL_AUTO_PM
	flush_work(&ar_usb->pm_resume_work);
#endif
}

static void ath6kl_usb_start_recv_pipes(struct ath6kl_usb *ar_usb)
{
	/*
	 * note: control pipe is no longer used
	 * ar_usb->pipes[ATH6KL_USB_PIPE_RX_CTRL].urb_cnt_thresh =
	 *      ar_usb->pipes[ATH6KL_USB_PIPE_RX_CTRL].urb_alloc/2;
	 * ath6kl_usb_post_recv_transfers(&ar_usb->
	 *		pipes[ATH6KL_USB_PIPE_RX_CTRL],
	 *		ATH6KL_USB_RX_BUFFER_SIZE);
	 */

	ar_usb->pipes[ATH6KL_USB_PIPE_RX_DATA].urb_cnt_thresh =
		ar_usb->pipes[ATH6KL_USB_PIPE_RX_DATA].urb_alloc / 2;
	ath6kl_usb_post_recv_transfers(&ar_usb->pipes[ATH6KL_USB_PIPE_RX_DATA],
			ATH6KL_USB_RX_BUFFER_SIZE);

	/* This path for non BAM2BAM path during compile time */
	ar_usb->pipes[ATH6KL_USB_PIPE_RX_DATA2].urb_cnt_thresh =
		ar_usb->pipes[ATH6KL_USB_PIPE_RX_DATA2].urb_alloc / 2;
	ath6kl_usb_post_recv_transfers(&ar_usb->pipes[ATH6KL_USB_PIPE_RX_DATA2],
			ATH6KL_USB_RX_BUFFER_SIZE);
}

/* hif usb rx/tx completion functions */
static void ath6kl_usb_recv_complete(struct urb *urb)
{
	struct ath6kl_urb_context *urb_context = urb->context;
	struct ath6kl_usb_pipe *pipe = urb_context->pipe;
	struct sk_buff *skb = NULL;
	int status = 0;
	struct ath6kl_usb_pipe_stats *pipe_stats = &pipe->stats;

	ath6kl_dbg(ATH6KL_DBG_USB_BULK,
		"%s: recv pipe: %d, stat:%d, len:%d urb:0x%p\n", __func__,
		pipe->logical_pipe_num, urb->status, urb->actual_length,
		urb);

	if (urb->status != 0) {
		status = -EIO;
		switch (urb->status) {
		case -ECONNRESET:
		case -ENOENT:
		case -ESHUTDOWN:
			/*
			 * no need to spew these errors when device
			 * removed or urb killed due to driver shutdown
			 */
			status = -ECANCELED;
			break;
		default:
			pipe_stats->num_rx_comp_err++;
			ath6kl_dbg(ATH6KL_DBG_USB_BULK,
				"%s recv pipe: %d (ep:0x%2.2X), failed:%d\n",
				__func__, pipe->logical_pipe_num,
				pipe->ep_address, urb->status);
			break;
		}
		goto cleanup_recv_urb;
	}

	if (urb->actual_length == 0)
		goto cleanup_recv_urb;

	skb = urb_context->skb;

	/* we are going to pass it up */
	urb_context->skb = NULL;
	skb_put(skb, urb->actual_length);

	usb_mark_last_busy(pipe->ar_usb->udev);

	/* note: queue implements a lock */
	skb_queue_tail(&pipe->rx_io_comp_queue, skb);
	pipe_stats->num_rx_comp++;
	queue_work(pipe->ar_usb->ar->ath6kl_wq_rx, &pipe->rx_io_complete_work);

cleanup_recv_urb:
	ath6kl_usb_cleanup_urb_context(urb_context);

	if (status == 0 || urb->status == -EPROTO) {
		/* No need to check the RxQ Thold for Event Pipe */
		if ((pipe->logical_pipe_num == ATH6KL_USB_PIPE_RX_DATA2) &&
			(pipe->urb_cnt >= pipe->urb_cnt_thresh)) {
			ath6kl_usb_post_recv_transfers(pipe,
					ATH6KL_USB_RX_BUFFER_SIZE);
			return;
		}

		if (pipe->urb_cnt >= pipe->urb_cnt_thresh &&
				skb_queue_len(&pipe->rx_io_comp_queue) <
				pipe->ar_usb->rxq_threshold) {
			/* our free urbs are piling up, post more transfers */
			ath6kl_usb_post_recv_transfers(pipe,
					ATH6KL_USB_RX_BUFFER_SIZE);
		}
	}
}

static void ath6kl_usb_usb_transmit_complete(struct urb *urb)
{
	struct ath6kl_urb_context *urb_context = urb->context;
	struct ath6kl_usb_pipe *pipe = urb_context->pipe;
	struct sk_buff *skb;
	struct ath6kl_usb_pipe_stats *pipe_stats = &pipe->stats;

	ath6kl_dbg(ATH6KL_DBG_USB_BULK,
			"%s: pipe: %d, stat:%d, len:%d\n",
			__func__, pipe->logical_pipe_num, urb->status,
			urb->actual_length);

	if (urb->status != 0) {
		pipe_stats->num_tx_comp_err++;
		ath6kl_dbg(ATH6KL_DBG_USB_BULK,
				"%s:  pipe: %d, failed:%d\n",
				__func__, pipe->logical_pipe_num, urb->status);
	}

#ifdef CONFIG_ATH6KL_AUTO_PM
	ath6kl_dbg(ATH6KL_DBG_SUSPEND, "TX complete: autopm: %d, pipe_id: %d\n",
			urb_context->autopm, pipe->logical_pipe_num);

	/* Asynchronously put to avoid blocking in interrupt context */
	if (urb_context->autopm)
		usb_autopm_put_interface_async(pipe->ar_usb->interface);
#endif

	skb = urb_context->skb;
	urb_context->skb = NULL;
	ath6kl_usb_free_urb_to_pipe(urb_context->pipe, urb_context);

	/* note: queue implements a lock */
	skb_queue_tail(&pipe->tx_io_comp_queue, skb);
	pipe_stats->num_tx_comp++;
	queue_work(pipe->ar_usb->ar->ath6kl_wq_tx, &pipe->tx_io_complete_work);
}

#ifdef CONFIG_ATH6KL_BAM2BAM
void ath6kl_usb_bam_transmit_complete(struct ath6kl_urb_context *urb_context)
{
	struct ath6kl_usb_pipe *pipe = urb_context->pipe;
	struct sk_buff *skb;

#ifdef CONFIG_ATH6KL_AUTO_PM
	ath6kl_dbg(ATH6KL_DBG_SUSPEND,
			"BAM TX complete: autopm: %d, pipe_id: %d\n",
			urb_context->autopm, pipe->logical_pipe_num);

	/* Asynchronously put to avoid blocking in interrupt context */
	if (urb_context->autopm)
		usb_autopm_put_interface_async(pipe->ar_usb->interface);
#endif

	skb = urb_context->skb;
	urb_context->skb = NULL;
	ath6kl_usb_free_urb_to_pipe(urb_context->pipe, urb_context);
	ath6kl_put_context(skb, NULL);

	/* note: queue implements a lock */
	skb_queue_tail(&pipe->tx_io_comp_queue, skb);
	pipe->stats.num_ipa_tx_comp++;
	queue_work(pipe->ar_usb->ar->ath6kl_wq_tx, &pipe->tx_io_complete_work);
}
#endif

static void ath6kl_usb_io_comp_work_tx(struct work_struct *work)
{
	struct ath6kl_usb_pipe *pipe = container_of(work,
			struct ath6kl_usb_pipe, tx_io_complete_work);
	struct ath6kl_usb *ar_usb;
	struct sk_buff *skb;
	struct ath6kl_usb_pipe_stats *pipe_stats = &pipe->stats;
	u32 tx = 0;

	ar_usb = pipe->ar_usb;

	pipe_stats->num_tx_wq_sched++;

	while ((skb = skb_dequeue(&pipe->tx_io_comp_queue))) {
		ath6kl_dbg(ATH6KL_DBG_USB_BULK,
				"ath6kl usb xmit callback buf:0x%p\n", skb);
		ath6kl_core_tx_complete(ar_usb->ar, skb);
		tx++;
	}

	pipe_stats->num_tx_io_comp += tx;
	if (tx > pipe_stats->num_max_tx)
		pipe_stats->num_max_tx = tx;
}

static void ath6kl_usb_io_comp_work_rx(struct work_struct *work)
{
	struct ath6kl_usb_pipe *pipe = container_of(work,
			struct ath6kl_usb_pipe, rx_io_complete_work);
	struct ath6kl_usb *ar_usb;
	struct sk_buff *skb;
	struct ath6kl_usb_pipe_stats *pipe_stats = &pipe->stats;
	u32 rx = 0;

	ar_usb = pipe->ar_usb;

	pipe_stats->num_rx_wq_sched++;

	while ((skb = skb_dequeue(&pipe->rx_io_comp_queue))) {
		ath6kl_dbg(ATH6KL_DBG_USB_BULK,
				"ath6kl usb recv callback buf:0x%p\n", skb);
		rx++;
		ath6kl_core_rx_complete(ar_usb->ar, skb,
				pipe->logical_pipe_num);
	}

	/* No need to check the RxQ Thold for Event Pipe */
	if ((pipe->logical_pipe_num == ATH6KL_USB_PIPE_RX_DATA2) &&
			(pipe->urb_cnt >= pipe->urb_cnt_thresh)) {
		ath6kl_usb_post_recv_transfers(pipe,
				ATH6KL_USB_RX_BUFFER_SIZE);
		return;
	}

	if (pipe->urb_cnt >= pipe->urb_cnt_thresh &&
			skb_queue_len(&pipe->rx_io_comp_queue) <
			pipe->ar_usb->rxq_threshold) {
		/* our free urbs are piling up, post more transfers */
		ath6kl_usb_post_recv_transfers(pipe, ATH6KL_USB_RX_BUFFER_SIZE);
	}

	pipe_stats->num_rx_io_comp += rx;
	if (rx > pipe_stats->num_max_rx)
		pipe_stats->num_max_rx = rx;
}

#define ATH6KL_USB_MAX_DIAG_CMD (sizeof(struct ath6kl_usb_ctrl_diag_cmd_write))
#define ATH6KL_USB_MAX_DIAG_RESP (sizeof(struct ath6kl_usb_ctrl_diag_resp_read))

static void ath6kl_usb_destroy(struct ath6kl_usb *ar_usb)
{

	ath6kl_usb_flush_all(ar_usb);

#ifdef CONFIG_ATH6KL_AUTO_PM
	spin_lock_bh(&ar_usb->pm_lock);
	while (!list_empty(&ar_usb->pm_q)) {
		struct ath6kl_urb_context *urb_context;

		urb_context = list_first_entry(&ar_usb->pm_q,
				struct ath6kl_urb_context, link);

		list_del(&urb_context->link);
		ath6kl_usb_cleanup_urb_context(urb_context);
	}
	spin_unlock_bh(&ar_usb->pm_lock);
#endif

	ath6kl_usb_cleanup_pipe_resources(ar_usb);

	usb_set_intfdata(ar_usb->interface, NULL);

	kfree(ar_usb->diag_cmd_buffer);
	kfree(ar_usb->diag_resp_buffer);

	kfree(ar_usb);
}

static struct ath6kl_usb *ath6kl_usb_create(struct usb_interface *interface)
{
	struct usb_device *dev = interface_to_usbdev(interface);
	struct ath6kl_usb *ar_usb;
	struct ath6kl_usb_pipe *pipe;
	int status = 0;
	int i;

	ar_usb = kzalloc(sizeof(struct ath6kl_usb), GFP_KERNEL);
	if (ar_usb == NULL)
		goto fail_ath6kl_usb_create;

	usb_set_intfdata(interface, ar_usb);
	spin_lock_init(&(ar_usb->cs_lock));
	ar_usb->udev = dev;
	ar_usb->interface = interface;

	for (i = 0; i < ATH6KL_USB_PIPE_MAX; i++) {
		pipe = &ar_usb->pipes[i];
		INIT_WORK(&pipe->tx_io_complete_work,
				ath6kl_usb_io_comp_work_tx);

		INIT_WORK(&pipe->rx_io_complete_work,
				ath6kl_usb_io_comp_work_rx);
		skb_queue_head_init(&pipe->tx_io_comp_queue);
		skb_queue_head_init(&pipe->rx_io_comp_queue);
	}

	ar_usb->diag_cmd_buffer = kzalloc(ATH6KL_USB_MAX_DIAG_CMD, GFP_KERNEL);
	if (ar_usb->diag_cmd_buffer == NULL) {
		status = -ENOMEM;
		goto fail_ath6kl_usb_create;
	}

	ar_usb->diag_resp_buffer = kzalloc(ATH6KL_USB_MAX_DIAG_RESP,
			GFP_KERNEL);
	if (ar_usb->diag_resp_buffer == NULL) {
		status = -ENOMEM;
		goto fail_ath6kl_usb_create;
	}

	ar_usb->rxq_threshold = HIF_USB_RX_QUEUE_THRESHOLD;

	status = ath6kl_usb_setup_pipe_resources(ar_usb);

fail_ath6kl_usb_create:
	if (status != 0) {
		ath6kl_usb_destroy(ar_usb);
		ar_usb = NULL;
	}
	return ar_usb;
}

static void ath6kl_usb_device_detached(struct usb_interface *interface)
{
	struct ath6kl_usb *ar_usb;

	ar_usb = usb_get_intfdata(interface);
	if (ar_usb == NULL)
		return;

	ath6kl_stop_txrx(ar_usb->ar);

	/* Delay to wait for target to reboot */
	mdelay(50);

#ifdef CONFIG_ATH6KL_BAM2BAM
	if (ath6kl_debug_quirks(ar_usb->ar, ATH6KL_MODULE_BAM2BAM)) {
		ath6kl_remove_ipa_exception_filters(ar_usb->ar);
		ath6kl_disconnect_sysbam_pipes(ar_usb->ar);
		ath6kl_disconnect_bam_pipes(ar_usb);
	}
#endif

	ath6kl_core_cleanup(ar_usb->ar);
	ath6kl_core_destroy(ar_usb->ar);

	ath6kl_usb_destroy(ar_usb);
}

/* exported hif usb APIs for htc pipe */
static void hif_start(struct ath6kl *ar)
{
	struct ath6kl_usb *device = ath6kl_usb_priv(ar);
	int i;

	ath6kl_usb_start_recv_pipes(device);

	/* set the TX resource avail threshold for each TX pipe */
	for (i = ATH6KL_USB_PIPE_TX_CTRL;
			i <= ATH6KL_USB_PIPE_TX_DATA_VHP; i++) {
		device->pipes[i].urb_cnt_thresh =
			device->pipes[i].urb_alloc / 2;
	}
}


static int ath6kl_usb_submit_urb(struct ath6kl *ar,
		struct ath6kl_urb_context *urb_context)
{
	struct urb *urb;
	struct ath6kl_usb *device = ath6kl_usb_priv(ar);
	struct ath6kl_usb_pipe *pipe = urb_context->pipe;
	struct sk_buff *skb  = urb_context->skb;
	u8 *data = skb->data;
	u32 len = skb->len;
	int usb_status, status = 0;
	struct ath6kl_usb_pipe_stats *pipe_stats = &pipe->stats;

#ifdef CONFIG_ATH6KL_BAM2BAM
	if (ath6kl_is_bam_pipe(pipe)) {
		ath6kl_put_context(skb, urb_context);
		/* send to IPA bam driver */
		status = ipa_tx_dp(pipe->bam_pipe.ipa_params.dst_client, skb,
				NULL);
		if (status) {
			pipe_stats->num_ipa_tx_err++;
			ath6kl_err("ath6kl usb : usb bam transmit failed %d\n",
					status);
		}

		pipe_stats->num_ipa_tx++;

		return status;
	}
#endif

	urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (urb == NULL) {
		status = -ENOMEM;
		pipe_stats->num_tx_err_others++;
		goto fail;
	}

	usb_fill_bulk_urb(urb,
			  device->udev,
			  urb_context->pipe->usb_pipe_handle,
			  data,
			  len,
			  ath6kl_usb_usb_transmit_complete, urb_context);

	if ((len % pipe->max_packet_size) == 0) {
		/* hit a max packet boundary on this pipe */
		urb->transfer_flags |= URB_ZERO_PACKET;
	}

	ath6kl_dbg(ATH6KL_DBG_USB_BULK,
		   "athusb bulk send submit:%d, 0x%X (ep:0x%2.2X), %d bytes\n",
		   pipe->logical_pipe_num, pipe->usb_pipe_handle,
		   pipe->ep_address, len);

	usb_anchor_urb(urb, &pipe->urb_submitted);
	usb_status = usb_submit_urb(urb, GFP_ATOMIC);

	if (usb_status) {
		ath6kl_dbg(ATH6KL_DBG_USB_BULK,
			   "ath6kl usb : usb bulk transmit failed %d\n",
			   usb_status);
		usb_unanchor_urb(urb);
		pipe_stats->num_tx_err++;
		status = -EINVAL;
	}
	usb_free_urb(urb);
	pipe_stats->num_tx++;

fail:
	return status;
}

#ifdef CONFIG_ATH6KL_AUTO_PM
static void ath6kl_auto_pm_wakeup_resume(struct work_struct *work)
{
	struct ath6kl_usb *ar_usb = container_of(work,
			struct ath6kl_usb, pm_resume_work);
	struct ath6kl_urb_context *urb_context;
	int status = 0;
	u32 tx_processed = 0;

	ath6kl_dbg(ATH6KL_DBG_SUSPEND,
			"Auto PM Resume, sumitting URBs, Queue len: %d\n",
			get_queue_depth(&ar_usb->pm_q));

	spin_lock_bh(&ar_usb->pm_lock);
	while (!list_empty(&ar_usb->pm_q)) {

		urb_context = list_first_entry(&ar_usb->pm_q,
				struct ath6kl_urb_context, link);

		list_del(&urb_context->link);
		spin_unlock_bh(&ar_usb->pm_lock);

		tx_processed++;

		status = ath6kl_usb_submit_urb(ar_usb->ar, urb_context);

		if (status) {
			ath6kl_usb_free_urb_to_pipe(urb_context->pipe,
					urb_context);
		}
		spin_lock_bh(&ar_usb->pm_lock);
	}

	spin_unlock_bh(&ar_usb->pm_lock);

	ar_usb->pm_stats.tx_processed += tx_processed;
	if (tx_processed > ar_usb->pm_stats.max_queue_len)
		ar_usb->pm_stats.max_queue_len = tx_processed;
}
#endif

static int ath6kl_usb_send(struct ath6kl *ar, u8 pipe_id,
			   struct sk_buff *hdr_skb, struct sk_buff *skb)
{
	struct ath6kl_usb *device = ath6kl_usb_priv(ar);
	struct ath6kl_usb_pipe *pipe = &device->pipes[pipe_id];
	struct ath6kl_urb_context *urb_context;
	int status = 0;
#ifdef CONFIG_ATH6KL_AUTO_PM
	int autopm_state;
#endif

	ath6kl_dbg(ATH6KL_DBG_USB_BULK, "+%s pipe : %d, buf:0x%p\n",
		   __func__, pipe_id, skb);

	urb_context = ath6kl_usb_alloc_urb_from_pipe(pipe);

	if (urb_context == NULL) {
		/*
		 * TODO: it is possible to run out of urbs if
		 * 2 endpoints map to the same pipe ID
		 */
		ath6kl_dbg(ATH6KL_DBG_USB_BULK,
			   "%s pipe:%d no urbs left. URB Cnt : %d\n",
			   __func__, pipe_id, pipe->urb_cnt);
		status = -ENOMEM;
		pipe->stats.num_tx_err_others++;
		goto fail_hif_send;
	}


	urb_context->skb = skb;

#ifdef CONFIG_ATH6KL_AUTO_PM
	urb_context->autopm = 0;
	autopm_state = atomic_read(&device->autopm_state);
	ath6kl_dbg(ATH6KL_DBG_SUSPEND,
			"USB send autopm_state: %d, pipe_id: %d\n",
			autopm_state, pipe_id);
	/* Dont do Get operation if Suspend/resume in progress and data is on
	 * control pipe, This is done to avoid Suspend/resume going in a loop
	 * because of Get operation when WMI commands are sent to firmware */
	if (!(autopm_state == ATH6KL_USB_AUTOPM_STATE_INPROGRESS &&
				pipe_id == ATH6KL_USB_PIPE_TX_CTRL)) {
		usb_autopm_get_interface_async(device->interface);
		urb_context->autopm = 1;

		/* Queue the packets into pm_q if port is suspended or in
		 * progress and there are already packets in pm_q */
		spin_lock_bh(&device->pm_lock);
		if (!list_empty(&device->pm_q) ||
				autopm_state != ATH6KL_USB_AUTOPM_STATE_ON) {
			list_add_tail(&urb_context->link, &device->pm_q);

			ath6kl_dbg(ATH6KL_DBG_SUSPEND,
					"USB send: Queue to PM queue, len %d",
					get_queue_depth(&device->pm_q));

			spin_unlock_bh(&device->pm_lock);
			device->pm_stats.tx_queued++;

			/* Make sure to schedule the work as resume may
			 * have completed by now and worker thread may have
			 * completed it's execution before even queuing
			 */
			if (atomic_read(&device->autopm_state) ==
					ATH6KL_USB_AUTOPM_STATE_ON) {
				schedule_work(&device->pm_resume_work);
			}

			return 0;
		}
		spin_unlock_bh(&device->pm_lock);
	}

#endif


	status = ath6kl_usb_submit_urb(ar, urb_context);

	if (status) {
#ifdef CONFIG_ATH6KL_AUTO_PM
		if (urb_context->autopm)
			usb_autopm_put_interface_async(device->interface);
#endif

		ath6kl_usb_free_urb_to_pipe(urb_context->pipe,
					    urb_context);
	}

fail_hif_send:
	return status;
}

static void hif_stop(struct ath6kl *ar)
{
	struct ath6kl_usb *device = ath6kl_usb_priv(ar);

	ath6kl_usb_flush_all(device);
}

static void ath6kl_usb_get_default_pipe(struct ath6kl *ar,
		u8 *ul_pipe, u8 *dl_pipe)
{
	*ul_pipe = ATH6KL_USB_PIPE_TX_CTRL;
	*dl_pipe = ATH6KL_USB_PIPE_RX_CTRL;
}

static int ath6kl_usb_map_service_pipe(struct ath6kl *ar, u16 svc_id,
		u8 *ul_pipe, u8 *dl_pipe)
{
	int status = 0;

	/* change this, while testing with all 4 Tx pipes */
#ifdef CONFIG_ATH6KL_BAM2BAM
	if (ath6kl_debug_quirks(ar, ATH6KL_MODULE_BAM2BAM))
	{
		switch (svc_id) {
		case HTC_CTRL_RSVD_SVC:
		case WMI_CONTROL_SVC:
			*ul_pipe = ATH6KL_USB_PIPE_TX_CTRL;
			/* due to large control packets, shift to data pipe */
			*dl_pipe = ATH6KL_USB_PIPE_RX_DATA2;
			break;
		case WMI_DATA_BE_SVC:
			/* BE and BK pipe hardware fifo are interchanged
			 * in HW, so need changes for BAM */
			*ul_pipe = ATH6KL_USB_PIPE_TX_DATA_LP;
			*dl_pipe = ATH6KL_USB_PIPE_RX_DATA;
			break;
		case WMI_DATA_BK_SVC:
			/* BE and BK pipe hardware fifo are interchanged
			 * in HW, so need changes for BAM */
			*ul_pipe = ATH6KL_USB_PIPE_TX_DATA_MP;
			*dl_pipe = ATH6KL_USB_PIPE_RX_DATA;
			break;
		case WMI_DATA_VI_SVC:
			*ul_pipe = ATH6KL_USB_PIPE_TX_DATA_HP;
			*dl_pipe = ATH6KL_USB_PIPE_RX_DATA;
			break;
		case WMI_DATA_VO_SVC:
			*ul_pipe = ATH6KL_USB_PIPE_TX_DATA_VHP;
			*dl_pipe = ATH6KL_USB_PIPE_RX_DATA;
			break;
		default:
			status = -EPERM;
			break;
		}
		return status;
	}
#endif
	/* This path for non BAM2BAM path during compile time */
	switch (svc_id) {
	case HTC_CTRL_RSVD_SVC:
	case WMI_CONTROL_SVC:
		*ul_pipe = ATH6KL_USB_PIPE_TX_CTRL;
		/* due to large control packets, shift to data pipe */
		*dl_pipe = ATH6KL_USB_PIPE_RX_DATA;
		break;
	case WMI_DATA_BE_SVC:
	case WMI_DATA_BK_SVC:
		*ul_pipe = ATH6KL_USB_PIPE_TX_DATA_LP;
		/*
		 * Disable rxdata2 directly, it will be enabled
		 * if FW enable rxdata2
		 */
		*dl_pipe = ATH6KL_USB_PIPE_RX_DATA;
		break;
	case WMI_DATA_VI_SVC:
		*ul_pipe = ATH6KL_USB_PIPE_TX_DATA_LP;
		/*
		 * Disable rxdata2 directly, it will be enabled
		 * if FW enable rxdata2
		 */
		*dl_pipe = ATH6KL_USB_PIPE_RX_DATA;
		break;
	case WMI_DATA_VO_SVC:
		*ul_pipe = ATH6KL_USB_PIPE_TX_DATA_LP;
		/*
		 * Disable rxdata2 directly, it will be enabled
		 * if FW enable rxdata2
		 */
		*dl_pipe = ATH6KL_USB_PIPE_RX_DATA;
		break;
	default:
		status = -EPERM;
		break;
	}

	return status;
}

static u16 ath6kl_usb_get_free_queue_number(struct ath6kl *ar, u8 pipe_id)
{
	struct ath6kl_usb *device = ath6kl_usb_priv(ar);

	return device->pipes[pipe_id].urb_cnt;
}

static void hif_detach_htc(struct ath6kl *ar)
{
	struct ath6kl_usb *device = ath6kl_usb_priv(ar);

	ath6kl_usb_flush_all(device);
}

static int ath6kl_usb_submit_ctrl_out(struct ath6kl_usb *ar_usb,
		u8 req, u16 value, u16 index, void *data,
		u32 size)
{
	u8 *buf = NULL;
	int ret;

	if (size > 0) {
		buf = kmalloc(size, GFP_KERNEL);
		if (buf == NULL)
			return -ENOMEM;

		memcpy(buf, data, size);
	}

	/* note: if successful returns number of bytes transfered */
	ret = usb_control_msg(ar_usb->udev,
			usb_sndctrlpipe(ar_usb->udev, 0),
			req,
			USB_DIR_OUT | USB_TYPE_VENDOR |
			USB_RECIP_DEVICE, value, index, buf,
			size, 1000);

	if (ret < 0) {
		ath6kl_dbg(ATH6KL_DBG_USB, "%s failed,result = %d\n",
				__func__, ret);
	}

	kfree(buf);

	return 0;
}

static int ath6kl_usb_submit_ctrl_in(struct ath6kl_usb *ar_usb,
		u8 req, u16 value, u16 index, void *data,
		u32 size)
{
	u8 *buf = NULL;
	int ret;

	if (size > 0) {
		buf = kmalloc(size, GFP_KERNEL);
		if (buf == NULL)
			return -ENOMEM;
	}

	/* note: if successful returns number of bytes transfered */
	ret = usb_control_msg(ar_usb->udev,
			usb_rcvctrlpipe(ar_usb->udev, 0),
			req,
			USB_DIR_IN | USB_TYPE_VENDOR |
			USB_RECIP_DEVICE, value, index, buf,
			size, 2 * HZ);

	if (ret < 0) {
		ath6kl_dbg(ATH6KL_DBG_USB, "%s failed,result = %d\n",
				__func__, ret);
	}

	memcpy((u8 *) data, buf, size);

	kfree(buf);

	return 0;
}

static int ath6kl_usb_ctrl_msg_exchange(struct ath6kl_usb *ar_usb,
		u8 req_val, u8 *req_buf, u32 req_len,
		u8 resp_val, u8 *resp_buf, u32 *resp_len)
{
	int ret;

	/* send command */
	ret = ath6kl_usb_submit_ctrl_out(ar_usb, req_val, 0, 0,
			req_buf, req_len);

	if (ret != 0)
		return ret;

	if (resp_buf == NULL) {
		/* no expected response */
		return ret;
	}

	/* get response */
	ret = ath6kl_usb_submit_ctrl_in(ar_usb, resp_val, 0, 0,
			resp_buf, *resp_len);

	return ret;
}

static int ath6kl_usb_diag_read32(struct ath6kl *ar, u32 address, u32 *data)
{
	struct ath6kl_usb *ar_usb = ar->hif_priv;
	struct ath6kl_usb_ctrl_diag_resp_read *resp;
	struct ath6kl_usb_ctrl_diag_cmd_read *cmd;
	u32 resp_len;
	int ret;

	cmd = (struct ath6kl_usb_ctrl_diag_cmd_read *) ar_usb->diag_cmd_buffer;

	memset(cmd, 0, sizeof(*cmd));
	cmd->cmd = ATH6KL_USB_CTRL_DIAG_CC_READ;
	cmd->address = cpu_to_le32(address);
	resp_len = sizeof(*resp);

	ret = ath6kl_usb_ctrl_msg_exchange(ar_usb,
			ATH6KL_USB_CONTROL_REQ_DIAG_CMD,
			(u8 *) cmd,
			sizeof(struct ath6kl_usb_ctrl_diag_cmd_write),
			ATH6KL_USB_CONTROL_REQ_DIAG_RESP,
			ar_usb->diag_resp_buffer, &resp_len);

	if (ret)
		return ret;

	resp = (struct ath6kl_usb_ctrl_diag_resp_read *)
		ar_usb->diag_resp_buffer;

	*data = le32_to_cpu(resp->value);

	return ret;
}

static int ath6kl_usb_diag_write32(struct ath6kl *ar, u32 address, __le32 data)
{
	struct ath6kl_usb *ar_usb = ar->hif_priv;
	struct ath6kl_usb_ctrl_diag_cmd_write *cmd;

	cmd = (struct ath6kl_usb_ctrl_diag_cmd_write *) ar_usb->diag_cmd_buffer;

	memset(cmd, 0, sizeof(struct ath6kl_usb_ctrl_diag_cmd_write));
	cmd->cmd = cpu_to_le32(ATH6KL_USB_CTRL_DIAG_CC_WRITE);
	cmd->address = cpu_to_le32(address);
	cmd->value = data;

	return ath6kl_usb_ctrl_msg_exchange(ar_usb,
			ATH6KL_USB_CONTROL_REQ_DIAG_CMD,
			(u8 *) cmd,
			sizeof(*cmd),
			0, NULL, NULL);

}

static int ath6kl_usb_bmi_read(struct ath6kl *ar, u8 *buf, u32 len)
{
	struct ath6kl_usb *ar_usb = ar->hif_priv;
	int ret;

	/* get response */
	ret = ath6kl_usb_submit_ctrl_in(ar_usb,
			ATH6KL_USB_CONTROL_REQ_RECV_BMI_RESP,
			0, 0, buf, len);
	if (ret != 0) {
		ath6kl_err("Unable to read the bmi data from the device: %d\n",
				ret);
		return ret;
	}

	return 0;
}

static int ath6kl_usb_bmi_write(struct ath6kl *ar, u8 *buf, u32 len)
{
	struct ath6kl_usb *ar_usb = ar->hif_priv;
	int ret;

	/* send command */
	ret = ath6kl_usb_submit_ctrl_out(ar_usb,
			ATH6KL_USB_CONTROL_REQ_SEND_BMI_CMD,
			0, 0, buf, len);
	if (ret != 0) {
		ath6kl_err("unable to send the bmi data to the device: %d\n",
				ret);
		return ret;
	}

	return 0;
}

static int ath6kl_usb_power_on(struct ath6kl *ar)
{
	hif_start(ar);
	return 0;
}

static int ath6kl_usb_power_off(struct ath6kl *ar)
{
	hif_detach_htc(ar);
	return 0;
}

static void ath6kl_usb_stop(struct ath6kl *ar)
{
	hif_stop(ar);
}

static void ath6kl_usb_cleanup_scatter(struct ath6kl *ar)
{
	/*
	 * USB doesn't support it. Just return.
	 */
	return;
}

static int ath6kl_usb_set_rxq_threshold(struct ath6kl *ar, u32 rxq_threshold)
{
	struct ath6kl_usb *ar_usb = ath6kl_usb_priv(ar);

	ar_usb->rxq_threshold = rxq_threshold;

	ath6kl_dbg(ATH6KL_DBG_USB, "rxq_threshold = %d\n", ar_usb->rxq_threshold);

	return 0;
}

static int ath6kl_usb_suspend(struct ath6kl *ar, struct cfg80211_wowlan *wow)
{
	/* Nothing to be done for now */
	return 0;
}

static int ath6kl_usb_resume(struct ath6kl *ar)
{
	/* Nothing to be done for now */
	return 0;
}

/* FIXME: It would be good to have it in debug.c but all the HIF data structures
 * are not exposed through header file to access in debug.c
 */
static int ath6kl_usb_get_stats(struct ath6kl *ar, u8 *buf, int buf_len,
		u32 stats_mask)
{
	int len = 0;
	struct ath6kl_usb *ar_usb = ath6kl_usb_priv(ar);
	struct ath6kl_usb_pipe *pipe;
	int i;
#ifdef CONFIG_ATH6KL_AUTO_PM
	char *autopm_state[] = {"ON", "INPROGRESS", "SUSPENDED"};
#endif

#define USB_PIPESTAT(_pipe, _buf, _len, _name) \
	snprintf(_buf, _len, "%10d : %s\n", _pipe->stats._name, #_name)

	/* Skip if pipe stats are not requested */
	if (!(stats_mask & ~BIT(31)))
		goto skip_pipe_stats;

	len += snprintf(buf + len, buf_len - len,
			"\n<--------------- USB PIPE STATS --------------->\n");
	for (i = 0; i < ATH6KL_USB_PIPE_MAX; i++) {
		pipe = &ar_usb->pipes[i];

		if (pipe->ar_usb == NULL)
			continue;

		/* Dump only if requested */
		if (!(stats_mask & BIT(pipe->logical_pipe_num)))
			continue;

		len += snprintf(buf + len, buf_len - len, "\npipe: %d, "
				"ep: 0x%x, urb_alloc: %d, urb_cnt: %d, "
				"tx_q_len: %d, rx_q_len: %d\n",
				pipe->logical_pipe_num, pipe->ep_address,
				pipe->urb_alloc, pipe->urb_cnt,
				skb_queue_len(&pipe->tx_io_comp_queue),
				skb_queue_len(&pipe->rx_io_comp_queue));

		len += USB_PIPESTAT(pipe, buf + len, buf_len - len,
				num_tx);
		len += USB_PIPESTAT(pipe, buf + len, buf_len - len,
				num_tx_err);
		len += USB_PIPESTAT(pipe, buf + len, buf_len - len,
				num_tx_err_others);
		len += USB_PIPESTAT(pipe, buf + len, buf_len - len,
				num_tx_comp);
		len += USB_PIPESTAT(pipe, buf + len, buf_len - len,
				num_tx_comp_err);
		len += USB_PIPESTAT(pipe, buf + len, buf_len - len,
				num_tx_io_comp);
		len += USB_PIPESTAT(pipe, buf + len, buf_len - len,
				num_tx_wq_sched);
		len += USB_PIPESTAT(pipe, buf + len, buf_len - len,
				num_max_tx);
		len += USB_PIPESTAT(pipe, buf + len, buf_len - len,
				num_rx_comp);
		len += USB_PIPESTAT(pipe, buf + len, buf_len - len,
				num_rx_comp_err);
		len += USB_PIPESTAT(pipe, buf + len, buf_len - len,
				num_rx_io_comp);
		len += USB_PIPESTAT(pipe, buf + len, buf_len - len,
				num_rx_wq_sched);
		len += USB_PIPESTAT(pipe, buf + len, buf_len - len,
				num_max_rx);

#ifdef CONFIG_ATH6KL_BAM2BAM
		len += USB_PIPESTAT(pipe, buf + len, buf_len - len,
				num_ipa_tx);
		len += USB_PIPESTAT(pipe, buf + len, buf_len - len,
				num_ipa_tx_err);
		len += USB_PIPESTAT(pipe, buf + len, buf_len - len,
				num_ipa_tx_comp);
		len += USB_PIPESTAT(pipe, buf + len, buf_len - len,
				num_ipa_rx);
#endif /* CONFIG_ATH6KL_BAM2BAM */

	}

skip_pipe_stats:

#ifdef CONFIG_ATH6KL_AUTO_PM
	/* Check if AutoPM stats is requested */
	if (!(stats_mask & BIT(31)))
		return len;

	len += snprintf(buf + len, buf_len - len,
			"\n<--------------- AUTO PM STATS --------------->\n");

	len += snprintf(buf +len, buf_len - len, "%10s : Auto PM state\n",
			autopm_state[atomic_read(&ar_usb->autopm_state)]);

	len += snprintf(buf +len, buf_len - len, "%10d : PM Usage count\n",
			atomic_read(&ar_usb->interface->pm_usage_cnt));

#define USB_PMSTAT(_ar_usb, _buf, _len, _name) \
	snprintf(_buf, _len, "%10d : %s\n", _ar_usb->pm_stats._name, #_name)
	len += USB_PMSTAT(ar_usb, buf + len, buf_len - len, suspended);
	len += USB_PMSTAT(ar_usb, buf + len, buf_len - len, suspend_err);
	len += USB_PMSTAT(ar_usb, buf + len, buf_len - len, resumed);
	len += USB_PMSTAT(ar_usb, buf + len, buf_len - len, reset_resume);
	len += USB_PMSTAT(ar_usb, buf + len, buf_len - len, tx_queued);
	len += USB_PMSTAT(ar_usb, buf + len, buf_len - len, tx_processed);
	len += USB_PMSTAT(ar_usb, buf + len, buf_len - len, max_queue_len);
	len += USB_PMSTAT(ar_usb, buf + len, buf_len - len, bam_activity);
	len += USB_PMSTAT(ar_usb, buf + len, buf_len - len, bam_inactivity);
	len += USB_PMSTAT(ar_usb, buf + len, buf_len - len, disable);
	len += USB_PMSTAT(ar_usb, buf + len, buf_len - len, enable);
#undef USB_PMSTAT
#endif /* CONFIG_ATH6KL_AUTO_PM */
#undef USB_PIPESTAT

	return len;
}

static int ath6kl_usb_clear_stats(struct ath6kl *ar)
{
	struct ath6kl_usb *ar_usb = ath6kl_usb_priv(ar);
	struct ath6kl_usb_pipe *pipe;
	int i;

	for (i = 0; i < ATH6KL_USB_PIPE_MAX; i++) {
		pipe = &ar_usb->pipes[i];

		if (pipe->ar_usb == NULL)
			continue;

		memset(&pipe->stats, 0, sizeof(pipe->stats));
	}

#ifdef CONFIG_ATH6KL_AUTO_PM
	memset(&ar_usb->pm_stats, 0, sizeof(ar_usb->pm_stats));
#endif

	return 0;
}

#ifdef CONFIG_ATH6KL_AUTO_PM
static int ath6kl_usb_disable_autopm(struct ath6kl *ar)
{
	struct ath6kl_usb *ar_usb = ath6kl_usb_priv(ar);

	usb_autopm_get_interface_async(ar_usb->interface);

	ar_usb->pm_stats.disable++;

	ath6kl_dbg(ATH6KL_DBG_SUSPEND, "%s: count: %d\n", __func__,
			ar_usb->pm_stats.disable);

	return 0;
}

static int ath6kl_usb_enable_autopm(struct ath6kl *ar)
{
	struct ath6kl_usb *ar_usb = ath6kl_usb_priv(ar);

	usb_autopm_put_interface_async(ar_usb->interface);

	ar_usb->pm_stats.enable++;

	ath6kl_dbg(ATH6KL_DBG_SUSPEND, "%s: count: %d\n", __func__,
			ar_usb->pm_stats.enable);

	return 0;
}
#endif /* CONFIG_ATH6KL_AUTO_PM */

static const struct ath6kl_hif_ops ath6kl_usb_ops = {
	.diag_read32 = ath6kl_usb_diag_read32,
	.diag_write32 = ath6kl_usb_diag_write32,
	.bmi_read = ath6kl_usb_bmi_read,
	.bmi_write = ath6kl_usb_bmi_write,
	.power_on = ath6kl_usb_power_on,
	.power_off = ath6kl_usb_power_off,
	.stop = ath6kl_usb_stop,
	.pipe_send = ath6kl_usb_send,
	.pipe_get_default = ath6kl_usb_get_default_pipe,
	.pipe_map_service = ath6kl_usb_map_service_pipe,
	.pipe_get_free_queue_number = ath6kl_usb_get_free_queue_number,
	.cleanup_scatter = ath6kl_usb_cleanup_scatter,
	.pipe_set_rxq_threshold = ath6kl_usb_set_rxq_threshold,
	.suspend = ath6kl_usb_suspend,
	.resume = ath6kl_usb_resume,
	.get_stats = ath6kl_usb_get_stats,
	.clear_stats = ath6kl_usb_clear_stats,
#ifdef CONFIG_ATH6KL_AUTO_PM
	.disable_autopm  = ath6kl_usb_disable_autopm,
	.enable_autopm  = ath6kl_usb_enable_autopm,
#endif
	.restart = ath6kl_hsic_restart,
};

/* ath6kl usb driver registered functions */
static int ath6kl_usb_probe(struct usb_interface *interface,
		const struct usb_device_id *id)
{
	struct usb_device *dev = interface_to_usbdev(interface);
	struct ath6kl *ar;
	struct ath6kl_usb *ar_usb = NULL;
	int vendor_id, product_id;
	int ret = 0;

	usb_get_dev(dev);

	vendor_id = le16_to_cpu(dev->descriptor.idVendor);
	product_id = le16_to_cpu(dev->descriptor.idProduct);

	ath6kl_dbg(ATH6KL_DBG_USB, "vendor_id = %04x\n", vendor_id);
	ath6kl_dbg(ATH6KL_DBG_USB, "product_id = %04x\n", product_id);

	if (interface->cur_altsetting)
		ath6kl_dbg(ATH6KL_DBG_USB, "USB Interface %d\n",
				interface->cur_altsetting->desc.bInterfaceNumber);


	if (dev->speed == USB_SPEED_HIGH)
		ath6kl_dbg(ATH6KL_DBG_USB, "USB 2.0 Host\n");
	else
		ath6kl_dbg(ATH6KL_DBG_USB, "USB 1.1 Host\n");

	ar_usb = ath6kl_usb_create(interface);

	if (ar_usb == NULL) {
		ret = -ENOMEM;
		goto err_usb_put;
	}
	mdelay(500);
#ifdef CONFIG_ATH6KL_BAM2BAM
	ath6kl_usb_bam_set_pipe_mask(ar_usb);
#endif

#ifdef CONFIG_ATH6KL_BAM2BAM
	ret = ath6kl_usb_setup_bampipe_resources(ar_usb);

	if (ret) {
		ath6kl_err("Failed to init ath6kl bampipe: %d\n", ret);
		ath6kl_remove_ipa_exception_filters(ar_usb->ar);
		ath6kl_disconnect_sysbam_pipes(ar_usb->ar);
		goto err_usb_destroy;
	}

#endif

#ifdef CONFIG_ATH6KL_AUTO_PM
	spin_lock_init(&ar_usb->pm_lock);
	INIT_LIST_HEAD(&ar_usb->pm_q);
	interface->needs_remote_wakeup = 1;
	atomic_set(&ar_usb->autopm_state, ATH6KL_USB_AUTOPM_STATE_ON);
	INIT_WORK(&ar_usb->pm_resume_work, ath6kl_auto_pm_wakeup_resume);
#endif

	ar = ath6kl_core_create(&ar_usb->udev->dev);
	if (ar == NULL) {
		ath6kl_err("Failed to alloc ath6kl core\n");
		ret = -ENOMEM;
		goto err_usb_destroy;
	}

	ar->hif_priv = ar_usb;
	ar->hif_type = ATH6KL_HIF_TYPE_USB;
	ar->hif_ops = &ath6kl_usb_ops;
	ar->mbox_info.block_size = 16;
	ar->bmi.max_data_size = 252;

	ar_usb->ar = ar;

	ret = ath6kl_core_init(ar, ATH6KL_HTC_TYPE_PIPE);
	if (ret) {
		ath6kl_err("Failed to init ath6kl core: %d\n", ret);
		goto err_core_free;
	}

#ifdef CONFIG_ATH6KL_AUTO_PM
	/* Enable Autsuspend (Delay 2sec)
	   Note, Autosuspend is enabled only after ath6kl_core_create is done
	   so that all the initialization completes */
	if (ath6kl_debug_quirks(ar_usb->ar, ATH6KL_MODULE_ENABLE_USB_AUTO_PM)) {
		device_init_wakeup(&interface->dev, 1);
		pm_runtime_set_autosuspend_delay(&dev->dev, 2000);
		usb_enable_autosuspend(dev);
	}
#endif

	return ret;

err_core_free:
#ifdef CONFIG_ATH6KL_BAM2BAM
	if ((!!(debug_quirks & ATH6KL_MODULE_BAM2BAM))) {
		ath6kl_remove_ipa_exception_filters(ar_usb->ar);
		ath6kl_disconnect_sysbam_pipes(ar_usb->ar);
	}
#endif
	ath6kl_core_destroy(ar);
err_usb_destroy:
	ath6kl_usb_destroy(ar_usb);
err_usb_put:
	usb_put_dev(dev);

	return ret;
}

static void ath6kl_usb_remove(struct usb_interface *interface)
{
	usb_put_dev(interface_to_usbdev(interface));
	ath6kl_usb_device_detached(interface);
}

#ifdef CONFIG_PM

static int ath6kl_usb_pm_suspend(struct usb_interface *interface,
			      pm_message_t message)
{
	struct ath6kl_usb *ar_usb;
	struct ath6kl *ar;
	bool try_deepsleep = false;
	int ret = 0;

	ath6kl_dbg(ATH6KL_DBG_SUSPEND, "USB PM Suspend\n");

	ar_usb = usb_get_intfdata(interface);
	if(ar_usb == NULL) {
		return -ENODEV;
	}

	ar = ar_usb->ar;

#ifdef CONFIG_ATH6KL_AUTO_PM
	atomic_set(&ar_usb->autopm_state, ATH6KL_USB_AUTOPM_STATE_INPROGRESS);
#endif

	if (ar->state == ATH6KL_STATE_SCHED_SCAN) {
		ath6kl_dbg(ATH6KL_DBG_SUSPEND, "sched scan is in progress\n");

		ret =  ath6kl_cfg80211_suspend(ar,
					       ATH6KL_CFG_SUSPEND_SCHED_SCAN,
					       NULL);
		goto end;

	}

	if (ar->suspend_mode == WLAN_POWER_STATE_WOW) {

		ret = ath6kl_cfg80211_suspend(ar,
				ATH6KL_CFG_SUSPEND_WOW,
				NULL);

		/* If the error is not ENOTCONN then return error to HCD */
		if (ret && ret != -ENOTCONN) {
			ath6kl_err("wow suspend failed: %d\n", ret);
			goto end;
		}

		if (ret && (!ar->wow_suspend_mode || ar->wow_suspend_mode ==
					WLAN_POWER_STATE_DEEP_SLEEP))
			try_deepsleep = true;
	}

	if (ar->suspend_mode == WLAN_POWER_STATE_DEEP_SLEEP ||
			!ar->suspend_mode || try_deepsleep) {

		ret = ath6kl_cfg80211_suspend(ar,
				ATH6KL_CFG_SUSPEND_DEEPSLEEP,
				NULL);
		goto end;
	}

end:
	if(ret) {
#ifdef CONFIG_ATH6KL_AUTO_PM
		atomic_set(&ar_usb->autopm_state, ATH6KL_USB_AUTOPM_STATE_ON);
		ar_usb->pm_stats.suspend_err++;
#endif
		ath6kl_err("Failed to suspend, returning error: %d\n", ret);
		return ret;
	}

	ath6kl_usb_suspend_flush_all(ar_usb);
#ifdef CONFIG_ATH6KL_AUTO_PM
	atomic_set(&ar_usb->autopm_state,
			ATH6KL_USB_AUTOPM_STATE_SUSPENDED);
	ar_usb->pm_stats.suspended++;
#endif

	return 0;
}

static int ath6kl_usb_pm_resume(struct usb_interface *interface)
{
	struct ath6kl_usb *ar_usb = usb_get_intfdata(interface);
	struct ath6kl *ar = ar_usb->ar;

	ath6kl_dbg(ATH6KL_DBG_SUSPEND, "USB PM Resume\n");

	ath6kl_usb_post_recv_transfers(&ar_usb->pipes[ATH6KL_USB_PIPE_RX_DATA],
				       ATH6KL_USB_RX_BUFFER_SIZE);
	ath6kl_usb_post_recv_transfers(&ar_usb->pipes[ATH6KL_USB_PIPE_RX_DATA2],
				       ATH6KL_USB_RX_BUFFER_SIZE);

#ifdef CONFIG_ATH6KL_BAM2BAM
	ath6kl_usb_bam_resubmit_urbs(ar_usb);
#endif

#ifdef CONFIG_ATH6KL_AUTO_PM
	atomic_set(&ar_usb->autopm_state, ATH6KL_USB_AUTOPM_STATE_INPROGRESS);
#endif
	ath6kl_cfg80211_resume(ar);
#ifdef CONFIG_ATH6KL_AUTO_PM
	atomic_set(&ar_usb->autopm_state, ATH6KL_USB_AUTOPM_STATE_ON);
	schedule_work(&ar_usb->pm_resume_work);
	ar_usb->pm_stats.resumed++;
#endif

	return 0;
}

static int ath6kl_usb_pm_reset_resume(struct usb_interface *intf)
{
	struct ath6kl_usb *ar_usb = (struct ath6kl_usb *)usb_get_intfdata(intf);

	ath6kl_dbg(ATH6KL_DBG_SUSPEND,
			"USB PM Reset Resume, Use normal resume path: %p!\n",
			ar_usb);

#ifdef CONFIG_ATH6KL_AUTO_PM
	ar_usb->pm_stats.reset_resume++;
#endif

	return ath6kl_usb_pm_resume(intf);
}

#endif

/* table of devices that work with this driver */
static struct usb_device_id ath6kl_usb_ids[] = {
	{USB_DEVICE(0x0cf3, 0x9374)},
	{ /* Terminating entry */ },
};

MODULE_DEVICE_TABLE(usb, ath6kl_usb_ids);

static struct usb_driver ath6kl_usb_driver = {
	.name = "ath6kl_usb",
	.probe = ath6kl_usb_probe,
#ifdef CONFIG_PM
	.suspend = ath6kl_usb_pm_suspend,
	.resume = ath6kl_usb_pm_resume,
	.reset_resume = ath6kl_usb_pm_reset_resume,
#endif
	.disconnect = ath6kl_usb_remove,
	.id_table = ath6kl_usb_ids,
	.supports_autosuspend = true,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,5,0))
	.disable_hub_initiated_lpm = 1,
#endif
};

static int ath6kl_usb_init(void)
{
	usb_register(&ath6kl_usb_driver);
	ath6kl_platform_driver_register();
	return 0;
}

static void ath6kl_usb_exit(void)
{
	usb_deregister(&ath6kl_usb_driver);
	ath6kl_platform_driver_unregister();
}

module_init(ath6kl_usb_init);
module_exit(ath6kl_usb_exit);

MODULE_AUTHOR("Atheros Communications, Inc.");
MODULE_DESCRIPTION("Driver support for Atheros AR600x USB devices");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_FIRMWARE(AR6004_HW_1_0_FIRMWARE_FILE);
MODULE_FIRMWARE(AR6004_HW_1_0_BOARD_DATA_FILE);
MODULE_FIRMWARE(AR6004_HW_1_0_DEFAULT_BOARD_DATA_FILE);
MODULE_FIRMWARE(AR6004_HW_1_1_FIRMWARE_FILE);
MODULE_FIRMWARE(AR6004_HW_1_1_BOARD_DATA_FILE);
MODULE_FIRMWARE(AR6004_HW_1_1_DEFAULT_BOARD_DATA_FILE);
MODULE_FIRMWARE(AR6004_HW_1_2_FIRMWARE_FILE);
MODULE_FIRMWARE(AR6004_HW_1_2_BOARD_DATA_FILE);
MODULE_FIRMWARE(AR6004_HW_1_2_DEFAULT_BOARD_DATA_FILE);
MODULE_FIRMWARE(AR6004_HW_1_3_FW_DIR "/" AR6004_HW_1_3_FIRMWARE_FILE);
MODULE_FIRMWARE(AR6004_HW_1_3_BOARD_DATA_FILE);
MODULE_FIRMWARE(AR6004_HW_1_3_DEFAULT_BOARD_DATA_FILE);
