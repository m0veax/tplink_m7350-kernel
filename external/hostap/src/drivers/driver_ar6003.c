/*
 * Copyright (c) 2010-2012 The Linux Foundation. All rights reserved
 *
 * Software was previously licensed under BSD license by Qualcomm Atheros, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following
 * disclaimer in the documentation and/or other materials provided
 * with the distribution.
 * Neither the name of The Linux Foundation nor the names of its
 * contributors may be used to endorse or promote products derived from this
 * software without specific prior written permission.
 *
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT ARE DISCLAIMED.  IN NO
 * EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "includes.h"
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <net/if_arp.h>
#include "linux_wext.h"
#include "common.h"
#include "eloop.h"
#include "common/ieee802_11_defs.h"
#include "common/wpa_common.h"
#include "priv_netlink.h"
#include "netlink.h"
#include "linux_ioctl.h"
#include "driver.h"
#include <net/if.h>
#undef WPA_OUI_TYPE
#undef WMM_OUI_TYPE
#include <athdefs.h>
#include <a_types.h>
#include <a_osapi.h>
#include <wmi.h>
#include <athdrv_linux.h>
#include <athtypes_linux.h>
#include <ieee80211.h>
#include <ieee80211_ioctl.h>
#include "radius/radius.h"
#include "l2_packet/l2_packet.h"

struct ar6003_driver_data {
    void *ctx;
    struct netlink_data *netlink;
    int ioctl_sock;
    int mlme_sock;
    char ifname[IFNAMSIZ + 1];
    char shared_ifname[IFNAMSIZ];
    int ifindex;
    int ifindex2;
    int if_removed;
    u8 *assoc_req_ies;
    size_t assoc_req_ies_len;
    u8 *assoc_resp_ies;
    size_t assoc_resp_ies_len;
    struct wpa_driver_capa capa;
    int has_capability;
    int we_version_compiled;

    int use_crypt;
    int auth_alg_fallback;
    int operstate;
    int scan_complete_events;
    struct l2_packet_data *sock_xmit;   /* raw packet xmit socket */
    struct l2_packet_data *sock_recv;   /* raw packet recv socket */
    int we_version;
    int wext_sock;                     /* socket for wireless events */
    u8 max_level;
};

void ar6003_driver_scan_timeout(void *eloop_ctx, void *timeout_ctx);
static int ar6003_driver_finish_drv_init(struct ar6003_driver_data *drv);
static int ar6003_driver_flush_pmkid(void *priv);
static int ar6003_driver_get_range(void *priv);
int ar6003_driver_set_mode(void *priv, int mode);
int ar6003_driver_alternative_ifindex(struct ar6003_driver_data *drv, const char *ifname);
static void ar6003_driver_disconnect(struct ar6003_driver_data *drv);
static int ar6003_set_wps_ie(void *priv, const u8 *iebuf, size_t iebuflen, u32 frametype);
static int ar6003_driver_set_auth_alg(void *priv, int auth_alg);
static int set80211param(struct ar6003_driver_data *drv, int op, int arg);

static void
ar6003_wireless_event_deinit(void *priv)
{
    struct ar6003_driver_data *drv = priv;

    if (drv != NULL) {
        if (drv->wext_sock < 0)
            return;
        eloop_unregister_read_sock(drv->wext_sock);
        close(drv->wext_sock);
    }
}

static int
set80211priv(struct ar6003_driver_data *drv, int op, void *data, int len)
{
    struct iwreq iwr;

    memset(&iwr, 0, sizeof(iwr));
    strncpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
    iwr.u.data.pointer = data;
    iwr.u.data.length = len;

    if (ioctl(drv->ioctl_sock, op, &iwr) < 0) {
        int first = IEEE80211_IOCTL_SETPARAM;
        static const char *opnames[] = {
            "ioctl[IEEE80211_IOCTL_SETPARAM]",
            "ioctl[IEEE80211_IOCTL_SETKEY]",
            "ioctl[IEEE80211_IOCTL_DELKEY]",
            "ioctl[IEEE80211_IOCTL_SETMLME]",
            "ioctl[IEEE80211_IOCTL_ADDPMKID]",
            "ioctl[IEEE80211_IOCTL_SETOPTIE]",
            "ioctl[SIOCIWFIRSTPRIV+6]",
            "ioctl[SIOCIWFIRSTPRIV+7]",
            "ioctl[SIOCIWFIRSTPRIV+8]",
            "ioctl[SIOCIWFIRSTPRIV+9]",
            "ioctl[SIOCIWFIRSTPRIV+10]",
            "ioctl[SIOCIWFIRSTPRIV+11]",
            "ioctl[SIOCIWFIRSTPRIV+12]",
            "ioctl[SIOCIWFIRSTPRIV+13]",
            "ioctl[SIOCIWFIRSTPRIV+14]",
            "ioctl[SIOCIWFIRSTPRIV+15]",
            "ioctl[SIOCIWFIRSTPRIV+16]",
            "ioctl[SIOCIWFIRSTPRIV+17]",
            "ioctl[SIOCIWFIRSTPRIV+18]",
        };
        int idx = op - first;
        if (first <= op &&
                idx < (int) (sizeof(opnames) / sizeof(opnames[0])) &&
                opnames[idx])
            perror(opnames[idx]);
        else
            perror("ioctl[unknown???]");
        return -1;
    }
    return 0;
}

static int ar6003_driver_capa(struct ar6003_driver_data *drv)
{
    drv->has_capability = 1;
    /* For now, assume TKIP, CCMP, WPA, WPA2 are supported */

    drv->capa.key_mgmt |= (WPA_DRIVER_CAPA_KEY_MGMT_WPA |
            WPA_DRIVER_CAPA_KEY_MGMT_WPA_PSK |
            WPA_DRIVER_CAPA_KEY_MGMT_WPA2 |
            WPA_DRIVER_CAPA_KEY_MGMT_WPA2_PSK);
    drv->capa.enc |= (WPA_DRIVER_CAPA_ENC_WEP40 |
            WPA_DRIVER_CAPA_ENC_WEP104 |
            WPA_DRIVER_CAPA_ENC_TKIP |
            WPA_DRIVER_CAPA_ENC_CCMP);
    drv->capa.auth |= (WPA_DRIVER_AUTH_OPEN |
            WPA_DRIVER_AUTH_SHARED);

    drv->capa.flags |= WPA_DRIVER_FLAGS_AP;
    drv->capa.flags |= WPA_DRIVER_FLAGS_SET_KEYS_AFTER_ASSOC_DONE;
#ifdef CONFIG_HS20
    drv->capa.flags |= WPA_DRIVER_FLAGS_OFFCHANNEL_TX ;
#endif /* CONFIG_HS20 */
    drv->capa.max_scan_ssids = 1;

    return 0;
}
int ar6003_driver_set_auth_param(struct ar6003_driver_data *drv,
        int idx, u32 value)
{
    struct iwreq iwr;
    int ret = 0;

    wpa_printf(MSG_DEBUG, "%s", __FUNCTION__);
    os_memset(&iwr, 0, sizeof(iwr));
    os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
    iwr.u.param.flags = idx & IW_AUTH_INDEX;
    iwr.u.param.value = value;


    if (ioctl(drv->ioctl_sock, SIOCSIWAUTH, &iwr) < 0) {
        if (errno != EOPNOTSUPP) {
            wpa_printf(MSG_DEBUG, "SIOCSIWAUTH(param %d "
                    "value 0x%x) failed: %s)",
                    idx, value, strerror(errno));
        }
        ret = errno == EOPNOTSUPP ? -2 : -1;
    }

    return ret;
}


/**
 * ar6003_driver_get_bssid - Get BSSID, SIOCGIWAP
 * @priv: Pointer to private data from ar6003_driver_init()
 * @bssid: Buffer for BSSID
 * Returns: 0 on success, -1 on failure
 */
int ar6003_driver_get_bssid(void *priv, u8 *bssid)
{
    struct ar6003_driver_data *drv = priv;
    struct iwreq iwr;
    int ret = 0;

    os_memset(&iwr, 0, sizeof(iwr));
    os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);

    if (ioctl(drv->ioctl_sock, SIOCGIWAP, &iwr) < 0) {
        perror("ioctl[SIOCGIWAP]");
        ret = -1;
    }
    os_memcpy(bssid, iwr.u.ap_addr.sa_data, ETH_ALEN);

    return ret;
}


/**
 * ar6003_driver_set_bssid - Set BSSID, SIOCSIWAP
 * @priv: Pointer to private data from ar6003_driver_init()
 * @bssid: BSSID
 * Returns: 0 on success, -1 on failure
 */
int ar6003_driver_set_bssid(void *priv, const u8 *bssid)
{
    struct ar6003_driver_data *drv = priv;
    struct iwreq iwr;
    int ret = 0;

    wpa_printf(MSG_DEBUG, "%s", __FUNCTION__);
    os_memset(&iwr, 0, sizeof(iwr));
    os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
    iwr.u.ap_addr.sa_family = ARPHRD_ETHER;
    if (bssid)
        os_memcpy(iwr.u.ap_addr.sa_data, bssid, ETH_ALEN);
    else
        os_memset(iwr.u.ap_addr.sa_data, 0, ETH_ALEN);

    if (ioctl(drv->ioctl_sock, SIOCSIWAP, &iwr) < 0) {
        perror("ioctl[SIOCSIWAP]");
        ret = -1;
    }

    return ret;
}


/**
 * ar6003_driver_get_ssid - Get SSID, SIOCGIWESSID
 * @priv: Pointer to private data from ar6003_driver_init()
 * @ssid: Buffer for the SSID; must be at least 32 bytes long
 * Returns: SSID length on success, -1 on failure
 */
int ar6003_driver_get_ssid(void *priv, u8 *ssid)
{
    struct ar6003_driver_data *drv = priv;
    struct iwreq iwr;
    int ret = 0;

    os_memset(&iwr, 0, sizeof(iwr));
    os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
    iwr.u.essid.pointer = (caddr_t) ssid;
    iwr.u.essid.length = 32;

    if (ioctl(drv->ioctl_sock, SIOCGIWESSID, &iwr) < 0) {
        perror("ioctl[SIOCGIWESSID]");
        ret = -1;
    } else {
        ret = iwr.u.essid.length;
        if (ret > 32)
            ret = 32;
        /* Some drivers include nul termination in the SSID, so let's
         * remove it here before further processing. WE-21 changes this
         * to explicitly require the length _not_ to include nul
         * termination. */
        if (ret > 0 && ssid[ret - 1] == '\0' &&
                drv->we_version_compiled < 21)
            ret--;
    }

    return ret;
}


/**
 * ar6003_driver_set_ssid - Set SSID, SIOCSIWESSID
 * @priv: Pointer to private data from ar6003_driver_init()
 * @ssid: SSID
 * @ssid_len: Length of SSID (0..32)
 * Returns: 0 on success, -1 on failure
 */
int ar6003_driver_set_ssid(void *priv, const u8 *ssid, size_t ssid_len)
{
    struct ar6003_driver_data *drv = priv;
    struct iwreq iwr;
    int ret = 0;
    char buf[33];


    wpa_printf(MSG_DEBUG, "%s", __FUNCTION__);

    if (ssid_len > 32)
        return -1;

    wpa_printf(MSG_DEBUG, "ssid len=%d",ssid_len);
    os_memset(&iwr, 0, sizeof(iwr));
    os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
    /* flags: 1 = ESSID is active, 0 = not (promiscuous) */
    iwr.u.essid.flags = (ssid_len != 0);
    os_memset(buf, 0, sizeof(buf));
    os_memcpy(buf, ssid, ssid_len);
    iwr.u.essid.pointer = (caddr_t) buf;
    if (drv->we_version_compiled < 21) {
        /* For historic reasons, set SSID length to include one extra
         * character, C string nul termination, even though SSID is
         * really an octet string that should not be presented as a C
         * string. Some Linux drivers decrement the length by one and
         * can thus end up missing the last octet of the SSID if the
         * length is not incremented here. WE-21 changes this to
         * explicitly require the length _not_ to include nul
         * termination. */
        if (ssid_len)
            ssid_len++;
    }
    iwr.u.essid.length = ssid_len;

    if (ioctl(drv->ioctl_sock, SIOCSIWESSID, &iwr) < 0) {
        wpa_printf(MSG_DEBUG, "command did not succeed");
        perror("ioctl[SIOCSIWESSID]");
        ret = -1;
    }

    return ret;
}


/**
 * ar6003_driver_set_freq - Set frequency/channel, SIOCSIWFREQ
 * @priv: Pointer to private data from ar6003_driver_init()
 * @freq: Frequency in MHz
 * Returns: 0 on success, -1 on failure
 */
