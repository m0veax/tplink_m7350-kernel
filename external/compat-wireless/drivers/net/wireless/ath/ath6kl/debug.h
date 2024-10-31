/*
 * Copyright (c) 2011 Atheros Communications Inc.
 * Copyright (c) 2011-2012 Qualcomm Atheros, Inc.
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

#ifndef DEBUG_H
#define DEBUG_H

#include "hif.h"

#define ATH6KL_DBGPRNT_FREQ 1000

#define ATH6KL_RECOVERY_CRASH_DUMP_FILE	"/tmp/crash_dump"

enum ATH6KL_MODULE_QUIRKS {
	/* Enable Cut power mode */
	ATH6KL_MODULE_SUSPEND_CUTPOWER		= BIT(0),

	/* Enable Deepsleep power mode */
	ATH6KL_MODULE_SUSPEND_DEEPSLEEP		= BIT(1),

	/* Enable Wake on Wireless power mode */
	ATH6KL_MODULE_SUSPEND_WOW		= BIT(2),

	/* Enable TCMD based test mode */
	ATH6KL_MODULE_TESTMODE_TCMD		= BIT(3),

	/* Enable UTF based test mode */
	ATH6KL_MODULE_TESTMODE_UTF		= BIT(4),

	/* Enable UART debug */
	ATH6KL_MODULE_UART_DEBUG		= BIT(5),

	/* Enable end point ping loop back test mode */
	ATH6KL_MODULE_ENABLE_EPPING		= BIT(6),

	/* enable lpl */
	AT6HKL_MODULE_LPL_ENABLE		= BIT(7),

	/* hole */

	/* Enable BAM2BAM feature */
	ATH6KL_MODULE_BAM2BAM			= BIT(10),

	/* Enable IPV6 support */
	ATH6KL_MODULE_IPA_WITH_IPV6		= BIT(11),

	/* Enable IPACM support */
	ATH6KL_MODULE_IPA_WITH_IPACM		= BIT(12),

	/* Send AMPDU packets to netif */
	ATH6KL_MODULE_BAM_AMPDU_TO_NETIF	= BIT(13),

	/* Use Rx SW path instead of BAM */
	ATH6KL_MODULE_BAM_RX_SW_PATH		= BIT(14),

	/* Enable SCC or by default MCC */
	ATH6KL_MODULE_MCC_FLOWCTRL		= BIT(15),

	/* Enable USB auto power management feature */
	ATH6KL_MODULE_ENABLE_USB_AUTO_PM	= BIT(16),

	/* Use TX SW path instead of BAM */
	ATH6KL_MODULE_BAM_TX_SW_PATH		= BIT(17),

	/*Firmware error recovery */
	ATH6KL_MODULE_FW_ERROR_RECOVERY		= BIT(18),
};

#define ATH6KL_DEF_DEBUG_QUIRKS (ATH6KL_MODULE_BAM2BAM |		\
				ATH6KL_MODULE_IPA_WITH_IPV6 |		\
				ATH6KL_MODULE_IPA_WITH_IPACM |		\
				ATH6KL_MODULE_BAM_AMPDU_TO_NETIF |	\
				ATH6KL_MODULE_BAM_RX_SW_PATH |		\
				ATH6KL_MODULE_MCC_FLOWCTRL |		\
				ATH6KL_MODULE_ENABLE_USB_AUTO_PM |	\
				ATH6KL_MODULE_SUSPEND_WOW)

enum ATH6K_DEBUG_MASK {
	ATH6KL_DBG_CREDIT	= BIT(0),
	/* hole */
	ATH6KL_DBG_WLAN_TX      = BIT(2),     /* wlan tx */
	ATH6KL_DBG_WLAN_RX      = BIT(3),     /* wlan rx */
	ATH6KL_DBG_BMI		= BIT(4),     /* bmi tracing */
	ATH6KL_DBG_HTC		= BIT(5),
	ATH6KL_DBG_HIF		= BIT(6),
	ATH6KL_DBG_IRQ		= BIT(7),     /* interrupt processing */
	/* hole */
	/* hole */
	ATH6KL_DBG_WMI          = BIT(10),    /* wmi tracing */
	ATH6KL_DBG_TRC	        = BIT(11),    /* generic func tracing */
	ATH6KL_DBG_SCATTER	= BIT(12),    /* hif scatter tracing */
	ATH6KL_DBG_WLAN_CFG     = BIT(13),    /* cfg80211 i/f file tracing */
	ATH6KL_DBG_RAW_BYTES    = BIT(14),    /* dump tx/rx frames */
	ATH6KL_DBG_AGGR		= BIT(15),    /* aggregation */
	ATH6KL_DBG_SDIO		= BIT(16),
	ATH6KL_DBG_SDIO_DUMP	= BIT(17),
	ATH6KL_DBG_BOOT		= BIT(18),    /* driver init and fw boot */
	ATH6KL_DBG_WMI_DUMP	= BIT(19),
	ATH6KL_DBG_SUSPEND	= BIT(20),
	ATH6KL_DBG_USB		= BIT(21),
	ATH6KL_DBG_USB_BULK	= BIT(22),
	ATH6KL_DBG_PLAT		= BIT(23),
	ATH6KL_DBG_BAM2BAM	= BIT(24),
	ATH6KL_DBG_OOO		= BIT(25),   /* Out of order tracing */
	ATH6KL_DBG_IPA_MSG	= BIT(26),   /* IPA message tracing */
	ATH6KL_DBG_FLOWCTRL     = BIT(27),
	ATH6KL_DBG_LTE_COEX	= BIT(28),
	ATH6KL_DBG_RECOVERY     = BIT(29),
	ATH6KL_DBG_ANY	        = 0xffffffff  /* enable all logs */
};

