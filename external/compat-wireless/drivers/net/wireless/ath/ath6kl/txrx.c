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

#undef pr_fmt
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/printk.h>

#include "core.h"
#include "debug.h"
#include "htc-ops.h"
#include "epping.h"

/*
 * tid - tid_mux0..tid_mux3
 * aid - tid_mux4..tid_mux7
 */
#define ATH6KL_TID_MASK 0xf
#define ATH6KL_AID_SHIFT 4

extern unsigned int debug_quirks;

static void ath6kl_mcc_event_ctrl_timer_handler(unsigned long ptr);

 /* Dont define if IPA conf. Manager is not present */

#ifdef CONFIG_ATH6KL_BAM2BAM
#define ATH6KL_IPA_HOLB_TIMER_VAL 0x7f

#define ATH6KL_BAM2BAM_IPV4_NAME_EXT "-ipv4"
#define ATH6KL_BAM2BAM_IPV6_NAME_EXT "-ipv6"

u32 un_ordered = 0;
u32 ordered = 0;
u32 flt_hdl_ipv6=0;
u32 flt_hdl_ipv4=0;

extern int ath6kl_usb_data_send_to_bam_pipe(int pipe_no, struct sk_buff *skb);

/* Strurue decl for SYSBAM pipes */
static struct usb_sysbam_pipe {
	u32 clnt_hdl;
	struct ipa_sys_connect_params ipa_params;
} sysbam_pipe[MAX_SYSBAM_PIPE];

/* SYS BAM Pipe, no connection index for this, this pipe is created between
   the wlan driver and ipa driver for sending the AMPDU re-ordered packets to
   IPA driver.
 */
static struct sysbam_inf {
	u8	idx; /* not used in sysbam pipe */
	enum 	ipa_client_type client;
}sysbam_info[MAX_SYSBAM_PIPE] = {
	{0, IPA_CLIENT_A5_WLAN_AMPDU_PROD}
};

/* Enable / Disable SW_ROUTING based on MCC flag */
int ath6kl_ipa_enable_host_route_config (struct ath6kl_vif *vif, bool enable)
{
        /*
	 * Invoke appropriate IPA-API to enable or disable sw routing between
	 * IPA System BAM to Host
	 */
	int status = 0;
	struct ath6kl *ar = vif->ar;
	struct ath6kl_vif *vif1;

	spin_lock_bh(&ar->list_lock);
	list_for_each_entry(vif1, &ar->vif_list, list) {
		spin_unlock_bh(&ar->list_lock);
		if (enable == true) {
			/* Enable software routing in IPA */
			ath6kl_dbg(ATH6KL_DBG_IPA_MSG, "MCC: IPA_EN SW ROUTING\n");
			status = ath6kl_send_msg_ipa(vif1, SW_ROUTING_ENABLE,
					vif1->ndev->dev_addr);
			if (status)
				ath6kl_err("Failed to send MCC enable msg to"
						" IPA, status: %d\n", status);
		} else {
			/* Disable software routing in IPA */
			ath6kl_dbg(ATH6KL_DBG_IPA_MSG, "MCC: IPA_DIS SW ROUTING\n");
			status = ath6kl_send_msg_ipa(vif1, SW_ROUTING_DISABLE,
					vif1->ndev->dev_addr);
			if (status)
				ath6kl_err("Failed to send MCC disable msg to"
						" IPA, status: %d\n", status);
		}
		spin_lock_bh(&ar->list_lock);
	}
	spin_unlock_bh(&ar->list_lock);

	return status;
}
/* Add the filter rule, after creating the BAM pipe, it is called by the
   bamcm file , whilte creating the bam pipe*/
int ath6kl_ipa_add_flt_rule(struct ath6kl *ar, enum ipa_client_type client)
{
	struct ipa_ioc_get_rt_tbl rt_lookup;
	struct ipa_ioc_add_flt_rule *flt = NULL;
	int ret = 0;

	if (client != IPA_CLIENT_HSIC1_PROD)
		return -EINVAL;

	flt_hdl_ipv4 = 0;
	flt_hdl_ipv6 = 0;

	ath6kl_dbg(ATH6KL_DBG_IPA_MSG,
			"IPA-CM: Creating flr rule for(HSIC1_PROD) client %d\n",
			client);

	flt = (struct ipa_ioc_add_flt_rule *)
		kzalloc(sizeof(struct ipa_ioc_add_flt_rule) +
				sizeof(struct ipa_flt_rule_add),
				GFP_KERNEL);
	if (flt == NULL) {
		ath6kl_err("IPA-CM: Failed to create filter "
				"rule for(HSIC1_PROD) client %d\n", client);
		return -ENOMEM;
	}

	flt->commit = 1;
	flt->ip = IPA_IP_v4;
	flt->ep = client;
	flt->global = 0;
	flt->num_rules = 1;

	/* Setting the Filter rule for Exception Packet */
	flt->rules[0].rule.action = IPA_PASS_TO_EXCEPTION;
	flt->rules[0].at_rear = 0;

	memset(&rt_lookup, 0, sizeof(rt_lookup));

	rt_lookup.ip = IPA_IP_v4;
	strlcpy(rt_lookup.name, IPA_DFLT_RT_TBL_NAME,
			sizeof(rt_lookup.name));

	ret = ipa_get_rt_tbl(&rt_lookup);

	if (ret) {
		ath6kl_err("IPA-CM: Geting RT table failed for IPV4 :"
				"client : %d, ret: %d\n", client, ret);
		goto end;
	}

	flt->rules[0].rule.rt_tbl_hdl = rt_lookup.hdl;

	/* 12th Byte, d0 bit is set for exception ,
	 * 			meta data offset is 11 */
	flt->rules[0].rule.attrib.meta_data = 0x01000000;
	flt->rules[0].rule.attrib.meta_data_mask = 0x01000000;
	flt->rules[0].rule.attrib.attrib_mask = IPA_FLT_META_DATA;

	ret = ipa_add_flt_rule(flt);

	if (ret || flt->rules[0].status != 0) {
		ath6kl_err("IPA-CM: Error in adding Flt for IPV4: %d "
				"ret: %d, status: %d\n",
				client, ret, flt->rules[0].status);
		goto end;
	}

	/* Remember the handle, needed while clean up */
	flt_hdl_ipv4 = flt->rules[0].flt_rule_hdl;

	ret = ipa_commit_flt(IPA_IP_v4);

	if (ret)
		goto end;

	if (ath6kl_debug_quirks(ar, ATH6KL_MODULE_IPA_WITH_IPV6)) {
		flt->ip = IPA_IP_v6;

		memset(&rt_lookup, 0, sizeof(rt_lookup));

		rt_lookup.ip = IPA_IP_v6;
		strlcpy(rt_lookup.name, IPA_DFLT_RT_TBL_NAME,
				sizeof(rt_lookup.name));

		ret = ipa_get_rt_tbl(&rt_lookup);

		if (ret) {
			ath6kl_err("IPA-CM: Get RT table failed for "
					"IPV6: client : %d, %d\n",
					client, ret);
			goto end;
		}

		flt->rules[0].rule.rt_tbl_hdl = rt_lookup.hdl;

		ret = ipa_add_flt_rule(flt);

		if (ret || flt->rules[0].status < 0) {
			ath6kl_err("IPA-CM: Error in adding Flt for IPV6"
					" client: %d ret: %d, status: %d\n",
					client, ret, flt->rules[0].status);
			goto end;
		}

		/* Remember the handle, needed while clean up */
		flt_hdl_ipv6 = flt->rules[0].flt_rule_hdl;

		ret = ipa_commit_flt(IPA_IP_v6);
		if (ret)
			goto end;
	}

end:
	kfree(flt);

	return ret;
}
EXPORT_SYMBOL(ath6kl_ipa_add_flt_rule);

#define ATH6KL_IPA_IP_MAX_TX_PROP		8
#define ATH6KL_IPA_TOS_MASK			0xE0

static struct {
	uint8_t tos_value;
	enum ipa_client_type dst_pipe;
} tx_prop_map [] = {
	/* BK */
	{1, IPA_CLIENT_HSIC2_CONS},
	{2, IPA_CLIENT_HSIC2_CONS},
	/* BE */
	{0, IPA_CLIENT_HSIC1_CONS},
	{3, IPA_CLIENT_HSIC1_CONS},
	/* VI */
	{4, IPA_CLIENT_HSIC3_CONS},
	{5, IPA_CLIENT_HSIC3_CONS},
	/* VO */
	{6, IPA_CLIENT_HSIC4_CONS},
	{7, IPA_CLIENT_HSIC4_CONS},
};

int ath6kl_ipa_register_interface(struct ath6kl *ar, u8 sta_ap,
		const char *name, char* hdr_name_ip)
{
	struct ipa_tx_intf tx_intf;
	struct ipa_rx_intf rx_intf;
	struct ipa_ioc_tx_intf_prop *tx_prop = NULL;
	struct ipa_ioc_rx_intf_prop *rx_prop = NULL;
	char ipv4_hdr_name[IPA_RESOURCE_NAME_MAX];
	char ipv6_hdr_name[IPA_RESOURCE_NAME_MAX];
	int i, j;
	int ret = 0;

	/* Allocate TX properties, 1 each for IPv4 & IPv6 */
	tx_prop = kzalloc(sizeof(struct ipa_ioc_tx_intf_prop) *
			ATH6KL_IPA_IP_MAX_TX_PROP * 2, GFP_KERNEL);

	if (tx_prop == NULL) {
		ath6kl_err("Failed to allocate memory for TX properties\n");
		goto end;
	}

	/* Allocate RX properties, 1 each for IPv4 & IPv6 */
	rx_prop = kzalloc(sizeof(struct ipa_ioc_rx_intf_prop) * 2, GFP_KERNEL);

	if (rx_prop == NULL) {
		ath6kl_err("Failed to allocate memory for RX properties\n");
		goto end;
	}

	strlcpy(ipv4_hdr_name, hdr_name_ip, sizeof(ipv4_hdr_name));
	strlcat(ipv4_hdr_name, ATH6KL_BAM2BAM_IPV4_NAME_EXT,
			sizeof(ipv4_hdr_name));

	strlcpy(ipv6_hdr_name, hdr_name_ip, sizeof(ipv6_hdr_name));
	strlcat(ipv6_hdr_name, ATH6KL_BAM2BAM_IPV6_NAME_EXT,
			sizeof(ipv6_hdr_name));

	memset(&tx_intf, 0, sizeof(tx_intf));
	memset(&rx_intf, 0, sizeof(rx_intf));

	/*
	   TOS value: 1, 2 - > Maps to BK pipe [HSIC2_CONS]
	   TOS value: 0, 3 - > Maps to BE pipe [HSIC1_CONS]
	   TOS value: 4, 5 - > Maps to VI pipe [HSIC3_CONS]
	   TOS Value: 6, 7 - > Maps to VO pipe [HSIC4_CONS]
	 */

	/*
	   IP-TOS - 8bits
	   : DSCP(6-bits) ECN(2-bits)
	   : DSCP - P2 P1 P0 X X X
	   where (P2 P1 P0) form 802.1D
	   So shifting tos_value by 5 bits before passing to IPA. Also IPA
	   required mask to be set for 3 MSB bits.
	 */

	for (i = 0; i < ATH6KL_IPA_IP_MAX_TX_PROP; i++) {
		tx_prop[i].ip = IPA_IP_v4;
		tx_prop[i].attrib.attrib_mask = IPA_FLT_TOS_MASKED;
		tx_prop[i].attrib.tos_value = tx_prop_map[i].tos_value << 5;
		tx_prop[i].attrib.tos_mask = ATH6KL_IPA_TOS_MASK;
		tx_prop[i].dst_pipe = tx_prop_map[i].dst_pipe;
		strlcpy(tx_prop[i].hdr_name, ipv4_hdr_name,
				IPA_RESOURCE_NAME_MAX);

		tx_intf.num_props++;

		/* Dont update if IPV6 is not enabled */
		if (!ath6kl_debug_quirks(ar, ATH6KL_MODULE_IPA_WITH_IPV6))
			continue;

		j = i + ATH6KL_IPA_IP_MAX_TX_PROP;

		tx_prop[j].ip = IPA_IP_v6;
		tx_prop[j].attrib.attrib_mask = IPA_FLT_TOS_MASKED;
		tx_prop[j].attrib.tos_value = tx_prop_map[i].tos_value << 5;
		tx_prop[j].attrib.tos_mask = ATH6KL_IPA_TOS_MASK;
		tx_prop[j].dst_pipe = tx_prop_map[i].dst_pipe;
		strlcpy(tx_prop[j].hdr_name, ipv6_hdr_name,
				IPA_RESOURCE_NAME_MAX);

		tx_intf.num_props++;
	}


	/* Enable this for switching between WLAN or WAN  */
	rx_prop[0].ip = IPA_IP_v4;
	rx_prop[0].attrib.attrib_mask = IPA_FLT_META_DATA;

	/* 12th Byte, d2 bit is set for wlan or wan interface*/
	if(sta_ap == 1) /* Means AP mode */
		rx_prop[0].attrib.meta_data = 0x04000000;
	else
		rx_prop[0].attrib.meta_data = 0x00000000;

	rx_prop[0].attrib.meta_data_mask = 0x04000000;
	rx_prop[0].src_pipe = IPA_CLIENT_HSIC1_PROD;
	rx_intf.num_props++;

	if (ath6kl_debug_quirks(ar, ATH6KL_MODULE_IPA_WITH_IPV6)) {

		/* Enable this for switching between WLAN or WAN  */
		rx_prop[1].ip = IPA_IP_v6;
		rx_prop[1].attrib.attrib_mask = IPA_FLT_META_DATA;

		/* 12th Byte, d2 bit is set for wlan or wan interface*/
		if(sta_ap == 1) /* Means AP mode */
			rx_prop[1].attrib.meta_data = 0x04000000;
		else
			rx_prop[1].attrib.meta_data = 0x00000000;

		rx_prop[1].attrib.meta_data_mask = 0x04000000;
		rx_prop[1].src_pipe = IPA_CLIENT_HSIC1_PROD;

		rx_intf.num_props++;
	}

	/* If Rx is SW path, then no need to set Rx properties */
	if (ath6kl_debug_quirks(ar, ATH6KL_MODULE_BAM_RX_SW_PATH))
		rx_intf.num_props = 0;

	tx_intf.prop = tx_prop;
	rx_intf.prop = rx_prop;

	/* Call the ipa api to register interface */
	ret = ipa_register_intf(name, &tx_intf, &rx_intf);

end:
	kfree(tx_prop);
	kfree(rx_prop);
	return ret;
}
EXPORT_SYMBOL(ath6kl_ipa_register_interface);

/* @brief
 * add the specified headers to SW and optionally commit them to IPA HW
 * @return
 * 0 on success,
 * error on failure
 */
int ath6kl_ipa_add_header_info(struct ath6kl *ar, u8 ap_sta, u8 device_id,
		char* interface_name, u8 *mac_addr)
{
	struct ipa_ioc_add_hdr *ipahdr = NULL;
	int ret;

	uint8_t hdr[ATH6KL_IPA_WLAN_HDR_LENGTH + 1]={
		/* HTC Header - 6 bytes */
		0x00, 0x00,  /* Reserved */

		/* length filled by IPA, after adding 32 with IP Payload
		 * length 32 will be set while intializing the hdr */
		0x00, 0x00,

		/* Reserved */
		0x00, 0x00,

		/* WMI header - 6 bytes*/
		0x00, 0x00, 0x00, 0x00,
		0x00, /* D0,D1 -> Device ID */
		0x00,

		/* 802.3 header - 14 bytes*/

		/* Des. MAC to be filled by IPA */
		0x00,0x03,0x7f,0xaa,0xbb,0xcc,

		/* Src. MAC to be filled by IPA */
		0x00,0x03,0x7f,0xdd,0xee,0xff,

		0x00,0x00, /* length can be zero */

		/* LLC SNAP header - 8 bytes */
		0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00,

		0x08, 0x00  /* type value(2 bytes) ,filled by wlan  */
		/* 0x0800 - IPV4, 0x86dd - IPV6 */
	};

	if(interface_name == NULL) {
		ath6kl_dbg(ATH6KL_DBG_IPA_MSG,
			"IPA-CM: Interface name is null and not defined\n");
		return -EINVAL;
	}
	/* dynamically allocate the memory to add the hdrs */
	ipahdr = (struct ipa_ioc_add_hdr *)
		kzalloc(sizeof(struct ipa_ioc_add_hdr) +
			  sizeof(struct ipa_hdr_add), GFP_KERNEL);
	if (ipahdr == NULL) {
		ath6kl_dbg(ATH6KL_DBG_IPA_MSG,
			"IPA-CM: Failed while creating hdr for interface %s\n",
				interface_name);
		return -ENOMEM;
	}

	ipahdr->commit = 0;
	ipahdr->num_hdrs = 1;

	/* Set the Source MAC */
	memcpy (&hdr[12+6], mac_addr, 6);

	/* Check the interface is AP OR STA mode, and set the Device ID */
	hdr[6+4] |= (device_id & 0x3);

	strcpy(ipahdr->hdr[0].name,interface_name);
	/* Actual headers to be inserted */
	memcpy(ipahdr->hdr[0].hdr, hdr, ATH6KL_IPA_WLAN_HDR_LENGTH);
	ipahdr->hdr[0].hdr_len = ATH6KL_IPA_WLAN_HDR_LENGTH;
	ipahdr->hdr[0].is_partial = ATH6KL_IPA_WLAN_HDR_PARTIAL;
	ipahdr->hdr[0].hdr_hdl 	= 0;   /* output param, no need to fill */

	/* Call the ipa api to configure ep */
	strlcpy(ipahdr->hdr[0].name, interface_name,
			sizeof(ipahdr->hdr[0].name));
	strlcat(ipahdr->hdr[0].name, ATH6KL_BAM2BAM_IPV4_NAME_EXT,
			sizeof(ipahdr->hdr[0].name));
	ret = ipa_add_hdr(ipahdr);

	if(ret) {
		ath6kl_err("IPA-CM: Failed adding IPv4 hdr for interface %s,"
				" ret: %d\n", interface_name, ret);
		goto end;
	}

	ath6kl_dbg(ATH6KL_DBG_IPA_MSG,
			"IPA-CM: IPA Handle for header: %s : %x\n",
			ipahdr->hdr[0].name, ipahdr->hdr[0].hdr_hdl);

	if (ath6kl_debug_quirks(ar, ATH6KL_MODULE_IPA_WITH_IPV6)) {
		strlcpy(ipahdr->hdr[0].name, interface_name,
				sizeof(ipahdr->hdr[0].name));
		strlcat(ipahdr->hdr[0].name, ATH6KL_BAM2BAM_IPV6_NAME_EXT,
				sizeof(ipahdr->hdr[0].name));
		/* Set the type to IPV6 in the header*/
		/* 0x0800 - IPV4, 0x86dd - IPV6 */
		ipahdr->hdr[0].hdr[32] = 0x86;
		ipahdr->hdr[0].hdr[33] = 0xdd;

		ret = ipa_add_hdr(ipahdr);
		if(ret) {
			ath6kl_err("IPA-CM: Failed adding IPv6 hdr for"
					" interface %s, ret: %d\n",
					interface_name, ret);
			goto end;
		}

		ath6kl_dbg(ATH6KL_DBG_IPA_MSG,
				"IPA Handle for header: %s : %x\n",
				ipahdr->hdr[0].name, ipahdr->hdr[0].hdr_hdl);
	}

	/* Configure the TX and RX pipes filter rules */
	ret = ath6kl_ipa_register_interface(ar, ap_sta, interface_name,
			interface_name);
	if(ret) {
		ath6kl_dbg(ATH6KL_DBG_IPA_MSG,
			"IPA-CM: Failed registering the interface for :%s,"
			" ret: %d\n", interface_name, ret);
	}

end:
	kfree(ipahdr);
	return ret;
}
EXPORT_SYMBOL(ath6kl_ipa_add_header_info);