int ar6003_driver_set_freq(void *priv, int freq)
{
    struct ar6003_driver_data *drv = priv;
    struct iwreq iwr;
    int ret = 0;

    wpa_printf(MSG_DEBUG, "%s", __FUNCTION__);
    os_memset(&iwr, 0, sizeof(iwr));
    os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
    iwr.u.freq.m = freq * 100000;
    iwr.u.freq.e = 1;

    if (ioctl(drv->ioctl_sock, SIOCSIWFREQ, &iwr) < 0) {
        perror("ioctl[SIOCSIWFREQ]");
        ret = -1;
    }

    return ret;
}

static void
ar6003_driver_event_wireless_custom(void *drv, char *custom)
{
    union wpa_event_data data;
    void *ctx;

    ctx = ((struct ar6003_driver_data *)drv)->ctx;

    wpa_printf(MSG_MSGDUMP, "Custom wireless event: '%s'",
            custom);

    os_memset(&data, 0, sizeof(data));
    /* Host AP driver */
    if (os_strncmp(custom, "MLME-MICHAELMICFAILURE.indication", 33) == 0) {
        data.michael_mic_failure.unicast =
            os_strstr(custom, " unicast ") != NULL;
        /* TODO: parse parameters(?) */
        wpa_supplicant_event(ctx, EVENT_MICHAEL_MIC_FAILURE, &data);
    } else if (os_strncmp(custom, "ASSOCINFO(ReqIEs=", 17) == 0) {
        char *spos;
        int bytes;
        u8 *req_ies = NULL, *resp_ies = NULL;

        spos = custom + 17;

        bytes = strspn(spos, "0123456789abcdefABCDEF");
        if (!bytes || (bytes & 1))
            return;
        bytes /= 2;

        req_ies = os_malloc(bytes);
        if (req_ies == NULL ||
                hexstr2bin(spos, req_ies, bytes) < 0)
            goto done;
        data.assoc_info.req_ies = req_ies;
        data.assoc_info.req_ies_len = bytes;

        spos += bytes * 2;

        data.assoc_info.resp_ies = NULL;
        data.assoc_info.resp_ies_len = 0;

        if (os_strncmp(spos, " RespIEs=", 9) == 0) {
            spos += 9;

            bytes = strspn(spos, "0123456789abcdefABCDEF");
            if (!bytes || (bytes & 1))
                goto done;
            bytes /= 2;

            resp_ies = os_malloc(bytes);
            if (resp_ies == NULL ||
                    hexstr2bin(spos, resp_ies, bytes) < 0)
                goto done;
            data.assoc_info.resp_ies = resp_ies;
            data.assoc_info.resp_ies_len = bytes;
        }

        wpa_supplicant_event(ctx, EVENT_ASSOCINFO, &data);

done:
        os_free(resp_ies);
        os_free(req_ies);
#ifdef CONFIG_PEERKEY
    } else if (os_strncmp(custom, "STKSTART.request=", 17) == 0) {
        if (hwaddr_aton(custom + 17, data.stkstart.peer)) {
            wpa_printf(MSG_DEBUG, "unrecognized "
                    "STKSTART.request '%s'", custom + 17);
            return;
        }
        wpa_supplicant_event(ctx, EVENT_STKSTART, &data);
#endif /* CONFIG_PEERKEY */
    }
}


static int ar6003_driver_event_wireless_michaelmicfailure(
        void *ctx, const char *ev, size_t len)
{
    const struct iw_michaelmicfailure *mic;
    union wpa_event_data data;

    if (len < sizeof(*mic))
        return -1;

    mic = (const struct iw_michaelmicfailure *) ev;

    wpa_printf(MSG_DEBUG, "Michael MIC failure wireless event: "
            "flags=0x%x src_addr=" MACSTR, mic->flags,
            MAC2STR(mic->src_addr.sa_data));

    os_memset(&data, 0, sizeof(data));
    data.michael_mic_failure.unicast = !(mic->flags & IW_MICFAILURE_GROUP);
    wpa_supplicant_event(ctx, EVENT_MICHAEL_MIC_FAILURE, &data);

    return 0;
}


static int ar6003_driver_event_wireless_pmkidcand(
        struct ar6003_driver_data *drv, const char *ev, size_t len)
{
    const struct iw_pmkid_cand *cand;
    union wpa_event_data data;
    const u8 *addr;

    if (len < sizeof(*cand))
        return -1;

    cand = (const struct iw_pmkid_cand *) ev;
    addr = (const u8 *) cand->bssid.sa_data;

    wpa_printf(MSG_DEBUG, "PMKID candidate wireless event: "
            "flags=0x%x index=%d bssid=" MACSTR, cand->flags,
            cand->index, MAC2STR(addr));

    os_memset(&data, 0, sizeof(data));
    os_memcpy(data.pmkid_candidate.bssid, addr, ETH_ALEN);
    data.pmkid_candidate.index = cand->index;
    data.pmkid_candidate.preauth = cand->flags & IW_PMKID_CAND_PREAUTH;
    wpa_supplicant_event(drv->ctx, EVENT_PMKID_CANDIDATE, &data);

    return 0;
}


static int ar6003_driver_event_wireless_assocreqie(
        struct ar6003_driver_data *drv, const char *ev, int len)
{
    if (len < 0)
        return -1;

    wpa_hexdump(MSG_DEBUG, "AssocReq IE wireless event", (const u8 *) ev,
            len);
    os_free(drv->assoc_req_ies);
    drv->assoc_req_ies = os_malloc(len);
    if (drv->assoc_req_ies == NULL) {
        drv->assoc_req_ies_len = 0;
        return -1;
    }
    os_memcpy(drv->assoc_req_ies, ev, len);
    drv->assoc_req_ies_len = len;

    return 0;
}


static int ar6003_driver_event_wireless_assocrespie(
        struct ar6003_driver_data *drv, const char *ev, int len)
{
    if (len < 0)
        return -1;

    wpa_hexdump(MSG_DEBUG, "AssocResp IE wireless event", (const u8 *) ev,
            len);
    os_free(drv->assoc_resp_ies);
    drv->assoc_resp_ies = os_malloc(len);
    if (drv->assoc_resp_ies == NULL) {
        drv->assoc_resp_ies_len = 0;
        return -1;
    }
    os_memcpy(drv->assoc_resp_ies, ev, len);
    drv->assoc_resp_ies_len = len;

    return 0;
}


static void ar6003_driver_event_assoc_ies(struct ar6003_driver_data *drv)
{
    union wpa_event_data data;

    if (drv->assoc_req_ies == NULL && drv->assoc_resp_ies == NULL)
        return;

    os_memset(&data, 0, sizeof(data));
    if (drv->assoc_req_ies) {
        data.assoc_info.req_ies = drv->assoc_req_ies;
        data.assoc_info.req_ies_len = drv->assoc_req_ies_len;
    }
    if (drv->assoc_resp_ies) {
        data.assoc_info.resp_ies = drv->assoc_resp_ies;
        data.assoc_info.resp_ies_len = drv->assoc_resp_ies_len;
    }

    wpa_supplicant_event(drv->ctx, EVENT_ASSOCINFO, &data);

    os_free(drv->assoc_req_ies);
    drv->assoc_req_ies = NULL;
    os_free(drv->assoc_resp_ies);
    drv->assoc_resp_ies = NULL;
}


static void ar6003_driver_ap_event_assoc(struct ar6003_driver_data *drv, u8 addr[ATH_MAC_LEN])
{
    union wpa_event_data data;
    struct ieee80211req_wpaie *ie;


    u8 buf[528];
    u8 *iebuf;
    /*
     * Fetch negotiated WPA/RSN parameters from the system.
     */
    memset(buf, 0, sizeof(buf));
    ((int *)buf)[0] = IEEE80211_IOCTL_GETWPAIE;
    ie = (struct ieee80211req_wpaie *)&buf[4];
    memcpy(ie->wpa_macaddr, addr, IEEE80211_ADDR_LEN);

    if (set80211priv(drv, AR6000_IOCTL_EXTENDED, buf, sizeof(*ie)+4)) {
        wpa_printf(MSG_ERROR, "%s: Failed to get WPA/RSN IE",
                __func__);
        printf("Failed to get WPA/RSN information element.\n");
        goto no_ie;
    }
    ie = (struct ieee80211req_wpaie *)&buf[4];
    iebuf = ie->wpa_ie;
    os_memset(&data, 0, sizeof(data));
    data.assoc_info.req_ies = ie->wpa_ie;
    data.assoc_info.req_ies_len = iebuf[1];
    if(data.assoc_info.req_ies_len == 0)
        data.assoc_info.req_ies_len =0;
    else
        data.assoc_info.req_ies_len +=2;
no_ie:
    data.assoc_info.addr = addr;
    wpa_supplicant_event(drv->ctx, EVENT_ASSOC, &data);

}

static void ar6003_driver_ap_event_disassoc(struct ar6003_driver_data *drv, u8 addr[ATH_MAC_LEN])
{
    union wpa_event_data data;


    os_memset(&data, 0, sizeof(data));

    data.disassoc_info.addr = addr;
    wpa_supplicant_event(drv->ctx, EVENT_DISASSOC, &data);

}

#ifdef CONFIG_HS20
static int ar6003_driver_event_wireless_hs20(
        void *ctx, const char *custom)
{
	u16 len,cmd;
	u8 *data;

	const struct ieee80211_mgmt *mgmt;
	union wpa_event_data event;
	u16 fc, stype;


	cmd = *(u16 *)custom;
	len = *((u16 *)(custom+2));
	data = (u8 *)(custom + 4);

	wpa_hexdump(MSG_DEBUG,"Data",data,len);

	mgmt = (const struct ieee80211_mgmt *) data;
	if (len < 24) {
		wpa_printf(MSG_DEBUG, "nl80211: Too short action data");
		return;
	}

	fc = le_to_host16(mgmt->frame_control);
	stype = WLAN_FC_GET_STYPE(fc);

	os_memset(&event, 0, sizeof(event));

	if (stype == WLAN_FC_STYPE_ACTION) {
		event.rx_action.da = mgmt->da;
		event.rx_action.sa = mgmt->sa;
		event.rx_action.bssid = mgmt->bssid;
		event.rx_action.category = mgmt->u.action.category;
		event.rx_action.data = &mgmt->u.action.category + 1;
		event.rx_action.len = data + len - event.rx_action.data;
		wpa_supplicant_event(ctx, EVENT_RX_ACTION, &event);
	}
	return 0;
}

#endif /* CONFIG_HS20*/


