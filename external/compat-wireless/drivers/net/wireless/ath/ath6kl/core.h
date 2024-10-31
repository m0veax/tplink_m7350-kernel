/*
 * Copyright (c) 2010-2011 Atheros Communications Inc.
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

#ifndef CORE_H
#define CORE_H

#include <linux/etherdevice.h>
#include <linux/rtnetlink.h>
#include <linux/firmware.h>
#include <linux/sched.h>
#include <linux/circ_buf.h>
#include <net/cfg80211.h>
#include <linux/wireless.h>
#ifdef CONFIG_ATH6KL_BAM2BAM
#include <linux/msm_ipa.h>	/* IPA driver */
#include <mach/ipa.h>		/* IPA driver */
#include <mach/usb_bam.h>	/* BAM driver */
#endif
#include "htc.h"
#include "wmi.h"
#include "bmi.h"
#include "target.h"

#define MAX_ATH6KL			1
#define ATH6KL_MAX_RX_BUFFERS		16
#define ATH6KL_BUFFER_SIZE		1664
#define ATH6KL_MAX_AMSDU_RX_BUFFERS	4
#define ATH6KL_AMSDU_REFILL_THRESHOLD	3
#define ATH6KL_AMSDU_BUFFER_SIZE	(WMI_MAX_AMSDU_RX_DATA_FRAME_LENGTH + 128)
#define MAX_MSDU_SUBFRAME_PAYLOAD_LEN	1508
#define MIN_MSDU_SUBFRAME_PAYLOAD_LEN	46

#define USER_SAVEDKEYS_STAT_INIT     0
#define USER_SAVEDKEYS_STAT_RUN      1

#define ATH6KL_TX_TIMEOUT      10
#define ATH6KL_MAX_ENDPOINTS   4
#define MAX_NODE_NUM           15

#define ATH6KL_APSD_ALL_FRAME		0xFFFF
#define ATH6KL_APSD_NUM_OF_AC		0x4
#define ATH6KL_APSD_FRAME_MASK		0xF

#define ATH6KL_MCC_FLOWCTRL_NULL_CONNID 	(0xFF)
#define ATH6KL_MCC_FLOWCTRL_RECYCLE_LIMIT 	(10)

#define BDATA_SELF_BIN_PATH "/misc/bdata_self.bin"
#define BDATA_SELF_BIN_PATH_FT_MODE "/lib/firmware/ath6k/AR6004/hw3.0/bdata_self.bin"

#define CAL_DATA_PATH "/misc/calData"
#define CAL_DATA_PATH_FT_MODE "/lib/firmware/ath6k/AR6004/hw3.0/calData"
#define CAL_DATA_NUM 22			/* 2 chain: 3@2.4G, 8@5G */
#define CAL_DATA_STR_LEN 4		/* pwr delt can't > 10 or < -10, so max len is 3, eg: -90 */
#define CAL_DATA_MAX_INT 100	/* pwr delt can't > 10 or < -10 */

/* For 2.4G chain0  */
#define OFFSE_REF_POWER_G_0_0 0x0198
#define OFFSE_REF_POWER_G_0_3 0x01CE

/* For 2.4G chain1  */
#define OFFSE_REF_POWER_G_1_0 0x0270
#define OFFSE_REF_POWER_G_1_3 0x02A6

/* For 5G chain0  */
#define OFFSE_REF_POWER_A_0_0 0x06DC
#define OFFSE_REF_POWER_A_0_8 0x076C

/* For 5G chain1  */
#define OFFSE_REF_POWER_A_1_0 0x088C
#define OFFSE_REF_POWER_A_1_8 0x091C

struct ath6kl_fw_board_self {
	u8 *fw_board;
	size_t fw_board_len;
};

#define BDATA_CHECKSUM_OFFSET                 4
#define BDATA_MAC_ADDR_OFFSET                 8

#ifdef CONFIG_ATH6KL_BAM2BAM
/* HBM needs 4 bytes alligned, and having issue with 3 bytes aligned */
#define ATH6KL_HTC_ALIGN_BYTES  4
#else
/* Extra bytes for htc header alignment */
#define ATH6KL_HTC_ALIGN_BYTES 3
#endif

/*CTRL_EP_RSVD_COOKIE_NUM are reserved for control endpoint */
#define CTRL_EP_RSVD_COOKIE_NUM           2
/* MAX_HI_COOKIE_NUM are reserved for high priority traffic */
#define MAX_DEF_COOKIE_NUM               288
#define MAX_HI_COOKIE_NUM                 28	/* 10% of MAX_COOKIE_NUM */
#define MAX_COOKIE_NUM                 (MAX_DEF_COOKIE_NUM + MAX_HI_COOKIE_NUM + CTRL_EP_RSVD_COOKIE_NUM)

#define NETIF_STOP_THOLD		(MAX_DEF_COOKIE_NUM/5)
#define NETIF_WAKE_THOLD		(NETIF_STOP_THOLD - ((8 * NETIF_STOP_THOLD)/10))

#define MAX_DEFAULT_SEND_QUEUE_DEPTH      ((MAX_DEF_COOKIE_NUM / WMM_NUM_AC) - NETIF_WAKE_THOLD)

#define MAX_OFFCH_HOLD_COOKIE_NUM         60

#define DISCON_TIMER_INTVAL               10000  /* in msec */

#define AP_RESTART_TIMER_INVAL		  1000  /* in msec */
/* Channel dwell time in fg scan */
#define ATH6KL_FG_SCAN_INTERVAL		50 /* in ms */

#define MCC_STOP_EVENT_TIMER_INTVL        1000 /* in msec */

/* includes also the null byte */
#define ATH6KL_FIRMWARE_MAGIC               "QCA-ATH6KL"

#define ATH6KL_CONN_TX_PSQ_MAX_LEN 128

#define ATH6KL_DEF_MCC_ADJ_CH_SPACING 20

#define ATH6KL_DEF_LTE_MARGIN 0

enum ath6kl_fw_ie_type {
	ATH6KL_FW_IE_FW_VERSION = 0,
	ATH6KL_FW_IE_TIMESTAMP = 1,
	ATH6KL_FW_IE_OTP_IMAGE = 2,
	ATH6KL_FW_IE_FW_IMAGE = 3,
	ATH6KL_FW_IE_PATCH_IMAGE = 4,
	ATH6KL_FW_IE_RESERVED_RAM_SIZE = 5,
	ATH6KL_FW_IE_CAPABILITIES = 6,
	ATH6KL_FW_IE_PATCH_ADDR = 7,
	ATH6KL_FW_IE_BOARD_ADDR = 8,
	ATH6KL_FW_IE_VIF_MAX = 9,
};

enum ath6kl_fw_capability {
	ATH6KL_FW_CAPABILITY_HOST_P2P = 0,
	ATH6KL_FW_CAPABILITY_SCHED_SCAN = 1,

	/*
	 * Firmware is capable of supporting P2P mgmt operations on a
	 * station interface. After group formation, the station
	 * interface will become a P2P client/GO interface as the case may be
	 */
	ATH6KL_FW_CAPABILITY_STA_P2PDEV_DUPLEX,