int ath6kl_ipa_put_header_hdl(char *hdr_name, uint32_t *hdl)
{
	int status;

	/* Put and release hdr, and it is equivalent of deleting the header */
	status = ipa_put_hdr(*hdl);

	if (status) {
		ath6kl_err("IPA-CM: Error in put the hdr Handle %x for hdr : "
				"%s, status: %d\n", *hdl, hdr_name, status);
	}

	return status;
}

int ath6kl_ipacm_get_ep_config_info(u32 ipa_client, struct ipa_ep_cfg *ep_cfg)
{
	if(!ep_cfg) {
		ath6kl_dbg(ATH6KL_DBG_BAM2BAM,
				"IPA-CM: ep_cfg : Null Value %s, %d\n",
				__func__, __LINE__);
		return -EINVAL;
	}

	// Fill the details for configuration of IPA end-point
	if (ipa_client == IPA_CLIENT_A5_WLAN_AMPDU_PROD) {
		ep_cfg->hdr.hdr_len = ATH6KL_IPA_WLAN_HDR_LENGTH;
		/* Rx pipe */
		// NAT configuration in IPA end-point
		ep_cfg->nat.nat_en = IPA_BYPASS_NAT; // IPA_SRC_NAT;

		/*
		 * 0: Metadata_Ofst value is invalid, i.e.,
		 * no metadata within header
		 * 1: Metadata_Ofst value is valid, i.e., metadata
		 * within header is in offset Metadata_Ofst
		 * Valid for Input Pipes only (IPA Consumer)
		 * (for output pipes, metadata already set within
		 * the header)
		 */
		ep_cfg->hdr.hdr_ofst_metadata_valid = 1;

		/*!< Offset within header in which metadata resides
		  Size of metadata - 4bytes
		  Example - Stream ID/SSID/mux ID
		  Valid for Input Pipes only (IPA Consumer)
		  (for output pipes, metadata already set within the
		  header) */
		ep_cfg->hdr.hdr_ofst_metadata = 11;

		ep_cfg->hdr.hdr_additional_const_len = 0;
		ep_cfg->hdr.hdr_ofst_pkt_size_valid = 0;
		ep_cfg->hdr.hdr_ofst_pkt_size = 0;

	} else if (ipa_client == IPA_CLIENT_HSIC1_PROD) {
		// Hdr conf in IPA end-point(HTC(6)+WMI(6)+ 802.3 (22) )
		ep_cfg->hdr.hdr_len = ATH6KL_IPA_WLAN_HDR_LENGTH;
		/* Rx pipe */
		// NAT configuration in IPA end-point
		ep_cfg->nat.nat_en = IPA_BYPASS_NAT; // IPA_SRC_NAT;

		/* 0: Metadata_Ofst value is invalid, i.e.,
		 * no metadata within header
		 * 1: Metadata_Ofst value is valid, i.e., metadata
		 * within header is in offset Metadata_Ofst
		 * Valid for Input Pipes only (IPA Consumer)
		 * (for output pipes, metadata already set within
		 * the header)
		 */
		ep_cfg->hdr.hdr_ofst_metadata_valid = 1;

		/*!< Offset within header in which metadata resides
		  Size of metadata - 4bytes
		  Example - Stream ID/SSID/mux ID
		  Valid for Input Pipes only (IPA Consumer)
		  (for output pipes, metadata already set within the
		  header) */
		/* 12th Byte, d0 set for excp, so offset should be 11 */
		ep_cfg->hdr.hdr_ofst_metadata = 11;

		ep_cfg->hdr.hdr_additional_const_len = 0;
		ep_cfg->hdr.hdr_ofst_pkt_size_valid = 0;
		ep_cfg->hdr.hdr_ofst_pkt_size = 0;
	} else {
		/* Header conf IPA end-point(HTC(6)+WMI(6)+802.3 (22) ) */
		ep_cfg->hdr.hdr_len = ATH6KL_IPA_WLAN_HDR_LENGTH;

		/* Tx pipe */
		ep_cfg->nat.nat_en = IPA_BYPASS_NAT; // IPA_DST_NAT;
		/* This is not valid for Tx Pipe */
		ep_cfg->hdr.hdr_ofst_metadata_valid = 0;

		ep_cfg->hdr.hdr_ofst_metadata = 0;
		/*!< Defines the constant length that should be added
		  to the payload length in order for IPA to update
		  correctly the length field within the header
		  (valid only in case Hdr_Ofst_Pkt_Size_Valid=1)
		  Valid for Output Pipes (IPA Producer) */
		ep_cfg->hdr.hdr_additional_const_len =
			ATH6KL_IPA_TX_PKT_LEN_POS;

		ep_cfg->hdr.hdr_ofst_pkt_size_valid = 1;

		/*!< Offset within header in which packet size
		  reside. Upon Header Insertion, IPA will update this
		  field within the header with the packet length .
		  Assumption is that header length field size is
		  constant and is 2Bytes
		  Valid for Output Pipes (IPA Producer) */
		ep_cfg->hdr.hdr_ofst_pkt_size = 2;
	}

	ep_cfg->hdr.hdr_a5_mux = 0;

	/* Mode setting type in IPA end-point */
	ep_cfg->mode.mode = IPA_BASIC;

	ep_cfg->mode.dst = ipa_client;

	/* Aggregation configuration in IPA end-point */
	ep_cfg->aggr.aggr_en = IPA_BYPASS_AGGR;
	ep_cfg->aggr.aggr = IPA_MBIM_16;
	ep_cfg->aggr.aggr_byte_limit = 0;
	ep_cfg->aggr.aggr_time_limit = 0;

	/* Route configuration in IPA end-point */
	ep_cfg->route.rt_tbl_hdl = 0;

	ep_cfg->route.rt_tbl_hdl = 0;

	return 0;
}

EXPORT_SYMBOL(ath6kl_ipacm_get_ep_config_info);

int ath6kl_data_ipa_ampdu_tx_complete_cb(enum ath6kl_bam_tx_evt_type evt_type,
		struct sk_buff *skb)
{
	switch (evt_type) {
	case AMPDU_FLUSH:
		if (!skb || !skb->data)
			goto fatal;
		ath6kl_dbg(ATH6KL_DBG_OOO,
				"ooo: AMPDU TX comp callback recv, "
				"Freeing the skb\n");
		dev_kfree_skb(skb);
		break;
	default:
		ath6kl_err("ooo: Unknown event from sysbam tx_complete: %d\n",
				evt_type);
		break;
	}

	return 0;
fatal:
	WARN_ON(1);
	return -1;

}

/* Callback function to handle only SYS BAM Pipe Tx Complete */
void ath6kl_ipa_sysbam_tx_callback(void *priv, enum ipa_dp_evt_type evt,
		unsigned long data)
{
	u32 client;
	struct sk_buff *skb;

	/* typecast the skb buffer pointer */
	skb = (struct sk_buff *) data;

	client = *((enum ipa_client_type *)priv);
	switch (evt) {
		/* IPA sends data to WLAN class driver */
	case IPA_RECEIVE:
		ath6kl_err("BAM-CM: Received Data from SysBAM pipe\n");
		dev_kfree_skb_any(skb);
		break;

		/* IPA sends Tx complete Event to WLAN */
	case IPA_WRITE_DONE:
		switch (client) {
			/* AMPDU Flush completed by IPA and event received */
		case IPA_CLIENT_A5_WLAN_AMPDU_PROD:
			ath6kl_dbg(ATH6KL_DBG_OOO,
					"BAM-CM: %s: sys pipe: %d, AMPDU "
					"Tx complete event received\n",
					__func__, client);
			/* send to wlan class driver */
			ath6kl_data_ipa_ampdu_tx_complete_cb(AMPDU_FLUSH
					, skb);
			break;
		default:
			ath6kl_err("BAM-CM: IPA sysbam pipe Tx complete\
					callback received wrong context : %d\n",
					client);
			break;
		}
		break;
	default:
		ath6kl_err("BAM-CM: IPA sysbam pipe Tx comp callback\
				received wrong event type : %d\n", evt);
		break;
	}
}
/* Disconnect all the Sys BAM pipes, in our case, only 1 pipe */
void ath6kl_disconnect_sysbam_pipes(struct ath6kl *ar)
{
	int status,i;

	/* If Rx SW path, then no need to create/delete sysbam pipe */
	if (!!(debug_quirks & ATH6KL_MODULE_BAM_RX_SW_PATH))
		return;

	for (i = 0; i < MAX_SYSBAM_PIPE; i++) {
		status = ipa_teardown_sys_pipe(sysbam_pipe[i].clnt_hdl);
		if (status)
			ath6kl_err("BAM-CM:Error in disconnect SYSBAM pipe"
					" status: %d\n", status);
	}

}
EXPORT_SYMBOL(ath6kl_disconnect_sysbam_pipes);

/* Create the SysBAM pipe */
int ath6kl_usb_create_sysbam_pipes(struct ath6kl *ar)
{
	int status,i;

	/* If Rx SW path, then no need to create/delete sysbam pipe */
	if (!!(debug_quirks & ATH6KL_MODULE_BAM_RX_SW_PATH))
		return 0;

	/* The config is similar to the RX Bam pipe configuration */
	for (i = 0; i < MAX_SYSBAM_PIPE; i++) {
		sysbam_pipe[i].ipa_params.client = sysbam_info[i].client;

		ath6kl_ipacm_get_ep_config_info(sysbam_pipe[i].ipa_params.client,
				&(sysbam_pipe[i].ipa_params.ipa_ep_cfg));

		sysbam_pipe[i].ipa_params.desc_fifo_sz = 0x400;

		sysbam_pipe[i].ipa_params.priv=(void *)&(sysbam_info[i].client);

		/* sysbam pipe callback evt handler same as bam pipe handler */
		sysbam_pipe[i].ipa_params.notify= ath6kl_ipa_sysbam_tx_callback;

		/* Create the SYS BAM pipe to send AMPDU re-ordered packets */
		status = ipa_setup_sys_pipe(&(sysbam_pipe[i].ipa_params),
				&(sysbam_pipe[i].clnt_hdl));

		if (status) {
			ath6kl_err("BAM-CM: Failed to create SYSBAM: %d pipe "
					"status: %d\n",
					sysbam_pipe[i].ipa_params.client,
					status);
			break;
		}

		ath6kl_dbg(ATH6KL_DBG_BAM2BAM,
				"BAM-CM: Successfully created SYSBAM pipe "
				"client: %d, Control handle: %d\n",
				sysbam_info[i].client, sysbam_pipe[i].clnt_hdl);
	}

	return status;
}
EXPORT_SYMBOL(ath6kl_usb_create_sysbam_pipes);

/* Send data to the IPA HW via SYSBAM */
int ath6kl_usb_data_send_to_sysbam_pipe(struct ath6kl *ar, struct sk_buff *skb)
{
	int status=0;

	if ( (ath6kl_debug_quirks(ar, ATH6KL_MODULE_IPA_WITH_IPACM)) &&
		!(ath6kl_debug_quirks(ar, ATH6KL_MODULE_BAM_AMPDU_TO_NETIF) &&
		!(ath6kl_debug_quirks(ar, ATH6KL_MODULE_BAM_RX_SW_PATH)))) {
		ath6kl_dbg(ATH6KL_DBG_OOO,
			"BAM-CM: TX:(AMPDU_PROD)Sending reorderd pkt of size %d (dec)\n",
			skb->len);
		/* Add the Hdr (HTC+WMI+802.3+LLC SNAP = 34(wlan hdr len) back ,
		   as required by the AMPDU pipe, since the filter settings are
		   same as HSIC1_PROD pipe */
		skb_push(skb, ATH6KL_IPA_WLAN_HDR_LENGTH);

		/* WMI header Byte6: D0 : Exception bit (0-LTE/WAN, 1-Data to
		 * 					Host for re-ordering) */

		/* Reset the exception bit, since the packets are orderded now,
		 * 				and not need to send to host */
		skb->data[11] &= 0xfe; /* Reset the D0 bit */

		status = ipa_tx_dp(IPA_CLIENT_A5_WLAN_AMPDU_PROD, skb, NULL);
		if (status)
			ath6kl_err("BAM-CM: Failed to send data over sysbam :%d"
				       "status: %d\n",
				       IPA_CLIENT_A5_WLAN_AMPDU_PROD, status);
		return status;
	}

	/* Use netif to send re-ordered packets in absence of IPACM */
	ath6kl_dbg(ATH6KL_DBG_OOO,
		"BAM-CM: TX:(To Netif)Sending reordered pkt of size %d (dec)\n",
		skb->len);
	netif_rx_ni(skb);

	return status;
}

void ath6kl_delete_ipa_header(uint32_t hdl)
{
	int status;
	int len;
	struct ipa_ioc_del_hdr *ipahdr;

	ath6kl_dbg(ATH6KL_DBG_IPA_MSG,
			"IPA-CM: Deleting the IPA header handle : %x\n", hdl);

	len = sizeof(struct ipa_ioc_del_hdr) + sizeof(struct ipa_hdr_del)*1;

	ipahdr = (struct ipa_ioc_del_hdr *) kzalloc(len, GFP_KERNEL);
	if (ipahdr == NULL) {
		ath6kl_err("IPA-CM: Failed to allocate memory for hdr removal "
				"for handle: %x\n", hdl);
		return;
	}

	ipahdr->num_hdls = 1;
	ipahdr->commit = 0;
	ipahdr->hdl[0].hdl = hdl;
	ipahdr->hdl[0].status = -1;
	status = ipa_del_hdr(ipahdr);
	if (status != 0) {
		ath6kl_err("IPA-CM: Delete hdr from IPA Failed, return-status: "
				"%d param-status: %d handle: %x\n",
				status, ipahdr->hdl[0].status,
				ipahdr->hdl[0].hdl);
		kfree(ipahdr);
		return;
	}

	kfree(ipahdr);
	return;
}
void ath6kl_remove_ipa_header(char *name)
{
	struct ipa_ioc_get_hdr hdrlookup;
	int ret = 0;

	memset(&hdrlookup, 0, sizeof(hdrlookup));
	strlcpy(hdrlookup.name, name, sizeof(hdrlookup.name));

	ret = ipa_get_hdr(&hdrlookup);

	if (ret) {
		ath6kl_info("IPA-CM: Header may have already been deleted for:"
				" %s, ret: %d\n", name, ret);
		return;
	}

	ath6kl_delete_ipa_header(hdrlookup.hdl);

	ath6kl_dbg(ATH6KL_DBG_IPA_MSG,
			"IPA-CM: Successfully removed the hdr:%s handle:%x\n",
			name, hdrlookup.hdl);
	return;
}

void ath6kl_remove_filter_rule(enum ipa_ip_type ip_type, uint32_t hdl)
{
	int status;
	struct ipa_ioc_del_flt_rule *fltdel;

	if (ip_type == IPA_IP_v4)
		ath6kl_dbg(ATH6KL_DBG_IPA_MSG,
			"IPA-CM: Deleting the exception filter for IPV4...\n");
	else
		ath6kl_dbg(ATH6KL_DBG_IPA_MSG,
			"IPA-CM: Deleting the exception filter for IPV6...\n");

	fltdel = (struct ipa_ioc_del_flt_rule *)
		kmalloc(sizeof(struct ipa_ioc_del_flt_rule) +
			  sizeof(struct ipa_flt_rule_del), GFP_KERNEL);
	if (fltdel == NULL) {
		ath6kl_err("IPA-CM: Failed in allocating memory for deleting "
				"the Filter : Handle :%x \n", hdl);
		return;
	}

	fltdel->hdl[0].hdl = hdl;
	fltdel->num_hdls = 1;
	fltdel->commit = 1;
	fltdel->ip = ip_type;
	fltdel->hdl[0].status = -1;

	status = ipa_del_flt_rule(fltdel);
	if (status || fltdel->hdl[0].status != 0) {
		ath6kl_err("IPA-CM: Failed to delete exception filter for "
				"Handle : %x status: %d, %d\n", hdl, status,
				fltdel->hdl[0].status);
		kfree(fltdel);
		return;
	}

	kfree(fltdel);
	return;
}

void ath6kl_remove_ipa_exception_filters(struct ath6kl *ar)
{
	/* In Rx SW path, no need to create/delete the exception filter */
	if (!!(debug_quirks & ATH6KL_MODULE_BAM_RX_SW_PATH))
		return;

	/* Remove the filters */
	ath6kl_remove_filter_rule(IPA_IP_v4, flt_hdl_ipv4);

	if (!!(debug_quirks & ATH6KL_MODULE_IPA_WITH_IPV6))
		ath6kl_remove_filter_rule(IPA_IP_v6, flt_hdl_ipv6);
}
EXPORT_SYMBOL(ath6kl_remove_ipa_exception_filters);

void ath6kl_clean_ipa_headers(struct ath6kl *ar, char *name)
{
	int status;
	char name_ipa[IPA_RESOURCE_NAME_MAX];

	if (!ath6kl_debug_quirks(ar, ATH6KL_MODULE_BAM2BAM))
		return;

	/* Remove the headers */
	strlcpy(name_ipa, name, sizeof(name_ipa));
	strlcat(name_ipa, ATH6KL_BAM2BAM_IPV4_NAME_EXT, sizeof(name_ipa));
	ath6kl_remove_ipa_header(name_ipa);

	if (ath6kl_debug_quirks(ar, ATH6KL_MODULE_IPA_WITH_IPV6)) {
		strlcpy(name_ipa, name, sizeof(name_ipa));
		strlcat(name_ipa, ATH6KL_BAM2BAM_IPV6_NAME_EXT,
				sizeof(name_ipa));
		ath6kl_remove_ipa_header(name_ipa);
	}

	/* unregister the interface with IPA */
	status = ipa_deregister_intf(name);
	if (status)
		ath6kl_err("IPA-CM: Interface %s : deregister failed,"
				" status: %d\n", name, status);
}
EXPORT_SYMBOL(ath6kl_clean_ipa_headers);

/* IPA calls this fn, once the message is processed */
static void ath6kl_ipa_msg_free_fn(void *buff, u32 len, u32 type)
{
	ath6kl_dbg(ATH6KL_DBG_IPA_MSG,
			"IPA-CM: Msg Callback msg type: %d, len: %d\n",
			type, len);
	kfree(buff);
}

#ifdef CONFIG_ATH6KL_DEBUG
/* Based on msm_ipa.h */
const char *ath6kl_ipa_event_name[IPA_WLAN_EVENT_MAX] = {
	__stringify(WLAN_CLIENT_CONNECT),
	__stringify(WLAN_CLIENT_DISCONNECT),
	__stringify(WLAN_CLIENT_POWER_SAVE_MODE),
	__stringify(WLAN_CLIENT_NORMAL_MODE),
	__stringify(SW_ROUTING_ENABLE),
	__stringify(SW_ROUTING_DISABLE),
	__stringify(WLAN_AP_CONNECT),
	__stringify(WLAN_AP_DISCONNECT),
	__stringify(WLAN_STA_CONNECT),
	__stringify(WLAN_STA_DISCONNECT),
};

#define ATH6KL_DBG_PRINT_IPA_EVENT(_mask, _iface_name, _type, _mac_addr) \
	ath6kl_dbg(_mask, \
			"IPA-CM: %s: %s event received, MAC Addr: %pM\n", \
			_iface_name, ath6kl_ipa_event_name[_type], _mac_addr);