extern unsigned int debug_mask;
extern __printf(2, 3)

int ath6kl_printk(const char *level, const char *fmt, ...);

#define ath6kl_info(fmt, ...)				\
	ath6kl_printk(KERN_INFO, fmt, ##__VA_ARGS__)
#define ath6kl_err(fmt, ...)					\
	ath6kl_printk(KERN_ERR, fmt, ##__VA_ARGS__)
#define ath6kl_warn(fmt, ...)					\
	ath6kl_printk(KERN_WARNING, fmt, ##__VA_ARGS__)

enum ath6kl_war {
	ATH6KL_WAR_INVALID_RATE,
};

/* Return true if any bit set in mask */
static inline bool ath6kl_debug_quirks_any(struct ath6kl *ar, enum
		ATH6KL_MODULE_QUIRKS mask)
{
	return !!(ar->debug_quirks & mask);
}

/* Return true only if all the bits are set in mask */
static inline bool ath6kl_debug_quirks_all(struct ath6kl *ar, enum
		ATH6KL_MODULE_QUIRKS mask)
{
	return (ar->debug_quirks & mask) == mask;
}

#define ath6kl_debug_quirks(_ar, _mask) ath6kl_debug_quirks_any(_ar, _mask)

#ifdef CONFIG_ATH6KL_DEBUG

void ath6kl_dbg(enum ATH6K_DEBUG_MASK mask, const char *fmt, ...);
void ath6kl_dbg_dump(enum ATH6K_DEBUG_MASK mask,
		     const char *msg, const char *prefix,
		     const void *buf, size_t len);

void ath6kl_dump_registers(struct ath6kl_device *dev,
			   struct ath6kl_irq_proc_registers *irq_proc_reg,
			   struct ath6kl_irq_enable_reg *irq_en_reg);
void dump_cred_dist_stats(struct htc_target *target);
void ath6kl_debug_fwlog_event(struct ath6kl *ar, const void *buf, size_t len);
void ath6kl_debug_war(struct ath6kl *ar, enum ath6kl_war war);
int ath6kl_debug_roam_tbl_event(struct ath6kl *ar, const void *buf,
				size_t len);
void ath6kl_debug_set_keepalive(struct ath6kl *ar, u8 keepalive);
void ath6kl_debug_set_disconnect_timeout(struct ath6kl *ar, u8 timeout);
void ath6kl_debug_init(struct ath6kl *ar);
int ath6kl_debug_init_fs(struct ath6kl *ar);
void ath6kl_debug_cleanup(struct ath6kl *ar);
void ath6kl_recovery_dump_crash_info(struct ath6kl *ar);

#else
static inline int ath6kl_dbg(enum ATH6K_DEBUG_MASK dbg_mask,
			     const char *fmt, ...)
{
	return 0;
}

static inline void ath6kl_dbg_dump(enum ATH6K_DEBUG_MASK mask,
				   const char *msg, const char *prefix,
				   const void *buf, size_t len)
{
}

static inline void ath6kl_dump_registers(struct ath6kl_device *dev,
		struct ath6kl_irq_proc_registers *irq_proc_reg,
		struct ath6kl_irq_enable_reg *irq_en_reg)
{

}
static inline void dump_cred_dist_stats(struct htc_target *target)
{
}

static inline void ath6kl_debug_fwlog_event(struct ath6kl *ar,
					    const void *buf, size_t len)
{
}

static inline void ath6kl_debug_war(struct ath6kl *ar, enum ath6kl_war war)
{
}

static inline int ath6kl_debug_roam_tbl_event(struct ath6kl *ar,
					      const void *buf, size_t len)
{
	return 0;
}

static inline void ath6kl_debug_set_keepalive(struct ath6kl *ar, u8 keepalive)
{
}

static inline void ath6kl_debug_set_disconnect_timeout(struct ath6kl *ar,
						       u8 timeout)
{
}

static inline void ath6kl_debug_init(struct ath6kl *ar)
{
}

static inline int ath6kl_debug_init_fs(struct ath6kl *ar)
{
	return 0;
}

static inline void ath6kl_debug_cleanup(struct ath6kl *ar)
{
}

#endif

#endif