	/*
	 * Firmware has support to cleanup inactive stations
	 * in AP mode.
	 */
	ATH6KL_FW_CAPABILITY_INACTIVITY_TIMEOUT,

	/* Firmware has support to override rsn cap of rsn ie */
	ATH6KL_FW_CAPABILITY_RSN_CAP_OVERRIDE,

	/*
	 * Multicast support in WOW and host awake mode.
	 * Allow all multicast in host awake mode.
	 * Apply multicast filter in WOW mode.
	 */
	ATH6KL_FW_CAPABILITY_WOW_MULTICAST_FILTER,

	/* Firmware supports enhanced bmiss detection */
	ATH6KL_FW_CAPABILITY_BMISS_ENHANCE,

	/*
	 * FW supports matching of ssid in schedule scan
	 */
	ATH6KL_FW_CAPABILITY_SCHED_SCAN_MATCH_LIST,

	/* Firmware supports filtering BSS results by RSSI */
	ATH6KL_FW_CAPABILITY_RSSI_SCAN_THOLD,

	/* FW sets mac_addr[4] ^= 0x80 for newly created interfaces */
	ATH6KL_FW_CAPABILITY_CUSTOM_MAC_ADDR,

	/* Firmware supports TX error rate notification */
	ATH6KL_FW_CAPABILITY_TX_ERR_NOTIFY,

	/* supports WMI_SET_REGDOMAIN_CMDID command */
	ATH6KL_FW_CAPABILITY_REGDOMAIN,

	/* Firmware supports sched scan decoupled from host sleep */
	ATH6KL_FW_CAPABILITY_SCHED_SCAN_V2,

	/* Firmware supports HT-40 in 2.4Ghz band */
	ATH6KL_FW_CAPABILITY_2GIG_HT40_SUPPORT,

	/*
	* Firmware supports mac address based ACL with
	* white/black list
	*/
	ATH6KL_FW_CAPABILITY_MAC_ACL,

	/*
	 * Firmware capability for hang detection through
	 * heart beat challenge messsages.
	 */
	ATH6KL_FW_CAPABILITY_HEART_BEAT_POLL,

	/* this needs to be last */
	ATH6KL_FW_CAPABILITY_MAX,
};

#define ATH6KL_CAPABILITY_LEN (ALIGN(ATH6KL_FW_CAPABILITY_MAX, 32) / 32)

struct ath6kl_fw_ie {
	__le32 id;
	__le32 len;
	u8 data[0];
};

enum ath6kl_hw_flags {
	ATH6KL_HW_FLAG_64BIT_RATES		= BIT(0),
	ATH6KL_HW_FLAG_AP_INACTIVITY_MINS	= BIT(1),
};

/* Linux Wirelesss Extensions, private ioctl interface */
#define ATH6KL_IOCTL_EXTENDED       (SIOCIWFIRSTPRIV+26)

/* TBD: ioctl number is aligned to olca branch
 *      will refine one the loopback tool is ready for native ath6kl
 */
typedef enum {
	ATH6KL_XIOCTL_TRAFFIC_ACTIVITY_CHANGE = 80,
} XTND_IOCLTS;


struct ath6kl_traffic_activity_change {
	u32    stream_id;   /* stream ID to indicate activity change */
	u32    active;     /* active (1) or inactive (0) */
};

#define ATH6KL_FW_API2_FILE "fw-2.bin"
#define ATH6KL_FW_API3_FILE "fw-3.bin"

/* AR6003 1.0 definitions */
#define AR6003_HW_1_0_VERSION                 0x300002ba

/* AR6003 2.0 definitions */
#define AR6003_HW_2_0_VERSION                 0x30000384
#define AR6003_HW_2_0_PATCH_DOWNLOAD_ADDRESS  0x57e910
#define AR6003_HW_2_0_FW_DIR			"ath6k/AR6003/hw2.0"
#define AR6003_HW_2_0_OTP_FILE			"otp.bin.z77"
#define AR6003_HW_2_0_FIRMWARE_FILE		"athwlan.bin.z77"
#define AR6003_HW_2_0_TCMD_FIRMWARE_FILE	"athtcmd_ram.bin"
#define AR6003_HW_2_0_PATCH_FILE		"data.patch.bin"
#define AR6003_HW_2_0_BOARD_DATA_FILE AR6003_HW_2_0_FW_DIR "/bdata.bin"
#define AR6003_HW_2_0_DEFAULT_BOARD_DATA_FILE \
			AR6003_HW_2_0_FW_DIR "/bdata.SD31.bin"

/* AR6003 3.0 definitions */
#define AR6003_HW_2_1_1_VERSION                 0x30000582
#define AR6003_HW_2_1_1_FW_DIR			"ath6k/AR6003/hw2.1.1"
#define AR6003_HW_2_1_1_OTP_FILE		"otp.bin"
#define AR6003_HW_2_1_1_FIRMWARE_FILE		"athwlan.bin"
#define AR6003_HW_2_1_1_TCMD_FIRMWARE_FILE	"athtcmd_ram.bin"
#define AR6003_HW_2_1_1_UTF_FIRMWARE_FILE	"utf.bin"
#define AR6003_HW_2_1_1_TESTSCRIPT_FILE	"nullTestFlow.bin"
#define AR6003_HW_2_1_1_PATCH_FILE		"data.patch.bin"
#define AR6003_HW_2_1_1_BOARD_DATA_FILE AR6003_HW_2_1_1_FW_DIR "/bdata.bin"
#define AR6003_HW_2_1_1_DEFAULT_BOARD_DATA_FILE	\
			AR6003_HW_2_1_1_FW_DIR "/bdata.SD31.bin"

/* AR6004 1.0 definitions */
#define AR6004_HW_1_0_VERSION                 0x30000623
#define AR6004_HW_1_0_FW_DIR			"ath6k/AR6004/hw1.0"
#define AR6004_HW_1_0_OTP_FILE			"otp.bin"
#define AR6004_HW_1_0_FIRMWARE_FILE		"fw.ram.bin"
#define AR6004_HW_1_0_BOARD_DATA_FILE         AR6004_HW_1_0_FW_DIR"/bdata.bin"
#define AR6004_HW_1_0_DEFAULT_BOARD_DATA_FILE \
	AR6004_HW_1_0_FW_DIR "/bdata.DB132.bin"

/* AR6004 1.1 definitions */
#define AR6004_HW_1_1_VERSION			0x30000001
#define AR6004_HW_1_1_FW_DIR			"ath6k/AR6004/hw1.1"
#define AR6004_HW_1_1_OTP_FILE			"otp.bin"
#define AR6004_HW_1_1_FIRMWARE_FILE		"fw.ram.bin"
#define AR6004_HW_1_1_BOARD_DATA_FILE		AR6004_HW_1_1_FW_DIR"/bdata.bin"
#define AR6004_HW_1_1_DEFAULT_BOARD_DATA_FILE \
	AR6004_HW_1_1_FW_DIR "/bdata.DB132.bin"