#else

#define ATH6KL_DBG_PRINT_IPA_EVENT(_mask, _iface_name, _type, _mac_addr)

#endif

int ath6kl_send_msg_ipa(struct ath6kl_vif *vif, enum ipa_wlan_event type,
		u8 *mac_addr)
{
	struct ipa_msg_meta meta;
	struct ipa_wlan_msg *buff;
	char iface_name[IPA_RESOURCE_NAME_MAX];
	int status;

	if (!ath6kl_debug_quirks(vif->ar, ATH6KL_MODULE_BAM2BAM))
		return 0;

	if (!ath6kl_debug_quirks(vif->ar, ATH6KL_MODULE_IPA_WITH_IPACM))
		return 0;

	if (type >= IPA_WLAN_EVENT_MAX) {
		ath6kl_err("IPA-CM: Unknown IPA event type: %d\n", type);
		return -EINVAL;
	}

	if (WARN_ON(is_zero_ether_addr(mac_addr)))
		return -EINVAL;

	strlcpy(iface_name, vif->ndev->name, sizeof(iface_name));

	ATH6KL_DBG_PRINT_IPA_EVENT(ATH6KL_DBG_IPA_MSG, iface_name, type,
			mac_addr);

	switch(type) {

	case WLAN_AP_CONNECT:
		ath6kl_dbg(ATH6KL_DBG_IPA_MSG,
				"IPA-CM: AP mode Adding Partial hdr: %s, %pM\n",
				vif->ndev->name, vif->ndev->dev_addr);
		/* Add partial header with IPA for this interface */
		if (!(test_bit(CONNECTED, &vif->flags))) {
			ath6kl_ipa_add_header_info(vif->ar, 1, vif->fw_vif_idx,
					vif->ndev->name, vif->ndev->dev_addr);
		}
		break;

	case WLAN_AP_DISCONNECT:
		ath6kl_clean_ipa_headers(vif->ar, vif->ndev->name);
		break;

	case WLAN_STA_CONNECT:
		ath6kl_dbg(ATH6KL_DBG_IPA_MSG,
			"IPA-CM: STA Connect adding partial hdr: %s, %pM\n",
				vif->ndev->name, vif->ndev->dev_addr);
		ath6kl_ipa_add_header_info(vif->ar, 0, vif->fw_vif_idx,
				vif->ndev->name, vif->ndev->dev_addr);
		break;

	case WLAN_STA_DISCONNECT:
		ath6kl_clean_ipa_headers(vif->ar, vif->ndev->name);
		break;

	case WLAN_CLIENT_POWER_SAVE_MODE:
	case WLAN_CLIENT_NORMAL_MODE:
		/* If MCC is enabled, then dont send PS events */
		if(vif->ar->is_mcc_enabled == true)
			return 0;

	case SW_ROUTING_ENABLE:
	case SW_ROUTING_DISABLE:
	case WLAN_CLIENT_CONNECT:
	case WLAN_CLIENT_DISCONNECT:
	default:
		/* Nothing to be done for these events */
		break;
	}

	/* Fille the message len */
	meta.msg_len = sizeof(struct ipa_wlan_msg);

	/* Allocate memory for the msg, Ipa call the callback fn to free this */
	buff = (struct ipa_wlan_msg *)kmalloc (meta.msg_len, GFP_KERNEL);

	if (buff == NULL) {
		ath6kl_err("IPA-CM: Failed to allocate memory for msg type:%d\n"
				, type);
		return -ENOMEM;
	}

	/* Fill the message type*/
	meta.msg_type = type;

	/* Fill the message */
	strcpy(buff->name, iface_name); /* need to be changed later */
	memcpy(buff->mac_addr, mac_addr, ETH_ALEN);

	ath6kl_dbg(ATH6KL_DBG_IPA_MSG,
		"IPA-CM: IPA send msg : Event ID : %d, interface name: %s \n",
		meta.msg_type, buff->name);

	status = ipa_send_msg(&meta, buff, ath6kl_ipa_msg_free_fn);

	if(status) {
		ath6kl_err ("IPA-CM: Failed to send msg for type: %d,"
				" status: %d\n", type, status);
		kfree(buff);
		return status;
	}

	/*Note:
	 *buff memory will be free by the callback func: ath6kl_ipa_msg_free_fn
	 */

	return status;
}

#endif /* CONFIG_ATH6KL_BAM2BAM */

static inline u8 ath6kl_get_tid(u8 tid_mux)
{
	return tid_mux & ATH6KL_TID_MASK;
}

static inline u8 ath6kl_get_aid(u8 tid_mux)
{
	return tid_mux >> ATH6KL_AID_SHIFT;
}

static u8 ath6kl_ibss_map_epid(struct sk_buff *skb, struct net_device *dev,
		u32 *map_no)
{
	struct ath6kl *ar = ath6kl_priv(dev);
	struct ethhdr *eth_hdr;
	u32 i, ep_map = -1;
	u8 *datap;

	*map_no = 0;
	datap = skb->data;
	eth_hdr = (struct ethhdr *) (datap + sizeof(struct wmi_data_hdr));

	if (is_multicast_ether_addr(eth_hdr->h_dest))
		return ENDPOINT_2;

	for (i = 0; i < ar->node_num; i++) {
		if (memcmp(eth_hdr->h_dest, ar->node_map[i].mac_addr,
					ETH_ALEN) == 0) {
			*map_no = i + 1;
			ar->node_map[i].tx_pend++;
			return ar->node_map[i].ep_id;
		}

		if ((ep_map == -1) && !ar->node_map[i].tx_pend)
			ep_map = i;
	}

	if (ep_map == -1) {
		ep_map = ar->node_num;
		ar->node_num++;
		if (ar->node_num > MAX_NODE_NUM)
			return ENDPOINT_UNUSED;
	}

	memcpy(ar->node_map[ep_map].mac_addr, eth_hdr->h_dest, ETH_ALEN);

	for (i = ENDPOINT_2; i <= ENDPOINT_5; i++) {
		if (!ar->tx_pending[i]) {
			ar->node_map[ep_map].ep_id = i;
			break;
		}

		/*
		 * No free endpoint is available, start redistribution on
		 * the inuse endpoints.
		 */
		if (i == ENDPOINT_5) {
			ar->node_map[ep_map].ep_id = ar->next_ep_id;
			ar->next_ep_id++;
			if (ar->next_ep_id > ENDPOINT_5)
				ar->next_ep_id = ENDPOINT_2;
		}
	}

	*map_no = ep_map + 1;
	ar->node_map[ep_map].tx_pend++;

	return ar->node_map[ep_map].ep_id;
}

static bool ath6kl_process_uapsdq(struct ath6kl_sta *conn,
		struct ath6kl_vif *vif,
		struct sk_buff *skb,
		u32 *flags)
{
	struct ath6kl *ar = vif->ar;
	bool is_apsdq_empty = false;
	struct ethhdr *datap = (struct ethhdr *) skb->data;
	u8 up = 0, traffic_class, *ip_hdr;
	u16 ether_type;
	struct ath6kl_llc_snap_hdr *llc_hdr;
	struct sk_buff *skb_to_drop = NULL;

	if (conn->sta_flags & STA_PS_APSD_TRIGGER) {
		/*
		 * This tx is because of a uAPSD trigger, determine
		 * more and EOSP bit. Set EOSP if queue is empty
		 * or sufficient frames are delivered for this trigger.
		 */
		spin_lock_bh(&conn->psq_lock);
		if (!skb_queue_empty(&conn->apsdq))
			*flags |= WMI_DATA_HDR_FLAGS_MORE;
		else if (conn->sta_flags & STA_PS_APSD_EOSP)
			*flags |= WMI_DATA_HDR_FLAGS_EOSP;
		*flags |= WMI_DATA_HDR_FLAGS_UAPSD;
		spin_unlock_bh(&conn->psq_lock);
		return false;
	} else if (!conn->apsd_info)
		return false;

	if (test_bit(WMM_ENABLED, &vif->flags)) {
		ether_type = be16_to_cpu(datap->h_proto);
		if (is_ethertype(ether_type)) {
			/* packet is in DIX format  */
			ip_hdr = (u8 *)(datap + 1);
		} else {
			/* packet is in 802.3 format */
			llc_hdr = (struct ath6kl_llc_snap_hdr *)
				(datap + 1);
			ether_type = be16_to_cpu(llc_hdr->eth_type);
			ip_hdr = (u8 *)(llc_hdr + 1);
		}

		if (ether_type == IP_ETHERTYPE)
			up = ath6kl_wmi_determine_user_priority(
					ip_hdr, 0);
	}

	traffic_class = ath6kl_wmi_get_traffic_class(up);

	if ((conn->apsd_info & (1 << traffic_class)) == 0)
		return false;

	/* Queue the frames if the STA is sleeping */
	spin_lock_bh(&conn->psq_lock);
	if (skb_queue_len(&conn->apsdq) >= ar->tx_psq_threshold) {
		ath6kl_dbg(ATH6KL_DBG_WLAN_TX, "TX APSD queue is full\n");
		skb_to_drop = skb_dequeue(&conn->apsdq);
	}
	is_apsdq_empty = skb_queue_empty(&conn->apsdq);
	skb_queue_tail(&conn->apsdq, skb);
	spin_unlock_bh(&conn->psq_lock);

	/*
	 * If this is the first pkt getting queued
	 * for this STA, update the PVB for this STA
	 */
	if (is_apsdq_empty) {
		ath6kl_wmi_set_apsd_bfrd_traf(ar->wmi,
				vif->fw_vif_idx,
				conn->aid, 1, 0);
	}
	*flags |= WMI_DATA_HDR_FLAGS_UAPSD;

	if (skb_to_drop != NULL) {
		dev_kfree_skb(skb_to_drop);
		vif->net_stats.tx_dropped++;
		vif->net_stats.tx_aborted_errors++;
	}
	return true;
}

static bool ath6kl_process_psq(struct ath6kl_sta *conn,
		struct ath6kl_vif *vif,
		struct sk_buff *skb,
		u32 *flags)
{
	bool is_psq_empty = false;
	struct ath6kl *ar = vif->ar;
	struct sk_buff *skb_to_drop = NULL;

	if (conn->sta_flags & STA_PS_POLLED) {
		spin_lock_bh(&conn->psq_lock);
		if (!skb_queue_empty(&conn->psq))
			*flags |= WMI_DATA_HDR_FLAGS_MORE;
		spin_unlock_bh(&conn->psq_lock);
		return false;
	}

	/* Queue the frames if the STA is sleeping */
	spin_lock_bh(&conn->psq_lock);
	if (skb_queue_len(&conn->psq) >= ar->tx_psq_threshold) {
		ath6kl_dbg(ATH6KL_DBG_WLAN_TX, "TX PS queue is full\n");
		skb_to_drop = skb_dequeue(&conn->psq);
	}

	is_psq_empty = skb_queue_empty(&conn->psq);
	skb_queue_tail(&conn->psq, skb);
	spin_unlock_bh(&conn->psq_lock);

	/*
	 * If this is the first pkt getting queued
	 * for this STA, update the PVB for this
	 * STA.
	 */
	if (is_psq_empty)
		ath6kl_wmi_set_pvb_cmd(ar->wmi,
				vif->fw_vif_idx,
				conn->aid, 1);

	if (skb_to_drop != NULL) {
		dev_kfree_skb(skb_to_drop);
		vif->net_stats.tx_dropped++;
		vif->net_stats.tx_aborted_errors++;
	}
	return true;
}

static bool ath6kl_powersave_ap(struct ath6kl_vif *vif, struct sk_buff *skb,
		u32 *flags)
{
	struct ethhdr *datap = (struct ethhdr *) skb->data;
	struct ath6kl_sta *conn = NULL;
	bool ps_queued = false;
	struct ath6kl *ar = vif->ar;
	struct sk_buff *skb_to_drop = NULL;

	if (is_multicast_ether_addr(datap->h_dest)) {
		u8 ctr = 0;
		bool q_mcast = false;

		for (ctr = 0; ctr < NUM_CONN; ctr++) {
			if (ar->sta_list[ctr].sta_flags & STA_PS_SLEEP) {
				q_mcast = true;
				break;
			}
		}

		if (q_mcast) {
			/*
			 * If this transmit is not because of a Dtim Expiry
			 * q it.
			 */
			if (!test_bit(DTIM_EXPIRED, &vif->flags)) {
				bool is_mcastq_empty = false;

				spin_lock_bh(&ar->mcastpsq_lock);
				if (skb_queue_len(&ar->mcastpsq) >=
							ar->tx_psq_threshold) {
					ath6kl_dbg(ATH6KL_DBG_WLAN_TX,
						"TX Mcast PS queue is full\n");
					skb_to_drop =
						skb_dequeue(&ar->mcastpsq);
				}
				is_mcastq_empty =
					skb_queue_empty(&ar->mcastpsq);
				skb_queue_tail(&ar->mcastpsq, skb);
				spin_unlock_bh(&ar->mcastpsq_lock);

				/*
				 * If this is the first Mcast pkt getting
				 * queued indicate to the target to set the
				 * BitmapControl LSB of the TIM IE.
				 */
				if (is_mcastq_empty)
					ath6kl_wmi_set_pvb_cmd(ar->wmi,
							vif->fw_vif_idx,
							MCAST_AID, 1);

				ps_queued = true;
				if (skb_to_drop != NULL) {
					dev_kfree_skb(skb_to_drop);
					vif->net_stats.tx_dropped++;
					vif->net_stats.tx_aborted_errors++;
				}
			} else {
				/*
				 * This transmit is because of Dtim expiry.
				 * Determine if MoreData bit has to be set.
				 */
				spin_lock_bh(&ar->mcastpsq_lock);
				if (!skb_queue_empty(&ar->mcastpsq))
					*flags |= WMI_DATA_HDR_FLAGS_MORE;
				spin_unlock_bh(&ar->mcastpsq_lock);
			}
		}
	} else {
		conn = ath6kl_find_sta(vif, datap->h_dest, false);
		if (!conn) {
			dev_kfree_skb(skb);

			/* Inform the caller that the skb is consumed */
			return true;
		}

		if (conn->sta_flags & STA_PS_SLEEP) {
			ps_queued = ath6kl_process_uapsdq(conn,
					vif, skb, flags);
			if (!(*flags & WMI_DATA_HDR_FLAGS_UAPSD))
				ps_queued = ath6kl_process_psq(conn,
						vif, skb, flags);
		}
	}
	return ps_queued;
}

/* Tx functions */

int ath6kl_control_tx(void *devt, struct sk_buff *skb,
		enum htc_endpoint_id eid)
{
	struct ath6kl *ar = devt;
	int status = 0;
	struct ath6kl_cookie *cookie = NULL;
	struct ath6kl_vif *vif;

	if (ar->fw_recovery->state == ATH6KL_FW_RECOVERY_INPROGRESS) {
		dev_kfree_skb(skb);
		return -EACCES;
	}

	spin_lock_bh(&ar->lock);

	ath6kl_dbg(ATH6KL_DBG_WLAN_TX,
			"%s: skb=0x%p, len=0x%x eid =%d\n", __func__,
			skb, skb->len, eid);

	if (test_bit(WMI_CTRL_EP_FULL, &ar->flag) && (eid == ar->ctrl_ep)) {
		/*
		 * Control endpoint is full, don't allocate resources, we
		 * are just going to drop this packet.
		 */
		cookie = NULL;
		ath6kl_err("wmi ctrl ep full, dropping pkt : 0x%p, len:%d\n",
				skb, skb->len);
	} else {
		vif = ath6kl_get_vif_by_index(ar, 0);
		cookie = ath6kl_alloc_cookie(ar, vif, eid);
	}

	if (cookie == NULL) {
		spin_unlock_bh(&ar->lock);
		status = -ENOMEM;
		goto fail_ctrl_tx;
	}

	ar->tx_pending[eid]++;

	if (eid != ar->ctrl_ep)
		ar->total_tx_data_pend++;

	spin_unlock_bh(&ar->lock);

	cookie->skb = skb;
	cookie->map_no = 0;
	set_htc_pkt_info(&cookie->htc_pkt, cookie, skb->data, skb->len,
			eid, ATH6KL_CONTROL_PKT_TAG);
	cookie->htc_pkt.skb = skb;

	/*
	 * This interface is asynchronous, if there is an error, cleanup
	 * will happen in the TX completion callback.
	 */
	ath6kl_htc_tx(ar->htc_target, &cookie->htc_pkt);

	return 0;

fail_ctrl_tx:
	ath6kl_err("ath6kl_control_tx failed !: %d", status);
	dev_kfree_skb(skb);
	return status;
}

void ath6kl_mcc_flowctrl_conn_list_cleanup(struct ath6kl *ar)
{
	int i;
	struct ath6kl_fw_conn_list *pcon;
	struct htc_packet *packet, *tmp_pkt;
	struct list_head container;

	INIT_LIST_HEAD(&container);

	for (i = 0; i < NUM_CONN; i++) {
		pcon = &ar->mcc_flowctrl_ctx->fw_conn_list[i];

		spin_lock_bh(&ar->lock);

		if (!list_empty(&pcon->re_queue)) {
			list_for_each_entry_safe(packet, tmp_pkt, &pcon->re_queue,
						list) {
				list_del(&packet->list);
				packet->status = 0;
				list_add_tail(&packet->list, &container);
			}
		}

		if (!list_empty(&pcon->conn_queue)) {
			list_for_each_entry_safe(packet, tmp_pkt, &pcon->conn_queue,
						list) {
				list_del(&packet->list);
				packet->status = 0;
				list_add_tail(&packet->list, &container);
			}
		}

		spin_unlock_bh(&ar->lock);
	}

	ath6kl_tx_complete(ar->htc_target, &container);
}