static void ar6003_driver_event_wireless(struct ar6003_driver_data *drv,
        char *data, int len)
{
    struct iw_event iwe_buf, *iwe = &iwe_buf;
    char *pos, *end, *custom, *buf;

    pos = data;
    end = data + len;

    while (pos + IW_EV_LCP_LEN <= end) {
        /* Event data may be unaligned, so make a local, aligned copy
         * before processing. */
        os_memcpy(&iwe_buf, pos, IW_EV_LCP_LEN);
        wpa_printf(MSG_DEBUG, "Wireless event: cmd=0x%x len=%d",
                iwe->cmd, iwe->len);
        if (iwe->len <= IW_EV_LCP_LEN)
            return;

        custom = pos + IW_EV_POINT_LEN;
        if (drv->we_version_compiled > 18 &&
                (iwe->cmd == IWEVMICHAELMICFAILURE ||
                 iwe->cmd == IWEVCUSTOM ||
                 iwe->cmd == IWEVASSOCREQIE ||
                 iwe->cmd == IWEVASSOCRESPIE ||
                 iwe->cmd == IWEVGENIE ||
                 iwe->cmd == IWEVPMKIDCAND)) {
            /* WE-19 removed the pointer from struct iw_point */
            char *dpos = (char *) &iwe_buf.u.data.length;
            int dlen = dpos - (char *) &iwe_buf;
            os_memcpy(dpos, pos + IW_EV_LCP_LEN,
                    sizeof(struct iw_event) - dlen);
        } else {
            os_memcpy(&iwe_buf, pos, sizeof(struct iw_event));
            custom += IW_EV_POINT_OFF;
        }

        switch (iwe->cmd) {
            case SIOCGIWAP:
                wpa_printf(MSG_DEBUG, "Wireless event: new AP: "
                        MACSTR,
                        MAC2STR((u8 *) iwe->u.ap_addr.sa_data));
                if (is_zero_ether_addr(
                            (const u8 *) iwe->u.ap_addr.sa_data) ||
                        os_memcmp(iwe->u.ap_addr.sa_data,
                            "\x44\x44\x44\x44\x44\x44", ETH_ALEN) ==
                        0) {
                    os_free(drv->assoc_req_ies);
                    drv->assoc_req_ies = NULL;
                    os_free(drv->assoc_resp_ies);
                    drv->assoc_resp_ies = NULL;
                    wpa_supplicant_event(drv->ctx, EVENT_DISASSOC, NULL);
                } else {
                    ar6003_driver_event_assoc_ies(drv);
                    wpa_supplicant_event(drv->ctx, EVENT_ASSOC, NULL);
                }
                break;
            case IWEVMICHAELMICFAILURE:
                if (custom + iwe->u.data.length > end) {
                    wpa_printf(MSG_DEBUG, "Invalid "
                            "IWEVMICHAELMICFAILURE length");
                    return;
                }
                ar6003_driver_event_wireless_michaelmicfailure(
                        drv->ctx, custom, iwe->u.data.length);
                break;
            case IWEVCUSTOM:
                if (custom + iwe->u.data.length > end) {
                    wpa_printf(MSG_DEBUG, "Invalid "
                            "IWEVCUSTOM length");
                    return;
                }
                buf = os_malloc(iwe->u.data.length + 1);
                if (buf == NULL)
                    return;
                os_memcpy(buf, custom, iwe->u.data.length);
                buf[iwe->u.data.length] = '\0';
                ar6003_driver_event_wireless_custom(drv, buf);
                os_free(buf);
                break;
            case SIOCGIWSCAN:
                drv->scan_complete_events = 1;
                eloop_cancel_timeout(ar6003_driver_scan_timeout,
                        drv, drv->ctx);
                wpa_supplicant_event(drv->ctx, EVENT_SCAN_RESULTS,
                        NULL);
                break;
            case IWEVASSOCREQIE:
                if (custom + iwe->u.data.length > end) {
                    wpa_printf(MSG_DEBUG, "Invalid "
                            "IWEVASSOCREQIE length");
                    return;
                }
                ar6003_driver_event_wireless_assocreqie(
                        drv, custom, iwe->u.data.length);
                break;
            case IWEVASSOCRESPIE:
                if (custom + iwe->u.data.length > end) {
                    wpa_printf(MSG_DEBUG, "Invalid "
                            "IWEVASSOCRESPIE length");
                    return;
                }
                ar6003_driver_event_wireless_assocrespie(
                        drv, custom, iwe->u.data.length);
                break;
            case IWEVPMKIDCAND:
                if (custom + iwe->u.data.length > end) {
                    wpa_printf(MSG_DEBUG, "Invalid "
                            "IWEVPMKIDCAND length");
                    return;
                }
                ar6003_driver_event_wireless_pmkidcand(
                        drv, custom, iwe->u.data.length);
                break;
            case IWEVEXPIRED:
                ar6003_driver_ap_event_disassoc(drv, (u8 *) iwe->u.addr.sa_data);
                break;
            case IWEVREGISTERED:
                ar6003_driver_ap_event_assoc(drv, (u8 *) iwe->u.addr.sa_data);
                break;
#ifdef CONFIG_HS20
	    case IWEVGENIE:
		if (WMI_HS20_RX_EVENTID != *(u16 *)custom)
			return;
		wpa_printf(MSG_DEBUG,"ar6003 : Received action \n ");
		ar6003_driver_event_wireless_hs20(drv->ctx,custom);
		break;
#endif /* CONFIG_HS20 */
        }

        pos += iwe->len;
    }
}


static void ar6003_driver_event_link(struct ar6003_driver_data *drv,
        char *buf, size_t len, int del)
{
    union wpa_event_data event;

    os_memset(&event, 0, sizeof(event));
    if (len > sizeof(event.interface_status.ifname))
        len = sizeof(event.interface_status.ifname) - 1;
    os_memcpy(event.interface_status.ifname, buf, len);
    event.interface_status.ievent = del ? EVENT_INTERFACE_REMOVED :
        EVENT_INTERFACE_ADDED;

    wpa_printf(MSG_DEBUG, "RTM_%sLINK, IFLA_IFNAME: Interface '%s' %s",
            del ? "DEL" : "NEW",
            event.interface_status.ifname,
            del ? "removed" : "added");

    if (os_strcmp(drv->ifname, event.interface_status.ifname) == 0) {
        if (del)
            drv->if_removed = 1;
        else
            drv->if_removed = 0;
    }

    wpa_supplicant_event(drv->ctx, EVENT_INTERFACE_STATUS, &event);
}


static int ar6003_driver_own_ifname(struct ar6003_driver_data *drv,
        u8 *buf, size_t len)
{
    int attrlen, rta_len;
    struct rtattr *attr;

    attrlen = len;
    attr = (struct rtattr *) buf;

    rta_len = RTA_ALIGN(sizeof(struct rtattr));
    while (RTA_OK(attr, attrlen)) {
        if (attr->rta_type == IFLA_IFNAME) {
            if (os_strcmp(((char *) attr) + rta_len, drv->ifname)
                    == 0)
                return 1;
            else
                break;
        }
        attr = RTA_NEXT(attr, attrlen);
    }

    return 0;
}


static int ar6003_driver_own_ifindex(struct ar6003_driver_data *drv,
        int ifindex, u8 *buf, size_t len)
{
    if (drv->ifindex == ifindex || drv->ifindex2 == ifindex)
        return 1;

    if (drv->if_removed && ar6003_driver_own_ifname(drv, buf, len)) {
        drv->ifindex = if_nametoindex(drv->ifname);
        wpa_printf(MSG_DEBUG, "Update ifindex for a removed "
                "interface");
        ar6003_driver_finish_drv_init(drv);
        return 1;
    }

    return 0;
}


static void ar6003_driver_event_rtm_newlink(void *ctx, struct ifinfomsg *ifi,
        u8 *buf, size_t len)
{
    struct ar6003_driver_data *drv = ctx;
    int attrlen, rta_len;
    struct rtattr *attr;

    if (!ar6003_driver_own_ifindex(drv, ifi->ifi_index, buf, len)) {
        wpa_printf(MSG_DEBUG, "Ignore event for foreign ifindex %d",
                ifi->ifi_index);
        return;
    }

    wpa_printf(MSG_DEBUG, "RTM_NEWLINK: operstate=%d ifi_flags=0x%x "
            "(%s%s%s%s)",
            drv->operstate, ifi->ifi_flags,
            (ifi->ifi_flags & IFF_UP) ? "[UP]" : "",
            (ifi->ifi_flags & IFF_RUNNING) ? "[RUNNING]" : "",
            (ifi->ifi_flags & IFF_LOWER_UP) ? "[LOWER_UP]" : "",
            (ifi->ifi_flags & IFF_DORMANT) ? "[DORMANT]" : "");
    /*
     * Some drivers send the association event before the operup event--in
     * this case, lifting operstate in ar6003_driver_set_operstate()
     * fails. This will hit us when wpa_supplicant does not need to do
     * IEEE 802.1X authentication
     */
    if (drv->operstate == 1 &&
            (ifi->ifi_flags & (IFF_LOWER_UP | IFF_DORMANT)) == IFF_LOWER_UP &&
            !(ifi->ifi_flags & IFF_RUNNING))
        netlink_send_oper_ifla(drv->netlink, drv->ifindex,
                -1, IF_OPER_UP);

    attrlen = len;
    attr = (struct rtattr *) buf;

    rta_len = RTA_ALIGN(sizeof(struct rtattr));
    while (RTA_OK(attr, attrlen)) {
        if (attr->rta_type == IFLA_WIRELESS) {
            ar6003_driver_event_wireless(
                    drv, ((char *) attr) + rta_len,
                    attr->rta_len - rta_len);
        } else if (attr->rta_type == IFLA_IFNAME) {
            ar6003_driver_event_link(drv,
                    ((char *) attr) + rta_len,
                    attr->rta_len - rta_len, 0);
        }
        attr = RTA_NEXT(attr, attrlen);
    }
}


static void ar6003_driver_event_rtm_dellink(void *ctx, struct ifinfomsg *ifi,
        u8 *buf, size_t len)
{
    struct ar6003_driver_data *drv = ctx;
    int attrlen, rta_len;
    struct rtattr *attr;

    attrlen = len;
    attr = (struct rtattr *) buf;

    rta_len = RTA_ALIGN(sizeof(struct rtattr));
    while (RTA_OK(attr, attrlen)) {
        if (attr->rta_type == IFLA_IFNAME) {
            ar6003_driver_event_link(drv,
                    ((char *) attr) + rta_len,
                    attr->rta_len - rta_len, 1);
        }
        attr = RTA_NEXT(attr, attrlen);
    }
}

static void
handle_read(void *ctx, const u8 *src_addr, const u8 *buf, size_t len)
{
    struct ar6003_driver_data *drv = ctx;
    union wpa_event_data data;
    os_memset(&data, 0, sizeof(data));

    data.eapol_rx.src = src_addr;
    data.eapol_rx.data = buf;
    data.eapol_rx.data_len = len;
    wpa_supplicant_event(drv->ctx, EVENT_EAPOL_RX, &data);
}

/**
 * ar6003_driver_init - Initialize WE driver interface
 * @ctx: context to be used when calling wpa_supplicant functions,
 * e.g., wpa_supplicant_event()
 * @ifname: interface name, e.g., wlan0
 * Returns: Pointer to private data, %NULL on failure
 */
void * ar6003_driver_init(void *ctx, const char *ifname)
{
    struct ar6003_driver_data *drv;
    struct netlink_config *cfg;

    drv = os_zalloc(sizeof(*drv));
    if (drv == NULL)
        return NULL;
    drv->ctx = ctx;
    os_strlcpy(drv->ifname, ifname, sizeof(drv->ifname));

    drv->ioctl_sock = socket(PF_INET, SOCK_DGRAM, 0);
    if (drv->ioctl_sock < 0) {
        perror("socket(PF_INET,SOCK_DGRAM)");
        goto err1;
    }

    cfg = os_zalloc(sizeof(*cfg));
    if (cfg == NULL)
        goto err1;
    cfg->ctx = drv;
    cfg->newlink_cb = ar6003_driver_event_rtm_newlink;
    cfg->dellink_cb = ar6003_driver_event_rtm_dellink;
    drv->netlink = netlink_init(cfg);
    if (drv->netlink == NULL) {
        os_free(cfg);
        goto err2;
    }

    drv->mlme_sock = -1;
    if (ar6003_driver_finish_drv_init(drv) < 0)
        goto err3;

    ar6003_driver_set_auth_param(drv, IW_AUTH_WPA_ENABLED, 1);

    drv->sock_xmit = l2_packet_init(drv->ifname, NULL, ETH_P_EAPOL,
            handle_read, drv, 1);
    if (drv->sock_xmit == NULL)
        goto err3;
    drv->sock_recv = drv->sock_xmit;

    return drv;

err3:
    netlink_deinit(drv->netlink);
err2:
    close(drv->ioctl_sock);
err1:
    os_free(drv);
    return NULL;
}


static int ar6003_driver_finish_drv_init(struct ar6003_driver_data *drv)
{
    if (linux_set_iface_flags(drv->ioctl_sock, drv->ifname, 1) < 0)
        return -1;

    /*
     * Make sure that the driver does not have any obsolete PMKID entries.
     */
    ar6003_driver_flush_pmkid(drv);

    if (ar6003_driver_set_mode(drv, 0) < 0) {
        wpa_printf(MSG_DEBUG, "Could not configure driver to use "
                "managed mode");
        /* Try to use it anyway */
    }

    ar6003_driver_get_range(drv);

    /*
     * Unlock the driver's BSSID and force to a random SSID to clear any
     * previous association the driver might have when the supplicant
     * starts up.
     */
    //    ar6003_driver_disconnect(drv);

    drv->ifindex = if_nametoindex(drv->ifname);

    if (os_strncmp(drv->ifname, "wlan", 4) == 0) {
        /*
         * Host AP driver may use both wlan# and wifi# interface in
         * wireless events. Since some of the versions included WE-18
         * support, let's add the alternative ifindex also from
         * driver_wext.c for the time being. This may be removed at
         * some point once it is believed that old versions of the
         * driver are not in use anymore.
         */
        char ifname2[IFNAMSIZ + 1];
        os_strlcpy(ifname2, drv->ifname, sizeof(ifname2));
        os_memcpy(ifname2, "wifi", 4);
        ar6003_driver_alternative_ifindex(drv, ifname2);
    }

    if (ar6003_driver_capa(drv))
        return -1;
    netlink_send_oper_ifla(drv->netlink, drv->ifindex,
            1, IF_OPER_DORMANT);

    return 0;
}