#define AR6004_HW_1_1_EPPING_FILE		"epping.bin"

/* AR6004 1.2 definitions */
#define AR6004_HW_1_2_VERSION			0x300007e8
#define AR6004_HW_1_2_FW_DIR			"ath6k/AR6004/hw1.2"
#define AR6004_HW_1_2_OTP_FILE			"otp.bin"
#define AR6004_HW_1_2_FIRMWARE_FILE		"fw.ram.bin"
#define AR6004_HW_1_2_BOARD_DATA_FILE		AR6004_HW_1_2_FW_DIR"/bdata.bin"
#define AR6004_HW_1_2_DEFAULT_BOARD_DATA_FILE \
	AR6004_HW_1_2_FW_DIR "/bdata.bin"
#define AR6004_HW_1_2_EPPING_FILE		"epping.bin"

/* AR6004 1.3 definitions */
#define AR6004_HW_1_3_VERSION			0x31c8088a
#define AR6004_HW_1_3_FW_DIR			"ath6k/AR6004/hw1.3"
#define AR6004_HW_1_3_OTP_FILE			"otp.bin"
#define AR6004_HW_1_3_FIRMWARE_FILE		"fw.ram.bin"
#define AR6004_HW_1_3_BOARD_DATA_FILE		"ath6k/AR6004/hw1.3/bdata.bin"
#define AR6004_HW_1_3_DEFAULT_BOARD_DATA_FILE	"ath6k/AR6004/hw1.3/bdata.bin"
#define AR6004_HW_1_3_TCMD_FIRMWARE_FILE	"utf.bin"
#define AR6004_HW_1_3_UTF_FIRMWARE_FILE		"utf.bin"
#define AR6004_HW_1_3_EPPING_FILE		"epping.bin"

/* AR6004 1.6 definitions */
#define AR6004_HW_1_6_VERSION                 0x31c80958
#define AR6004_HW_1_6_FW_DIR			"ath6k/AR6004/hw1.6"
#define AR6004_HW_1_6_OTP_FILE			"otp.bin"
#define AR6004_HW_1_6_FIRMWARE_FILE		"fw.ram.bin"
#define AR6004_HW_1_6_TCMD_FIRMWARE_FILE	"utf.bin"
#define AR6004_HW_1_6_UTF_FIRMWARE_FILE		"utf.bin"
#define AR6004_HW_1_6_BOARD_DATA_FILE		"ath6k/AR6004/hw1.6/bdata.bin"
#define AR6004_HW_1_6_DEFAULT_BOARD_DATA_FILE \
	"ath6k/AR6004/hw1.6/bdata.bin"
#define AR6004_HW_1_6_EPPING_FILE		"epping.bin"

/* AR6004 3.0 definitions */
#define AR6004_HW_3_0_VERSION			0x31C809F8
#define AR6004_HW_3_0_FW_DIR			"ath6k/AR6004/hw3.0"
#define AR6004_HW_3_0_OTP_FILE			"otp.bin"
#define AR6004_HW_3_0_FIRMWARE_FILE		"fw.ram.bin"
#define AR6004_HW_3_0_TCMD_FIRMWARE_FILE	"utf.bin"
#define AR6004_HW_3_0_UTF_FIRMWARE_FILE		"utf.bin"
#define AR6004_HW_3_0_BOARD_DATA_FILE		"ath6k/AR6004/hw3.0/bdata.bin"
#define AR6004_HW_3_0_DEFAULT_BOARD_DATA_FILE  	"ath6k/AR6004/hw3.0/bdata.bin"
#define AR6004_HW_3_0_EPPING_FILE		"epping.bin"

/* Per STA data, used in AP mode */
#define STA_PS_AWAKE		BIT(0)
#define	STA_PS_SLEEP		BIT(1)
#define	STA_PS_POLLED		BIT(2)
#define STA_PS_APSD_TRIGGER     BIT(3)
#define STA_PS_APSD_EOSP        BIT(4)

/* HTC TX packet tagging definitions */
#define ATH6KL_CONTROL_PKT_TAG    HTC_TX_PACKET_TAG_USER_DEFINED
#define ATH6KL_DATA_PKT_TAG       (ATH6KL_CONTROL_PKT_TAG + 1)

#define AR6003_CUST_DATA_SIZE 16

#define AGGR_WIN_IDX(x, y)          ((x) % (y))
#define AGGR_INCR_IDX(x, y)         AGGR_WIN_IDX(((x) + 1), (y))
#define AGGR_DCRM_IDX(x, y)         AGGR_WIN_IDX(((x) - 1), (y))
#define ATH6KL_MAX_SEQ_NO		0xFFF
#define ATH6KL_NEXT_SEQ_NO(x)		(((x) + 1) & ATH6KL_MAX_SEQ_NO)

#define NUM_OF_TIDS         8
#define AGGR_SZ_DEFAULT     8

#define AGGR_WIN_SZ_MIN     2
#define AGGR_WIN_SZ_MAX     8

#define TID_WINDOW_SZ(_x)   ((_x) << 1)

#define AGGR_NUM_OF_FREE_NETBUFS    16

#ifdef CONFIG_ATH6KL_BAM2BAM
#define AGGR_RX_TIMEOUT			200	/* in ms */
#else
#define AGGR_RX_TIMEOUT			100	/* in ms */
#endif

#define WMI_TIMEOUT (2 * HZ)

#define MBOX_YIELD_LIMIT 99

#define ATH6KL_DEFAULT_LISTEN_INTVAL	100 /* in TUs */
#define ATH6KL_DEFAULT_BMISS_TIME	1500
#define ATH6KL_MAX_WOW_LISTEN_INTL	300 /* in TUs */
#define ATH6KL_MAX_BMISS_TIME		5000

/* configuration lags */
/*
 * ATH6KL_CONF_IGNORE_ERP_BARKER: Ignore the barker premable in
 * ERP IE of beacon to determine the short premable support when
 * sending (Re)Assoc req.
 * ATH6KL_CONF_IGNORE_PS_FAIL_EVT_IN_SCAN: Don't send the power
 * module state transition failure events which happen during
 * scan, to the host.
 */
#define ATH6KL_CONF_IGNORE_ERP_BARKER		BIT(0)
#define ATH6KL_CONF_IGNORE_PS_FAIL_EVT_IN_SCAN  BIT(1)
#define ATH6KL_CONF_ENABLE_11N			BIT(2)
#define ATH6KL_CONF_ENABLE_TX_BURST		BIT(3)
#define ATH6KL_CONF_UART_DEBUG			BIT(4)

#define P2P_WILDCARD_SSID_LEN			7 /* DIRECT- */

enum wlan_low_pwr_state {
	WLAN_POWER_STATE_ON,
	WLAN_POWER_STATE_CUT_PWR,
	WLAN_POWER_STATE_DEEP_SLEEP,
	WLAN_POWER_STATE_WOW
};

enum sme_state {
	SME_DISCONNECTED,
	SME_CONNECTING,
	SME_CONNECTED
};