int ath6kl_data_tx(struct sk_buff *skb, struct net_device *dev)
{
	struct ath6kl *ar = ath6kl_priv(dev);
	struct ath6kl_cookie *cookie = NULL;
	enum htc_endpoint_id eid = ENDPOINT_UNUSED;
	struct ath6kl_vif *vif = netdev_priv(dev);
	u32 map_no = 0;
	u16 htc_tag = ATH6KL_DATA_PKT_TAG;
	u8 ac = 99 ; /* initialize to unmapped ac */
	bool chk_adhoc_ps_mapping = false;
	int ret;
	struct wmi_tx_meta_v2 meta_v2;
	void *meta;
	u8 csum_start = 0, csum_dest = 0, csum = skb->ip_summed;
	u8 meta_ver = 0;
	u32 flags = 0;

	ath6kl_dbg(ATH6KL_DBG_WLAN_TX,
			"%s: skb=0x%p, data=0x%p, len=0x%x\n", __func__,
			skb, skb->data, skb->len);

	if(skb_cloned(skb)) {
		skb = skb_unshare(skb, GFP_ATOMIC);
		if(skb == NULL) {
			ath6kl_dbg(ATH6KL_DBG_WLAN_TX,
					"skb cannot be shared\n");
			goto fail_tx;
		}
	}

	/* If target is not associated */
	if (!test_bit(CONNECTED, &vif->flags) &&
			!test_bit(TESTMODE_EPPING, &ar->flag))
		goto fail_tx;

	if (!test_bit(WMI_READY, &ar->flag) &&
			!test_bit(TESTMODE_EPPING, &ar->flag))
		goto fail_tx;

	/* AP mode Power saving processing */
	if (vif->nw_type == AP_NETWORK) {
		if (ath6kl_powersave_ap(vif, skb, &flags))
			return 0;
	}

	if (test_bit(WMI_ENABLED, &ar->flag)) {
		if ((dev->features & NETIF_F_IP_CSUM) &&
				(csum == CHECKSUM_PARTIAL)) {
			csum_start = skb->csum_start -
				(skb_network_header(skb) - skb->head) +
				sizeof(struct ath6kl_llc_snap_hdr);
			csum_dest = skb->csum_offset + csum_start;
		}

		if (skb_headroom(skb) < dev->needed_headroom) {
			struct sk_buff *tmp_skb = skb;

			skb = skb_realloc_headroom(skb, dev->needed_headroom);
			kfree_skb(tmp_skb);
			if (skb == NULL) {
				vif->net_stats.tx_dropped++;
				return 0;
			}
		}

		if (ath6kl_wmi_dix_2_dot3(ar->wmi, skb)) {
			ath6kl_err("ath6kl_wmi_dix_2_dot3 failed\n");
			goto fail_tx;
		}

		if ((dev->features & NETIF_F_IP_CSUM) &&
				(csum == CHECKSUM_PARTIAL)) {
			meta_v2.csum_start = csum_start;
			meta_v2.csum_dest = csum_dest;

			/* instruct target to calculate checksum */
			meta_v2.csum_flags = WMI_META_V2_FLAG_CSUM_OFFLOAD;
			meta_ver = WMI_META_VERSION_2;
			meta = &meta_v2;
		} else {
			meta_ver = 0;
			meta = NULL;
		}

		ret = ath6kl_wmi_data_hdr_add(ar->wmi, skb,
				DATA_MSGTYPE, flags, 0,
				meta_ver,
				meta, vif->fw_vif_idx);

		if (ret) {
			ath6kl_warn("failed to add wmi data header:%d\n"
					, ret);
			goto fail_tx;
		}

		if ((vif->nw_type == ADHOC_NETWORK) &&
			ar->ibss_ps_enable && test_bit(CONNECTED, &vif->flags))
			chk_adhoc_ps_mapping = true;
		else {
			/* get the stream mapping */
			ret = ath6kl_wmi_implicit_create_pstream(ar->wmi,
				vif->fw_vif_idx, skb,
				0, test_bit(WMM_ENABLED, &vif->flags), &ac);
			if (ret)
				goto fail_tx;
		}
	} else if (test_bit(TESTMODE_EPPING, &ar->flag)) {
		struct epping_header    *epping_hdr;

		epping_hdr = (struct epping_header *)skb->data;

		if (IS_EPPING_PACKET(epping_hdr)) {
			ac = epping_hdr->stream_no_h;

			/* some EPPING packets cannot be dropped no matter what
			 * access class it was sent on. Change the packet tag
			 * to guarantee it will not get dropped */
			if (IS_EPING_PACKET_NO_DROP(epping_hdr)) {
				htc_tag = ATH6KL_CONTROL_PKT_TAG;
			}

			if (ac == HCI_TRANSPORT_STREAM_NUM) {
				goto fail_tx;
			} else {
				/* The payload of the frame is 32-bit aligned
				 * and thus the addition of the HTC header will
				 * mis-align the start of the HTC frame,
				 * the padding will be stripped off in the
				 * target */
				if (EPPING_ALIGNMENT_PAD > 0) {
					skb_push(skb, EPPING_ALIGNMENT_PAD);
				}
			}
		} else {
			/* In loopback mode, drop non-loopback packet */
			goto fail_tx;
		}

	} else
		goto fail_tx;

	spin_lock_bh(&ar->lock);

	if (chk_adhoc_ps_mapping)
		eid = ath6kl_ibss_map_epid(skb, dev, &map_no);
	else
		eid = ar->ac2ep_map[ac];

	if (eid == 0 || eid == ENDPOINT_UNUSED) {
		if ((ac == WMM_NUM_AC) && test_bit(TESTMODE_EPPING, &ar->flag)){
			/* for epping testing, the last AC maps to the control
			 * endpoint */
			eid = ar->ctrl_ep;
		} else {
			ath6kl_err("eid %d is not mapped!\n", eid);
			spin_unlock_bh(&ar->lock);
			goto fail_tx;
		}
	}

	if (eid <= ENDPOINT_UNUSED || eid >= ENDPOINT_MAX) {
		ath6kl_err("Invalid eid %d!\n", eid);
		goto fail_tx;
	}

	/* allocate resource for this packet */
	cookie = ath6kl_alloc_cookie(ar, vif, eid);

	if (!cookie) {
		spin_unlock_bh(&ar->lock);
		goto fail_tx;
	}

	/* update counts while the lock is held */
	ar->tx_pending[eid]++;
	ar->total_tx_data_pend++;

	spin_unlock_bh(&ar->lock);

	if (!IS_ALIGNED((unsigned long) skb->data - HTC_HDR_LENGTH, 4) &&
			skb_cloned(skb)) {
		/*
		 * We will touch (move the buffer data to align it. Since the
		 * skb buffer is cloned and not only the header is changed, we
		 * have to copy it to allow the changes. Since we are copying
		 * the data here, we may as well align it by reserving suitable
		 * headroom to avoid the memmove in ath6kl_htc_tx_buf_align().
		 */
		struct sk_buff *nskb;

		nskb = skb_copy_expand(skb, HTC_HDR_LENGTH, 0, GFP_ATOMIC);
		if (nskb == NULL)
			goto fail_skbexp;
		kfree_skb(skb);
		skb = nskb;
	}

	cookie->skb = skb;
	cookie->map_no = map_no;
	set_htc_pkt_info(&cookie->htc_pkt, cookie, skb->data, skb->len,
			eid, htc_tag);
	cookie->htc_pkt.skb = skb;

	ath6kl_dbg_dump(ATH6KL_DBG_RAW_BYTES, __func__, "tx ",
			skb->data, skb->len);

	/* MCC Flowctrl */
	if ((ar->is_mcc_enabled == true) &&
		(ath6kl_debug_quirks(ar, ATH6KL_MODULE_MCC_FLOWCTRL))) {
		enum htc_send_queue_result queue_result;
		u8 conn_id = ath6kl_mcc_flowctrl_get_conn_id(vif, skb);

		if (conn_id >= NUM_CONN)
			goto fail_skbexp;

		cookie->htc_pkt.connid = conn_id;
		cookie->htc_pkt.recycle_count = 0;

		queue_result = ath6kl_mcc_flowctrl_tx_schedule_pkt(ar, (void *)cookie);
		if (queue_result == HTC_SEND_QUEUE_OK) { /* Queue it */
			ar->mcc_flowctrl_ctx->fw_conn_list[conn_id].mcc_stats.sche_tx_queued += 1;
			return 0;
		} else if (queue_result == HTC_SEND_QUEUE_DROP) {
			/* Error, drop it. */
			ar->mcc_flowctrl_ctx->fw_conn_list[conn_id].mcc_stats.tx_sched_dropped += 1;
			goto fail_skbexp;
		}
	}

	/*
	 * HTC interface is asynchronous, if this fails, cleanup will
	 * happen in the ath6kl_tx_complete callback.
	 */
	ath6kl_htc_tx(ar->htc_target, &cookie->htc_pkt);

	return 0;

fail_skbexp:
	/* Release the cookie */
	ath6kl_free_cookie(ar, vif, cookie);

fail_tx:
	dev_kfree_skb(skb);

	vif->net_stats.tx_dropped++;
	vif->net_stats.tx_aborted_errors++;

	return 0;
}

/* indicate tx activity or inactivity on a WMI stream */
void ath6kl_indicate_tx_activity(void *devt, u8 traffic_class, bool active)
{
	struct ath6kl *ar = devt;
	enum htc_endpoint_id eid;
	int i;

	eid = ar->ac2ep_map[traffic_class];

	if (!test_bit(WMI_ENABLED, &ar->flag))
		goto notify_htc;

	spin_lock_bh(&ar->lock);

	ar->ac_stream_active[traffic_class] = active;

	if (active) {
		/*
		 * Keep track of the active stream with the highest
		 * priority.
		 */
		if (ar->ac_stream_pri_map[traffic_class] >
				ar->hiac_stream_active_pri)
			/* set the new highest active priority */
			ar->hiac_stream_active_pri =
				ar->ac_stream_pri_map[traffic_class];

	} else {
		/*
		 * We may have to search for the next active stream
		 * that is the highest priority.
		 */
		if (ar->hiac_stream_active_pri ==
				ar->ac_stream_pri_map[traffic_class]) {
			/*
			 * The highest priority stream just went inactive
			 * reset and search for the "next" highest "active"
			 * priority stream.
			 */
			ar->hiac_stream_active_pri = 0;

			for (i = 0; i < WMM_NUM_AC; i++) {
				if (ar->ac_stream_active[i] &&
						(ar->ac_stream_pri_map[i] >
						 ar->hiac_stream_active_pri))
					/*
					 * Set the new highest active
					 * priority.
					 */
					ar->hiac_stream_active_pri =
						ar->ac_stream_pri_map[i];
			}
		}
	}

	spin_unlock_bh(&ar->lock);

notify_htc:
	/* notify HTC, this may cause credit distribution changes */
	ath6kl_htc_activity_changed(ar->htc_target, eid, active);
}

enum htc_send_full_action ath6kl_tx_queue_full(struct htc_target *target,
		struct htc_packet *packet)
{
	struct ath6kl *ar = target->dev->ar;
	struct ath6kl_vif *vif;
	enum htc_endpoint_id endpoint = packet->endpoint;
	enum htc_send_full_action action = HTC_SEND_FULL_KEEP;

	if (test_bit(TESTMODE_EPPING, &ar->flag)) {
		int ac;

		if (packet->info.tx.tag == ATH6KL_CONTROL_PKT_TAG) {
			/* don't drop special control packets */
			return HTC_SEND_FULL_KEEP;
		}

		ac = ar->ep2ac_map[endpoint];

		/* for endpoint ping testing drop Best Effort and Background
		 * if any of the higher priority traffic is active */
		if ((ar->ac_stream_active[WMM_AC_VO] ||
					ar->ac_stream_active[WMM_AC_BE])
				&& ((ac == WMM_AC_BE) || (ac == WMM_AC_BK))) {
			return HTC_SEND_FULL_DROP;
		} else {
			spin_lock_bh(&ar->list_lock);
			list_for_each_entry(vif, &ar->vif_list, list) {
				spin_unlock_bh(&ar->list_lock);

				/* keep but stop the netqueues */
				spin_lock_bh(&vif->if_lock);
				set_bit(NETQ_STOPPED, &vif->flags);
				spin_unlock_bh(&vif->if_lock);
				netif_stop_queue(vif->ndev);
			}
			return HTC_SEND_FULL_KEEP;
		}
	}

	if (endpoint == ar->ctrl_ep) {
		/*
		 * Under normal WMI if this is getting full, then something
		 * is running rampant the host should not be exhausting the
		 * WMI queue with too many commands the only exception to
		 * this is during testing using endpointping.
		 */
		set_bit(WMI_CTRL_EP_FULL, &ar->flag);
		ath6kl_err("wmi ctrl ep is full\n");
		ath6kl_recovery_err_notify(ar, ATH6KL_FW_EP_FULL);
		return action;
	}

	if (packet->info.tx.tag == ATH6KL_CONTROL_PKT_TAG)
		return action;

	/*
	 * The last MAX_HI_COOKIE_NUM "batch" of cookies are reserved for
	 * the highest active stream.
	 */
	if (ar->ac_stream_pri_map[ar->ep2ac_map[endpoint]] <
			ar->hiac_stream_active_pri &&
			ar->cookie_count <=
			target->endpoint[endpoint].tx_drop_packet_threshold)
		/*
		 * Give preference to the highest priority stream by
		 * dropping the packets which overflowed.
		 */
		action = HTC_SEND_FULL_DROP;

	/* FIXME: Locking */
	spin_lock_bh(&ar->list_lock);
	list_for_each_entry(vif, &ar->vif_list, list) {
		spin_unlock_bh(&ar->list_lock);
		if (vif->nw_type == ADHOC_NETWORK ||
				action != HTC_SEND_FULL_DROP) {
			if ((!test_bit(NETQ_STOPPED, &vif->flags)) &&
				(vif->cookie_used > NETIF_STOP_THOLD)) {
				set_bit(NETQ_STOPPED, &vif->flags);
				netif_stop_queue(vif->ndev);
			}
		}
		spin_lock_bh(&ar->list_lock);
	}
	spin_unlock_bh(&ar->list_lock);

	return action;
}

/* TODO this needs to be looked at */
static void ath6kl_tx_clear_node_map(struct ath6kl_vif *vif,
		enum htc_endpoint_id eid, u32 map_no)
{
	struct ath6kl *ar = vif->ar;
	u32 i;

	if (vif->nw_type != ADHOC_NETWORK)
		return;

	if (!ar->ibss_ps_enable)
		return;

	if (eid == ar->ctrl_ep)
		return;

	if (map_no == 0)
		return;

	map_no--;
	ar->node_map[map_no].tx_pend--;

	if (ar->node_map[map_no].tx_pend)
		return;

	if (map_no != (ar->node_num - 1))
		return;

	for (i = ar->node_num; i > 0; i--) {
		if (ar->node_map[i - 1].tx_pend)
			break;

		memset(&ar->node_map[i - 1], 0,
				sizeof(struct ath6kl_node_mapping));
		ar->node_num--;
	}
}

void ath6kl_tx_complete(struct htc_target *target,
		struct list_head *packet_queue)
{
	struct ath6kl *ar = target->dev->ar;
	struct sk_buff_head skb_queue;
	struct htc_packet *packet;
	struct sk_buff *skb;
	struct ath6kl_cookie *ath6kl_cookie;
	u32 map_no = 0;
	int status;
	enum htc_endpoint_id eid;
	bool wake_event = false;
	bool flushing[ATH6KL_VIF_MAX] = {false};
	u8 if_idx;
	struct ath6kl_vif *vif = NULL;
	u16 tag;

	skb_queue_head_init(&skb_queue);

	/* lock the driver as we update internal state */
	spin_lock_bh(&ar->lock);

	/* reap completed packets */
	while (!list_empty(packet_queue)) {

		packet = list_first_entry(packet_queue, struct htc_packet,
				list);
		list_del(&packet->list);

		ath6kl_cookie = (struct ath6kl_cookie *)packet->pkt_cntxt;
		if (!ath6kl_cookie)
			goto fatal;

		status = packet->status;
		skb = ath6kl_cookie->skb;
		eid = packet->endpoint;
		map_no = ath6kl_cookie->map_no;
                tag = packet->info.tx.tag;
		if (!skb || !skb->data)
			goto fatal;

		__skb_queue_tail(&skb_queue, skb);

		if (!status && (packet->act_len != skb->len))
			goto fatal;

		if (eid <= ENDPOINT_UNUSED || eid >= ENDPOINT_MAX)
			goto fatal;

		ar->tx_pending[eid]--;

		if (!test_bit(TESTMODE_EPPING, &ar->flag)) {
			if (eid != ar->ctrl_ep)
				ar->total_tx_data_pend--;

			if (eid == ar->ctrl_ep) {
				if (test_bit(WMI_CTRL_EP_FULL, &ar->flag))
					clear_bit(WMI_CTRL_EP_FULL, &ar->flag);

				if (ar->tx_pending[eid] == 0)
					wake_event = true;
			}

			if (eid == ar->ctrl_ep) {
				if_idx = wmi_cmd_hdr_get_if_idx(
						(struct wmi_cmd_hdr *) packet->buf);
			} else {
				if_idx = wmi_data_hdr_get_if_idx(
						(struct wmi_data_hdr *) packet->buf);
			}
		} else {
			/* The epping packet is not coming from wmi, skip the index
			 * retrival, epping assume using the first if_idx anyway
			 */
			if_idx = 0;
		}

		vif = ath6kl_get_vif_by_index(ar, if_idx);

		if (!vif || if_idx >= ATH6KL_VIF_MAX) {
			ath6kl_free_cookie(ar, vif, ath6kl_cookie);
			continue;
		}

		if (status) {
			if (status == -ECANCELED)
				/* a packet was flushed  */
				flushing[if_idx] = true;

			if (status != -ENOSPC && status != -ECANCELED) {
				if (status == -ENOMEM) {
					if(!(vif->net_stats.tx_errors %
						ATH6KL_DBGPRNT_FREQ)) {
						ath6kl_warn("tx complete error"
							": -ENOMEM\n");
					}
				}
				else {
					ath6kl_warn("tx complete error: %d\n",
						status);
				}
			}

			vif->net_stats.tx_errors++;

			ath6kl_dbg(ATH6KL_DBG_WLAN_TX,
				"%s: skb=0x%p data=0x%p len=0x%x eid=%d %s\n",
				__func__, skb, packet->buf, packet->act_len,
				eid, "error!");
		} else {
			ath6kl_dbg(ATH6KL_DBG_WLAN_TX,
				"%s: skb=0x%p data=0x%p len=0x%x eid=%d %s\n",
				__func__, skb, packet->buf, packet->act_len,
				eid, "OK");

			flushing[if_idx] = false;
			vif->net_stats.tx_packets++;
			vif->net_stats.tx_bytes += skb->len;
		}

		ath6kl_tx_clear_node_map(vif, eid, map_no);

		if (eid == ar->ctrl_ep || tag == ATH6KL_CONTROL_PKT_TAG)
			vif = ath6kl_get_vif_by_index(ar, 0);
		ath6kl_free_cookie(ar, vif, ath6kl_cookie);

#ifdef ATH6KL_IF_NEEDED_LATER
		if (test_bit(NETQ_STOPPED, &vif->flags))
			clear_bit(NETQ_STOPPED, &vif->flags);
#endif
	}

	spin_unlock_bh(&ar->lock);

	__skb_queue_purge(&skb_queue);

	/* FIXME: Locking */
	spin_lock_bh(&ar->list_lock);
	list_for_each_entry(vif, &ar->vif_list, list) {
		if ((test_bit(CONNECTED, &vif->flags) ||
					test_bit(TESTMODE_EPPING, &ar->flag)) &&
				!flushing[vif->fw_vif_idx]) {
			spin_unlock_bh(&ar->list_lock);
			if ((vif->intra_bss_data_cnt > 0) ||
				(vif->cookie_used < NETIF_WAKE_THOLD)) {
				if (test_bit(NETQ_STOPPED, &vif->flags)) {
					clear_bit(NETQ_STOPPED, &vif->flags);
					netif_wake_queue(vif->ndev);
				}
			}
			if (vif->intra_bss_data_cnt > 0)
				vif->intra_bss_data_cnt--;
			spin_lock_bh(&ar->list_lock);
		}
	}
	spin_unlock_bh(&ar->list_lock);

	if (wake_event)
		wake_up(&ar->event_wq);

	return;

fatal:
	WARN_ON(1);
	spin_unlock_bh(&ar->lock);
	return;
}

void ath6kl_tx_data_cleanup(struct ath6kl *ar)
{
	int i;

	/* flush all the data (non-control) streams */
	for (i = 0; i < WMM_NUM_AC; i++)
		ath6kl_htc_flush_txep(ar->htc_target, ar->ac2ep_map[i],
				ATH6KL_DATA_PKT_TAG);
	ath6kl_mcc_flowctrl_conn_list_cleanup(ar);
}