/**
 * ar6003_driver_deinit - Deinitialize WE driver interface
 * @priv: Pointer to private data from ar6003_driver_init()
 *
 * Shut down driver interface and processing of driver events. Free
 * private data buffer if one was allocated in ar6003_driver_init().
 */
void ar6003_driver_deinit(void *priv)
{
    struct ar6003_driver_data *drv = priv;

    ar6003_driver_set_auth_param(drv, IW_AUTH_WPA_ENABLED, 0);

    eloop_cancel_timeout(ar6003_driver_scan_timeout, drv, drv->ctx);

    /*
     * Clear possibly configured driver parameters in order to make it
     * easier to use the driver after wpa_supplicant has been terminated.
     */
    ar6003_driver_disconnect(drv);

    netlink_send_oper_ifla(drv->netlink, drv->ifindex, 0, IF_OPER_UP);
    netlink_deinit(drv->netlink);

    if (drv->mlme_sock >= 0)
        eloop_unregister_read_sock(drv->mlme_sock);

    ar6003_wireless_event_deinit(priv);

    (void) linux_set_iface_flags(drv->ioctl_sock, drv->ifname, 0);

    close(drv->ioctl_sock);
    if (drv->mlme_sock >= 0)
        close(drv->mlme_sock);

    if (drv->sock_xmit != NULL)
        l2_packet_deinit(drv->sock_xmit);

    os_free(drv->assoc_req_ies);
    os_free(drv->assoc_resp_ies);
    os_free(drv);
}


/**
 * ar6003_driver_scan_timeout - Scan timeout to report scan completion
 * @eloop_ctx: Unused
 * @timeout_ctx: ctx argument given to ar6003_driver_init()
 *
 * This function can be used as registered timeout when starting a scan to
 * generate a scan completed event if the driver does not report this.
 */
void ar6003_driver_scan_timeout(void *eloop_ctx, void *timeout_ctx)
{
    wpa_printf(MSG_DEBUG, "Scan timeout - try to get results");
    wpa_supplicant_event(timeout_ctx, EVENT_SCAN_RESULTS, NULL);
}

/**
 * ar6003_driver_scan - Request the driver to initiate scan
 * @priv: Pointer to private data from ar6003_driver_init()
 * @param: Scan parameters (specific SSID to scan for (ProbeReq), etc.)
 * Returns: 0 on success, -1 on failure
 */
int ar6003_driver_scan(void *priv, struct wpa_driver_scan_params *params)
{
    struct ar6003_driver_data *drv = priv;
    struct iwreq iwr;
    int ret = 0, timeout;
    struct iw_scan_req req;
    const u8 *ssid = params->ssids[0].ssid;
    size_t ssid_len = params->ssids[0].ssid_len;
    const u8 *appie = params->extra_ies;
    size_t appie_len = params->extra_ies_len;
    unsigned int ctr;
    int opmode;
    char str[16];
    struct ifreq ifr;

    os_memset(&ifr, 0, sizeof(ifr));
    os_strlcpy(ifr.ifr_name, drv->ifname, IFNAMSIZ);
    ((int *)str)[0] = AR6000_XIOCTL_GET_SUBMODE;
    ifr.ifr_data = str;
    if (ioctl(drv->ioctl_sock, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
        perror("ioctl[AR6000_XIOCTL_GET_SUBMODE]");
        return -1;
    }

    opmode = (int)str[0];

    if(opmode == SUBTYPE_NONE) {
        if (ar6003_set_wps_ie(priv, appie_len ? appie : NULL,
                    appie_len, IEEE80211_APPIE_FRAME_PROBE_REQ))
            return -1;
    }

    if (ssid_len > IW_ESSID_MAX_SIZE) {
        wpa_printf(MSG_DEBUG, "%s: too long SSID (%lu)",
                __FUNCTION__, (unsigned long) ssid_len);
        return -1;
    }

    os_memset(&iwr, 0, sizeof(iwr));
    os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);

    os_memset(&req, 0, sizeof(req));

    if (ssid && ssid_len) {
        req.essid_len = ssid_len;
        req.bssid.sa_family = ARPHRD_ETHER;
        os_memset(req.bssid.sa_data, 0xff, ETH_ALEN);
        os_memcpy(req.essid, ssid, ssid_len);
        iwr.u.data.flags = IW_SCAN_THIS_ESSID;
    }

    if (params->freqs) {
        for (ctr=0; params->freqs[ctr] != 0; ctr++) {
            req.channel_list[ctr].m = params->freqs[ctr] * 100000;
            req.channel_list[ctr].e = 1;
        }
        req.num_channels = ctr;
        iwr.u.data.flags |= IW_SCAN_THIS_FREQ;
    }

    iwr.u.data.pointer = (caddr_t) &req;
    iwr.u.data.length = sizeof(req);

    if (ioctl(drv->ioctl_sock, SIOCSIWSCAN, &iwr) < 0) {
        perror("ioctl[SIOCSIWSCAN]");
        ret = -1;
    }

    /* Not all drivers generate "scan completed" wireless event, so try to
     * read results after a timeout. */
    timeout = 5;
    if (drv->scan_complete_events) {
        /*
         * The driver seems to deliver SIOCGIWSCAN events to notify
         * when scan is complete, so use longer timeout to avoid race
         * conditions with scanning and following association request.
         */
        timeout = 30;
    }
    wpa_printf(MSG_DEBUG, "Scan requested (ret=%d) - scan timeout %d "
            "seconds", ret, timeout);
    eloop_cancel_timeout(ar6003_driver_scan_timeout, drv, drv->ctx);
    eloop_register_timeout(timeout, 0, ar6003_driver_scan_timeout, drv,
            drv->ctx);

    return ret;
}


static u8 * ar6003_driver_giwscan(struct ar6003_driver_data *drv,
        size_t *len)
{
    struct iwreq iwr;
    u8 *res_buf;
    size_t res_buf_len;

    res_buf_len = IW_SCAN_MAX_DATA;
    for (;;) {
        res_buf = os_malloc(res_buf_len);
        if (res_buf == NULL)
            return NULL;
        os_memset(&iwr, 0, sizeof(iwr));
        os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
        iwr.u.data.pointer = res_buf;
        iwr.u.data.length = res_buf_len;

        if (ioctl(drv->ioctl_sock, SIOCGIWSCAN, &iwr) == 0)
            break;

        if (errno == E2BIG && res_buf_len < 65535) {
            os_free(res_buf);
            res_buf = NULL;
            res_buf_len *= 2;
            if (res_buf_len > 65535)
                res_buf_len = 65535; /* 16-bit length field */
            wpa_printf(MSG_DEBUG, "Scan results did not fit - "
                    "trying larger buffer (%lu bytes)",
                    (unsigned long) res_buf_len);
        } else {
            perror("ioctl[SIOCGIWSCAN]");
            os_free(res_buf);
            return NULL;
        }
    }

    if (iwr.u.data.length > res_buf_len) {
        os_free(res_buf);
        return NULL;
    }
    *len = iwr.u.data.length;

    return res_buf;
}


/*
 * Data structure for collecting scan results. This is needed to allow
 * the various methods of reporting IEs to be combined into a single IE buffer.
 */
struct ar6003_scan_data {
    struct wpa_scan_res res;
    u8 *ie;
    size_t ie_len;
    u8 ssid[32];
    size_t ssid_len;
    int maxrate;
};


static void ar6003_get_scan_mode(struct iw_event *iwe,
        struct ar6003_scan_data *res)
{
    if (iwe->u.mode == IW_MODE_ADHOC)
        res->res.caps |= IEEE80211_CAP_IBSS;
    else if (iwe->u.mode == IW_MODE_MASTER || iwe->u.mode == IW_MODE_INFRA)
        res->res.caps |= IEEE80211_CAP_ESS;
}


static void ar6003_get_scan_ssid(struct iw_event *iwe,
        struct ar6003_scan_data *res, char *custom,
        char *end)
{
    int ssid_len = iwe->u.essid.length;
    if (custom + ssid_len > end)
        return;
    if (iwe->u.essid.flags &&
            ssid_len > 0 &&
            ssid_len <= IW_ESSID_MAX_SIZE) {
        os_memcpy(res->ssid, custom, ssid_len);
        res->ssid_len = ssid_len;
    }
}


static void ar6003_get_scan_freq(struct iw_event *iwe,
        struct ar6003_scan_data *res)
{
    int divi = 1000000, i;

    if (iwe->u.freq.e == 0) {
        /*
         * Some drivers do not report frequency, but a channel.
         * Try to map this to frequency by assuming they are using
         * IEEE 802.11b/g.  But don't overwrite a previously parsed
         * frequency if the driver sends both frequency and channel,
         * since the driver may be sending an A-band channel that we
         * don't handle here.
         */

        if (res->res.freq)
            return;

        if (iwe->u.freq.m >= 1 && iwe->u.freq.m <= 13) {
            res->res.freq = 2407 + 5 * iwe->u.freq.m;
            return;
        } else if (iwe->u.freq.m == 14) {
            res->res.freq = 2484;
            return;
        }
    }

    if (iwe->u.freq.e > 6) {
        wpa_printf(MSG_DEBUG, "Invalid freq in scan results (BSSID="
                MACSTR " m=%d e=%d)",
                MAC2STR(res->res.bssid), iwe->u.freq.m,
                iwe->u.freq.e);
        return;
    }

    for (i = 0; i < iwe->u.freq.e; i++)
        divi /= 10;
    res->res.freq = iwe->u.freq.m / divi;
}


static void ar6003_get_scan_qual(struct ar6003_driver_data *drv,
        struct iw_event *iwe,
        struct ar6003_scan_data *res)
{
    res->res.qual = iwe->u.qual.qual;
    res->res.noise = iwe->u.qual.noise;
    res->res.level = iwe->u.qual.level;
    if (iwe->u.qual.updated & IW_QUAL_QUAL_INVALID)
        res->res.flags |= WPA_SCAN_QUAL_INVALID;
    if (iwe->u.qual.updated & IW_QUAL_LEVEL_INVALID)
        res->res.flags |= WPA_SCAN_LEVEL_INVALID;
    if (iwe->u.qual.updated & IW_QUAL_NOISE_INVALID)
        res->res.flags |= WPA_SCAN_NOISE_INVALID;
    if (iwe->u.qual.updated & IW_QUAL_DBM)
        res->res.flags |= WPA_SCAN_LEVEL_DBM;
    if ((iwe->u.qual.updated & IW_QUAL_DBM) ||
            ((iwe->u.qual.level != 0) &&
             (iwe->u.qual.level > drv->max_level))) {
        if (iwe->u.qual.level >= 64)
            res->res.level -= 0x100;
        if (iwe->u.qual.noise >= 64)
            res->res.noise -= 0x100;
    }
}


static void ar6003_get_scan_encode(struct iw_event *iwe,
        struct ar6003_scan_data *res)
{
    if (!(iwe->u.data.flags & IW_ENCODE_DISABLED))
        res->res.caps |= IEEE80211_CAP_PRIVACY;
}


static void ar6003_get_scan_rate(struct iw_event *iwe,
        struct ar6003_scan_data *res, char *pos,
        char *end)
{
    int maxrate;
    char *custom = pos + IW_EV_LCP_LEN;
    struct iw_param p;
    size_t clen;

    clen = iwe->len;
    if (custom + clen > end)
        return;
    maxrate = 0;
    while (((ssize_t) clen) >= (ssize_t) sizeof(struct iw_param)) {
        /* Note: may be misaligned, make a local, aligned copy */
        os_memcpy(&p, custom, sizeof(struct iw_param));
        if (p.value > maxrate)
            maxrate = p.value;
        clen -= sizeof(struct iw_param);
        custom += sizeof(struct iw_param);
    }

    /* Convert the maxrate from WE-style (b/s units) to
     * 802.11 rates (500000 b/s units).
     */
    res->maxrate = maxrate / 500000;
}


static void ar6003_get_scan_iwevgenie(struct iw_event *iwe,
        struct ar6003_scan_data *res, char *custom,
        char *end)
{
    char *genie, *gpos, *gend;
    u8 *tmp;

    if (iwe->u.data.length == 0)
        return;

    gpos = genie = custom;
    gend = genie + iwe->u.data.length;
    if (gend > end) {
        wpa_printf(MSG_INFO, "IWEVGENIE overflow");
        return;
    }

    tmp = os_realloc(res->ie, res->ie_len + gend - gpos);
    if (tmp == NULL)
        return;
    os_memcpy(tmp + res->ie_len, gpos, gend - gpos);
    res->ie = tmp;
    res->ie_len += gend - gpos;
}