struct skb_hold_q {
	struct sk_buff *skb;
	bool is_amsdu;
	u16 seq_no;
};

struct rxtid {
	bool aggr;
	bool timer_mon;
	u16 win_sz;
	u16 seq_next;
	u32 hold_q_sz;
	struct skb_hold_q *hold_q;
	struct sk_buff_head q;

	/*
	 * lock mainly protects seq_next and hold_q. Movement of seq_next
	 * needs to be protected between aggr_timeout() and
	 * aggr_process_recv_frm(). hold_q will be holding the pending
	 * reorder frames and it's access should also be protected.
	 * Some of the other fields like hold_q_sz, win_sz and aggr are
	 * initialized/reset when receiving addba/delba req, also while
	 * deleting aggr state all the pending buffers are flushed before
	 * resetting these fields, so there should not be any race in accessing
	 * these fields.
	 */
	spinlock_t lock;
};

struct rxtid_stats {
	u32 num_into_aggr;
	u32 num_dups;
	u32 num_oow;
	u32 num_mpdu;
	u32 num_amsdu;
	u32 num_delivered;
	u32 num_timeouts;
	u32 num_hole;
	u32 num_bar;
};

struct aggr_info_conn {
	u8 aggr_sz;
	u8 timer_scheduled;
	struct timer_list timer;
	struct net_device *dev;
	struct rxtid rx_tid[NUM_OF_TIDS];
	struct rxtid_stats stat[NUM_OF_TIDS];
#ifdef CONFIG_ATH6KL_BAM2BAM
	struct ath6kl_vif *vif;
#endif
	struct aggr_info *aggr_info;
};

struct aggr_info {
	struct aggr_info_conn *aggr_conn;
	struct sk_buff_head rx_amsdu_freeq;
};

struct ath6kl_wep_key {
	u8 key_index;
	u8 key_len;
	u8 key[64];
};

#define ATH6KL_KEY_SEQ_LEN 8

struct ath6kl_key {
	u8 key[WLAN_MAX_KEY_LEN];
	u8 key_len;
	u8 seq[ATH6KL_KEY_SEQ_LEN];
	u8 seq_len;
	u32 cipher;
};

struct ath6kl_node_mapping {
	u8 mac_addr[ETH_ALEN];
	u8 ep_id;
	u8 tx_pend;
};

struct ath6kl_cookie {
	struct sk_buff *skb;
	u32 map_no;
	struct htc_packet htc_pkt;
	struct ath6kl_cookie *arc_list_next;
};

struct ath6kl_mgmt_buff {
	struct list_head list;
	u32 freq;
	u32 wait;
	u32 id;
	bool no_cck;
	size_t len;
	u8 buf[0];
};

struct ath6kl_sta {
	u16 sta_flags;
	u8 mac[ETH_ALEN];
	u8 aid;
	u8 keymgmt;
	u8 ucipher;
	u8 auth;
	u8 wpa_ie[ATH6KL_MAX_IE];
	struct sk_buff_head psq;

	/* protects psq, mgmt_psq, apsdq, and mgmt_psq_len fields */
	spinlock_t psq_lock;

	struct list_head mgmt_psq;
	size_t mgmt_psq_len;
	u8 apsd_info;
	struct sk_buff_head apsdq;
	struct aggr_info_conn *aggr_conn;
	struct ath6kl_vif *vif;
};

struct ath6kl_version {
	u32 target_ver;
	u32 wlan_ver;
	u32 abi_ver;
};

struct ath6kl_bmi {
	u32 cmd_credits;
	bool done_sent;
	u8 *cmd_buf;
	u32 max_data_size;
	u32 max_cmd_size;
};

struct target_stats {
	u64 tx_pkt;
	u64 tx_byte;
	u64 tx_ucast_pkt;
	u64 tx_ucast_byte;
	u64 tx_mcast_pkt;
	u64 tx_mcast_byte;
	u64 tx_bcast_pkt;
	u64 tx_bcast_byte;
	u64 tx_rts_success_cnt;
	u64 tx_pkt_per_ac[4];

	u64 tx_err;
	u64 tx_fail_cnt;
	u64 tx_retry_cnt;
	u64 tx_mult_retry_cnt;
	u64 tx_rts_fail_cnt;

	u64 rx_pkt;
	u64 rx_byte;
	u64 rx_ucast_pkt;
	u64 rx_ucast_byte;
	u64 rx_mcast_pkt;
	u64 rx_mcast_byte;
	u64 rx_bcast_pkt;
	u64 rx_bcast_byte;
	u64 rx_frgment_pkt;

	u64 rx_err;
	u64 rx_crc_err;
	u64 rx_key_cache_miss;
	u64 rx_decrypt_err;
	u64 rx_dupl_frame;

	u64 tkip_local_mic_fail;
	u64 tkip_cnter_measures_invoked;
	u64 tkip_replays;
	u64 tkip_fmt_err;
	u64 ccmp_fmt_err;
	u64 ccmp_replays;

	u64 pwr_save_fail_cnt;

	u64 cs_bmiss_cnt;
	u64 cs_low_rssi_cnt;
	u64 cs_connect_cnt;
	u64 cs_discon_cnt;

	s32 tx_ucast_rate;
	s32 rx_ucast_rate;

	u32 lq_val;

	u32 wow_pkt_dropped;
	u16 wow_evt_discarded;

	s16 noise_floor_calib;
	s16 cs_rssi;
	s16 cs_ave_beacon_rssi;
	u8 cs_ave_beacon_snr;
	u8 cs_last_roam_msec;
	u8 cs_snr;

	u8 wow_host_pkt_wakeups;
	u8 wow_host_evt_wakeups;

	u32 arp_received;
	u32 arp_matched;
	u32 arp_replied;
};

struct ath6kl_mcc_stats {
	u32 sche_tx_queued;
	u32 tx_sched_dropped;
	u32 sche_re_tx;
	u32 recycle_drop_count;
};

struct ath6kl_mbox_info {
	u32 htc_addr;
	u32 htc_ext_addr;
	u32 htc_ext_sz;

	u32 block_size;

	u32 gmbox_addr;

	u32 gmbox_sz;
};

/*
 * 802.11i defines an extended IV for use with non-WEP ciphers.
 * When the EXTIV bit is set in the key id byte an additional
 * 4 bytes immediately follow the IV for TKIP.  For CCMP the
 * EXTIV bit is likewise set but the 8 bytes represent the
 * CCMP header rather than IV+extended-IV.
 */

#define ATH6KL_KEYBUF_SIZE 16
#define ATH6KL_MICBUF_SIZE (8+8)	/* space for both tx and rx */

#define ATH6KL_KEY_XMIT  0x01
#define ATH6KL_KEY_RECV  0x02
#define ATH6KL_KEY_DEFAULT   0x80	/* default xmit key */

/* Initial group key for AP mode */
struct ath6kl_req_key {
	bool valid;
	u8 key_index;
	int key_type;
	u8 key[WLAN_MAX_KEY_LEN];
	u8 key_len;
};