#ifdef CONFIG_ATH6KL_BAM2BAM
static void ath6kl_deliver_ampdu_frames_to_ipa(struct ath6kl *ar,
		struct net_device *dev, struct sk_buff *skb)
{
	int status;
	struct ath6kl_vif *vif = netdev_priv(dev);
	struct ethhdr *datap = NULL;
	struct sk_buff *skb1 = NULL;
	struct ath6kl_sta *conn = NULL;
	if (!skb)
		return;
	datap = (struct ethhdr *) skb->data;
	if (vif->nw_type == AP_NETWORK) {
		if (datap)
			conn = ath6kl_find_sta(vif, datap->h_dest,
					ar->inter_bss);
		/*
		* Search for a connected STA with dstMac
		* as the Mac address. If found send the
		* frame to it on the air else send the
		* frame up the stack.
		*/
		if (conn) {
			if (vif->intra_bss) {
				skb1 = skb;
				skb = NULL;
			} else {
				if(vif == conn->vif) {
					dev_kfree_skb(skb);
					skb = NULL;
				} else {
					skb1 = skb;
					skb = NULL;
				}
			}
		} else if (!is_broadcast_ether_addr(datap->h_dest) &&
				!is_multicast_ether_addr(datap->h_dest) &&
				(NULL != ath6kl_find_sta(vif, datap->h_dest,
				true))) {
			dev_kfree_skb(skb);
			skb = NULL;
		}

		if (skb1 && conn) {
			if (vif->intra_bss_data_cnt < vif->cookie_used)
				vif->intra_bss_data_cnt++;
			ath6kl_data_tx(skb1, conn->vif->ndev);
		}

		if (skb == NULL)
			/* nothing to deliver up the stack */
			return;
	}

	skb->dev = dev;

	if (!(skb->dev->flags & IFF_UP)) {
		dev_kfree_skb(skb);
		return;
	}

	skb->protocol = eth_type_trans(skb, skb->dev);

	status = ath6kl_usb_data_send_to_sysbam_pipe(ar, skb);
	if (status) {
		ath6kl_dbg(ATH6KL_DBG_OOO,
			"BAM-CM: Failed to send data over sysbam pipe %s\n",
		       			__func__);
	}
}
#endif
/* Rx functions */

static void ath6kl_deliver_frames_to_nw_stack(struct net_device *dev,
		struct sk_buff *skb)
{
	struct ath6kl *ar = ath6kl_priv(dev);
	struct ath6kl_vif *vif = netdev_priv(dev);
	struct ethhdr *datap = NULL;
	struct sk_buff *skb1 = NULL;
	struct ath6kl_sta *conn = NULL;
	if (!skb)
		return;
	datap = (struct ethhdr *) skb->data;
	if (vif->nw_type == AP_NETWORK) {
		if (datap)
			conn = ath6kl_find_sta(vif, datap->h_dest,
					ar->inter_bss);
		/*
		* Search for a connected STA with dstMac
		* as the Mac address. If found send the
		* frame to it on the air else send the
		* frame up the stack.
		*/
		if (conn) {
			if (vif->intra_bss) {
				skb1 = skb;
				skb = NULL;
			} else {
				if(vif == conn->vif) {
					dev_kfree_skb(skb);
					skb = NULL;
				} else {
					skb1 = skb;
					skb = NULL;
				}
			}
		} else if (!is_broadcast_ether_addr(datap->h_dest) &&
				!is_multicast_ether_addr(datap->h_dest) &&
				(NULL != ath6kl_find_sta(vif, datap->h_dest,
					true))) {
			dev_kfree_skb(skb);
			skb = NULL;
		}

		if (skb1 && conn && conn->vif) {
			if (vif->intra_bss_data_cnt < vif->cookie_used)
				vif->intra_bss_data_cnt++;
			ath6kl_data_tx(skb1, conn->vif->ndev);
		}

		if (skb == NULL)
			/* nothing to deliver up the stack */
			return;
	}

	skb->dev = dev;

	if (!(skb->dev->flags & IFF_UP)) {
		dev_kfree_skb(skb);
		return;
	}

	skb->protocol = eth_type_trans(skb, skb->dev);

	netif_rx_ni(skb);
}

static void ath6kl_alloc_netbufs(struct sk_buff_head *q, u16 num)
{
	struct sk_buff *skb;

	while (num) {
		skb = ath6kl_buf_alloc(ATH6KL_BUFFER_SIZE);
		if (!skb) {
			ath6kl_err("netbuf allocation failed\n");
			return;
		}
		skb_queue_tail(q, skb);
		num--;
	}
}

void ath6kl_rx_refill(struct htc_target *target, enum htc_endpoint_id endpoint)
{
	struct ath6kl *ar = target->dev->ar;
	struct sk_buff *skb;
	int rx_buf;
	int n_buf_refill;
	struct htc_packet *packet;
	struct list_head queue;

	n_buf_refill = ATH6KL_MAX_RX_BUFFERS -
		ath6kl_htc_get_rxbuf_num(ar->htc_target, endpoint);

	if (n_buf_refill <= 0)
		return;

	INIT_LIST_HEAD(&queue);

	ath6kl_dbg(ATH6KL_DBG_WLAN_RX,
			"%s: providing htc with %d buffers at eid=%d\n",
			__func__, n_buf_refill, endpoint);

	for (rx_buf = 0; rx_buf < n_buf_refill; rx_buf++) {
		skb = ath6kl_buf_alloc(ATH6KL_BUFFER_SIZE);
		if (!skb)
			break;

		packet = (struct htc_packet *) skb->head;
		if (!IS_ALIGNED((unsigned long) skb->data, 4)) {
			size_t len = skb_headlen(skb);
			skb->data = PTR_ALIGN(skb->data - 4, 4);
			skb_set_tail_pointer(skb, len);
		}
		set_htc_rxpkt_info(packet, skb, skb->data,
				ATH6KL_BUFFER_SIZE, endpoint);
		packet->skb = skb;
		list_add_tail(&packet->list, &queue);
	}

	if (!list_empty(&queue))
		ath6kl_htc_add_rxbuf_multiple(ar->htc_target, &queue);
}

void ath6kl_refill_amsdu_rxbufs(struct ath6kl *ar, int count)
{
	struct htc_packet *packet;
	struct sk_buff *skb;

	while (count) {
		skb = ath6kl_buf_alloc(ATH6KL_AMSDU_BUFFER_SIZE);
		if (!skb)
			return;

		packet = (struct htc_packet *) skb->head;
		if (!IS_ALIGNED((unsigned long) skb->data, 4)) {
			size_t len = skb_headlen(skb);
			skb->data = PTR_ALIGN(skb->data - 4, 4);
			skb_set_tail_pointer(skb, len);
		}
		set_htc_rxpkt_info(packet, skb, skb->data,
				ATH6KL_AMSDU_BUFFER_SIZE, 0);
		packet->skb = skb;

		spin_lock_bh(&ar->lock);
		list_add_tail(&packet->list, &ar->amsdu_rx_buffer_queue);
		spin_unlock_bh(&ar->lock);
		count--;
	}
}

/*
 * Callback to allocate a receive buffer for a pending packet. We use a
 * pre-allocated list of buffers of maximum AMSDU size (4K).
 */
struct htc_packet *ath6kl_alloc_amsdu_rxbuf(struct htc_target *target,
		enum htc_endpoint_id endpoint,
		int len)
{
	struct ath6kl *ar = target->dev->ar;
	struct htc_packet *packet = NULL;
	struct list_head *pkt_pos;
	int refill_cnt = 0, depth = 0;

	ath6kl_dbg(ATH6KL_DBG_WLAN_RX, "%s: eid=%d, len:%d\n",
			__func__, endpoint, len);

	if ((len <= ATH6KL_BUFFER_SIZE) ||
			(len > ATH6KL_AMSDU_BUFFER_SIZE))
		return NULL;

	spin_lock_bh(&ar->lock);

	if (list_empty(&ar->amsdu_rx_buffer_queue)) {
		spin_unlock_bh(&ar->lock);
		refill_cnt = ATH6KL_MAX_AMSDU_RX_BUFFERS;
		goto refill_buf;
	}

	packet = list_first_entry(&ar->amsdu_rx_buffer_queue,
			struct htc_packet, list);
	list_del(&packet->list);
	list_for_each(pkt_pos, &ar->amsdu_rx_buffer_queue)
		depth++;

	refill_cnt = ATH6KL_MAX_AMSDU_RX_BUFFERS - depth;
	spin_unlock_bh(&ar->lock);

	/* set actual endpoint ID */
	packet->endpoint = endpoint;

refill_buf:
	if (refill_cnt >= ATH6KL_AMSDU_REFILL_THRESHOLD)
		ath6kl_refill_amsdu_rxbufs(ar, refill_cnt);

	return packet;
}

static struct sk_buff *ath6kl_aggr_get_free_skb(struct aggr_info *p_aggr)
{
	struct sk_buff *skb = NULL;

	if (skb_queue_len(&p_aggr->rx_amsdu_freeq) <
			(AGGR_NUM_OF_FREE_NETBUFS >> 2))
		ath6kl_alloc_netbufs(&p_aggr->rx_amsdu_freeq,
				AGGR_NUM_OF_FREE_NETBUFS);

	skb = skb_dequeue(&p_aggr->rx_amsdu_freeq);

	return skb;
}

static void aggr_slice_amsdu(struct aggr_info *p_aggr,
		struct rxtid *rxtid, struct sk_buff *skb)
{
	struct sk_buff *new_skb;
	struct ethhdr *hdr;
	u16 frame_8023_len, payload_8023_len, mac_hdr_len, amsdu_len;
	u8 *framep;
	mac_hdr_len = sizeof(struct ethhdr);
	framep = skb->data + mac_hdr_len;
	amsdu_len = skb->len - mac_hdr_len;

	while (amsdu_len > mac_hdr_len) {
		hdr = (struct ethhdr *) framep;
		payload_8023_len = ntohs(hdr->h_proto);

		if (payload_8023_len < MIN_MSDU_SUBFRAME_PAYLOAD_LEN ||
			payload_8023_len > MAX_MSDU_SUBFRAME_PAYLOAD_LEN) {
			ath6kl_err("802.3 AMSDU bound check failed. len %d\n",
					payload_8023_len);
			break;
		}

		frame_8023_len = payload_8023_len + mac_hdr_len;
		new_skb = ath6kl_aggr_get_free_skb(p_aggr);
		if (!new_skb) {
			ath6kl_err("no buffer available\n");
			break;
		}

		memcpy(new_skb->data, framep, frame_8023_len);
		skb_put(new_skb, frame_8023_len);
		if (ath6kl_wmi_dot3_2_dix(new_skb)) {
			ath6kl_err("dot3_2_dix error\n");
			dev_kfree_skb(new_skb);
			break;
		}

		skb_queue_tail(&rxtid->q, new_skb);

		/* Is this the last subframe within this aggregate ? */
		if ((amsdu_len - frame_8023_len) == 0)
			break;

		/* Add the length of A-MSDU subframe padding bytes -
		 * Round to nearest word.
		 */
		frame_8023_len = ALIGN(frame_8023_len, 4);

		framep += frame_8023_len;
		amsdu_len -= frame_8023_len;
	}

	dev_kfree_skb(skb);
}

#ifdef CONFIG_ATH6KL_BAM2BAM
static void aggr_deque_frms_bam2bam(struct aggr_info_conn *agg_conn, u8 tid,
		u16 seq_no, u8 order)
{
	struct sk_buff *skb;
	struct rxtid *rxtid;
	struct skb_hold_q *node;
	u16 idx, idx_end, seq_end, i, j, ext_end;
	struct rxtid_stats *stats;
	bool is_update = false;

	rxtid = &agg_conn->rx_tid[tid];
	stats = &agg_conn->stat[tid];
	if (!rxtid->aggr) {
		return;
	}

	spin_lock_bh(&rxtid->lock);
	if (order == 1) {
		idx = AGGR_WIN_IDX(seq_no, rxtid->hold_q_sz);
		rxtid->seq_next = seq_no;
		ath6kl_dbg(ATH6KL_DBG_OOO,
				"ooo:flush seq_no with order 1= %d\n", seq_no);
	} else {
		idx = AGGR_WIN_IDX(rxtid->seq_next, rxtid->hold_q_sz);
		ath6kl_dbg(ATH6KL_DBG_OOO,
				"ooo:flush seq_no = %d\n", seq_no);
	}
	/*
	 * idx_end is typically the last possible frame in the window,
	 * but changes to 'the' seq_no, when BAR comes. If seq_no
	 * is non-zero, we will go up to that and stop.
	 * Note: last seq no in current window will occupy the same
	 * index position as index that is just previous to start.
	 * An imp point : if win_sz is 7, for seq_no space of 4095,
	 * then, there would be holes when sequence wrap around occurs.
	 * Target should judiciously choose the win_sz, based on
	 * this condition. For 4095, (TID_WINDOW_SZ = 2 x win_sz
	 * 2, 4, 8, 16 win_sz works fine).
	 * We must deque from "idx" to "idx_end", including both.
	 */
	if (!order || order == 2) {
		seq_end = rxtid->seq_next;
	} else {
		seq_end = seq_no;
	}
	idx_end = AGGR_WIN_IDX(seq_end, rxtid->hold_q_sz);

	do {
		ext_end = (rxtid->seq_next + rxtid->hold_q_sz - 1) &
				ATH6KL_MAX_SEQ_NO;
		node = &rxtid->hold_q[idx];
		if (((order == 1) && (!node->skb))
			|| ((order == 2) && ((ext_end >= seq_no) && (!node->skb))))
			break;

		if (node->skb) {
			if (node->is_amsdu)
				aggr_slice_amsdu(agg_conn->aggr_info, rxtid,
						node->skb);
			else
				skb_queue_tail(&rxtid->q, node->skb);
			node->skb = NULL;
			ath6kl_dbg(ATH6KL_DBG_OOO,
				"ooo:Data removed = %d\n", rxtid->seq_next);
		} else
			stats->num_hole++;

		rxtid->seq_next = ATH6KL_NEXT_SEQ_NO(rxtid->seq_next);
		idx = AGGR_WIN_IDX(rxtid->seq_next, rxtid->hold_q_sz);
	} while (idx != idx_end);

	spin_unlock_bh(&rxtid->lock);

	stats->num_delivered += skb_queue_len(&rxtid->q);

	while ((skb = skb_dequeue(&rxtid->q)))
		ath6kl_deliver_ampdu_frames_to_ipa(agg_conn->vif->ar,
				agg_conn->dev, skb);

	spin_lock_bh(&rxtid->lock);
	if (!order) {
		rxtid->seq_next = 0;
	} else if (order == 1){
		idx_end = idx;
		do {
			node = &rxtid->hold_q[idx];
			if (node->skb) {
				ath6kl_dbg(ATH6KL_DBG_OOO,
					"ooo:Now seq_next = %d\n",
						rxtid->seq_next);
				is_update = true;
				break;
			}
			rxtid->seq_next = ATH6KL_NEXT_SEQ_NO(rxtid->seq_next);
			idx = AGGR_WIN_IDX(rxtid->seq_next, rxtid->hold_q_sz);
		} while (idx != idx_end);
		if (!is_update)
			rxtid->seq_next = 0;
	}
	spin_unlock_bh(&rxtid->lock);


	if (agg_conn->timer_scheduled) {
		agg_conn->timer_scheduled = false;
		for (i = 0; i < NUM_OF_TIDS; i++) {
			rxtid = &agg_conn->rx_tid[i];

			if (rxtid->aggr && rxtid->hold_q) {
				spin_lock_bh(&rxtid->lock);
				for (j = 0; j < rxtid->hold_q_sz; j++) {
					if (rxtid->hold_q[j].skb) {
						agg_conn->timer_scheduled = true;
						rxtid->timer_mon = true;
						break;
					}
				}
				spin_unlock_bh(&rxtid->lock);

				if (j >= rxtid->hold_q_sz) {
				rxtid->timer_mon = false;
				ath6kl_dbg(ATH6KL_DBG_OOO,
				"ooo:No hole is present and timer is stopped\n");
				}
			}
		}


		if (agg_conn->timer_scheduled) {
			mod_timer(&agg_conn->timer,
				jiffies + msecs_to_jiffies(AGGR_RX_TIMEOUT));
		} else {
			del_timer(&agg_conn->timer);
		}
	}
}

static bool aggr_process_recv_frm_bam2bam(struct aggr_info_conn *agg_conn,
		u8 tid,
		u16 seq_no,
		bool is_amsdu, struct sk_buff *frame)
{
	struct sk_buff *skb;
	struct rxtid *rxtid;
	struct rxtid_stats *stats;
	struct skb_hold_q *node;
	u16 idx;
	bool is_queued = false;

	rxtid = &agg_conn->rx_tid[tid];
	stats = &agg_conn->stat[tid];

	stats->num_into_aggr++;

	if (!rxtid->aggr) {
		if (is_amsdu) {
			aggr_slice_amsdu(agg_conn->aggr_info, rxtid, frame);
			is_queued = true;
			stats->num_amsdu++;
			while ((skb = skb_dequeue(&rxtid->q)))
				ath6kl_deliver_frames_to_nw_stack(agg_conn->dev,
						skb);
		}
		return is_queued;
	}
	/* Set the seq next to current incoming seq no if
	   1. There are no frames in hold q for this tid/aid pair
	   2. The incoming seq no is before the current seq next
	      sequence number
	 */
	if ((rxtid->timer_mon) || (rxtid->seq_next)) {
		if ((((rxtid->seq_next > seq_no) && ((seq_no + rxtid->hold_q_sz -1)
			> rxtid->seq_next )) || (((rxtid->seq_next < seq_no) &&
				(rxtid->seq_next + rxtid->hold_q_sz -1) < seq_no))))
		rxtid->seq_next = seq_no;
	} else {
		rxtid->seq_next = seq_no;
	}

	idx = AGGR_WIN_IDX(seq_no, rxtid->hold_q_sz);

	node = &rxtid->hold_q[idx];

	spin_lock_bh(&rxtid->lock);
	ath6kl_dbg(ATH6KL_DBG_OOO,
			"ooo:frame %d is in idx = %d\n",seq_no, idx);
	/*
	 * Is the cur frame duplicate or something beyond our window(hold_q
	 * -> which is 2x, already)?
	 *
	 * 1. Duplicate is easy - drop incoming frame.
	 * 2. Not falling in current sliding window.
	 *  2a. is the frame_seq_no preceding current tid_seq_no?
	 *      -> drop the frame. perhaps sender did not get our ACK.
	 *         this is taken care of above.
	 *  2b. is the frame_seq_no beyond window(st, TID_WINDOW_SZ);
	 *      -> Taken care of it above, by moving window forward.
	 */
	if (node->skb) {
		dev_kfree_skb(node->skb);
		stats->num_dups++;
	}

	node->skb = frame;
	is_queued = true;
	stats->num_mpdu++;
	node->is_amsdu = is_amsdu;
	node->seq_no = seq_no;

	spin_unlock_bh(&rxtid->lock);

	if (agg_conn->timer_scheduled) {
		ath6kl_dbg(ATH6KL_DBG_OOO,
				"ooo:timer is already scheduled \n");
		return is_queued;
	}

	spin_lock_bh(&rxtid->lock);
	for (idx = 0 ; idx < rxtid->hold_q_sz; idx++) {
		if (rxtid->hold_q[idx].skb) {
			/*
			 * There is a frame in the queue and no
			 * timer so start a timer to ensure that
			 * the frame doesn't remain stuck
			 * forever.
			 */
			ath6kl_dbg(ATH6KL_DBG_OOO,
					"ooo:start the timer\n");
			agg_conn->timer_scheduled = true;
			mod_timer(&agg_conn->timer,
				(jiffies + (HZ * AGGR_RX_TIMEOUT) / 1000));
			rxtid->timer_mon = true;
			break;
		}
	}
	spin_unlock_bh(&rxtid->lock);

	return is_queued;
}