static void ar6003_get_scan_custom(struct iw_event *iwe,
        struct ar6003_scan_data *res, char *custom,
        char *end)
{
    size_t clen;
    u8 *tmp;

    clen = iwe->u.data.length;
    if (custom + clen > end)
        return;

    if (clen > 7 && os_strncmp(custom, "wpa_ie=", 7) == 0) {
        char *spos;
        int bytes;
        spos = custom + 7;
        bytes = custom + clen - spos;
        if (bytes & 1 || bytes == 0)
            return;
        bytes /= 2;
        tmp = os_realloc(res->ie, res->ie_len + bytes);
        if (tmp == NULL)
            return;
        res->ie = tmp;
        if (hexstr2bin(spos, tmp + res->ie_len, bytes) < 0)
            return;
        res->ie_len += bytes;
    } else if (clen > 7 && os_strncmp(custom, "rsn_ie=", 7) == 0) {
        char *spos;
        int bytes;
        spos = custom + 7;
        bytes = custom + clen - spos;
        if (bytes & 1 || bytes == 0)
            return;
        bytes /= 2;
        tmp = os_realloc(res->ie, res->ie_len + bytes);
        if (tmp == NULL)
            return;
        res->ie = tmp;
        if (hexstr2bin(spos, tmp + res->ie_len, bytes) < 0)
            return;
        res->ie_len += bytes;
#ifdef CONFIG_HS20
    } else if (clen > 8 && os_strncmp(custom, "hs20_ie=", 8) == 0) {
        char *spos;
        int bytes;
        spos = custom + 8;
        bytes = custom + clen - spos;
        if (bytes & 1 || bytes == 0)
            return;
        bytes /= 2;
        tmp = os_realloc(res->ie, res->ie_len + bytes);
        if (tmp == NULL)
            return;
        res->ie = tmp;
        if (hexstr2bin(spos, tmp + res->ie_len, bytes) < 0)
            return;
        res->ie_len += bytes;
    } else if (clen > 10 && os_strncmp(custom, "extcap_ie=", 10) == 0) {
        char *spos;
        int bytes;
        spos = custom + 10;
        bytes = custom + clen - spos;
        if (bytes & 1 || bytes == 0)
            return;
        bytes /= 2;
        tmp = os_realloc(res->ie, res->ie_len + bytes);
        if (tmp == NULL)
            return;
        res->ie = tmp;
        if (hexstr2bin(spos, tmp + res->ie_len, bytes) < 0)
            return;
        res->ie_len += bytes;
#endif /* CONFIG_HS20 */
    } else if (clen > 4 && os_strncmp(custom, "tsf=", 4) == 0) {
        char *spos;
        int bytes;
        u8 bin[8];
        spos = custom + 4;
        bytes = custom + clen - spos;
        if (bytes != 16) {
            wpa_printf(MSG_INFO, "Invalid TSF length (%d)", bytes);
            return;
        }
        bytes /= 2;
        if (hexstr2bin(spos, bin, bytes) < 0) {
            wpa_printf(MSG_DEBUG, "Invalid TSF value");
            return;
        }
        res->res.tsf += WPA_GET_BE64(bin);
    }
}


static int ar6003_19_iw_point(struct ar6003_driver_data *drv, u16 cmd)
{
    return drv->we_version_compiled > 18 &&
        (cmd == SIOCGIWESSID || cmd == SIOCGIWENCODE ||
         cmd == IWEVGENIE || cmd == IWEVCUSTOM);
}


static void ar6003_driver_add_scan_entry(struct wpa_scan_results *res,
        struct ar6003_scan_data *data)
{
    struct wpa_scan_res **tmp;
    struct wpa_scan_res *r;
    size_t extra_len;
    u8 *pos, *end, *ssid_ie = NULL, *rate_ie = NULL;

    /* Figure out whether we need to fake any IEs */
    pos = data->ie;
    end = pos + data->ie_len;
    while (pos && pos + 1 < end) {
        if (pos + 2 + pos[1] > end)
            break;
        if (pos[0] == WLAN_EID_SSID)
            ssid_ie = pos;
        else if (pos[0] == WLAN_EID_SUPP_RATES)
            rate_ie = pos;
        else if (pos[0] == WLAN_EID_EXT_SUPP_RATES)
            rate_ie = pos;
        pos += 2 + pos[1];
    }

    extra_len = 0;
    if (ssid_ie == NULL)
        extra_len += 2 + data->ssid_len;
    if (rate_ie == NULL && data->maxrate)
        extra_len += 3;

    r = os_zalloc(sizeof(*r) + extra_len + data->ie_len);
    if (r == NULL)
        return;
    os_memcpy(r, &data->res, sizeof(*r));
    r->ie_len = extra_len + data->ie_len;
    pos = (u8 *) (r + 1);
    if (ssid_ie == NULL) {
        /*
         * Generate a fake SSID IE since the driver did not report
         * a full IE list.
         */
        *pos++ = WLAN_EID_SSID;
        *pos++ = data->ssid_len;
        os_memcpy(pos, data->ssid, data->ssid_len);
        pos += data->ssid_len;
    }
    if (rate_ie == NULL && data->maxrate) {
        /*
         * Generate a fake Supported Rates IE since the driver did not
         * report a full IE list.
         */
        *pos++ = WLAN_EID_SUPP_RATES;
        *pos++ = 1;
        *pos++ = data->maxrate;
    }
    if (data->ie)
        os_memcpy(pos, data->ie, data->ie_len);

    tmp = os_realloc(res->res,
            (res->num + 1) * sizeof(struct wpa_scan_res *));
    if (tmp == NULL) {
        os_free(r);
        return;
    }
    tmp[res->num++] = r;
    res->res = tmp;
}


/**
 * ar6003_driver_get_scan_results - Fetch the latest scan results
 * @priv: Pointer to private data from ar6003_driver_init()
 * Returns: Scan results on success, -1 on failure
 */
struct wpa_scan_results * ar6003_driver_get_scan_results(void *priv)
{
    struct ar6003_driver_data *drv = priv;
    size_t ap_num = 0, len;
    int first;
    u8 *res_buf;
    struct iw_event iwe_buf, *iwe = &iwe_buf;
    char *pos, *end, *custom;
    struct wpa_scan_results *res;
    struct ar6003_scan_data data;

    res_buf = ar6003_driver_giwscan(drv, &len);
    if (res_buf == NULL)
        return NULL;

    ap_num = 0;
    first = 1;

    res = os_zalloc(sizeof(*res));
    if (res == NULL) {
        os_free(res_buf);
        return NULL;
    }

    pos = (char *) res_buf;
    end = (char *) res_buf + len;
    os_memset(&data, 0, sizeof(data));

    while (pos + IW_EV_LCP_LEN <= end) {
        /* Event data may be unaligned, so make a local, aligned copy
         * before processing. */
        os_memcpy(&iwe_buf, pos, IW_EV_LCP_LEN);
        if (iwe->len <= IW_EV_LCP_LEN)
            break;

        custom = pos + IW_EV_POINT_LEN;
        if (ar6003_19_iw_point(drv, iwe->cmd)) {
            /* WE-19 removed the pointer from struct iw_point */
            char *dpos = (char *) &iwe_buf.u.data.length;
            int dlen = dpos - (char *) &iwe_buf;
            os_memcpy(dpos, pos + IW_EV_LCP_LEN,
                    sizeof(struct iw_event) - dlen);
        } else {
            os_memcpy(&iwe_buf, pos, sizeof(struct iw_event));
            custom += IW_EV_POINT_OFF;
        }

        switch (iwe->cmd) {
            case SIOCGIWAP:
                if (!first)
                    ar6003_driver_add_scan_entry(res, &data);
                first = 0;
                os_free(data.ie);
                os_memset(&data, 0, sizeof(data));
                os_memcpy(data.res.bssid,
                        iwe->u.ap_addr.sa_data, ETH_ALEN);
                break;
            case SIOCGIWMODE:
                ar6003_get_scan_mode(iwe, &data);
                break;
            case SIOCGIWESSID:
                ar6003_get_scan_ssid(iwe, &data, custom, end);
                break;
            case SIOCGIWFREQ:
                ar6003_get_scan_freq(iwe, &data);
                break;
            case IWEVQUAL:
                ar6003_get_scan_qual(drv, iwe, &data);
                break;
            case SIOCGIWENCODE:
                ar6003_get_scan_encode(iwe, &data);
                break;
            case SIOCGIWRATE:
                ar6003_get_scan_rate(iwe, &data, pos, end);
                break;
            case IWEVGENIE:
                ar6003_get_scan_iwevgenie(iwe, &data, custom, end);
                break;
            case IWEVCUSTOM:
                ar6003_get_scan_custom(iwe, &data, custom, end);
                break;
        }

        pos += iwe->len;
    }
    os_free(res_buf);
    res_buf = NULL;
    if (!first)
        ar6003_driver_add_scan_entry(res, &data);
    os_free(data.ie);

    wpa_printf(MSG_DEBUG, "Received %lu bytes of scan results (%lu BSSes)",
            (unsigned long) len, (unsigned long) res->num);

    return res;
}


static int ar6003_driver_get_range(void *priv)
{
    struct ar6003_driver_data *drv = priv;
    struct iw_range *range;
    struct iwreq iwr;
    int minlen;
    size_t buflen;

    /*
     * Use larger buffer than struct iw_range in order to allow the
     * structure to grow in the future.
     */
    buflen = sizeof(struct iw_range) + 500;
    range = os_zalloc(buflen);
    if (range == NULL)
        return -1;

    os_memset(&iwr, 0, sizeof(iwr));
    os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
    iwr.u.data.pointer = (caddr_t) range;
    iwr.u.data.length = buflen;

    minlen = ((char *) &range->enc_capa) - (char *) range +
        sizeof(range->enc_capa);

    if (ioctl(drv->ioctl_sock, SIOCGIWRANGE, &iwr) < 0) {
        perror("ioctl[SIOCGIWRANGE]");
        os_free(range);
        return -1;
    } else if (iwr.u.data.length >= minlen &&
            range->we_version_compiled >= 18) {
        wpa_printf(MSG_DEBUG, "SIOCGIWRANGE: WE(compiled)=%d "
                "WE(source)=%d enc_capa=0x%x",
                range->we_version_compiled,
                range->we_version_source,
                range->enc_capa);
        drv->has_capability = 1;
        drv->we_version_compiled = range->we_version_compiled;
        if (range->enc_capa & IW_ENC_CAPA_WPA) {
            drv->capa.key_mgmt |= WPA_DRIVER_CAPA_KEY_MGMT_WPA |
                WPA_DRIVER_CAPA_KEY_MGMT_WPA_PSK;
        }
        if (range->enc_capa & IW_ENC_CAPA_WPA2) {
            drv->capa.key_mgmt |= WPA_DRIVER_CAPA_KEY_MGMT_WPA2 |
                WPA_DRIVER_CAPA_KEY_MGMT_WPA2_PSK;
        }
        drv->capa.enc |= WPA_DRIVER_CAPA_ENC_WEP40 |
            WPA_DRIVER_CAPA_ENC_WEP104;
        if (range->enc_capa & IW_ENC_CAPA_CIPHER_TKIP)
            drv->capa.enc |= WPA_DRIVER_CAPA_ENC_TKIP;
        if (range->enc_capa & IW_ENC_CAPA_CIPHER_CCMP)
            drv->capa.enc |= WPA_DRIVER_CAPA_ENC_CCMP;
        if (range->enc_capa & IW_ENC_CAPA_4WAY_HANDSHAKE)
            drv->capa.flags |= WPA_DRIVER_FLAGS_4WAY_HANDSHAKE;
        drv->capa.auth = WPA_DRIVER_AUTH_OPEN |
            WPA_DRIVER_AUTH_SHARED |
            WPA_DRIVER_AUTH_LEAP;
        drv->capa.max_scan_ssids = 1;

        wpa_printf(MSG_DEBUG, "  capabilities: key_mgmt 0x%x enc 0x%x "
                "flags 0x%x",
                drv->capa.key_mgmt, drv->capa.enc, drv->capa.flags);
    } else {
        wpa_printf(MSG_DEBUG, "SIOCGIWRANGE: too old (short) data - "
                "assuming WPA is not supported");
    }

    drv->max_level = range->max_qual.level;

    os_free(range);
    return 0;
}