enum ath6kl_hif_type {
	ATH6KL_HIF_TYPE_SDIO,
	ATH6KL_HIF_TYPE_USB,
};

enum ath6kl_htc_type {
	ATH6KL_HTC_TYPE_MBOX,
	ATH6KL_HTC_TYPE_PIPE,
};

/* Max number of filters that hw supports */
#define ATH6K_MAX_MC_FILTERS_PER_LIST 7
struct ath6kl_mc_filter {
	struct list_head list;
	char hw_addr[ATH6KL_MCAST_FILTER_MAC_ADDR_SIZE];
};

struct ath6kl_htcap {
	bool ht_enable;
	u8 ampdu_factor;
	unsigned short cap_info;
	bool require_ht;
	u8 ext_chan;
};

/*
 * Driver's maximum limit, note that some firmwares support only one vif
 * and the runtime (current) limit must be checked from ar->vif_max.
 */
#define ATH6KL_VIF_MAX	3

/* vif flags info */
enum ath6kl_vif_state {
	CONNECTED,
	CONNECT_PEND,
	WMM_ENABLED,
	NETQ_STOPPED,
	DTIM_EXPIRED,
	NETDEV_REGISTERED,
	CLEAR_BSSFILTER_ON_BEACON,
	DTIM_PERIOD_AVAIL,
	WLAN_ENABLED,
	STATS_UPDATE_PEND,
	HOST_SLEEP_MODE_CMD_PROCESSED,
	NETDEV_MCAST_ALL_ON,
	NETDEV_MCAST_ALL_OFF,
	SCANNING,
};

struct ath6kl_vif {
	struct list_head list;
	struct wireless_dev wdev;
	struct net_device *ndev;
	struct ath6kl *ar;
	/* Lock to protect vif specific net_stats and flags */
	spinlock_t if_lock;
	u8 fw_vif_idx;
	unsigned long flags;
	int ssid_len;
	u8 ssid[IEEE80211_MAX_SSID_LEN];
	u8 dot11_auth_mode;
	u8 auth_mode;
	u8 prwise_crypto;
	u8 prwise_crypto_len;
	u8 grp_crypto;
	u8 grp_crypto_len;
	u8 def_txkey_index;
	u8 next_mode;
	u8 nw_type;
	u8 bssid[ETH_ALEN];
	u8 req_bssid[ETH_ALEN];
	u16 ch_hint;
	u16 bss_ch;
	struct ath6kl_wep_key wep_key_list[WMI_MAX_KEY_INDEX + 1];
	struct ath6kl_key keys[WMI_MAX_SUPPORT_11W_KEY_INDEX + 1];
	struct aggr_info *aggr_cntxt;
	struct ath6kl_htcap htcap[IEEE80211_NUM_BANDS];

	struct timer_list disconnect_timer;
	struct timer_list sched_scan_timer;

	struct cfg80211_scan_request *scan_req;
	enum sme_state sme_state;
	int reconnect_flag;
	u32 last_roc_id;
	u32 last_cancel_roc_id;
	u32 send_action_id;
	bool probe_req_report;
	u16 assoc_bss_beacon_int;
	u16 listen_intvl_t;
	u16 bmiss_time_t;
	u16 bg_scan_period;
	u8 assoc_bss_dtim_period;
	struct net_device_stats net_stats;
	struct target_stats target_stats;
	struct wmi_connect_cmd profile;
	u16 rsn_cap;  /* for 802.11w */

	struct list_head mc_filter;
	enum wmi_phy_mode phy_mode;
	u8 intra_bss;
	u8 ap_hold_conn;
	struct timer_list ap_restart_timer;
	struct wmi_ap_acl_list ap_acl_list;
	u32 cookie_used;
	u32 intra_bss_data_cnt;
	u32 max_num_sta;
	u32 acs_chan_mask;
};

#define WOW_LIST_ID		0
#define WOW_HOST_REQ_DELAY	500 /* ms */

#define ATH6KL_SCHED_SCAN_RESULT_DELAY 5000 /* ms */

/* Flag info */
enum ath6kl_dev_state {
	WMI_ENABLED,
	WMI_READY,
	WMI_CTRL_EP_FULL,
	TESTMODE_TCMD,
	TESTMODE_EPPING,
	DESTROY_IN_PROGRESS,
	SKIP_SCAN,
	ROAM_TBL_PEND,
	FIRST_BOOT,
};

enum ath6kl_state {
	ATH6KL_STATE_OFF,
	ATH6KL_STATE_ON,
	ATH6KL_STATE_SUSPENDING,
	ATH6KL_STATE_RESUMING,
	ATH6KL_STATE_DEEPSLEEP,
	ATH6KL_STATE_CUTPOWER,
	ATH6KL_STATE_WOW,
	ATH6KL_STATE_SCHED_SCAN,
};

/* Fw error recovery */
#define ATH6KL_HB_RESP_MISS_THRES	3
#define ATH6KL_SUSPEND_HB_DELAY	5

enum ath6kl_fw_recovery_state {
	ATH6KL_FW_RECOVERY_NONE,
	ATH6KL_FW_RECOVERY_INPROGRESS,
	ATH6KL_FW_RECOVERY_CLEANUP,
};

struct ath6kl_fw_err_recovery {
	struct ath6kl *ar;
	struct work_struct recovery_work;
	enum ath6kl_fw_recovery_state state;
	unsigned long err_reason;
	unsigned long hb_poll;
	struct timer_list hb_timer;
	u32 seq_num;
	bool hb_pending;
	u8 hb_misscnt;
};

enum ath6kl_fw_err {
	ATH6KL_FW_ASSERT,
	ATH6KL_FW_HB_RESP_FAILURE,
	ATH6KL_FW_EP_FULL,
};

struct ath6kl_fw_conn_list
{
	struct list_head conn_queue;
	struct list_head re_queue;

	u8 conn_id;
	u8 mac_addr[ETH_ALEN];
	struct ath6kl_vif *vif;

	union
	{
		struct
		{
			u8 bk_uapsd    : 1;
			u8 be_uapsd    : 1;
			u8 vi_uapsd    : 1;
			u8 vo_uapsd    : 1;
			u8 ps          : 1;
			u8 ocs         : 1;
			u8 res         : 2;
		};
		u8 connect_status;
	};
	bool previous_can_send;

	/* stats  */
	struct ath6kl_mcc_stats mcc_stats;
};

struct ath6kl_mcc_flowctrl {
	struct ath6kl *ar;
	spinlock_t mcc_flowctrl_lock;
	struct ath6kl_fw_conn_list fw_conn_list[NUM_CONN];
	u32 mcc_flowctrl_event_cnt;
	struct timer_list mcc_event_ctrl_timer;
	bool mcc_events_resumed;

};

struct ath6kl {
	struct device *dev;
	struct wiphy *wiphy;

	enum ath6kl_state state;