static void aggr_process_amsdu_bam2bam(struct aggr_info_conn *agg_conn,
		u8 tid, struct sk_buff *frame)
{
	struct sk_buff *skb;
	struct rxtid *rxtid;

	rxtid = &agg_conn->rx_tid[tid];
	aggr_slice_amsdu(agg_conn->aggr_info, rxtid, frame);
	while ((skb = skb_dequeue(&rxtid->q)))
		ath6kl_deliver_frames_to_nw_stack(agg_conn->dev,
				skb);
}
#endif  /* CONFIG_ATH6KL_BAM2BAM */
static void aggr_deque_frms(struct aggr_info_conn *agg_conn, u8 tid,
		u16 seq_no, u8 order)
{
	struct sk_buff *skb;
	struct rxtid *rxtid;
	struct skb_hold_q *node;
	u16 idx, idx_end, seq_end;
	struct rxtid_stats *stats;

	rxtid = &agg_conn->rx_tid[tid];
	stats = &agg_conn->stat[tid];

	spin_lock_bh(&rxtid->lock);
	idx = AGGR_WIN_IDX(rxtid->seq_next, rxtid->hold_q_sz);

	/*
	 * idx_end is typically the last possible frame in the window,
	 * but changes to 'the' seq_no, when BAR comes. If seq_no
	 * is non-zero, we will go up to that and stop.
	 * Note: last seq no in current window will occupy the same
	 * index position as index that is just previous to start.
	 * An imp point : if win_sz is 7, for seq_no space of 4095,
	 * then, there would be holes when sequence wrap around occurs.
	 * Target should judiciously choose the win_sz, based on
	 * this condition. For 4095, (TID_WINDOW_SZ = 2 x win_sz
	 * 2, 4, 8, 16 win_sz works fine).
	 * We must deque from "idx" to "idx_end", including both.
	 */
	seq_end = seq_no ? seq_no : rxtid->seq_next;
	idx_end = AGGR_WIN_IDX(seq_end, rxtid->hold_q_sz);

	do {
		node = &rxtid->hold_q[idx];
		if ((order == 1) && (!node->skb))
			break;

		if (node->skb) {
			if (node->is_amsdu)
				aggr_slice_amsdu(agg_conn->aggr_info, rxtid,
						node->skb);
			else
				skb_queue_tail(&rxtid->q, node->skb);
			node->skb = NULL;
		} else
			stats->num_hole++;

		rxtid->seq_next = ATH6KL_NEXT_SEQ_NO(rxtid->seq_next);
		idx = AGGR_WIN_IDX(rxtid->seq_next, rxtid->hold_q_sz);
	} while (idx != idx_end);

	spin_unlock_bh(&rxtid->lock);

	stats->num_delivered += skb_queue_len(&rxtid->q);

	while ((skb = skb_dequeue(&rxtid->q)))
		ath6kl_deliver_frames_to_nw_stack(agg_conn->dev, skb);
}

static bool aggr_process_recv_frm(struct aggr_info_conn *agg_conn, u8 tid,
		u16 seq_no,
		bool is_amsdu, struct sk_buff *frame)
{
	struct rxtid *rxtid;
	struct rxtid_stats *stats;
	struct sk_buff *skb;
	struct skb_hold_q *node;
	u16 idx, st, cur, end;
	bool is_queued = false;
	u16 extended_end;

	rxtid = &agg_conn->rx_tid[tid];
	stats = &agg_conn->stat[tid];

	stats->num_into_aggr++;

	if (!rxtid->aggr) {
		if (is_amsdu) {
			aggr_slice_amsdu(agg_conn->aggr_info, rxtid, frame);
			is_queued = true;
			stats->num_amsdu++;
			while ((skb = skb_dequeue(&rxtid->q)))
				ath6kl_deliver_frames_to_nw_stack(agg_conn->dev,
						skb);
		}
		return is_queued;
	}

	/* Check the incoming sequence no, if it's in the window */
	st = rxtid->seq_next;
	cur = seq_no;
	end = (st + rxtid->hold_q_sz-1) & ATH6KL_MAX_SEQ_NO;

	if (((st < end) && (cur < st || cur > end)) ||
			((st > end) && (cur > end) && (cur < st))) {
		extended_end = (end + rxtid->hold_q_sz - 1) &
			ATH6KL_MAX_SEQ_NO;

		if (((end < extended_end) &&
					(cur < end || cur > extended_end)) ||
				((end > extended_end) && (cur > extended_end) &&
				 (cur < end))) {
			aggr_deque_frms(agg_conn, tid, 0, 0);
			spin_lock_bh(&rxtid->lock);
			if (cur >= rxtid->hold_q_sz - 1)
				rxtid->seq_next = cur - (rxtid->hold_q_sz - 1);
			else
				rxtid->seq_next = ATH6KL_MAX_SEQ_NO -
					(rxtid->hold_q_sz - 2 - cur);
			spin_unlock_bh(&rxtid->lock);
		} else {
			/*
			 * Dequeue only those frames that are outside the
			 * new shifted window.
			 */
			if (cur >= rxtid->hold_q_sz - 1)
				st = cur - (rxtid->hold_q_sz - 1);
			else
				st = ATH6KL_MAX_SEQ_NO -
					(rxtid->hold_q_sz - 2 - cur);

			aggr_deque_frms(agg_conn, tid, st, 0);
		}

		stats->num_oow++;
	}

	idx = AGGR_WIN_IDX(seq_no, rxtid->hold_q_sz);

	node = &rxtid->hold_q[idx];

	spin_lock_bh(&rxtid->lock);

	/*
	 * Is the cur frame duplicate or something beyond our window(hold_q
	 * -> which is 2x, already)?
	 *
	 * 1. Duplicate is easy - drop incoming frame.
	 * 2. Not falling in current sliding window.
	 *  2a. is the frame_seq_no preceding current tid_seq_no?
	 *      -> drop the frame. perhaps sender did not get our ACK.
	 *         this is taken care of above.
	 *  2b. is the frame_seq_no beyond window(st, TID_WINDOW_SZ);
	 *      -> Taken care of it above, by moving window forward.
	 */
	dev_kfree_skb(node->skb);
	stats->num_dups++;

	node->skb = frame;
	is_queued = true;
	node->is_amsdu = is_amsdu;
	node->seq_no = seq_no;

	if (node->is_amsdu)
		stats->num_amsdu++;
	else
		stats->num_mpdu++;

	spin_unlock_bh(&rxtid->lock);

	aggr_deque_frms(agg_conn, tid, 0, 1);

	if (agg_conn->timer_scheduled)
		return is_queued;

	spin_lock_bh(&rxtid->lock);
	for (idx = 0 ; idx < rxtid->hold_q_sz; idx++) {
		if (rxtid->hold_q[idx].skb) {
			/*
			 * There is a frame in the queue and no
			 * timer so start a timer to ensure that
			 * the frame doesn't remain stuck
			 * forever.
			 */
			agg_conn->timer_scheduled = true;
			mod_timer(&agg_conn->timer,
				(jiffies + (HZ * AGGR_RX_TIMEOUT) / 1000));
			rxtid->timer_mon = true;
			break;
		}
	}
	spin_unlock_bh(&rxtid->lock);

	return is_queued;
}

static void ath6kl_uapsd_trigger_frame_rx(struct ath6kl_vif *vif,
		struct ath6kl_sta *conn)
{
	struct ath6kl *ar = vif->ar;
	bool is_apsdq_empty, is_apsdq_empty_at_start;
	u32 num_frames_to_deliver, flags;
	struct sk_buff *skb = NULL;

	/*
	 * If the APSD q for this STA is not empty, dequeue and
	 * send a pkt from the head of the q. Also update the
	 * More data bit in the WMI_DATA_HDR if there are
	 * more pkts for this STA in the APSD q.
	 * If there are no more pkts for this STA,
	 * update the APSD bitmap for this STA.
	 */

	num_frames_to_deliver = (conn->apsd_info >> ATH6KL_APSD_NUM_OF_AC) &
		ATH6KL_APSD_FRAME_MASK;
	/*
	 * Number of frames to send in a service period is
	 * indicated by the station
	 * in the QOS_INFO of the association request
	 * If it is zero, send all frames
	 */
	if (!num_frames_to_deliver)
		num_frames_to_deliver = ATH6KL_APSD_ALL_FRAME;

	spin_lock_bh(&conn->psq_lock);
	is_apsdq_empty = skb_queue_empty(&conn->apsdq);
	spin_unlock_bh(&conn->psq_lock);
	is_apsdq_empty_at_start = is_apsdq_empty;

	while ((!is_apsdq_empty) && (num_frames_to_deliver)) {

		spin_lock_bh(&conn->psq_lock);
		skb = skb_dequeue(&conn->apsdq);
		is_apsdq_empty = skb_queue_empty(&conn->apsdq);
		spin_unlock_bh(&conn->psq_lock);

		/*
		 * Set the STA flag to Trigger delivery,
		 * so that the frame will go out
		 */
		conn->sta_flags |= STA_PS_APSD_TRIGGER;
		num_frames_to_deliver--;

		/* Last frame in the service period, set EOSP or queue empty */
		if ((is_apsdq_empty) || (!num_frames_to_deliver))
			conn->sta_flags |= STA_PS_APSD_EOSP;

		if (skb)
			ath6kl_data_tx(skb, vif->ndev);
		conn->sta_flags &= ~(STA_PS_APSD_TRIGGER);
		conn->sta_flags &= ~(STA_PS_APSD_EOSP);
	}

	if (is_apsdq_empty) {
		if (is_apsdq_empty_at_start)
			flags = WMI_AP_APSD_NO_DELIVERY_FRAMES;
		else
			flags = 0;

		ath6kl_wmi_set_apsd_bfrd_traf(ar->wmi,
				vif->fw_vif_idx,
				conn->aid, 0, flags);
	}

	return;
}

void ath6kl_rx(struct htc_target *target, struct htc_packet *packet)
{
	struct ath6kl *ar = target->dev->ar;
	struct sk_buff *skb = packet->pkt_cntxt;
	struct wmi_rx_meta_v2 *meta;
	struct wmi_data_hdr *dhdr;
	int min_hdr_len;
	u8 meta_type, dot11_hdr = 0;
	u8 pad_before_data_start = 0;
	int status = packet->status;
	enum htc_endpoint_id ept = packet->endpoint;
	bool is_amsdu, prev_ps, ps_state = false;
	bool trig_state = false;
	struct ath6kl_sta *conn = NULL;
	struct sk_buff *skb1 = NULL;
	struct ethhdr *datap = NULL;
	struct ath6kl_vif *vif;
	struct aggr_info_conn *aggr_conn;
	u16 seq_no, offset;
	u8 tid, if_idx;
#ifdef CONFIG_ATH6KL_BAM2BAM
	bool is_flush = 0, is_out_of_order = 0;
	bool is_partial_flush = 0, is_flush_all = 0;
#endif

	ath6kl_dbg(ATH6KL_DBG_WLAN_RX,
		"%s: ar=0x%p eid=%d, skb=0x%p, data=0x%p, len=0x%x status:%d",
		__func__, ar, ept, skb, packet->buf,
		packet->act_len, status);

	if (status || !(skb->data + HTC_HDR_LENGTH)) {
		dev_kfree_skb(skb);
		return;
	}

	skb_put(skb, packet->act_len + HTC_HDR_LENGTH);
	skb_pull(skb, HTC_HDR_LENGTH);

	ath6kl_dbg_dump(ATH6KL_DBG_RAW_BYTES, __func__, "rx ",
			skb->data, skb->len);

	if (!test_bit(TESTMODE_EPPING, &ar->flag)) {
		if (ept == ar->ctrl_ep) {
			if (test_bit(WMI_ENABLED, &ar->flag)) {
				ath6kl_check_wow_status(ar);
				ath6kl_wmi_control_rx(ar->wmi, skb);
				return;
			}
			if_idx =
			wmi_cmd_hdr_get_if_idx((struct wmi_cmd_hdr *) skb->data);
		} else {
			if_idx =
			wmi_data_hdr_get_if_idx((struct wmi_data_hdr *) skb->data);
		}
	} else {
		/* The epping packet is not coming from wmi, skip the index
		 * retrival, epping assume using the first if_idx anyway
		 */
		if_idx = 0;
	}

	vif = ath6kl_get_vif_by_index(ar, if_idx);
	if (!vif) {
		dev_kfree_skb(skb);
		return;
	}

	/*
	 * Take lock to protect buffer counts and adaptive power throughput
	 * state.
	 */
	spin_lock_bh(&vif->if_lock);

	vif->net_stats.rx_packets++;
	vif->net_stats.rx_bytes += packet->act_len;

	spin_unlock_bh(&vif->if_lock);

	skb->dev = vif->ndev;

	if (!test_bit(WMI_ENABLED, &ar->flag)) {
		if (EPPING_ALIGNMENT_PAD > 0)
			skb_pull(skb, EPPING_ALIGNMENT_PAD);
		ath6kl_deliver_frames_to_nw_stack(vif->ndev, skb);
		return;
	}

	ath6kl_check_wow_status(ar);

	min_hdr_len = sizeof(struct ethhdr) + sizeof(struct wmi_data_hdr) +
		sizeof(struct ath6kl_llc_snap_hdr);

	dhdr = (struct wmi_data_hdr *) skb->data;

	/*
	 * In the case of AP mode we may receive NULL data frames
	 * that do not have LLC hdr. They are 16 bytes in size.
	 * Allow these frames in the AP mode.
	 */
	if (vif->nw_type != AP_NETWORK &&
			((packet->act_len < min_hdr_len) ||
			 (packet->act_len > WMI_MAX_AMSDU_RX_DATA_FRAME_LENGTH))) {
		ath6kl_info("frame len is too short or too long\n");
		vif->net_stats.rx_errors++;
		vif->net_stats.rx_length_errors++;
		dev_kfree_skb(skb);
		return;
	}

	/* Get the Power save state of the STA */
	if (vif->nw_type == AP_NETWORK) {
		meta_type = wmi_data_hdr_get_meta(dhdr);

		ps_state = !!((dhdr->info >> WMI_DATA_HDR_PS_SHIFT) &
				WMI_DATA_HDR_PS_MASK);

		offset = sizeof(struct wmi_data_hdr);
		trig_state = !!(le16_to_cpu(dhdr->info3) & WMI_DATA_HDR_TRIG);

		switch (meta_type) {
		case 0:
			break;
		case WMI_META_VERSION_1:
			offset += sizeof(struct wmi_rx_meta_v1);
			break;
		case WMI_META_VERSION_2:
			offset += sizeof(struct wmi_rx_meta_v2);
			break;
		default:
			break;
		}

		datap = (struct ethhdr *) (skb->data + offset);
		conn = ath6kl_find_sta(vif, datap->h_source, false);

		if (!conn) {
			dev_kfree_skb(skb);
			return;
		}

		/*
		 * If there is a change in PS state of the STA,
		 * take appropriate steps:
		 *
		 * 1. If Sleep-->Awake, flush the psq for the STA
		 *    Clear the PVB for the STA.
		 * 2. If Awake-->Sleep, Starting queueing frames
		 *    the STA.
		 */
		prev_ps = !!(conn->sta_flags & STA_PS_SLEEP);

		if (ps_state)
			conn->sta_flags |= STA_PS_SLEEP;
		else
			conn->sta_flags &= ~STA_PS_SLEEP;

		/* Accept trigger only when the station is in sleep */
		if ((conn->sta_flags & STA_PS_SLEEP) && trig_state)
			ath6kl_uapsd_trigger_frame_rx(vif, conn);

		if (prev_ps ^ !!(conn->sta_flags & STA_PS_SLEEP)) {
#ifdef CONFIG_ATH6KL_BAM2BAM
			if (ps_state)
			{
				ath6kl_send_msg_ipa(vif,
						WLAN_CLIENT_POWER_SAVE_MODE,
						datap->h_source);
			} else {
				ath6kl_send_msg_ipa(vif,
						WLAN_CLIENT_NORMAL_MODE,
						datap->h_source);
			}
#endif
			if (!(conn->sta_flags & STA_PS_SLEEP)) {
				struct sk_buff *skbuff = NULL;
				bool is_apsdq_empty;
				struct ath6kl_mgmt_buff *mgmt;
				u8 idx;

				spin_lock_bh(&conn->psq_lock);
				while (conn->mgmt_psq_len > 0) {
					mgmt = list_first_entry(
							&conn->mgmt_psq,
							struct ath6kl_mgmt_buff,
							list);
					list_del(&mgmt->list);
					conn->mgmt_psq_len--;
					spin_unlock_bh(&conn->psq_lock);
					idx = vif->fw_vif_idx;

					ath6kl_wmi_send_mgmt_cmd(ar->wmi,
							idx,
							mgmt->id,
							mgmt->freq,
							mgmt->wait,
							mgmt->buf,
							mgmt->len,
							mgmt->no_cck);

					kfree(mgmt);
					spin_lock_bh(&conn->psq_lock);
				}
				conn->mgmt_psq_len = 0;
				while ((skbuff = skb_dequeue(&conn->psq))) {
					spin_unlock_bh(&conn->psq_lock);
					ath6kl_data_tx(skbuff, vif->ndev);
					spin_lock_bh(&conn->psq_lock);
				}

				is_apsdq_empty = skb_queue_empty(&conn->apsdq);
				while ((skbuff = skb_dequeue(&conn->apsdq))) {
					spin_unlock_bh(&conn->psq_lock);
					ath6kl_data_tx(skbuff, vif->ndev);
					spin_lock_bh(&conn->psq_lock);
				}
				spin_unlock_bh(&conn->psq_lock);

				if (!is_apsdq_empty)
					ath6kl_wmi_set_apsd_bfrd_traf(
							ar->wmi,
							vif->fw_vif_idx,
							conn->aid, 0, 0);

				/* Clear the PVB for this STA */
				ath6kl_wmi_set_pvb_cmd(ar->wmi, vif->fw_vif_idx,
						conn->aid, 0);
			}
		}

		/* drop NULL data frames here */
		if ((packet->act_len < min_hdr_len) ||
				(packet->act_len >
				 WMI_MAX_AMSDU_RX_DATA_FRAME_LENGTH)) {
			dev_kfree_skb(skb);
			return;
		}
	}

	is_amsdu = wmi_data_hdr_is_amsdu(dhdr) ? true : false;
	tid = wmi_data_hdr_get_up(dhdr);
	seq_no = wmi_data_hdr_get_seqno(dhdr);
	meta_type = wmi_data_hdr_get_meta(dhdr);
	dot11_hdr = wmi_data_hdr_get_dot11(dhdr);

#ifdef CONFIG_ATH6KL_BAM2BAM
	if (ath6kl_debug_quirks(vif->ar, ATH6KL_MODULE_BAM2BAM))
	{
		is_out_of_order = wmi_data_hdr_is_out_of_order(dhdr);
		is_flush = wmi_data_hdr_is_ampdu_flush(dhdr);
		is_partial_flush = wmi_data_hdr_is_ampdu_partial_flush(dhdr);
		is_flush_all = wmi_data_hdr_is_ampdu_flush_all(dhdr);
	}
#endif

	/* Padding is done only for 1_3 HW VERSION */
	if (ar->version.target_ver == AR6004_HW_1_3_VERSION) {
		pad_before_data_start =
		(le16_to_cpu(dhdr->info3) >> WMI_DATA_HDR_PAD_BEFORE_DATA_SHIFT)
		& WMI_DATA_HDR_PAD_BEFORE_DATA_MASK;
	}

	skb_pull(skb, sizeof(struct wmi_data_hdr));

	switch (meta_type) {
	case WMI_META_VERSION_1:
		skb_pull(skb, sizeof(struct wmi_rx_meta_v1));
		break;
	case WMI_META_VERSION_2:
		meta = (struct wmi_rx_meta_v2 *) skb->data;
		if (meta->csum_flags & 0x1) {
			skb->ip_summed = CHECKSUM_COMPLETE;
			skb->csum = (__force __wsum) meta->csum;
		}
		skb_pull(skb, sizeof(struct wmi_rx_meta_v2));
		break;
	default:
		break;
	}

	skb_pull(skb, pad_before_data_start);

	if (dot11_hdr)
		status = ath6kl_wmi_dot11_hdr_remove(ar->wmi, skb);
	else if (!is_amsdu)
		status = ath6kl_wmi_dot3_2_dix(skb);

	if (status) {
		/*
		 * Drop frames that could not be processed (lack of
		 * memory, etc.)
		 */
		dev_kfree_skb(skb);
		return;
	}

	if (!(vif->ndev->flags & IFF_UP)) {
		dev_kfree_skb(skb);
		return;
	}

	if (vif->nw_type == AP_NETWORK) {
		datap = (struct ethhdr *) skb->data;
		if (is_multicast_ether_addr(datap->h_dest)) {
			/*
			 * Bcast/Mcast frames should be sent to the
			 * OS stack as well as over the air.
			 */
			skb1 = skb_copy(skb, GFP_ATOMIC);
			if (skb1)
				ath6kl_data_tx(skb1, conn->vif->ndev);
		}
	}

	datap = (struct ethhdr *) skb->data;

	if (!((is_multicast_ether_addr(datap->h_dest)) &&
			(vif->nw_type == INFRA_NETWORK))) {
		if (vif->nw_type == AP_NETWORK) {
			conn = ath6kl_find_sta(vif, datap->h_source, false);
			if (!conn)
				return;
			aggr_conn = conn->aggr_conn;
		} else
			aggr_conn = vif->aggr_cntxt->aggr_conn;

#ifdef CONFIG_ATH6KL_BAM2BAM
		if (ath6kl_debug_quirks(vif->ar, ATH6KL_MODULE_BAM2BAM) &&
			!ath6kl_debug_quirks(vif->ar,
					     ATH6KL_MODULE_BAM_RX_SW_PATH)) {
			if (!is_out_of_order && is_amsdu) {
				aggr_process_amsdu_bam2bam(aggr_conn, tid,
						 skb);
				return;
			}

			if (is_flush_all)
				aggr_deque_frms_bam2bam(aggr_conn, tid, 0, 0);

			if (is_partial_flush)
				aggr_deque_frms_bam2bam(aggr_conn, tid, seq_no , 2);

			if (is_out_of_order &&
			aggr_process_recv_frm_bam2bam(aggr_conn, tid, seq_no,
					is_amsdu, skb)) {
				/* Counter */
				un_ordered++;
				aggr_conn->vif = vif;
				if (is_flush)
					aggr_deque_frms_bam2bam(aggr_conn, tid,
						       	seq_no, 1);
				/* aggregation code will handle the skb */
				return;
			}
		}
		else
		{
			if (aggr_process_recv_frm(aggr_conn, tid, seq_no,
						is_amsdu, skb)) {
				/* aggregation code will handle the skb */
				return;
			}
		}
#else
		/* Non BAM2BAM path */
		if (aggr_process_recv_frm(aggr_conn, tid, seq_no,
					is_amsdu, skb)) {
			/* aggregation code will handle the skb */
			return;
		}
#endif
	} else if (!is_broadcast_ether_addr(datap->h_dest))
		vif->net_stats.multicast++;

#ifdef CONFIG_ATH6KL_BAM2BAM
	if (ath6kl_debug_quirks(vif->ar, ATH6KL_MODULE_BAM2BAM))
	{
		ordered++;
	}
#endif
	ath6kl_deliver_frames_to_nw_stack(vif->ndev, skb);
}