static int ar6003_driver_set_psk(struct ar6003_driver_data *drv,
        const u8 *psk)
{
    struct iw_encode_ext *ext;
    struct iwreq iwr;
    int ret;

    wpa_printf(MSG_DEBUG, "%s", __FUNCTION__);

    if (!(drv->capa.flags & WPA_DRIVER_FLAGS_4WAY_HANDSHAKE))
        return 0;

    if (!psk)
        return 0;

    os_memset(&iwr, 0, sizeof(iwr));
    os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);

    ext = os_zalloc(sizeof(*ext) + PMK_LEN);
    if (ext == NULL)
        return -1;

    iwr.u.encoding.pointer = (caddr_t) ext;
    iwr.u.encoding.length = sizeof(*ext) + PMK_LEN;
    ext->key_len = PMK_LEN;
    os_memcpy(&ext->key, psk, ext->key_len);
    ext->alg = IW_ENCODE_ALG_PMK;

    ret = ioctl(drv->ioctl_sock, SIOCSIWENCODEEXT, &iwr);
    if (ret < 0)
        perror("ioctl[SIOCSIWENCODEEXT] PMK");
    os_free(ext);

    return ret;
}


static int ar6003_driver_set_key_ext(void *priv, enum wpa_alg alg,
        const u8 *addr, int key_idx,
        int set_tx, const u8 *seq,
        size_t seq_len,
        const u8 *key, size_t key_len)
{
    struct ar6003_driver_data *drv = priv;
    struct iwreq iwr;
    int ret = 0;
    struct iw_encode_ext *ext;

    if (seq_len > IW_ENCODE_SEQ_MAX_SIZE) {
        wpa_printf(MSG_DEBUG, "%s: Invalid seq_len %lu",
                __FUNCTION__, (unsigned long) seq_len);
        return -1;
    }

    ext = os_zalloc(sizeof(*ext) + key_len);
    if (ext == NULL)
        return -1;
    os_memset(&iwr, 0, sizeof(iwr));
    os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
    iwr.u.encoding.flags = key_idx + 1;
    iwr.u.encoding.flags |= IW_ENCODE_TEMP;
    if (alg == WPA_ALG_NONE)
        iwr.u.encoding.flags |= IW_ENCODE_DISABLED;
    iwr.u.encoding.pointer = (caddr_t) ext;
    iwr.u.encoding.length = sizeof(*ext) + key_len;

    if (addr == NULL ||
            os_memcmp(addr, "\xff\xff\xff\xff\xff\xff", ETH_ALEN) == 0)
        ext->ext_flags |= IW_ENCODE_EXT_GROUP_KEY;
    if (set_tx)
        ext->ext_flags |= IW_ENCODE_EXT_SET_TX_KEY;

    ext->addr.sa_family = ARPHRD_ETHER;
    if (addr)
        os_memcpy(ext->addr.sa_data, addr, ETH_ALEN);
    else
        os_memset(ext->addr.sa_data, 0xff, ETH_ALEN);
    if (key && key_len) {
        os_memcpy(ext + 1, key, key_len);
        ext->key_len = key_len;
    }
    switch (alg) {
        case WPA_ALG_NONE:
            ext->alg = IW_ENCODE_ALG_NONE;
            break;
        case WPA_ALG_WEP:
            ext->alg = IW_ENCODE_ALG_WEP;
            break;
        case WPA_ALG_TKIP:
            ext->alg = IW_ENCODE_ALG_TKIP;
            break;
        case WPA_ALG_CCMP:
            ext->alg = IW_ENCODE_ALG_CCMP;
            break;
        case WPA_ALG_PMK:
            ext->alg = IW_ENCODE_ALG_PMK;
            break;
#ifdef CONFIG_IEEE80211W
        case WPA_ALG_IGTK:
            ext->alg = IW_ENCODE_ALG_AES_CMAC;
            break;
#endif /* CONFIG_IEEE80211W */
        default:
            wpa_printf(MSG_DEBUG, "%s: Unknown algorithm %d",
                    __FUNCTION__, alg);
            os_free(ext);
            return -1;
    }

    if (seq && seq_len) {
        ext->ext_flags |= IW_ENCODE_EXT_RX_SEQ_VALID;
        os_memcpy(ext->rx_seq, seq, seq_len);
    }

    if (ioctl(drv->ioctl_sock, SIOCSIWENCODEEXT, &iwr) < 0) {
        ret = errno == EOPNOTSUPP ? -2 : -1;
        if (errno == ENODEV) {
            /*
             * ndiswrapper seems to be returning incorrect error
             * code.. */
            ret = -2;
        }

        perror("ioctl[SIOCSIWENCODEEXT]");
    }

    os_free(ext);
    return ret;
}


/**
 * ar6003_driver_set_key - Configure encryption key
 * @priv: Pointer to private data from ar6003_driver_init()
 * @priv: Private driver interface data
 * @alg: Encryption algorithm (%WPA_ALG_NONE, %WPA_ALG_WEP,
 *    %WPA_ALG_TKIP, %WPA_ALG_CCMP); %WPA_ALG_NONE clears the key.
 * @addr: Address of the peer STA or ff:ff:ff:ff:ff:ff for
 *    broadcast/default keys
 * @key_idx: key index (0..3), usually 0 for unicast keys
 * @set_tx: Configure this key as the default Tx key (only used when
 *    driver does not support separate unicast/individual key
 * @seq: Sequence number/packet number, seq_len octets, the next
 *    packet number to be used for in replay protection; configured
 *    for Rx keys (in most cases, this is only used with broadcast
 *    keys and set to zero for unicast keys)
 * @seq_len: Length of the seq, depends on the algorithm:
 *    TKIP: 6 octets, CCMP: 6 octets
 * @key: Key buffer; TKIP: 16-byte temporal key, 8-byte Tx Mic key,
 *    8-byte Rx Mic Key
 * @key_len: Length of the key buffer in octets (WEP: 5 or 13,
 *    TKIP: 32, CCMP: 16)
 * Returns: 0 on success, -1 on failure
 *
 * This function uses SIOCSIWENCODEEXT by default, but tries to use
 * SIOCSIWENCODE if the extended ioctl fails when configuring a WEP key.
 */
int ar6003_driver_set_key(const char *ifname, void *priv, enum wpa_alg alg,
        const u8 *addr, int key_idx,
        int set_tx, const u8 *seq, size_t seq_len,
        const u8 *key, size_t key_len)
{
    struct ar6003_driver_data *drv = priv;
    struct iwreq iwr;
    int ret = 0;

    wpa_printf(MSG_DEBUG, "%s: alg=%d key_idx=%d set_tx=%d seq_len=%lu "
            "key_len=%lu",
            __FUNCTION__, alg, key_idx, set_tx,
            (unsigned long) seq_len, (unsigned long) key_len);

    ret = ar6003_driver_set_key_ext(drv, alg, addr, key_idx, set_tx,
            seq, seq_len, key, key_len);
    if (ret == 0)
        return 0;

    if (ret == -2 &&
            (alg == WPA_ALG_NONE || alg == WPA_ALG_WEP)) {
        wpa_printf(MSG_DEBUG, "Driver did not support "
                "SIOCSIWENCODEEXT, trying SIOCSIWENCODE");
        ret = 0;
    } else {
        wpa_printf(MSG_DEBUG, "Driver did not support "
                "SIOCSIWENCODEEXT");
        return ret;
    }

    os_memset(&iwr, 0, sizeof(iwr));
    os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
    iwr.u.encoding.flags = key_idx + 1;
    iwr.u.encoding.flags |= IW_ENCODE_TEMP;
    if (alg == WPA_ALG_NONE)
        iwr.u.encoding.flags |= IW_ENCODE_DISABLED;
    iwr.u.encoding.pointer = (caddr_t) key;
    iwr.u.encoding.length = key_len;

    if (ioctl(drv->ioctl_sock, SIOCSIWENCODE, &iwr) < 0) {
        perror("ioctl[SIOCSIWENCODE]");
        ret = -1;
    }

    if (set_tx && alg != WPA_ALG_NONE) {
        os_memset(&iwr, 0, sizeof(iwr));
        os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
        iwr.u.encoding.flags = key_idx + 1;
        iwr.u.encoding.flags |= IW_ENCODE_TEMP;
        iwr.u.encoding.pointer = (caddr_t) NULL;
        iwr.u.encoding.length = 0;
        if (ioctl(drv->ioctl_sock, SIOCSIWENCODE, &iwr) < 0) {
            perror("ioctl[SIOCSIWENCODE] (set_tx)");
            ret = -1;
        }
    }

    return ret;
}


static int ar6003_driver_set_countermeasures(void *priv,
        int enabled)
{
    struct ar6003_driver_data *drv = priv;
    wpa_printf(MSG_DEBUG, "%s", __FUNCTION__);
    return ar6003_driver_set_auth_param(drv,
            IW_AUTH_TKIP_COUNTERMEASURES,
            enabled);
}


static int ar6003_driver_set_drop_unencrypted(void *priv,
        int enabled)
{
    struct ar6003_driver_data *drv = priv;
    wpa_printf(MSG_DEBUG, "%s", __FUNCTION__);
    drv->use_crypt = enabled;
    return ar6003_driver_set_auth_param(drv, IW_AUTH_DROP_UNENCRYPTED,
            enabled);
}


static int ar6003_driver_mlme(struct ar6003_driver_data *drv,
        const u8 *addr, int cmd, int reason_code)
{
    struct iwreq iwr;
    struct iw_mlme mlme;
    int ret = 0;

    os_memset(&iwr, 0, sizeof(iwr));
    os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
    os_memset(&mlme, 0, sizeof(mlme));
    mlme.cmd = cmd;
    mlme.reason_code = reason_code;
    mlme.addr.sa_family = ARPHRD_ETHER;
    os_memcpy(mlme.addr.sa_data, addr, ETH_ALEN);
    iwr.u.data.pointer = (caddr_t) &mlme;
    iwr.u.data.length = sizeof(mlme);

    if (ioctl(drv->ioctl_sock, SIOCSIWMLME, &iwr) < 0) {
        perror("ioctl[SIOCSIWMLME]");
        ret = -1;
    }

    return ret;
}


static void ar6003_driver_disconnect(struct ar6003_driver_data *drv)
{
    struct iwreq iwr;
    const u8 null_bssid[ETH_ALEN] = { 0, 0, 0, 0, 0, 0 };

    /*
     * Only force-disconnect when the card is in infrastructure mode,
     * otherwise the driver might interpret the cleared BSSID and random
     * SSID as an attempt to create a new ad-hoc network.
     */
    os_memset(&iwr, 0, sizeof(iwr));
    os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
    if (ioctl(drv->ioctl_sock, SIOCGIWMODE, &iwr) < 0) {
        perror("ioctl[SIOCGIWMODE]");
        iwr.u.mode = IW_MODE_INFRA;
    }


    if ((iwr.u.mode == IW_MODE_INFRA)) {
        if (ar6003_driver_set_bssid(drv, null_bssid) < 0 ||
                ar6003_driver_set_ssid(drv, (u8 *) "", 0) < 0) {
            wpa_printf(MSG_DEBUG, "Failed to clear "
                    "to disconnect");
        }
    }
}


static int ar6003_driver_deauthenticate(void *priv, const u8 *addr,
        int reason_code)
{
    struct ar6003_driver_data *drv = priv;
    int ret;
    wpa_printf(MSG_DEBUG, "%s", __FUNCTION__);
    ret = ar6003_driver_mlme(drv, addr, IW_MLME_DEAUTH, reason_code);
    ar6003_driver_disconnect(drv);
    return ret;
}

static int ar6003_driver_disassociate(void *priv, const u8 *addr,
        int reason_code)
{
    struct ar6003_driver_data *drv = priv;
    int ret;
    wpa_printf(MSG_DEBUG, "%s", __FUNCTION__);
    ret = ar6003_driver_mlme(drv, addr, IW_MLME_DISASSOC, reason_code);
    ar6003_driver_disconnect(drv);
    return ret;
}


static int ar6003_driver_set_gen_ie(void *priv, const u8 *ie,
        size_t ie_len)
{
    struct ar6003_driver_data *drv = priv;
    struct iwreq iwr;
    int ret = 0;

    os_memset(&iwr, 0, sizeof(iwr));
    os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
    iwr.u.data.pointer = (caddr_t) ie;
    iwr.u.data.length = ie_len;

    if (ioctl(drv->ioctl_sock, SIOCSIWGENIE, &iwr) < 0) {
        perror("ioctl[SIOCSIWGENIE]");
        ret = -1;
    }

    return ret;
}


int ar6003_driver_cipher2wext(int cipher)
{
    switch (cipher) {
        case CIPHER_NONE:
            return IW_AUTH_CIPHER_NONE;
        case CIPHER_WEP40:
            return IW_AUTH_CIPHER_WEP40;
        case CIPHER_TKIP:
            return IW_AUTH_CIPHER_TKIP;
        case CIPHER_CCMP:
            return IW_AUTH_CIPHER_CCMP;
        case CIPHER_WEP104:
            return IW_AUTH_CIPHER_WEP104;
        default:
            return 0;
    }
}