	struct ath6kl_bmi bmi;
	const struct ath6kl_hif_ops *hif_ops;
	const struct ath6kl_htc_ops *htc_ops;
	struct wmi *wmi;
	int tx_pending[ENDPOINT_MAX];
	int total_tx_data_pend;
	struct htc_target *htc_target;
	enum ath6kl_hif_type hif_type;
	void *hif_priv;
	struct list_head vif_list;
	/* Lock to avoid race in vif_list entries among add/del/traverse */
	spinlock_t list_lock;
	u8 num_vif;
	unsigned int vif_max;
	u8 max_norm_iface;
	u8 avail_idx_map;

	/*
	 * Protects at least amsdu_rx_buffer_queue, ath6kl_alloc_cookie()
	 * calls, tx_pending and total_tx_data_pend.
	 */
	spinlock_t lock;

	struct semaphore sem;
	u8 lrssi_roam_threshold;
	struct ath6kl_version version;
	u32 target_type;
	u8 tx_pwr;
	struct ath6kl_node_mapping node_map[MAX_NODE_NUM];
	u8 ibss_ps_enable;
	bool ibss_if_active;
	u8 node_num;
	u8 next_ep_id;
	struct ath6kl_cookie *cookie_list;
	u32 cookie_count;
	enum htc_endpoint_id ac2ep_map[WMM_NUM_AC];
	bool ac_stream_active[WMM_NUM_AC];
	u8 ac_stream_pri_map[WMM_NUM_AC];
	u8 hiac_stream_active_pri;
	u8 ep2ac_map[ENDPOINT_MAX];
	enum htc_endpoint_id ctrl_ep;
	struct ath6kl_htc_credit_info credit_state_info;
	u32 connect_ctrl_flags;
	u32 user_key_ctrl;
	u8 usr_bss_filter;
	struct ath6kl_sta sta_list[NUM_CONN];
	u8 sta_list_index;
	struct ath6kl_req_key ap_mode_bkey;
	struct sk_buff_head mcastpsq;
	u32 want_ch_switch;

	/*
	 * FIXME: protects access to mcastpsq but is actually useless as
	 * all skbe_queue_*() functions provide serialisation themselves
	 */
	spinlock_t mcastpsq_lock;

	struct wmi_ap_mode_stat ap_stats;
	u8 ap_country_code[3];
	struct list_head amsdu_rx_buffer_queue;
	u8 rx_meta_ver;
	enum wlan_low_pwr_state wlan_pwr_state;
	u8 mac_addr[ETH_ALEN];
#define AR_MCAST_FILTER_MAC_ADDR_SIZE  4
	struct {
		void *rx_report;
		size_t rx_report_len;
	} tm;

	struct ath6kl_hw {
		u32 id;
		const char *name;
		u32 dataset_patch_addr;
		u32 app_load_addr;
		u32 app_start_override_addr;
		u32 board_ext_data_addr;
		u32 reserved_ram_size;
		u32 board_addr;
		u32 refclk_hz;
		u32 uarttx_pin;
		u32 testscript_addr;
		enum wmi_phy_cap cap;

		u32 flags;

		struct ath6kl_hw_fw {
			const char *dir;
			const char *otp;
			const char *fw;
			const char *tcmd;
			const char *patch;
			const char *utf;
			const char *testscript;
			const char *epping;
		} fw;

		const char *fw_board;
		const char *fw_default_board;
		u32 uart_baud_rate;
	} hw;

	u16 conf_flags;
	u16 suspend_mode;
	u16 wow_suspend_mode;
	wait_queue_head_t event_wq;
	struct ath6kl_mbox_info mbox_info;

	struct ath6kl_cookie cookie_mem[MAX_COOKIE_NUM];
	unsigned long flag;

	u8 *fw_board;
	size_t fw_board_len;

	u8 *fw_otp;
	size_t fw_otp_len;

	u8 *fw;
	size_t fw_len;

	u8 *fw_patch;
	size_t fw_patch_len;

	u8 *fw_testscript;
	size_t fw_testscript_len;

	unsigned int fw_api;
	unsigned long fw_capabilities[ATH6KL_CAPABILITY_LEN];

	struct workqueue_struct *ath6kl_wq;
	struct workqueue_struct *ath6kl_wq_tx;
	struct workqueue_struct *ath6kl_wq_rx;

	struct dentry *debugfs_phy;

	bool p2p;

	bool wiphy_registered;

	u32 debug_quirks;

	struct ath6kl_fw_err_recovery *fw_recovery;

#ifdef CONFIG_ATH6KL_DEBUG
	struct {
		struct sk_buff_head fwlog_queue;
		struct completion fwlog_completion;
		bool fwlog_open;

		u32 fwlog_mask;

		unsigned int dbgfs_diag_reg;
		u32 diag_reg_addr_wr;
		u32 diag_reg_val_wr;

		struct {
			unsigned int invalid_rate;
		} war_stats;

		u8 *roam_tbl;
		unsigned int roam_tbl_len;

		u8 keepalive;
		u8 disc_timeout;

		struct sk_buff_head cdlog_queue;

		u32 hif_stats_mask;
	} debug;
#endif /* CONFIG_ATH6KL_DEBUG */
	struct ath6kl_mcc_flowctrl *mcc_flowctrl_ctx;
	struct ath6kl_lte_coex_priv *lte_coex;
	bool inter_bss;

	u32 tx_psq_threshold;
	bool is_mcc_enabled;
	bool acs_in_prog;
	u16 mcc_adj_ch_spacing;
	bool sta_bh_override;
	struct wmi_scan_params_cmd scan_params;
	u16 scan_params_mask;
	u16 pas_chdwell_time;
	u32 bootstrap_mode;
	u8 lte_margin;
	bool enable_ani;
};


#ifdef CONFIG_ATH6KL_BAM2BAM
/*! Fixed header configuration required in IPA end-point
	for RX/TX [ HTC (6) + WMI(6) + 802.3 (14) + LLC SNAP (8) ]

Rx-Pipe (From HSIC/HSUSB to IPA)
===============================

WLAN Header - 12 Bytes (HTC + WMI)
--------------------------------
HTC - 6 bytes (Byte1: MSB, Byte6 : LSB)
Byte1-6: Reserved for WLAN

WMI - 6 bytes (Byte1: MSB, Byte6 : LSB)
Byte1: Reserved
Byte2:
	D4-D0: Reserved
	D6 : Client Power Save (0x20)
	D7,D6 : Reserved
Byte3,4,5 : Reserved for WLAN
(Byte5 and 6 are from 16 bit value, little endian, with the below order)
Byte5:(LSB)
Byte6:(MSB)
	D0 : Exception bit (0-LTE, 1-Data to Host)
	D1 : Flush re-ordered packets to ampdu pipe
	D2 : WLAN or WAN [0-WLAN(Rx2), 1-WAN(Rx3)]
		[IPA to check and apply Source NAT or Destination NAT]
	D3 : Intra BSS
	D4 : FCS
	D5-D7 : Reserved for future requirement

802.3 Header - 14 bytes
-----------------------
Source MAC address 	: 6 bytes
Destination MAC address : 6 bytes
Type/Length 		: 2 bytes

LLC SNAP - 8 bytes
------------------
(ipv4 / ipv6 settings)

Tx-Pipe (From IPA to HSIC/HSUSB
-------------------------------
HTC - 6 Bytes (Byte1: MSB, Byte6 : LSB)
Byte1,2 : Reserved for WLAN
Byte3,4 : Length (To be filled by IPA in big endian. Length is calculated from
	the next byte)
Byte5,6 : Reserved

WMI - 6 bytes (Byte1: MSB, Byte6 : LSB)
Byte1,2,3,4 : Reserved for WLAN
Byte5 : D0,D1 (Device ID, WLAN fills this while adding partial header during
	interface is up)
	00 -"wlan0 - AP"
	01 -"wlan1 - STA"
	02 -"wlan2" (for future use)
	03 to 07-Reserved
Byte6 :
	D0 : Data from (1-Host, 0-IPA) [Filled by WLAN configuration module]
	D1 : Endianess (1-Little, 0-Big) [Filled by WLAN configuration module]
	D2-D7 : Reserved for future requirement

802.3 Header - 14 bytes
Source MAC Address	 : 6 bytes (filled by WLAN)
Destination MAC Address  : 6 bytes (To be filled by IPA)
Type/Length 		 : 2 bytes [Set to zero, WLAN FW will not use this]

LLC SNAP - 8 bytes [Filled by WLAN configuration module]

*/