static void aggr_timeout(unsigned long arg)
{
	u8 i, j;
	struct aggr_info_conn *aggr_conn = (struct aggr_info_conn *) arg;
	struct rxtid *rxtid;
	struct rxtid_stats *stats;

	for (i = 0; i < NUM_OF_TIDS; i++) {
		rxtid = &aggr_conn->rx_tid[i];
		stats = &aggr_conn->stat[i];

		if (!rxtid->aggr || !rxtid->timer_mon)
			continue;

		stats->num_timeouts++;
		ath6kl_dbg(ATH6KL_DBG_AGGR,
				"aggr timeout (st %d end %d)\n",
				rxtid->seq_next,
				((rxtid->seq_next + rxtid->hold_q_sz-1) &
				 ATH6KL_MAX_SEQ_NO));
#ifdef CONFIG_ATH6KL_BAM2BAM
		if (ath6kl_debug_quirks(aggr_conn->vif->ar,
						ATH6KL_MODULE_BAM2BAM) &&
			!ath6kl_debug_quirks(aggr_conn->vif->ar,
					      ATH6KL_MODULE_BAM_RX_SW_PATH))
			aggr_deque_frms_bam2bam(aggr_conn, i, 0, 0);
		else {
			/* This path for non BAM2BAM path during run time */
			aggr_deque_frms(aggr_conn, i, 0, 0);
		}
#else
		/* This path for non BAM2BAM path during compile time */
		aggr_deque_frms(aggr_conn, i, 0, 0);
#endif
	}

	aggr_conn->timer_scheduled = false;

	for (i = 0; i < NUM_OF_TIDS; i++) {
		rxtid = &aggr_conn->rx_tid[i];

		if (rxtid->aggr && rxtid->hold_q) {
			spin_lock_bh(&rxtid->lock);
			for (j = 0; j < rxtid->hold_q_sz; j++) {
				if (rxtid->hold_q[j].skb) {
					aggr_conn->timer_scheduled = true;
					rxtid->timer_mon = true;
					break;
				}
			}
			spin_unlock_bh(&rxtid->lock);

			if (j >= rxtid->hold_q_sz)
				rxtid->timer_mon = false;
		}
	}

	if (aggr_conn->timer_scheduled)
		mod_timer(&aggr_conn->timer,
				jiffies + msecs_to_jiffies(AGGR_RX_TIMEOUT));
}

static void aggr_delete_tid_state(struct aggr_info_conn *aggr_conn, u8 tid)
{
	struct rxtid *rxtid;
	struct rxtid_stats *stats;

	if (!aggr_conn || tid >= NUM_OF_TIDS)
		return;

	rxtid = &aggr_conn->rx_tid[tid];
	stats = &aggr_conn->stat[tid];

	if (rxtid->aggr)
	{
#ifdef CONFIG_ATH6KL_BAM2BAM
		if (ath6kl_debug_quirks(aggr_conn->vif->ar,
					ATH6KL_MODULE_BAM2BAM) &&
			!ath6kl_debug_quirks(aggr_conn->vif->ar,
					ATH6KL_MODULE_BAM_RX_SW_PATH))
				aggr_deque_frms_bam2bam(aggr_conn, tid, 0, 0);
		else
		{
			/* This path for non BAM2BAM path during run time */
			aggr_deque_frms(aggr_conn, tid, 0, 0);
		}
#else
		/* This path for non BAM2BAM path during compile time */
		aggr_deque_frms(aggr_conn, tid, 0, 0);
#endif
	}

	rxtid->aggr = false;
	rxtid->timer_mon = false;
	rxtid->win_sz = 0;
	rxtid->seq_next = 0;
	rxtid->hold_q_sz = 0;

	kfree(rxtid->hold_q);
	rxtid->hold_q = NULL;

	memset(stats, 0, sizeof(struct rxtid_stats));
}

void aggr_recv_addba_req_evt(struct ath6kl_vif *vif, u8 tid_mux, u16 seq_no,
		u8 win_sz)
{
	struct ath6kl_sta *sta;
	struct aggr_info_conn *aggr_conn = NULL;
	struct rxtid *rxtid;
	struct rxtid_stats *stats;
	u16 hold_q_size;
	u8 tid, aid;

	if (vif->nw_type == AP_NETWORK) {
		aid = ath6kl_get_aid(tid_mux);
		sta = ath6kl_find_sta_by_aid(vif, aid);
		if (sta)
			aggr_conn = sta->aggr_conn;
	} else
		aggr_conn = vif->aggr_cntxt->aggr_conn;

	if (!aggr_conn)
		return;

	tid = ath6kl_get_tid(tid_mux);
	if (tid >= NUM_OF_TIDS)
		return;

	rxtid = &aggr_conn->rx_tid[tid];
	stats = &aggr_conn->stat[tid];

	if (win_sz < AGGR_WIN_SZ_MIN || win_sz > AGGR_WIN_SZ_MAX)
		ath6kl_dbg(ATH6KL_DBG_WLAN_RX, "%s: win_sz %d, tid %d\n",
				__func__, win_sz, tid);

#ifdef CONFIG_ATH6KL_BAM2BAM
	aggr_conn->vif = vif;
#endif

	if (rxtid->aggr)
		aggr_delete_tid_state(aggr_conn, tid);

	rxtid->seq_next = seq_no;
	hold_q_size = TID_WINDOW_SZ(win_sz) * sizeof(struct skb_hold_q);
	rxtid->hold_q = kzalloc(hold_q_size, GFP_KERNEL);
	if (!rxtid->hold_q)
		return;

	rxtid->win_sz = win_sz;
	rxtid->hold_q_sz = TID_WINDOW_SZ(win_sz);
	if (!skb_queue_empty(&rxtid->q))
		return;

	rxtid->aggr = true;
}

void aggr_conn_init(struct ath6kl_vif *vif, struct aggr_info *aggr_info,
		struct aggr_info_conn *aggr_conn)
{
	struct rxtid *rxtid;
	u8 i;

	aggr_conn->aggr_sz = AGGR_SZ_DEFAULT;
	aggr_conn->dev = vif->ndev;
#ifdef CONFIG_ATH6KL_BAM2BAM
	aggr_conn->vif = vif;
#endif
	init_timer(&aggr_conn->timer);
	aggr_conn->timer.function = aggr_timeout;
	aggr_conn->timer.data = (unsigned long) aggr_conn;
	aggr_conn->aggr_info = aggr_info;

	aggr_conn->timer_scheduled = false;

	for (i = 0; i < NUM_OF_TIDS; i++) {
		rxtid = &aggr_conn->rx_tid[i];
		rxtid->aggr = false;
		rxtid->timer_mon = false;
		skb_queue_head_init(&rxtid->q);
		spin_lock_init(&rxtid->lock);
	}

}

struct aggr_info *aggr_init(struct ath6kl_vif *vif)
{
	struct aggr_info *p_aggr = NULL;

	p_aggr = kzalloc(sizeof(struct aggr_info), GFP_KERNEL);
	if (!p_aggr) {
		ath6kl_err("failed to alloc memory for aggr_node\n");
		return NULL;
	}

	p_aggr->aggr_conn = kzalloc(sizeof(struct aggr_info_conn), GFP_KERNEL);
	if (!p_aggr->aggr_conn) {
		ath6kl_err("failed to alloc memory for conn spec aggr info\n");
		kfree(p_aggr);
		return NULL;
	}

	aggr_conn_init(vif, p_aggr, p_aggr->aggr_conn);

	skb_queue_head_init(&p_aggr->rx_amsdu_freeq);
	ath6kl_alloc_netbufs(&p_aggr->rx_amsdu_freeq, AGGR_NUM_OF_FREE_NETBUFS);

	return p_aggr;
}

void aggr_recv_delba_req_evt(struct ath6kl_vif *vif, u8 tid_mux)
{
	struct ath6kl_sta *sta;
	struct rxtid *rxtid;
	struct aggr_info_conn *aggr_conn = NULL;
	u8 tid, aid;

	if (vif->nw_type == AP_NETWORK) {
		aid = ath6kl_get_aid(tid_mux);
		sta = ath6kl_find_sta_by_aid(vif, aid);
		if (sta)
			aggr_conn = sta->aggr_conn;
	} else
		aggr_conn = vif->aggr_cntxt->aggr_conn;

	if (!aggr_conn)
		return;

	tid = ath6kl_get_tid(tid_mux);
	if (tid >= NUM_OF_TIDS)
		return;

	rxtid = &aggr_conn->rx_tid[tid];

#ifdef CONFIG_ATH6KL_BAM2BAM
	aggr_conn->vif = vif;
#endif

	if (rxtid->aggr)
		aggr_delete_tid_state(aggr_conn, tid);
}

void aggr_reset_state(struct aggr_info_conn *aggr_conn)
{
	u8 tid;

	if (!aggr_conn)
		return;

	if (aggr_conn->timer_scheduled) {
		del_timer(&aggr_conn->timer);
		aggr_conn->timer_scheduled = false;
	}

	for (tid = 0; tid < NUM_OF_TIDS; tid++)
		aggr_delete_tid_state(aggr_conn, tid);
}

/* clean up our amsdu buffer list */
void ath6kl_cleanup_amsdu_rxbufs(struct ath6kl *ar)
{
	struct htc_packet *packet, *tmp_pkt;

	spin_lock_bh(&ar->lock);
	if (list_empty(&ar->amsdu_rx_buffer_queue)) {
		spin_unlock_bh(&ar->lock);
		return;
	}

	list_for_each_entry_safe(packet, tmp_pkt, &ar->amsdu_rx_buffer_queue,
			list) {
		list_del(&packet->list);
		spin_unlock_bh(&ar->lock);
		dev_kfree_skb(packet->pkt_cntxt);
		spin_lock_bh(&ar->lock);
	}

	spin_unlock_bh(&ar->lock);
}

void aggr_module_destroy(struct aggr_info *aggr_info)
{
	if (!aggr_info)
		return;

	aggr_reset_state(aggr_info->aggr_conn);
	skb_queue_purge(&aggr_info->rx_amsdu_freeq);
	kfree(aggr_info->aggr_conn);
	kfree(aggr_info);
}

struct
ath6kl_mcc_flowctrl *ath6kl_mcc_flowctrl_conn_list_init(struct ath6kl *ar)
{
	struct ath6kl_mcc_flowctrl *mcc_flowctrl;
	struct ath6kl_fw_conn_list *fw_conn;
	int i;

	mcc_flowctrl = kzalloc(sizeof(struct ath6kl_mcc_flowctrl), GFP_KERNEL);
	if (!mcc_flowctrl) {
		ath6kl_err("failed to alloc memory for mcc_flowctrl\n");
		return NULL;
	}

	mcc_flowctrl->ar = ar;
	spin_lock_init(&mcc_flowctrl->mcc_flowctrl_lock);

	mcc_flowctrl->mcc_events_resumed = 0;
	setup_timer(&mcc_flowctrl->mcc_event_ctrl_timer,
		ath6kl_mcc_event_ctrl_timer_handler, (unsigned long) ar);

	for (i = 0; i < NUM_CONN; i++) {
		fw_conn = &mcc_flowctrl->fw_conn_list[i];
		INIT_LIST_HEAD(&fw_conn->conn_queue);
		INIT_LIST_HEAD(&fw_conn->re_queue);
		fw_conn->connect_status = 0;
		fw_conn->previous_can_send = true;
		fw_conn->conn_id = ATH6KL_MCC_FLOWCTRL_NULL_CONNID;
		memset(fw_conn->mac_addr, 0, ETH_ALEN);
	}

	ath6kl_dbg(ATH6KL_DBG_FLOWCTRL,
		"mcc_flowctrl init (ar %p) NUM_CONN %d\n",
		ar,
		NUM_CONN);

	return mcc_flowctrl;
}

void ath6kl_mcc_flowctrl_conn_list_deinit(struct ath6kl *ar)
{
	struct ath6kl_mcc_flowctrl *mcc_flowctrl = ar->mcc_flowctrl_ctx;

	del_timer(&mcc_flowctrl->mcc_event_ctrl_timer);

	if (mcc_flowctrl) {
		/*
		 * It's better to check whether any conn_queue/re_queue
		 * need to reclaim.
		 */

		kfree(mcc_flowctrl);
	}

	ar->mcc_flowctrl_ctx = NULL;

	ath6kl_dbg(ATH6KL_DBG_FLOWCTRL,
		"mcc_flowctrl deinit (ar %p)\n",
		ar);

	return;
}

/* Check if this connId is off-channel
 * return 0 - the connId is off-channel
 *        1 - the device is in connId channel, so tx can be sent
 */
static bool ath6kl_check_can_send(struct ath6kl_mcc_flowctrl *mcc_flowctrl,
				u8 conn_id)
{
	struct ath6kl_fw_conn_list *fw_conn;
	bool can_send = false;

	fw_conn = &mcc_flowctrl->fw_conn_list[conn_id];

	do {
		if (fw_conn->ocs)
			break;

		if (!mcc_flowctrl->mcc_events_resumed)
				break;

		can_send = true;
	} while(false);

	return can_send;
}

void ath6kl_mcc_flowctrl_tx_schedule(struct ath6kl *ar, u8 is_ch_chg)
{
	struct ath6kl_mcc_flowctrl *mcc_flowctrl = ar->mcc_flowctrl_ctx;
	struct ath6kl_fw_conn_list *fw_conn;
	struct htc_packet *packet, *tmp_pkt;
	int i, tx, re_tx;

	WARN_ON(!mcc_flowctrl);

	for (i = 0; i < NUM_CONN; i++) {
		fw_conn = &mcc_flowctrl->fw_conn_list[i];

		spin_lock_bh(&mcc_flowctrl->mcc_flowctrl_lock);

		/* Bypass this fw_conn if it not yet used. */
		if (fw_conn->conn_id == ATH6KL_MCC_FLOWCTRL_NULL_CONNID) {
			spin_unlock_bh(&mcc_flowctrl->mcc_flowctrl_lock);
			continue;
		}

		tx = re_tx = 0;
		if (ath6kl_check_can_send(mcc_flowctrl, i)) {
			if (!list_empty(&fw_conn->re_queue)) {
				list_for_each_entry_safe(packet, tmp_pkt,
						&fw_conn->re_queue, list) {
					list_del(&packet->list);

					if (packet->endpoint >= ENDPOINT_MAX)
						continue;

					re_tx++;
					ath6kl_htc_tx(ar->htc_target, packet);
				}
			}

			if (!list_empty(&fw_conn->conn_queue)) {
				list_for_each_entry_safe(packet, tmp_pkt,
						&fw_conn->conn_queue, list) {
					list_del(&packet->list);

					if (packet->endpoint >= ENDPOINT_MAX)
						continue;

					tx++;
					ath6kl_htc_tx(ar->htc_target, packet);
				}
			}
		}
		spin_unlock_bh(&mcc_flowctrl->mcc_flowctrl_lock);

		ath6kl_dbg(ATH6KL_DBG_FLOWCTRL,
			"mcc_flowctrl schedule(ar %p)connId %d tx%d re_tx%d\n",
			ar, i, tx, re_tx);
	}

	return;
}

/* Check if this packet needs to be held(cached) in host.
 * If not, send the packets that were cached earlier for this conn
 *
 * for home channel packet : netif -> ep txq -> fw
 * for off channel packet : netif -> conn queue -> channel switch event ->
 *				conn queue -> ep txq
 * for tx epq packets that are not drained : channel switch event -> eq txq
 *				-> conn queue
 */