int ar6003_driver_keymgmt2wext(int keymgmt)
{
    switch (keymgmt) {
        case KEY_MGMT_802_1X:
        case KEY_MGMT_802_1X_NO_WPA:
            return IW_AUTH_KEY_MGMT_802_1X;
        case KEY_MGMT_PSK:
            return IW_AUTH_KEY_MGMT_PSK;
        default:
            return 0;
    }
}

static int
ar6003_driver_auth_alg_fallback(struct ar6003_driver_data *drv,
        struct wpa_driver_associate_params *params)
{
    struct iwreq iwr;
    int ret = 0;

    wpa_printf(MSG_DEBUG, "Driver did not support "
            "SIOCSIWAUTH for AUTH_ALG, trying SIOCSIWENCODE");

    os_memset(&iwr, 0, sizeof(iwr));
    os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
    /* Just changing mode, not actual keys */
    iwr.u.encoding.flags = 0;
    iwr.u.encoding.pointer = (caddr_t) NULL;
    iwr.u.encoding.length = 0;

    /*
     * Note: IW_ENCODE_{OPEN,RESTRICTED} can be interpreted to mean two
     * different things. Here they are used to indicate Open System vs.
     * Shared Key authentication algorithm. However, some drivers may use
     * them to select between open/restricted WEP encrypted (open = allow
     * both unencrypted and encrypted frames; restricted = only allow
     * encrypted frames).
     */

    if (!drv->use_crypt) {
        iwr.u.encoding.flags |= IW_ENCODE_DISABLED;
    } else {
        if (params->auth_alg & WPA_AUTH_ALG_OPEN)
            iwr.u.encoding.flags |= IW_ENCODE_OPEN;
        if (params->auth_alg & WPA_AUTH_ALG_SHARED)
            iwr.u.encoding.flags |= IW_ENCODE_RESTRICTED;
    }

    if (ioctl(drv->ioctl_sock, SIOCSIWENCODE, &iwr) < 0) {
        perror("ioctl[SIOCSIWENCODE]");
        ret = -1;
    }

    return ret;
}

int ar6003_driver_associate(void *priv,
        struct wpa_driver_associate_params *params)
{
    struct ar6003_driver_data *drv = priv;
    int ret = 0;
    int allow_unencrypted_eapol;
    int value;

    wpa_printf(MSG_DEBUG, "%s", __FUNCTION__);

    if (ar6003_driver_set_drop_unencrypted(drv, params->drop_unencrypted)
            < 0)
        ret = -1;

    if (ar6003_driver_set_auth_alg(drv, params->auth_alg) < 0)
        ret = -1;
    if (ar6003_driver_set_mode(drv, params->mode) < 0)
        ret = -1;

    /*
     * If the driver did not support SIOCSIWAUTH, fallback to
     * SIOCSIWENCODE here.
     */
    if (drv->auth_alg_fallback &&
            ar6003_driver_auth_alg_fallback(drv, params) < 0)
        ret = -1;

    if (!params->bssid &&
            ar6003_driver_set_bssid(drv, NULL) < 0)
        ret = -1;

    /* TODO: should consider getting wpa version and cipher/key_mgmt suites
     * from configuration, not from here, where only the selected suite is
     * available */

    if(params->mode != IEEE80211_MODE_AP) {
        if (ar6003_driver_set_gen_ie(drv, params->wpa_ie, params->wpa_ie_len)
                < 0)
            ret = -1;

        if (params->wpa_ie == NULL || params->wpa_ie_len == 0)
            value = IW_AUTH_WPA_VERSION_DISABLED;
        else if (params->wpa_ie[0] == WLAN_EID_RSN)
            value = IW_AUTH_WPA_VERSION_WPA2;
        else
            value = IW_AUTH_WPA_VERSION_WPA;
        if (ar6003_driver_set_auth_param(drv,
                    IW_AUTH_WPA_VERSION, value) < 0)
            ret = -1;

        value = ar6003_driver_keymgmt2wext(params->key_mgmt_suite);
        if (ar6003_driver_set_auth_param(drv,
                    IW_AUTH_KEY_MGMT, value) < 0)
            ret = -1;
    }
    value = ar6003_driver_cipher2wext(params->pairwise_suite);
    if (ar6003_driver_set_auth_param(drv,
                IW_AUTH_CIPHER_PAIRWISE, value) < 0)
        ret = -1;
    value = ar6003_driver_cipher2wext(params->group_suite);
    if (ar6003_driver_set_auth_param(drv,
                IW_AUTH_CIPHER_GROUP, value) < 0)
        ret = -1;

    value = params->key_mgmt_suite != KEY_MGMT_NONE ||
        params->pairwise_suite != CIPHER_NONE ||
        params->group_suite != CIPHER_NONE ||
        params->wpa_ie_len;
    if (ar6003_driver_set_auth_param(drv,
                IW_AUTH_PRIVACY_INVOKED, value) < 0)
        ret = -1;

    /* Allow unencrypted EAPOL messages even if pairwise keys are set when
     * not using WPA. IEEE 802.1X specifies that these frames are not
     * encrypted, but WPA encrypts them when pairwise keys are in use. */
    if (params->key_mgmt_suite == KEY_MGMT_802_1X ||
            params->key_mgmt_suite == KEY_MGMT_PSK)
        allow_unencrypted_eapol = 0;
    else
        allow_unencrypted_eapol = 1;

    if (ar6003_driver_set_psk(drv, params->psk) < 0)
        ret = -1;
    if (ar6003_driver_set_auth_param(drv,
                IW_AUTH_RX_UNENCRYPTED_EAPOL,
                allow_unencrypted_eapol) < 0)
        ret = -1;
#ifdef CONFIG_IEEE80211W
    switch (params->mgmt_frame_protection) {
        case NO_MGMT_FRAME_PROTECTION:
            value = IW_AUTH_MFP_DISABLED;
            break;
        case MGMT_FRAME_PROTECTION_OPTIONAL:
            value = IW_AUTH_MFP_OPTIONAL;
            break;
        case MGMT_FRAME_PROTECTION_REQUIRED:
            value = IW_AUTH_MFP_REQUIRED;
            break;
    };
    if (ar6003_driver_set_auth_param(drv, IW_AUTH_MFP, value) < 0)
        ret = -1;
#endif /* CONFIG_IEEE80211W */
    if (params->freq && ar6003_driver_set_freq(drv, params->freq) < 0)
        ret = -1;

    if (params->bssid &&
            ar6003_driver_set_bssid(drv, params->bssid) < 0)
        ret = -1;

    if (params->ssid &&
            ar6003_driver_set_ssid(drv, params->ssid, params->ssid_len) < 0)
        ret = -1;

    return ret;
}


static int ar6003_driver_set_auth_alg(void *priv, int auth_alg)
{
    struct ar6003_driver_data *drv = priv;
    int algs = 0, res;

    if (auth_alg & WPA_AUTH_ALG_OPEN)
        algs |= IW_AUTH_ALG_OPEN_SYSTEM;
    if (auth_alg & WPA_AUTH_ALG_SHARED)
        algs |= IW_AUTH_ALG_SHARED_KEY;
    if (auth_alg & WPA_AUTH_ALG_LEAP)
        algs |= IW_AUTH_ALG_LEAP;
    if (algs == 0) {
        /* at least one algorithm should be set */
        algs = IW_AUTH_ALG_OPEN_SYSTEM;
    }

    res = ar6003_driver_set_auth_param(drv, IW_AUTH_80211_AUTH_ALG,
            algs);
    drv->auth_alg_fallback = res == -2;
    return res;
}


/**
 * ar6003_driver_set_mode - Set wireless mode (infra/adhoc), SIOCSIWMODE
 * @priv: Pointer to private data from ar6003_driver_init()
 * @mode: 0 = infra/BSS (associate with an AP), 1 = adhoc/IBSS
 * Returns: 0 on success, -1 on failure
 */
int ar6003_driver_set_mode(void *priv, int mode)
{
    struct ar6003_driver_data *drv = priv;
    struct iwreq iwr;
    int ret = -1;
    unsigned int new_mode;

    switch (mode) {
        case 0:
            new_mode = IW_MODE_INFRA;
            break;
        case 1:
            new_mode = IW_MODE_ADHOC;
            break;
        case 2:
            new_mode = IW_MODE_MASTER;
            break;
        default:
            return -1;
    }
    os_memset(&iwr, 0, sizeof(iwr));
    os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
    iwr.u.mode = new_mode;
    if (ioctl(drv->ioctl_sock, SIOCSIWMODE, &iwr) == 0) {
        ret = 0;
        goto done;
    }

    if (errno != EBUSY) {
        perror("ioctl[SIOCSIWMODE]");
        goto done;
    }

    /* mac80211 doesn't allow mode changes while the device is up, so if
     * the device isn't in the mode we're about to change to, take device
     * down, try to set the mode again, and bring it back up.
     */
    if (ioctl(drv->ioctl_sock, SIOCGIWMODE, &iwr) < 0) {
        perror("ioctl[SIOCGIWMODE]");
        goto done;
    }

    if (iwr.u.mode == new_mode) {
        ret = 0;
        goto done;
    }

    if (linux_set_iface_flags(drv->ioctl_sock, drv->ifname, 0) == 0) {
        /* Try to set the mode again while the interface is down */
        iwr.u.mode = new_mode;
        if (ioctl(drv->ioctl_sock, SIOCSIWMODE, &iwr) < 0)
            perror("ioctl[SIOCSIWMODE]");
        else
            ret = 0;

        (void) linux_set_iface_flags(drv->ioctl_sock, drv->ifname, 1);
    }

done:
    return ret;
}


static int ar6003_driver_pmksa(struct ar6003_driver_data *drv,
        u32 cmd, const u8 *bssid, const u8 *pmkid)
{
    struct iwreq iwr;
    struct iw_pmksa pmksa;
    int ret = 0;

    os_memset(&iwr, 0, sizeof(iwr));
    os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
    os_memset(&pmksa, 0, sizeof(pmksa));
    pmksa.cmd = cmd;
    pmksa.bssid.sa_family = ARPHRD_ETHER;
    if (bssid)
        os_memcpy(pmksa.bssid.sa_data, bssid, ETH_ALEN);
    if (pmkid)
        os_memcpy(pmksa.pmkid, pmkid, IW_PMKID_LEN);
    iwr.u.data.pointer = (caddr_t) &pmksa;
    iwr.u.data.length = sizeof(pmksa);

    if (ioctl(drv->ioctl_sock, SIOCSIWPMKSA, &iwr) < 0) {
        if (errno != EOPNOTSUPP)
            perror("ioctl[SIOCSIWPMKSA]");
        ret = -1;
    }

    return ret;
}


static int ar6003_driver_add_pmkid(void *priv, const u8 *bssid,
        const u8 *pmkid)
{
    struct ar6003_driver_data *drv = priv;
    return ar6003_driver_pmksa(drv, IW_PMKSA_ADD, bssid, pmkid);
}


static int ar6003_driver_remove_pmkid(void *priv, const u8 *bssid,
        const u8 *pmkid)
{
    struct ar6003_driver_data *drv = priv;
    return ar6003_driver_pmksa(drv, IW_PMKSA_REMOVE, bssid, pmkid);
}


static int ar6003_driver_flush_pmkid(void *priv)
{
    struct ar6003_driver_data *drv = priv;
    return ar6003_driver_pmksa(drv, IW_PMKSA_FLUSH, NULL, NULL);
}


int ar6003_driver_get_capa(void *priv, struct wpa_driver_capa *capa)
{
    struct ar6003_driver_data *drv = priv;
    if (!drv->has_capability)
        return -1;
    os_memcpy(capa, &drv->capa, sizeof(*capa));
    return 0;
}


int ar6003_driver_alternative_ifindex(struct ar6003_driver_data *drv,
        const char *ifname)
{
    if (ifname == NULL) {
        drv->ifindex2 = -1;
        return 0;
    }

    drv->ifindex2 = if_nametoindex(ifname);
    if (drv->ifindex2 <= 0)
        return -1;

    wpa_printf(MSG_DEBUG, "Added alternative ifindex %d (%s) for "
            "wireless events", drv->ifindex2, ifname);

    return 0;
}


int ar6003_driver_set_operstate(void *priv, int state)
{
    struct ar6003_driver_data *drv = priv;

    wpa_printf(MSG_DEBUG, "%s: operstate %d->%d (%s)",
            __func__, drv->operstate, state, state ? "UP" : "DORMANT");
    drv->operstate = state;
    return netlink_send_oper_ifla(drv->netlink, drv->ifindex, -1,
            state ? IF_OPER_UP : IF_OPER_DORMANT);
}