#define ATH6KL_IPA_WLAN_MAC_ADDR_SIZE		6
#define ATH6KL_IPA_WLAN_HDR_LENGTH 		34
#define ATH6KL_IPA_WLAN_MAX_TX_PIPE	 	4
#define ATH6KL_IPA_WLAN_MAX_RX_PIPE	 	1
#define ATH6KL_IPA_WLAN_META_DATA_LEN		30
#define ATH6KL_IPA_WLAN_HDR_PARTIAL		1
#define ATH6KL_IPA_WLAN_IPA_HDR_COMPLETE	0
#define ATH6KL_IPA_TX_PKT_LEN_POS		28

/* SYSBAM PIPE defines */
#define MAX_SYSBAM_PIPE				1

enum ath6kl_bam_tx_evt_type {
	AMPDU_FLUSH = 0,
	BAM_WMM_AC_BK,
	BAM_WMM_AC_BE,
	BAM_WMM_AC_VI,
	BAM_WMM_AC_VO
};

#endif

enum ATH6KL_WLAN_MODE {
	ATH6KL_SINGLE_DEV_MODE     = 1,
	ATH6KL_DEFAULT_DEV_MODE    = 2,
};

static inline struct ath6kl *ath6kl_priv(struct net_device *dev)
{
	return ((struct ath6kl_vif *) netdev_priv(dev))->ar;
}

static inline u32 ath6kl_get_hi_item_addr(struct ath6kl *ar,
					u32 item_offset)
{
	u32 addr = 0;

	if (ar->target_type == TARGET_TYPE_AR6003)
		addr = ATH6KL_AR6003_HI_START_ADDR + item_offset;
	else if (ar->target_type == TARGET_TYPE_AR6004)
		addr = ATH6KL_AR6004_HI_START_ADDR + item_offset;

	return addr;
}

int ath6kl_use_regulatory_hint(void);
int ath6kl_configure_target(struct ath6kl *ar);
void ath6kl_detect_error(unsigned long ptr);
void disconnect_timer_handler(unsigned long ptr);
void init_netdev(struct net_device *dev);
void ath6kl_cookie_init(struct ath6kl *ar);
void ath6kl_cookie_cleanup(struct ath6kl *ar);
void ath6kl_rx(struct htc_target *target, struct htc_packet *packet);
void ath6kl_tx_complete(struct htc_target *context,
			struct list_head *packet_queue);
enum htc_send_full_action ath6kl_tx_queue_full(struct htc_target *target,
					       struct htc_packet *packet);
void ath6kl_stop_txrx(struct ath6kl *ar);
void ath6kl_cleanup_amsdu_rxbufs(struct ath6kl *ar);
int ath6kl_diag_write32(struct ath6kl *ar, u32 address, __le32 value);
int ath6kl_diag_write(struct ath6kl *ar, u32 address, void *data, u32 length);
int ath6kl_diag_read32(struct ath6kl *ar, u32 address, u32 *value);
int ath6kl_diag_read(struct ath6kl *ar, u32 address, void *data, u32 length);
int ath6kl_read_fwlogs(struct ath6kl *ar);
void ath6kl_init_profile_info(struct ath6kl_vif *vif);
void ath6kl_tx_data_cleanup(struct ath6kl *ar);
int ath6kl_restore_htcap(struct ath6kl_vif *vif);

struct ath6kl_cookie *ath6kl_alloc_cookie(struct ath6kl *ar, struct ath6kl_vif *vif,
							enum htc_endpoint_id eid);
void ath6kl_free_cookie(struct ath6kl *ar, struct ath6kl_vif *vif,
						struct ath6kl_cookie *cookie);
int ath6kl_data_tx(struct sk_buff *skb, struct net_device *dev);
void ath6kl_tx_scheduler(struct ath6kl_vif *vif);
void ath6kl_flowctrl_change(struct ath6kl_vif *vif);
struct aggr_info *aggr_init(struct ath6kl_vif *vif);
void aggr_conn_init(struct ath6kl_vif *vif, struct aggr_info *aggr_info,
		    struct aggr_info_conn *aggr_conn);
void ath6kl_rx_refill(struct htc_target *target,
		      enum htc_endpoint_id endpoint);
void ath6kl_refill_amsdu_rxbufs(struct ath6kl *ar, int count);
struct htc_packet *ath6kl_alloc_amsdu_rxbuf(struct htc_target *target,
					    enum htc_endpoint_id endpoint,
					    int len);
void aggr_module_destroy(struct aggr_info *aggr_info);
void aggr_reset_state(struct aggr_info_conn *aggr_conn);

struct ath6kl_sta *ath6kl_find_sta(struct ath6kl_vif *vif, u8 *node_addr,
		bool inter_bss);
struct ath6kl_sta *ath6kl_find_sta_by_aid(struct ath6kl_vif *vif, u8 aid);

void ath6kl_ready_event(void *devt, u8 *datap, u32 sw_ver, u32 abi_ver,
			enum wmi_phy_cap cap);
int ath6kl_control_tx(void *devt, struct sk_buff *skb,
		      enum htc_endpoint_id eid);
void ath6kl_connect_event(struct ath6kl_vif *vif, u16 channel,
			  u8 *bssid, u16 listen_int,
			  u16 beacon_int, enum network_type net_type,
			  u8 beacon_ie_len, u8 assoc_req_len,
			  u8 assoc_resp_len, u8 *assoc_info);
void ath6kl_connect_ap_mode_bss(struct ath6kl_vif *vif, u16 channel, u8 sec_ch, u8 phymode);
void ath6kl_connect_ap_mode_sta(struct ath6kl_vif *vif, u16 aid, u8 *mac_addr,
				u8 keymgmt, u8 ucipher, u8 auth,
				u8 assoc_req_len, u8 *assoc_info, u8 apsd_info);