enum htc_send_queue_result ath6kl_mcc_flowctrl_tx_schedule_pkt(struct ath6kl *ar,
					void *pkt)
{
	struct ath6kl_mcc_flowctrl *mcc_flowctrl = ar->mcc_flowctrl_ctx;
	struct ath6kl_fw_conn_list *fw_conn;
	struct ath6kl_cookie *cookie = (struct ath6kl_cookie *)pkt;
	int conn_id = cookie->htc_pkt.connid;
	enum htc_send_queue_result ret = HTC_SEND_QUEUE_OK;
	struct ath6kl_vif *vif = NULL;

	WARN_ON(!mcc_flowctrl);

	if (conn_id == ATH6KL_MCC_FLOWCTRL_NULL_CONNID) {
		ath6kl_err("mcc_flowctrl tx schedule packet fail, NULL connId,"
				"just send??\n");
		/* Just send it */
		return HTC_SEND_QUEUE_SENT;
	}

	mod_timer(&mcc_flowctrl->mcc_event_ctrl_timer,
		jiffies + msecs_to_jiffies(MCC_STOP_EVENT_TIMER_INTVL));

	if (!mcc_flowctrl->mcc_events_resumed) {
		ath6kl_wmi_set_mcc_event_mode_cmd(ar->wmi,
				MCC_START_EVENT);
		mcc_flowctrl->mcc_events_resumed = 1;
	}

	fw_conn = &mcc_flowctrl->fw_conn_list[conn_id];
	vif = fw_conn->vif;

	/* irrespective of channel, keep consuming only in multiples of
	 * this count
	 */
	if(((MAX_OFFCH_HOLD_COOKIE_NUM) < vif->cookie_used)  &&
		(!test_bit(NETQ_STOPPED, &vif->flags))) {
		set_bit(NETQ_STOPPED, &vif->flags);
		netif_stop_queue(vif->ndev);
	}

	spin_lock_bh(&mcc_flowctrl->mcc_flowctrl_lock);
	fw_conn = &mcc_flowctrl->fw_conn_list[conn_id];

	if (!ath6kl_check_can_send(mcc_flowctrl, conn_id)) {
		spin_unlock_bh(&mcc_flowctrl->mcc_flowctrl_lock);
		list_add_tail(&cookie->htc_pkt.list, &fw_conn->conn_queue);
		goto result;
	} else {
		/* send thr packet through */
		ret = HTC_SEND_QUEUE_SENT;
	}

	spin_unlock_bh(&mcc_flowctrl->mcc_flowctrl_lock);

result:
	ath6kl_dbg(ATH6KL_DBG_FLOWCTRL,
		"mcc_flowctrl schedule pkt (ar %p) %s\n",
		ar,
		((ret == HTC_SEND_QUEUE_OK) ? "queue" : "send"));

	return ret;
}

/* channel switch event -> ep txq -> conn queue
 * if packets are cycled between eq txq and conn queue for more than
 * ATH6KL_MCC_FLOWCTRL_RECYCLE_LIMIT times, drop the pkt
 */
void ath6kl_mcc_flowctrl_state_change(struct ath6kl *ar)
{
	struct ath6kl_mcc_flowctrl *mcc_flowctrl = ar->mcc_flowctrl_ctx;
	struct ath6kl_fw_conn_list *fw_conn;
	struct htc_packet *packet, *tmp_pkt;
	struct htc_endpoint *endpoint;
	struct list_head    *tx_queue, container;
	int i, eid, re_tx, drop_tx;

	WARN_ON(!mcc_flowctrl);

	INIT_LIST_HEAD(&container);

	for (i = 0; i < NUM_CONN; i++) {
		re_tx = drop_tx = 0;
		fw_conn = &mcc_flowctrl->fw_conn_list[i];

		if (!ath6kl_check_can_send(mcc_flowctrl, i) &&
			fw_conn->previous_can_send) {
			spin_lock_bh(&mcc_flowctrl->mcc_flowctrl_lock);
			spin_lock_bh(&ar->htc_target->tx_lock);
			for (eid = ENDPOINT_2; eid <= ENDPOINT_5; eid++) {
				endpoint = &ar->htc_target->endpoint[eid];
				tx_queue = &endpoint->txq;
				if (list_empty(tx_queue))
					continue;

				list_for_each_entry_safe(packet, tmp_pkt,
						tx_queue, list) {
					if (packet->connid != i)
						continue;

					list_del(&packet->list);
					if (packet->recycle_count >
					ATH6KL_MCC_FLOWCTRL_RECYCLE_LIMIT) {
						packet->status = 0;
						list_add_tail(&packet->list,
								&container);
						drop_tx++;
					} else {
						packet->recycle_count++;
						list_add_tail(&packet->list,
							&fw_conn->re_queue);
						re_tx++;
					}
				}
			}
			spin_unlock_bh(&ar->htc_target->tx_lock);
			spin_unlock_bh(&mcc_flowctrl->mcc_flowctrl_lock);
		}

		fw_conn->previous_can_send = ath6kl_check_can_send(mcc_flowctrl, i);
		fw_conn->mcc_stats.sche_re_tx += re_tx;
		fw_conn->mcc_stats.recycle_drop_count += drop_tx;
		ath6kl_dbg(ATH6KL_DBG_FLOWCTRL, "mcc_flowctrl state_change"
				" (conn %pM) re_tx %d drop_tx %d \n",
				fw_conn->mac_addr, re_tx, drop_tx);

	}

	ath6kl_tx_complete(ar->htc_target, &container);

	return;
}

/* Update the bit-map for each conn, as sent in flowctrl event from FW
 * This flag needs to be checked for all Tx
 */
void ath6kl_mcc_flowctrl_state_update(struct ath6kl *ar,
					u8 num_conn,
					u8 ac_map[],
					u8 ac_queue_depth[])
{
	struct ath6kl_mcc_flowctrl *mcc_flowctrl = ar->mcc_flowctrl_ctx;
	struct ath6kl_fw_conn_list *fw_conn;
	int i;

	WARN_ON(!mcc_flowctrl);

	spin_lock_bh(&mcc_flowctrl->mcc_flowctrl_lock);
	for (i = 0; i < NUM_CONN; i++) {
		fw_conn = &mcc_flowctrl->fw_conn_list[i];
		fw_conn->connect_status = ac_map[i];
	}
	spin_unlock_bh(&mcc_flowctrl->mcc_flowctrl_lock);

	ath6kl_dbg(ATH6KL_DBG_FLOWCTRL,
		"mcc_flowctrl state_update(ar %p)ac_map %02x %02x %02x %02x\n",
		ar, ac_map[0], ac_map[1], ac_map[2], ac_map[3]);

	return;
}

/* Find conn based on the mac address.
 * If this packet doesnt have any of the matching mac, assume doing
 * transmit of this packet is fine
 */
void ath6kl_mcc_flowctrl_set_conn_id(struct ath6kl_vif *vif,
					u8 mac_addr[],
					u8 conn_id)
{
	struct ath6kl *ar = vif->ar;
	struct ath6kl_mcc_flowctrl *mcc_flowctrl = ar->mcc_flowctrl_ctx;
	struct ath6kl_fw_conn_list *fw_conn;

	WARN_ON(!mcc_flowctrl);

	/* Here using "connection" based concept.
	 * For STA/AP mode, set mac address as STA/AP-Client's.
	 * For AP node, set mac address as AP's.
	 * For AP's client, set mac address as client's.
	 *
	 * Mac address is a "hint" used to find the connection id when TX.
	 * Host driver need to
	 * 1.Call this API w/ NULL mac address if DISCONNECT event.
	 * 2.Recycle the conn_queue/re_queue.
	 * 3.Reset the flowctrl status.
	 */

	fw_conn = &mcc_flowctrl->fw_conn_list[conn_id];
	spin_lock_bh(&mcc_flowctrl->mcc_flowctrl_lock);

	if (mac_addr) {
		fw_conn->conn_id = conn_id;
		memcpy(fw_conn->mac_addr, mac_addr, ETH_ALEN);
		fw_conn->vif = vif;
	} else {
		fw_conn->conn_id = ATH6KL_MCC_FLOWCTRL_NULL_CONNID;
		memset(fw_conn->mac_addr, 0, ETH_ALEN);
		memset(&fw_conn->mcc_stats, 0, sizeof(struct ath6kl_mcc_stats));
		fw_conn->vif = NULL;
	}

	spin_unlock_bh(&mcc_flowctrl->mcc_flowctrl_lock);

	ath6kl_dbg(ATH6KL_DBG_FLOWCTRL,
		"mcc_flowctrl set conn_id (ar %p) mode %d connId %d"
		"mac_addr %pM\n",
		ar, vif->nw_type, conn_id,
		mac_addr);

	return;
}

u8 ath6kl_mcc_flowctrl_get_conn_id(struct ath6kl_vif *vif,
				struct sk_buff *skb)
{
	struct ath6kl *ar = vif->ar;
	struct ath6kl_mcc_flowctrl *mcc_flowctrl = ar->mcc_flowctrl_ctx;
	struct ath6kl_fw_conn_list *fw_conn;
	struct ethhdr *ethhdr;
	u8 *hint;
	u8 conn_id = ATH6KL_MCC_FLOWCTRL_NULL_CONNID;
	int i;

	if (!mcc_flowctrl) {
		return conn_id;
	}

	ethhdr = (struct ethhdr *)(skb->data + sizeof(struct wmi_data_hdr));

	if (vif->nw_type != AP_NETWORK) {
		hint = ethhdr->h_source;
	} else {
		if (is_multicast_ether_addr(ethhdr->h_dest))
			hint = ethhdr->h_source;
		else
			hint = ethhdr->h_dest;
	}

	fw_conn = &mcc_flowctrl->fw_conn_list[0];
	spin_lock_bh(&mcc_flowctrl->mcc_flowctrl_lock);
	for (i = 0; i < NUM_CONN; i++, fw_conn++) {
		if (fw_conn->conn_id == ATH6KL_MCC_FLOWCTRL_NULL_CONNID)
			continue;

		if (memcmp(fw_conn->mac_addr, hint, ETH_ALEN) == 0) {
			conn_id = fw_conn->conn_id;
			break;
		}
	}
	spin_unlock_bh(&mcc_flowctrl->mcc_flowctrl_lock);

	ath6kl_dbg(ATH6KL_DBG_FLOWCTRL,
		"mcc_flowctrl get conn_id (ar %p) connId %d"
		"hint %pM\n",
		ar,
		conn_id,
		hint);

	return conn_id;
}

#ifdef CONFIG_ATH6KL_BAM2BAM
void ath6kl_aggr_deque_bam2bam(struct ath6kl_vif *vif, u16 seq_no,u8 tid,
		u8 aid)
{
	struct ath6kl_sta *sta;
	struct aggr_info_conn *aggr_conn = NULL;
	struct rxtid *rxtid;

	if (vif->nw_type == AP_NETWORK) {
		sta = ath6kl_find_sta_by_aid(vif, aid);
		if (sta)
			aggr_conn = sta->aggr_conn;
	} else {
		aggr_conn = vif->aggr_cntxt->aggr_conn;
	}
	if (!aggr_conn)
		return;

	if (tid >= NUM_OF_TIDS)
		return;
	rxtid = &aggr_conn->rx_tid[tid];
	if (!rxtid->aggr)
		return;
	ath6kl_dbg(ATH6KL_DBG_OOO,
		"ooo: Control Packet from Rx-non bam2bam Event pipe...\n");
		aggr_conn->vif = vif;
	if (seq_no > ATH6KL_MAX_SEQ_NO) {
		if (ath6kl_debug_quirks(vif->ar, ATH6KL_MODULE_BAM2BAM)) {
			aggr_deque_frms_bam2bam(aggr_conn, tid, 0, 0);
		}
	} else {
		if (ath6kl_debug_quirks(vif->ar, ATH6KL_MODULE_BAM2BAM)) {
			aggr_deque_frms_bam2bam(aggr_conn, tid, seq_no, 2);
		}
	}
}

int ath6kl_send_dummy_data(struct ath6kl_vif *vif, u8 ac_category)
{
	struct ath6kl *ar = vif->ar;
	struct ath6kl_cookie *cookie = NULL;
	enum htc_endpoint_id eid = ENDPOINT_UNUSED;
	u16 htc_tag = ATH6KL_DATA_PKT_TAG;
	struct sk_buff *skb = NULL;
	int ret = 0;
	struct wmi_data_hdr *data_hdr;

	if(ar->cookie_count < (MAX_DEF_COOKIE_NUM/3))
		return -ENOMEM;

	if (WARN_ON_ONCE(ar->state != ATH6KL_STATE_ON))
		return -EINVAL;

	if (!test_bit(WMI_READY, &ar->flag))
		return -EINVAL;

	skb = ath6kl_buf_alloc(0);
	if (skb == NULL)
		return -ENOMEM;

	ret = ath6kl_wmi_data_hdr_add(ar->wmi, skb,
			DATA_MSGTYPE, 0, 0,
			0,
			NULL, vif->fw_vif_idx);
	if (ret) {
		ath6kl_warn("failed to add wmi data header:%d\n"
				, ret);
		goto dfail_tx;
	}

	data_hdr = (struct wmi_data_hdr *)skb->data;

	data_hdr->info3 |=
		cpu_to_le16(WMI_DATA_HDR_DUMMY_DATA_BIT_MASK <<
				WMI_DATA_HDR_DUMMY_DATA_BIT_SHIFT);

	spin_lock_bh(&ar->lock);

	eid = ac_category;

	if (eid == 0 || eid == ENDPOINT_UNUSED) {
		ath6kl_err("eid %d is not mapped!\n", eid);
		spin_unlock_bh(&ar->lock);
		ret = -EINVAL;
		goto dfail_tx;
	}

	/* allocate resource for this packet */
	cookie = ath6kl_alloc_cookie(ar, vif, eid);

	if (!cookie) {
		spin_unlock_bh(&ar->lock);
		ret = -ENOMEM;
		goto dfail_tx;
	}

	/* update counts while the lock is held */
	ar->tx_pending[eid]++;
	ar->total_tx_data_pend++;

	spin_unlock_bh(&ar->lock);

	if (!IS_ALIGNED((unsigned long) skb->data - HTC_HDR_LENGTH, 4) &&
			skb_cloned(skb)) {
		/*
		 * We will touch (move the buffer data to align it. Since the
		 * skb buffer is cloned and not only the header is changed, we
		 * have to copy it to allow the changes. Since we are copying
		 * the data here, we may as well align it by reserving suitable
		 * headroom to avoid the memmove in ath6kl_htc_tx_buf_align().
		 */
		struct sk_buff *nskb;

		nskb = skb_copy_expand(skb, HTC_HDR_LENGTH, 0, GFP_ATOMIC);
		if (nskb == NULL)
			goto dfail_skbexp;
		kfree_skb(skb);
		skb = nskb;
	}

	cookie->skb = skb;
	cookie->map_no = 0;
	set_htc_pkt_info(&cookie->htc_pkt, cookie, skb->data, skb->len,
			eid, htc_tag);
	cookie->htc_pkt.skb = skb;

	ath6kl_dbg_dump(ATH6KL_DBG_RAW_BYTES, __func__, "dummy tx ",
			skb->data, skb->len);

	/*
	 * HTC interface is asynchronous, if this fails, cleanup will
	 * happen in the ath6kl_tx_complete callback.
	 */
	ath6kl_htc_tx(ar->htc_target, &cookie->htc_pkt);

	return 0;

dfail_skbexp:
	/* Release the cookie */
	ath6kl_free_cookie(ar, vif, cookie);

dfail_tx:
	if (skb)
		dev_kfree_skb(skb);

	return ret;
}

void ath6kl_client_power_save(struct ath6kl_vif *vif, u8 power_save, u8 aid)
{
	struct ath6kl_sta *conn;
	struct ath6kl *ar = vif->ar;

	ath6kl_dbg(ATH6KL_DBG_BAM2BAM,
			"IPA-CM: Power save event is successfully received\n");

	if (vif->nw_type == AP_NETWORK) {
		conn = ath6kl_find_sta_by_aid(vif, aid);
		if(!conn)
			return;
	} else {
		return;
	}

	if (power_save ^ !!(conn->sta_flags & STA_PS_SLEEP)) {
		if (power_save)
		{
			conn->sta_flags |= STA_PS_SLEEP;
			ath6kl_send_msg_ipa(vif, WLAN_CLIENT_POWER_SAVE_MODE,
					conn->mac);
		} else {
			conn->sta_flags &= ~STA_PS_SLEEP;
			ath6kl_send_msg_ipa(vif, WLAN_CLIENT_NORMAL_MODE,
					conn->mac);
		}
	}

	if (!(conn->sta_flags & STA_PS_SLEEP)) {
		struct sk_buff *skbuff = NULL;
		bool is_apsdq_empty;
		struct ath6kl_mgmt_buff *mgmt;
		u8 idx;

		spin_lock_bh(&conn->psq_lock);
		while (conn->mgmt_psq_len > 0) {
			mgmt = list_first_entry(
					&conn->mgmt_psq,
					struct ath6kl_mgmt_buff,
					list);
			list_del(&mgmt->list);
			conn->mgmt_psq_len--;
			spin_unlock_bh(&conn->psq_lock);
			idx = vif->fw_vif_idx;

			ath6kl_wmi_send_mgmt_cmd(ar->wmi,
					idx,
					mgmt->id,
					mgmt->freq,
					mgmt->wait,
					mgmt->buf,
					mgmt->len,
					mgmt->no_cck);

			kfree(mgmt);
			spin_lock_bh(&conn->psq_lock);
		}
		conn->mgmt_psq_len = 0;
		while ((skbuff = skb_dequeue(&conn->psq))) {
			spin_unlock_bh(&conn->psq_lock);
			ath6kl_data_tx(skbuff, vif->ndev);
			spin_lock_bh(&conn->psq_lock);
		}

		is_apsdq_empty = skb_queue_empty(&conn->apsdq);
		while ((skbuff = skb_dequeue(&conn->apsdq))) {
			spin_unlock_bh(&conn->psq_lock);
			ath6kl_data_tx(skbuff, vif->ndev);
			spin_lock_bh(&conn->psq_lock);
		}
		spin_unlock_bh(&conn->psq_lock);

		if (!is_apsdq_empty)
			ath6kl_wmi_set_apsd_bfrd_traf(
					ar->wmi,
					vif->fw_vif_idx,
					conn->aid, 0, 0);

		/* Clear the PVB for this STA */
		ath6kl_wmi_set_pvb_cmd(ar->wmi, vif->fw_vif_idx,
				conn->aid, 0);
	}
}

void ath6kl_allow_packet_drop(struct ath6kl_vif *vif, u8 enable_drop)
{
	struct ipa_ep_cfg_holb ipa_ep_cfg;
	ath6kl_dbg(ATH6KL_DBG_BAM2BAM,
			"IPA-CM: IPA HOLB event is successfully received\n");
	if (enable_drop)
		ipa_ep_cfg.en = true;
	else
		ipa_ep_cfg.en = false;

	ipa_ep_cfg.tmr_val = ATH6KL_IPA_HOLB_TIMER_VAL;
	ipa_cfg_ep_holb_by_client(IPA_CLIENT_HSIC1_CONS, &ipa_ep_cfg);
	ipa_cfg_ep_holb_by_client(IPA_CLIENT_HSIC2_CONS, &ipa_ep_cfg);
	ipa_cfg_ep_holb_by_client(IPA_CLIENT_HSIC3_CONS, &ipa_ep_cfg);
	ipa_cfg_ep_holb_by_client(IPA_CLIENT_HSIC4_CONS, &ipa_ep_cfg);

}
#endif

static void ath6kl_mcc_event_ctrl_timer_handler(unsigned long ptr)
{
	struct ath6kl *ar = (struct ath6kl *)ptr;
	struct ath6kl_mcc_flowctrl *mcc_flowctrl = ar->mcc_flowctrl_ctx;

	ath6kl_wmi_set_mcc_event_mode_cmd(ar->wmi, MCC_STOP_EVENT);
	mcc_flowctrl->mcc_events_resumed = 0;
	return;
}