int ar6003_driver_get_version(struct ar6003_driver_data *drv)
{
    return drv->we_version_compiled;
}

/*
 * Configure WPA parameters.
 */
static int
ar6003_configure_wpa(struct ar6003_driver_data *drv,
        struct wpa_bss_params *conf)
{
    int v;

    switch (conf->wpa_group) {
        case WPA_CIPHER_CCMP:
            v = IEEE80211_CIPHER_AES_CCM;
            break;
        case WPA_CIPHER_TKIP:
            v = IEEE80211_CIPHER_TKIP;
            break;
        case WPA_CIPHER_WEP104:
            v = IEEE80211_CIPHER_WEP;
            break;
        case WPA_CIPHER_WEP40:
            v = IEEE80211_CIPHER_WEP;
            break;
        case WPA_CIPHER_NONE:
            v = IEEE80211_CIPHER_NONE;
            break;
        default:
            wpa_printf(MSG_ERROR, "Unknown group key cipher %u",
                    conf->wpa_group);
            return -1;
    }
    wpa_printf(MSG_DEBUG, "%s: group key cipher=%d", __func__, v);
    if (set80211param(drv, IEEE80211_PARAM_MCASTCIPHER, v)) {
        printf("Unable to set group key cipher to %u\n", v);
        return -1;
    }
    if (v == IEEE80211_CIPHER_WEP) {
        /* key length is done only for specific ciphers */
        v = (conf->wpa_group == WPA_CIPHER_WEP104 ? 13 : 5);
        if (set80211param(drv, IEEE80211_PARAM_MCASTKEYLEN, v)) {
            printf("Unable to set group key length to %u\n", v);
            return -1;
        }
    }

    v = 0;
    if (conf->wpa_pairwise & WPA_CIPHER_CCMP)
        v |= 1<<IEEE80211_CIPHER_AES_CCM;
    if (conf->wpa_pairwise & WPA_CIPHER_TKIP)
        v |= 1<<IEEE80211_CIPHER_TKIP;
    if (conf->wpa_pairwise & WPA_CIPHER_NONE)
        v |= 1<<IEEE80211_CIPHER_NONE;
    wpa_printf(MSG_DEBUG,"%s: pairwise key ciphers=0x%x", __func__, v);
    if (set80211param(drv, IEEE80211_PARAM_UCASTCIPHER, v)) {
        printf("Unable to set pairwise key ciphers to 0x%x\n", v);
        return -1;
    }
    wpa_printf(MSG_DEBUG, "%s: enable WPA=0x%x\n", __func__, conf->wpa);
    if (set80211param(drv, IEEE80211_PARAM_WPA, conf->wpa)) {
        printf("Unable to set WPA to %u\n", conf->wpa);
        return -1;
    }
    return 0;
}

static int
set80211param(struct ar6003_driver_data *drv, int op, int arg)
{
    struct iwreq iwr;

    memset(&iwr, 0, sizeof(iwr));
    os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
    iwr.u.mode = op;
    memcpy(iwr.u.name+sizeof(__u32), &arg, sizeof(arg));

    if (ioctl(drv->ioctl_sock, IEEE80211_IOCTL_SETPARAM, &iwr) < 0) {
        perror("ioctl[IEEE80211_IOCTL_SETPARAM]");
        wpa_printf(MSG_DEBUG, "%s: Failed to set parameter (op %d "
                "arg %d)", __func__, op, arg);
        return -1;
    }
    return 0;
}

static int ar6003_key_mgmt(int key_mgmt, int auth_alg)
{
    switch (key_mgmt) {
        case WPA_KEY_MGMT_IEEE8021X:
            return IEEE80211_AUTH_WPA;
        case WPA_KEY_MGMT_PSK:
            return IEEE80211_AUTH_WPA_PSK;
        default:
            return IEEE80211_AUTH_OPEN;
    }
}

static int
ar6003_set_ieee8021x(void *priv, struct wpa_bss_params *params)
{
    struct ar6003_driver_data *drv = priv;
    int auth;

    wpa_printf(MSG_DEBUG, "%s: enabled=%d", __func__, params->enabled);

    if (!params->enabled) {
        /* XXX restore state */
        return set80211param(priv, IEEE80211_PARAM_AUTHMODE,
                IEEE80211_AUTH_AUTO);
    }
    if (!params->wpa && !params->ieee802_1x) {
        hostapd_logger(drv->ctx, NULL, HOSTAPD_MODULE_DRIVER,
                HOSTAPD_LEVEL_WARNING, "No 802.1X or WPA enabled!");
        return -1;
    }
    if (params->wpa && ar6003_configure_wpa(drv, params) != 0) {
        hostapd_logger(drv->ctx, NULL, HOSTAPD_MODULE_DRIVER,
                HOSTAPD_LEVEL_WARNING, "Error configuring WPA state!");
        return -1;
    }
    auth = ar6003_key_mgmt(params->wpa_key_mgmt, AUTH_ALG_OPEN_SYSTEM);
    if (set80211param(priv, IEEE80211_PARAM_AUTHMODE, auth)) {
        hostapd_logger(drv->ctx, NULL, HOSTAPD_MODULE_DRIVER,
                HOSTAPD_LEVEL_WARNING, "Error enabling WPA/802.1X!");
        return -1;
    }

    return 0;
}


static int
ar6003_set_privacy(void *priv, int enabled)
{
    wpa_printf(MSG_DEBUG, "%s: enabled=%d\n", __func__, enabled);

    return set80211param(priv, IEEE80211_PARAM_PRIVACY, enabled);
}

static int
ar6003_set_opt_ie(void *priv, const u8 *ie, size_t ie_len)
{
    /*
     * Do nothing; we setup parameters at startup that define the
     * contents of the beacon information element.
     */
    return 0;
}

static int
ar6003_set_wps_ie(void *priv, const u8 *iebuf, size_t iebuflen, u32 frametype)
{
    u8 buf[256];
    struct ieee80211req_getset_appiebuf * ie;

    ((int *)buf)[0] = AR6000_XIOCTL_WMI_SET_APPIE;
    ie = (struct ieee80211req_getset_appiebuf *) &buf[4];
    ie->app_frmtype = frametype;
    ie->app_buflen = iebuflen;
    if (iebuflen > 0)
        os_memcpy(&(ie->app_buf[0]), iebuf, iebuflen);

    return set80211priv(priv, AR6000_IOCTL_EXTENDED, buf,
            sizeof(struct ieee80211req_getset_appiebuf) + iebuflen);
}

#ifdef CONFIG_WPS
#ifdef IEEE80211_IOCTL_FILTERFRAME
static void ar6003_raw_receive(void *ctx, const u8 *src_addr, const u8 *buf,
        size_t len)
{
    struct ar6003_driver_data *drv = ctx;
    const struct ieee80211_mgmt *mgmt;
    const u8 *end, *ie;
    u16 fc;
    size_t ie_len;

    /* Send Probe Request information to WPS processing */

    if (len < IEEE80211_HDRLEN + sizeof(mgmt->u.probe_req))
        return;
    mgmt = (const struct ieee80211_mgmt *) buf;

    fc = le_to_host16(mgmt->frame_control);
    if (WLAN_FC_GET_TYPE(fc) != WLAN_FC_TYPE_MGMT ||
            WLAN_FC_GET_STYPE(fc) != WLAN_FC_STYPE_PROBE_REQ)
        return;

    end = buf + len;
    ie = mgmt->u.probe_req.variable;
    ie_len = len - (IEEE80211_HDRLEN + sizeof(mgmt->u.probe_req));

    hostapd_wps_probe_req_rx(drv->ctx, mgmt->sa, ie, ie_len);
}
#endif /* IEEE80211_IOCTL_FILTERFRAME */
#endif /* CONFIG_WPS */

static int ar6003_set_param(void *priv, const char *param)
{
    struct ar6003_driver_data *drv = priv;
    const char *pos, *end;

    if (param == NULL)
        return 0;

    wpa_printf(MSG_DEBUG, "AR6003: Set param '%s'", param);
    pos = os_strstr(param, "shared_interface=");
    if (pos) {
        pos += 17;
        end = os_strchr(pos, ' ');
        if (end == NULL)
            end = pos + os_strlen(pos);
        if (end - pos >= IFNAMSIZ) {
            wpa_printf(MSG_ERROR, "AR6003: Too long "
                    "shared_interface name");
            return -1;
        }
        os_memcpy(drv->shared_ifname, pos, end - pos);
        drv->shared_ifname[end - pos] = '\0';
        wpa_printf(MSG_DEBUG, "AR6003: Shared interface: %s",
                drv->shared_ifname);
    }

    return 0;
}

#ifdef CONFIG_HS20
static int wpa_driver_ar6003_send_action(void *priv, unsigned int freq,
					  unsigned int wait_time,
					  const u8 *dst, const u8 *src,
					  const u8 *bssid,
					  const u8 *data, size_t data_len,
					  int no_cck)
{
	struct ar6003_driver_data *drv = priv;
	int ret = -1;
	u8 *buf;
	u16 len=0,frequency=0;
	struct ieee80211_hdr *hdr;


	wpa_printf(MSG_DEBUG, "ar6003: Send Action frame (ifindex=%d, "
		   "freq=%u MHz wait=%d ms no_cck=%d)",
		   drv->ifindex, freq, wait_time, no_cck);

	buf = os_zalloc(4 + 2 + 2 + 4 + 24 + data_len);
	if (buf == NULL)
		return ret;
	os_memcpy((buf + 4 + 2 + 2 + 4 + 24), data, data_len);

	((int *)buf)[0] = AR6000_XIOCTL_WMI_SEND_FRAME;

	hdr = (struct ieee80211_hdr *)(buf + 12);
	hdr->frame_control =
		IEEE80211_FC(WLAN_FC_TYPE_MGMT, WLAN_FC_STYPE_ACTION);
	os_memcpy(hdr->addr1, dst, ETH_ALEN);
	os_memcpy(hdr->addr2, src, ETH_ALEN);
	os_memcpy(hdr->addr3, bssid, ETH_ALEN);

	data_len += 24;
	len = (u16)data_len;
	os_memcpy(&buf[4],&len,sizeof(u16));

	frequency = (u16)freq;
	os_memcpy(&buf[6],&frequency,sizeof(u16));

	wait_time = 50;
	os_memcpy(&buf[8],&wait_time,sizeof(int));

	wpa_hexdump(MSG_DEBUG,"Action frame to firmware ", buf, (12 + data_len));
	ret = set80211priv(drv, AR6000_IOCTL_EXTENDED, buf, (12 + data_len));
	if (-1 == ret ) {
		wpa_printf(MSG_ERROR, "%s: Failed to send public action frame",__func__);
		goto fail;
	}
fail:
	os_free(buf);
	return ret;
}


static void wpa_driver_ar6003_send_action_cancel_wait(void *priv)
{
	wpa_printf(MSG_DEBUG,"ar6003 : send_action_cancel_wait\n");
}

#endif /* CONFIG_HS20 */
const struct wpa_driver_ops wpa_driver_ar6003_ops = {
    .name               = "ar6003",
    .desc               = "Linux wireless extension for AR6003",
    .get_bssid          = ar6003_driver_get_bssid,
    .get_ssid           = ar6003_driver_get_ssid,
    .set_key            = ar6003_driver_set_key,
    .set_countermeasures = ar6003_driver_set_countermeasures,
    .scan2              = ar6003_driver_scan,
    .get_scan_results2  = ar6003_driver_get_scan_results,
    .deauthenticate     = ar6003_driver_deauthenticate,
    .disassociate       = ar6003_driver_disassociate,
    .associate          = ar6003_driver_associate,
    .init               = ar6003_driver_init,
    .deinit             = ar6003_driver_deinit,
    .add_pmkid          = ar6003_driver_add_pmkid,
    .remove_pmkid       = ar6003_driver_remove_pmkid,
    .flush_pmkid        = ar6003_driver_flush_pmkid,
    .get_capa           = ar6003_driver_get_capa,
    .set_operstate      = ar6003_driver_set_operstate,
    .set_ieee8021x      = ar6003_set_ieee8021x,
    .set_privacy        = ar6003_set_privacy,
    .set_generic_elem   = ar6003_set_opt_ie,
    .set_param          = ar6003_set_param,
#ifdef CONFIG_HS20
    .send_action 	= wpa_driver_ar6003_send_action,
    .send_action_cancel_wait = wpa_driver_ar6003_send_action_cancel_wait,
#endif /* CONFIG_HS20 */
};