void ath6kl_disconnect_event(struct ath6kl_vif *vif, u8 reason,
			     u8 *bssid, u8 assoc_resp_len,
			     u8 *assoc_info, u16 prot_reason_status);
void ath6kl_tkip_micerr_event(struct ath6kl_vif *vif, u8 keyid, bool ismcast);
void ath6kl_txpwr_rx_evt(void *devt, u8 tx_pwr);
void ath6kl_scan_complete_evt(struct ath6kl_vif *vif, int status);
void ath6kl_tgt_stats_event(struct ath6kl_vif *vif, u8 *ptr, u32 len);
void ath6kl_get_rsn_cap_event(struct ath6kl_vif *vif, u16 rsn_cap);
void ath6kl_indicate_tx_activity(void *devt, u8 traffic_class, bool active);
enum htc_endpoint_id ath6kl_ac2_endpoint_id(void *devt, u8 ac);

void ath6kl_pspoll_event(struct ath6kl_vif *vif, u8 aid);

void ath6kl_dtimexpiry_event(struct ath6kl_vif *vif);
void ath6kl_disconnect(struct ath6kl_vif *vif);
void aggr_recv_delba_req_evt(struct ath6kl_vif *vif, u8 tid);
void aggr_recv_addba_req_evt(struct ath6kl_vif *vif, u8 tid, u16 seq_no,
			     u8 win_sz);
void ath6kl_wakeup_event(void *dev);

void ath6kl_reset_device(struct ath6kl *ar, u32 target_type,
			 bool wait_fot_compltn, bool cold_reset);
void ath6kl_init_control_info(struct ath6kl_vif *vif);
struct ath6kl_vif *ath6kl_vif_first(struct ath6kl *ar);
void ath6kl_cleanup_vif(struct ath6kl_vif *vif, bool wmi_ready);
int ath6kl_init_hw_start(struct ath6kl *ar);
int ath6kl_init_hw_stop(struct ath6kl *ar);
int ath6kl_init_fetch_firmwares(struct ath6kl *ar);
int ath6kl_init_hw_params(struct ath6kl *ar);

void ath6kl_check_wow_status(struct ath6kl *ar);

void ath6kl_core_tx_complete(struct ath6kl *ar, struct sk_buff *skb);
void ath6kl_core_rx_complete(struct ath6kl *ar, struct sk_buff *skb, u8 pipe);

struct ath6kl *ath6kl_core_create(struct device *dev);
int ath6kl_core_init(struct ath6kl *ar, enum ath6kl_htc_type htc_type);
void ath6kl_core_cleanup(struct ath6kl *ar);
void ath6kl_core_destroy(struct ath6kl *ar);
void ath6kl_ap_restart_timer(unsigned long ptr);
int _string_to_mac(char *string, int len, u8 *macaddr);

/* Fw error recovery */
void ath6kl_recovery_work(struct work_struct *work);
void ath6kl_recovery_init(struct ath6kl *ar);
void ath6kl_recovery_err_notify(struct ath6kl *ar, enum ath6kl_fw_err reason);
void ath6kl_update_hb_event(struct ath6kl *ar);
void ath6kl_init_hw_restart(struct ath6kl *ar);
void ath6kl_init_hw_cold_restart(struct ath6kl *ar);
void ath6kl_recovery_cleanup(struct ath6kl *ar);
void ath6kl_recovery_suspend(struct ath6kl *ar);
void ath6kl_recovery_resume(struct ath6kl *ar);

#ifdef CONFIG_ATH6KL_BAM2BAM
/* IPA configuration related APIs */
int ath6kl_ipa_add_flt_rule(struct ath6kl *ar, enum ipa_client_type client);
int ath6kl_ipa_add_header_info(struct ath6kl *ar, u8 sta_ap, u8 device_id,
		char *hdr_name, u8 *mac_addr);
int ath6kl_ipa_get_header_info(char *hdr_name, uint32_t *hdl);
int ath6kl_ipa_register_interface(struct ath6kl *ar,u8 sta_ap, const char *name,
		char *hdr_name);
int ath6kl_ipacm_get_ep_config_info(u32 ipa_client, struct ipa_ep_cfg *ep_cfg);
int ath6kl_send_msg_ipa(struct ath6kl_vif *vif, enum ipa_wlan_event type,
								u8 *mac_addr);

/* IPA SYSBAM configuration related APIs */
void ath6kl_disconnect_sysbam_pipes(struct ath6kl *ar);
int ath6kl_usb_create_sysbam_pipes(struct ath6kl *ar);

/* Out of order processing APIs */
void ath6kl_aggr_deque_bam2bam(struct ath6kl_vif *vif, u16 seq_no,u8 tid,
		u8 aid);
int ath6kl_send_dummy_data(struct ath6kl_vif *vif, u8 ac_category);
/* Power save event handler */
void ath6kl_client_power_save(struct ath6kl_vif *vif, u8 power_save, u8 aid);
void ath6kl_allow_packet_drop(struct ath6kl_vif *vif, u8 enable_drop);
/* IPA interface clean up */
void ath6kl_clean_ipa_headers(struct ath6kl *ar, char *name);
void ath6kl_remove_ipa_exception_filters(struct ath6kl *ar);
/* MCC related functions */
int ath6kl_ipa_enable_host_route_config (struct ath6kl_vif *vif, bool enable);
#endif

struct ath6kl_mcc_flowctrl *ath6kl_mcc_flowctrl_conn_list_init(struct ath6kl *ar);
void ath6kl_mcc_flowctrl_conn_list_deinit(struct ath6kl *ar);
void ath6kl_mcc_flowctrl_tx_schedule(struct ath6kl *ar, u8 is_ch_chg);
enum htc_send_queue_result ath6kl_mcc_flowctrl_tx_schedule_pkt(struct ath6kl *ar, void *pkt);
void ath6kl_mcc_flowctrl_state_change(struct ath6kl *ar);
void ath6kl_mcc_flowctrl_state_update(struct ath6kl *ar,u8 numConn,
		u8 ac_map[], u8 ac_queue_depth[]);
void ath6kl_mcc_flowctrl_set_conn_id(struct ath6kl_vif *vif, u8 mac_addr[],
		u8 connId);
u8 ath6kl_mcc_flowctrl_get_conn_id(struct ath6kl_vif *vif, struct sk_buff *skb);
int ath6kl_mcc_flowctrl_stat(struct ath6kl_fw_conn_list *conn, u8 *buf,
		int buf_len);
int ath6kl_is_mcc_enabled (struct ath6kl *ar);
int ath6kl_check_adj_vif_ch_overlap(struct ath6kl_vif *vif, uint16_t chan);
u16 ath6kl_process_user_defined_acs(struct ath6kl *ar,
                                struct ath6kl_vif *vif,
                                u16 adj_vif_ch,
                                u16 cur_vif_ch,
                                u32 *acs_chan_mask);
u32 ath6kl_get_chmask_for_acstype(struct ath6kl_vif *vif, u32 ch);
#endif /* CORE_H */
