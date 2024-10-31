/*
 * Copyright (c) 2010-2013  The Linux Foundation. All rights reserved
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
#include "ap/hostapd.h"
#include "ap/ap_config.h"
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
#include "ap/sta_info.h"
#include "ap/ieee802_1x.h"
#include "ap/wpa_auth.h"
#include "ap/accounting.h"
#include "ap/wps_hostapd.h"

#ifdef CONFIG_WPS
#ifndef ETH_P_80211_RAW
#define ETH_P_80211_RAW 0x0019
#endif
#endif /* CONFIG_WPS */

struct ar6000_driver_data {
    struct hostapd_data *hapd;      /* back pointer */

    char    iface[IFNAMSIZ + 1];
    int     ifindex;
    struct l2_packet_data *sock_xmit;   /* raw packet xmit socket */
    struct l2_packet_data *sock_recv;   /* raw packet recv socket */
    int ioctl_sock;         /* socket for ioctl() use */
    struct netlink_data *netlink;
    int we_version;
    u8  acct_mac[ETH_ALEN];
    struct hostap_sta_driver_data acct_data;

    struct l2_packet_data *sock_raw; /* raw 802.11 management frames */
};

static int ar6000_sta_deauth(void *priv, const u8 *own_addr, const u8 *addr, int reason_code);

static int ar6000_key_mgmt(int key_mgmt, int auth_alg);

static int ar6000_get_wmm(struct ar6000_driver_data *drv);

int ar6000_set_hw_mode(void *priv, int hw_mode);
int ar6000_driver_set_intra_bss(void *priv, int ap_isolate);

static int
set80211priv(struct ar6000_driver_data *drv, int op, void *data, int len)
{
    struct iwreq iwr;

    memset(&iwr, 0, sizeof(iwr));
    strncpy(iwr.ifr_name, drv->iface, IFNAMSIZ);
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

static int
set80211param(struct ar6000_driver_data *drv, int op, int arg)
{
    struct iwreq iwr;

    memset(&iwr, 0, sizeof(iwr));
    os_strlcpy(iwr.ifr_name, drv->iface, IFNAMSIZ);
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

static const char *
ether_sprintf(const u8 *addr)
{
    static char buf[sizeof(MACSTR)];

    if (addr != NULL)
        snprintf(buf, sizeof(buf), MACSTR, MAC2STR(addr));
    else
        snprintf(buf, sizeof(buf), MACSTR, 0,0,0,0,0,0);
    return buf;
}

/*
 * Configure WPA parameters.
 */
static int
ar6000_configure_wpa(struct ar6000_driver_data *drv,
        struct wpa_bss_params *params)
{
    int v;
    struct hostapd_bss_config *conf = drv->hapd->conf;

    switch (params->wpa_group) {
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
                    params->wpa_group);
            return -1;
    }
    wpa_printf(MSG_DEBUG, "%s: group key cipher=%d", __func__, v);
    if (set80211param(drv, IEEE80211_PARAM_MCASTCIPHER, v)) {
        printf("Unable to set group key cipher to %u\n", v);
        return -1;
    }
    if (v == IEEE80211_CIPHER_WEP) {
        /* key length is done only for specific ciphers */
        v = (params->wpa_group == WPA_CIPHER_WEP104 ? 13 : 5);
        if (set80211param(drv, IEEE80211_PARAM_MCASTKEYLEN, v)) {
            printf("Unable to set group key length to %u\n", v);
            return -1;
        }
    }

    v = 0;
    if (params->wpa_pairwise & WPA_CIPHER_CCMP)
        v |= 1<<IEEE80211_CIPHER_AES_CCM;
    if (params->wpa_pairwise & WPA_CIPHER_TKIP)
        v |= 1<<IEEE80211_CIPHER_TKIP;
    if (params->wpa_pairwise & WPA_CIPHER_NONE)
        v |= 1<<IEEE80211_CIPHER_NONE;
    wpa_printf(MSG_DEBUG,"%s: pairwise key ciphers=0x%x", __func__, v);
    if (set80211param(drv, IEEE80211_PARAM_UCASTCIPHER, v)) {
        printf("Unable to set pairwise key ciphers to 0x%x\n", v);
        return -1;
    }
    /* wmmenable */
    conf->wmm_enabled = ar6000_get_wmm(drv);
    wpa_printf(MSG_DEBUG,"WMM in hostapd  %d \n",conf->wmm_enabled);

    wpa_printf(MSG_DEBUG, "%s: enable WPA=0x%x\n", __func__, params->wpa);
    if (set80211param(drv, IEEE80211_PARAM_WPA, params->wpa)) {
        printf("Unable to set WPA to %u\n", params->wpa);
        return -1;
    }
    return 0;
}

static int
ar6000_set_ieee8021x(void *priv, struct wpa_bss_params *params)
{
    struct ar6000_driver_data *drv = priv;

    int auth;

    wpa_printf(MSG_DEBUG, "%s: enabled=%d", __func__,params->enabled);

    if (!params->wpa && !params->ieee802_1x) {
        hostapd_logger(drv->hapd, NULL, HOSTAPD_MODULE_DRIVER,
                HOSTAPD_LEVEL_WARNING, "No 802.1X or WPA enabled!");
        return -1;
    }
    if (params->wpa && ar6000_configure_wpa(drv,params) != 0) {
        hostapd_logger(drv->hapd, NULL, HOSTAPD_MODULE_DRIVER,
                HOSTAPD_LEVEL_WARNING, "Error configuring WPA state!");
        return -1;
    }
    auth = ar6000_key_mgmt(params->wpa_key_mgmt, AUTH_ALG_OPEN_SYSTEM);
    if (set80211param(priv, IEEE80211_PARAM_AUTHMODE, auth)) {
        hostapd_logger(drv->hapd, NULL, HOSTAPD_MODULE_DRIVER,
                HOSTAPD_LEVEL_WARNING, "Error enabling WPA/802.1X!");
        return -1;
    }

    return 0;
}

static int
ar6000_set_privacy(void *priv, int enabled)
{
    struct ar6000_driver_data *drv = priv;
    wpa_printf(MSG_DEBUG, "%s: enabled=%d\n", __func__, enabled);

    return set80211param(drv, IEEE80211_PARAM_PRIVACY, enabled);
}

static int
ar6000_set_sta_authorized(void *priv, const u8 *addr, int authorized)
{
    struct ar6000_driver_data *drv = priv;
    struct ieee80211req_mlme mlme;
    int ret;

    wpa_printf(MSG_DEBUG, "%s: addr=%s authorized=%d\n",
            __func__, ether_sprintf(addr), authorized);

    if (authorized)
        mlme.im_op = IEEE80211_MLME_AUTHORIZE;
    else
        mlme.im_op = IEEE80211_MLME_UNAUTHORIZE;
    mlme.im_reason = 0;
    memcpy(mlme.im_macaddr, addr, IEEE80211_ADDR_LEN);
    ret = set80211priv(drv, IEEE80211_IOCTL_SETMLME, &mlme,sizeof(mlme));
    if (ret < 0) {
        wpa_printf(MSG_DEBUG, "%s: Failed to %sauthorize STA " MACSTR,
                __func__, authorized ? "" : "un", MAC2STR(addr));
    }

    return ret;
}

static int
ar6000_sta_set_flags(void *priv, const u8 *addr,int total_flags,
        int flags_or, int flags_and)
{
    /* For now, only support setting Authorized flag */
    if (flags_or & WLAN_STA_AUTHORIZED)
        return ar6000_set_sta_authorized(priv, addr, 1);
    if (!(flags_and & WLAN_STA_AUTHORIZED))
        return ar6000_set_sta_authorized(priv, addr, 0);
    return 0;
}

static int
ar6000_del_key(void *priv, const u8 *addr, int key_idx)
{
    struct ar6000_driver_data *drv = priv;
    struct ieee80211req_del_key wk;
    int ret;

    wpa_printf(MSG_DEBUG, "%s: addr=%s key_idx=%d\n",
            __func__, ether_sprintf(addr), key_idx);

    memset(&wk, 0, sizeof(wk));
    if (addr != NULL) {
        memcpy(wk.idk_macaddr, addr, IEEE80211_ADDR_LEN);
        wk.idk_keyix = 0; //(u8) IEEE80211_KEYIX_NONE;
    } else {
        wk.idk_keyix = key_idx;
    }

    ret = set80211priv(drv, IEEE80211_IOCTL_DELKEY, &wk, sizeof(wk));
    if (ret < 0) {
        wpa_printf(MSG_DEBUG, "%s: Failed to delete key (addr %s"
                " key_idx %d)", __func__, ether_sprintf(addr),
                key_idx);
    }

    return ret;
}

static int
ar6000_set_wep_key(void *priv, int key_idx, const u8 *key,
        size_t key_len, int txkey)
{
    struct ar6000_driver_data *drv = priv;
    struct hostapd_bss_config *conf = drv->hapd->conf;
    struct iwreq iwr;

    memset(&iwr, 0, sizeof(iwr));
    strncpy(iwr.ifr_name, drv->iface, IFNAMSIZ);
    iwr.u.data.pointer = (caddr_t) key;
    iwr.u.data.length = key_len;
    iwr.u.data.flags = key_idx+1;

    if(conf->auth_algs & (WPA_AUTH_ALG_OPEN | WPA_AUTH_ALG_SHARED)) {
        if(conf->auth_algs & WPA_AUTH_ALG_OPEN) {
            iwr.u.data.flags |= IW_ENCODE_OPEN;
        }
        if(conf->auth_algs & WPA_AUTH_ALG_SHARED) {
            iwr.u.data.flags |= IW_ENCODE_RESTRICTED;
        }
    } else {
        wpa_printf(MSG_ERROR, "%s: auth_algs=%d not supported\n",
                __func__, conf->auth_algs);
        return -1;
    }

    if (ioctl(drv->ioctl_sock, SIOCSIWENCODE, &iwr) < 0) {
        perror("ioctl[SIOCSIWENCODE]");
        return -1;
    }

    if(txkey) {
        memset(&iwr, 0, sizeof(iwr));
        strncpy(iwr.ifr_name, drv->iface, IFNAMSIZ);
        iwr.u.data.pointer = NULL;
        iwr.u.data.length = 0;
        iwr.u.data.flags = key_idx+1;

        if (ioctl(drv->ioctl_sock, SIOCSIWENCODE, &iwr) < 0) {
            perror("ioctl[SIOCSIWENCODE]");
            return -1;
        }

    }
    return 0;
}


static int
ar6000_set_key(const char *ifname, void *priv, enum wpa_alg alg,
        const u8 *addr, int key_idx, int set_tx,
        const u8 *seq, size_t seq_len,
        const u8 *key, size_t key_len)
{
    struct ar6000_driver_data *drv = priv;
    struct ieee80211req_key wk;
    u_int8_t cipher;
    int ret;

    if (alg == WPA_ALG_NONE)
        return ar6000_del_key(drv, addr, key_idx);

    wpa_printf(MSG_DEBUG, "%s: alg=%d addr=%s key_idx=%d\n",
            __func__, alg, ether_sprintf(addr), key_idx);

    if (alg == WPA_ALG_WEP)
        return ar6000_set_wep_key(priv, key_idx, key, key_len, set_tx);
    else if (alg == WPA_ALG_TKIP)
        cipher = IEEE80211_CIPHER_TKIP;
    else if (alg == WPA_ALG_CCMP)
        cipher = IEEE80211_CIPHER_AES_CCM;
    else {
        printf("%s: unknown/unsupported algorithm %d\n",
                __func__, alg);
        return -1;
    }

    if (key_len > sizeof(wk.ik_keydata)) {
        printf("%s: key length %lu too big\n", __func__,
                (unsigned long) key_len);
        return -3;
    }

    memset(&wk, 0, sizeof(wk));
    wk.ik_type = cipher;
    wk.ik_flags = IEEE80211_KEY_RECV | IEEE80211_KEY_XMIT;
    wk.ik_keyix = key_idx;

    if (addr == NULL) {
        memset(wk.ik_macaddr, 0xff, IEEE80211_ADDR_LEN);
        wk.ik_flags |= IEEE80211_KEY_DEFAULT;
    } else {
        memcpy(wk.ik_macaddr, addr, IEEE80211_ADDR_LEN);
    }
    wk.ik_keylen = key_len;
    memcpy(wk.ik_keydata, key, key_len);

    ret = set80211priv(drv, IEEE80211_IOCTL_SETKEY, &wk, sizeof(wk));
    if (ret < 0) {
        wpa_printf(MSG_DEBUG, "%s: Failed to set key (addr %s"
                " key_idx %d alg %d key_len %lu set_tx %d)",
                __func__, ether_sprintf(wk.ik_macaddr), key_idx,
                alg, (unsigned long) key_len, set_tx);
    }

    return ret;
}

static int
ar6000_flush(void *priv)
{
#ifdef ar6000_BSD
    u8 allsta[IEEE80211_ADDR_LEN];
    memset(allsta, 0xff, IEEE80211_ADDR_LEN);
    return ar6000_sta_deauth(priv, allsta, IEEE80211_REASON_AUTH_LEAVE);
#else /* ar6000_BSD */
    return 0;       /* XXX */
#endif /* ar6000_BSD */
}


static int
ar6000_set_opt_ie(void *priv, const u8 *ie, size_t ie_len)
{
    /*
     * Do nothing; we setup parameters at startup that define the
     * contents of the beacon information element.
     */
    return 0;
}

static int
ar6000_sta_deauth(void *priv, const u8 *own_addr, const u8 *addr, int reason_code)
{
    struct ar6000_driver_data *drv = priv;
    struct ieee80211req_mlme mlme;
    int ret;

    wpa_printf(MSG_DEBUG, "%s: addr=%s reason_code=%d\n",
            __func__, ether_sprintf(addr), reason_code);

    mlme.im_op = IEEE80211_MLME_DEAUTH;
    mlme.im_reason = reason_code;
    memcpy(mlme.im_macaddr, addr, IEEE80211_ADDR_LEN);
    ret = set80211priv(drv, IEEE80211_IOCTL_SETMLME, &mlme, sizeof(mlme));
    if (ret < 0) {
        wpa_printf(MSG_DEBUG, "%s: Failed to deauth STA (addr " MACSTR
                " reason %d)",
                __func__, MAC2STR(addr), reason_code);
    }

    return ret;
}

static int
ar6000_sta_disassoc(void *priv, const u8 *own_addr, const u8 *addr, int reason_code)
{
    struct ieee80211req_mlme mlme;
    int ret;

    wpa_printf(MSG_DEBUG, "%s: addr=%s reason_code=%d\n",
            __func__, ether_sprintf(addr), reason_code);

    mlme.im_op = IEEE80211_MLME_DISASSOC;
    mlme.im_reason = reason_code;
    memcpy(mlme.im_macaddr, addr, IEEE80211_ADDR_LEN);
    ret = set80211priv(priv, IEEE80211_IOCTL_SETMLME, &mlme, sizeof(mlme));
    if (ret < 0) {
        wpa_printf(MSG_DEBUG, "%s: Failed to disassoc STA (addr "
                MACSTR " reason %d)",
                __func__, MAC2STR(addr), reason_code);
    }

    return ret;
}

static int
ar6000_set_freq(void *priv, struct hostapd_freq_params *freq)
{
    struct ar6000_driver_data *drv = priv;
    struct iwreq iwr;

    memset(&iwr, 0, sizeof(iwr));
    strncpy(iwr.ifr_name, drv->iface, IFNAMSIZ);
    iwr.u.freq.m = freq->channel;

    if (ioctl(drv->ioctl_sock, SIOCSIWFREQ, &iwr) < 0) {
        perror("ioctl[SIOCSIWFREQ]");
        return -1;
    }
    return 0;
}

#ifdef CONFIG_WPS
static void ar6000_raw_receive(void *ctx, const u8 *src_addr, const u8 *buf,
        size_t len)
{
    struct ar6000_driver_data *drv = ctx;
    union wpa_event_data event;

    os_memset(&event, 0, sizeof(event));
    event.rx_probe_req.sa = buf;
    event.rx_probe_req.ie = buf+6;
    event.rx_probe_req.ie_len = len-6;
    wpa_supplicant_event(drv->hapd, EVENT_RX_PROBE_REQ, &event);
}
#endif /* CONFIG_WPS */

static int ar6000_receive_probe_req(struct ar6000_driver_data *drv)
{
    int ret = 0;
#ifdef CONFIG_WPS
    drv->sock_raw = l2_packet_init(drv->iface, NULL, ETH_P_80211_RAW,
            ar6000_raw_receive, drv, 1);
    if (drv->sock_raw == NULL)
        return -1;
#endif /* CONFIG_WPS */
    return ret;
}

#ifdef CONFIG_WPS
static int
ar6000_set_wps_ie(void *priv, const u8 *iebuf, size_t iebuflen, u32 frametype)
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

static int
ar6000_set_ap_wps_ie(void *priv, const struct wpabuf *beacon,
        const struct wpabuf *proberesp,
        const struct wpabuf *assocresp)
{
    if (ar6000_set_wps_ie(priv, beacon ? wpabuf_head(beacon) : NULL,
                beacon ? wpabuf_len(beacon) : 0,
                IEEE80211_APPIE_FRAME_BEACON) < 0)
        return -1;
    return ar6000_set_wps_ie(priv,
            proberesp ? wpabuf_head(proberesp) : NULL,
            proberesp ? wpabuf_len(proberesp) : 0,
            IEEE80211_APPIE_FRAME_PROBE_RESP);
}

#else /* CONFIG_WPS */
#define ar6000_set_wps_beacon_ie NULL
#endif /* CONFIG_WPS */

static void
ar6000_new_sta(struct ar6000_driver_data *drv, u8 addr[IEEE80211_ADDR_LEN])
{

    struct hostapd_data *hapd = drv->hapd;
    struct ieee80211req_wpaie *ie;
    int ielen = 0;
    u8 *iebuf=NULL;
    u8 buf[528]; //sizeof(struct ieee80211req_wpaie) + 4 + extra 6 bytes

    /*
     * Fetch negotiated WPA/RSN parameters from the system.
     */
    memset(buf, 0, sizeof(buf));
    ((int *)buf)[0] = IEEE80211_IOCTL_GETWPAIE;
    ie = (struct ieee80211req_wpaie *)&buf[4];
    memcpy(ie->wpa_macaddr, addr, IEEE80211_ADDR_LEN);

    if (set80211priv(drv, AR6000_IOCTL_EXTENDED, buf, sizeof(*ie)+4)) {
        wpa_printf(MSG_ERROR, "%s: Failed to get WPA/RSN IE", __func__);
        goto no_ie;      /* XXX not right */
    }
    ie = (struct ieee80211req_wpaie *)&buf[4];
    iebuf = ie->wpa_ie;
    ielen = iebuf[1];
    if (ielen == 0)
        iebuf = NULL;
    else
        ielen += 2;
no_ie:
    drv_event_assoc(hapd, addr, iebuf, ielen, 0);

    if (memcmp(addr, drv->acct_mac, ETH_ALEN) == 0) {
        /* Cached accounting data is not valid anymore. */
        memset(drv->acct_mac, 0, ETH_ALEN);
        memset(&drv->acct_data, 0, sizeof(drv->acct_data));
    }
}

static void
ar6000_wireless_event_wireless_custom(struct ar6000_driver_data *drv,
        char *custom)
{

    if (strncmp(custom, "MLME-MICHAELMICFAILURE.indication", 33) == 0) {
        char *pos;
        u8 addr[ETH_ALEN];
        pos = strstr(custom, "addr=");
        if (pos == NULL) {
            wpa_printf(MSG_DEBUG, "MLME-MICHAELMICFAILURE.indication "
                    "without sender address ignored\n");
            return;
        }
        pos += 5;
        if (hwaddr_aton(pos, addr) == 0) {
            union wpa_event_data data;
            os_memset(&data, 0, sizeof(data));
            data.michael_mic_failure.unicast = 1;
            data.michael_mic_failure.src = addr;
            wpa_supplicant_event(drv->hapd,
                    EVENT_MICHAEL_MIC_FAILURE, &data);
        } else {
            wpa_printf(MSG_DEBUG, "MLME-MICHAELMICFAILURE.indication "
                    "with invalid MAC address");
        }
    } else if (strncmp(custom, "STA-TRAFFIC-STAT", 16) == 0) {
        char *key, *value;
        u32 val;
        key = custom;
        while ((key = strchr(key, '\n')) != NULL) {
            key++;
            value = strchr(key, '=');
            if (value == NULL)
                continue;
            *value++ = '\0';
            val = strtoul(value, NULL, 10);
            if (strcmp(key, "mac") == 0)
                hwaddr_aton(value, drv->acct_mac);
            else if (strcmp(key, "rx_packets") == 0)
                drv->acct_data.rx_packets = val;
            else if (strcmp(key, "tx_packets") == 0)
                drv->acct_data.tx_packets = val;
            else if (strcmp(key, "rx_bytes") == 0)
                drv->acct_data.rx_bytes = val;
            else if (strcmp(key, "tx_bytes") == 0)
                drv->acct_data.tx_bytes = val;
            key = value;
        }
    }
}

static void
ar6000_wireless_event_wireless(struct ar6000_driver_data *drv,
        char *data, int len)
{
    struct iw_event iwe_buf, *iwe = &iwe_buf;
    char *pos, *end, *custom, *buf;

    pos = data;
    end = data + len;

    while (pos + IW_EV_LCP_LEN <= end) {
        /* Event data may be unaligned, so make a local, aligned copy
         * before processing. */
        memcpy(&iwe_buf, pos, IW_EV_LCP_LEN);
        wpa_printf(MSG_DEBUG,"Wireless event: "
                "cmd=0x%x len=%d\n", iwe->cmd, iwe->len);
        if (iwe->len <= IW_EV_LCP_LEN)
            return;

        custom = pos + IW_EV_POINT_LEN;
        if (drv->we_version > 18 &&
                (iwe->cmd == IWEVMICHAELMICFAILURE ||
                 iwe->cmd == IWEVCUSTOM)) {
            /* WE-19 removed the pointer from struct iw_point */
            char *dpos = (char *) &iwe_buf.u.data.length;
            int dlen = dpos - (char *) &iwe_buf;
            memcpy(dpos, pos + IW_EV_LCP_LEN,
                    sizeof(struct iw_event) - dlen);
        } else {
            memcpy(&iwe_buf, pos, sizeof(struct iw_event));
            custom += IW_EV_POINT_OFF;
        }

        switch (iwe->cmd) {
            case IWEVEXPIRED:
                drv_event_disassoc(drv->hapd, (u8 *) iwe->u.addr.sa_data);
                break;
            case IWEVREGISTERED:
                ar6000_new_sta(drv, (u8 *) iwe->u.addr.sa_data);
                break;
            case IWEVCUSTOM:
                if (custom + iwe->u.data.length > end)
                    return;
                buf = malloc(iwe->u.data.length + 1);
                if (buf == NULL)
                    return;     /* XXX */
                memcpy(buf, custom, iwe->u.data.length);
                buf[iwe->u.data.length] = '\0';
                ar6000_wireless_event_wireless_custom(drv, buf);
                free(buf);
                break;
        }

        pos += iwe->len;
    }
}


static void
ar6000_wireless_event_rtm_newlink(void *ctx, struct ifinfomsg *ifi,
        u8 *buf, size_t len)
{
    struct ar6000_driver_data *drv = ctx;
    int attrlen, rta_len;
    struct rtattr *attr;

    if (ifi->ifi_index != drv->ifindex)
        return;

    attrlen = len;
    attr = (struct rtattr *) buf;

    rta_len = RTA_ALIGN(sizeof(struct rtattr));
    while (RTA_OK(attr, attrlen)) {
        if (attr->rta_type == IFLA_WIRELESS) {
            ar6000_wireless_event_wireless(
                    drv, ((char *) attr) + rta_len,
                    attr->rta_len - rta_len);
        }
        attr = RTA_NEXT(attr, attrlen);
    }
}

static int
ar6000_get_we_version(struct ar6000_driver_data *drv)
{
    struct iw_range *range;
    struct iwreq iwr;
    int minlen;
    size_t buflen;

    drv->we_version = 0;

    /*
     * Use larger buffer than struct iw_range in order to allow the
     * structure to grow in the future.
     */
    buflen = sizeof(struct iw_range) + 500;
    range = os_zalloc(buflen);
    if (range == NULL)
        return -1;

    memset(&iwr, 0, sizeof(iwr));
    os_strlcpy(iwr.ifr_name, drv->iface, IFNAMSIZ);
    iwr.u.data.pointer = (caddr_t) range;
    iwr.u.data.length = buflen;

    minlen = ((char *) &range->enc_capa) - (char *) range +
        sizeof(range->enc_capa);

    if (ioctl(drv->ioctl_sock, SIOCGIWRANGE, &iwr) < 0) {
        perror("ioctl[SIOCGIWRANGE]");
        free(range);
        return -1;
    } else if (iwr.u.data.length >= minlen &&
            range->we_version_compiled >= 18) {
        wpa_printf(MSG_DEBUG, "SIOCGIWRANGE: WE(compiled)=%d "
                "WE(source)=%d enc_capa=0x%x",
                range->we_version_compiled,
                range->we_version_source,
                range->enc_capa);
        drv->we_version = range->we_version_compiled;
    }

    free(range);
    return 0;
}


static int
ar6000_wireless_event_init(struct ar6000_driver_data *drv)
{
    struct netlink_config *cfg;

    ar6000_get_we_version(drv);

    cfg = os_zalloc(sizeof(*cfg));
    if (cfg == NULL)
        return -1;
    cfg->ctx = drv;
    cfg->newlink_cb = ar6000_wireless_event_rtm_newlink;
    drv->netlink = netlink_init(cfg);
    if (drv->netlink == NULL) {
        os_free(cfg);
        return -1;
    }

    return 0;
}

static int
ar6000_send_eapol(void *priv, const u8 *addr, const u8 *data, size_t data_len,
        int encrypt, const u8 *own_addr, u32 flags)
{
    struct ar6000_driver_data *drv = priv;
    unsigned char buf[3000];
    unsigned char *bp = buf;
    struct l2_ethhdr *eth;
    size_t len;
    int status;

    /*
     * Prepend the Ethernet header.  If the caller left us
     * space at the front we could just insert it but since
     * we don't know we copy to a local buffer.  Given the frequency
     * and size of frames this probably doesn't matter.
     */
    len = data_len + sizeof(struct l2_ethhdr);
    if (len > sizeof(buf)) {
        bp = malloc(len);
        if (bp == NULL) {
            printf("EAPOL frame discarded, cannot malloc temp "
                    "buffer of size %lu!\n", (unsigned long) len);
            return -1;
        }
    }
    eth = (struct l2_ethhdr *) bp;
    memcpy(eth->h_dest, addr, ETH_ALEN);
    memcpy(eth->h_source, own_addr, ETH_ALEN);
    eth->h_proto = htons(ETH_P_EAPOL);
    memcpy(eth+1, data, data_len);

    wpa_hexdump(MSG_MSGDUMP, "TX EAPOL", bp, len);

    status = l2_packet_send(drv->sock_xmit, addr, ETH_P_EAPOL, bp, len);

    if (bp != buf)
        free(bp);
    return status;
}

static void
handle_read(void *ctx, const u8 *src_addr, const u8 *buf, size_t len)
{
    struct ar6000_driver_data *drv = ctx;

    drv_event_eapol_rx(drv->hapd, src_addr, buf + sizeof(struct l2_ethhdr),
            len - sizeof(struct l2_ethhdr));
}

static void *
ar6000_init(struct hostapd_data *hapd, struct wpa_init_params *params)
{
    struct ar6000_driver_data *drv;
    struct ifreq ifr;
    struct iwreq iwr;

    drv = os_zalloc(sizeof(struct ar6000_driver_data));
    if (drv == NULL) {
        printf("Could not allocate memory for ar6000 driver data\n");
        return NULL;
    }

    drv->hapd = hapd;
    drv->ioctl_sock = socket(PF_INET, SOCK_DGRAM, 0);
    if (drv->ioctl_sock < 0) {
        perror("socket[PF_INET,SOCK_DGRAM]");
        goto bad;
    }
    memcpy(drv->iface, params->ifname, sizeof(drv->iface));

    memset(&ifr, 0, sizeof(ifr));
    os_strlcpy(ifr.ifr_name, drv->iface, sizeof(ifr.ifr_name));
    if (ioctl(drv->ioctl_sock, SIOCGIFINDEX, &ifr) != 0) {
        perror("ioctl(SIOCGIFINDEX)");
        goto bad;
    }
    drv->ifindex = ifr.ifr_ifindex;

    drv->sock_xmit = l2_packet_init(drv->iface, NULL, ETH_P_EAPOL,
            handle_read, drv, 1);
    if (drv->sock_xmit == NULL)
        goto bad;
    if (l2_packet_get_own_addr(drv->sock_xmit, params->own_addr))
        goto bad;
    if (params->bridge[0]) {
        wpa_printf(MSG_DEBUG, "Configure bridge %s for EAPOL traffic.",
                params->bridge[0]);
        drv->sock_recv = l2_packet_init(params->bridge[0], NULL,
                ETH_P_EAPOL, handle_read, drv,
                1);
        if (drv->sock_recv == NULL)
            goto bad;
    } else
        drv->sock_recv = drv->sock_xmit;

    memset(&iwr, 0, sizeof(iwr));
    os_strlcpy(iwr.ifr_name, drv->iface, IFNAMSIZ);

    iwr.u.mode = IW_MODE_MASTER;

    if (ioctl(drv->ioctl_sock, SIOCSIWMODE, &iwr) < 0) {
        perror("ioctl[SIOCSIWMODE]");
        goto bad;
    }

    linux_set_iface_flags(drv->ioctl_sock, drv->iface, 1);
    ar6000_set_privacy(drv, 0); /* default to no privacy */

    ar6000_receive_probe_req(drv);

    if (ar6000_wireless_event_init(drv))
        goto bad;

    return drv;
bad:
    if (drv->sock_xmit != NULL)
        l2_packet_deinit(drv->sock_xmit);
    if (drv->ioctl_sock >= 0)
        close(drv->ioctl_sock);
    if (drv != NULL)
        free(drv);
    return NULL;
}


static void
ar6000_deinit(void *priv)
{
    struct ar6000_driver_data *drv = priv;

    drv->hapd->driver = NULL;

    (void) linux_set_iface_flags(drv->ioctl_sock, drv->iface, 0);
    if (drv->ioctl_sock >= 0)
        close(drv->ioctl_sock);
    if (drv->sock_recv != NULL && drv->sock_recv != drv->sock_xmit)
        l2_packet_deinit(drv->sock_recv);
    if (drv->sock_xmit != NULL)
        l2_packet_deinit(drv->sock_xmit);
    if (drv->sock_raw)
        l2_packet_deinit(drv->sock_raw);
    free(drv);
}

static int
ar6000_set_ssid(void *priv, const u8 *buf, int len)
{
    struct ar6000_driver_data *drv = priv;
    struct iwreq iwr;

    memset(&iwr, 0, sizeof(iwr));
    os_strlcpy(iwr.ifr_name, drv->iface, IFNAMSIZ);
    if(buf != NULL) {
        iwr.u.essid.flags = 1; /* SSID active */
        iwr.u.essid.pointer = (caddr_t) buf;
        iwr.u.essid.length = len + 1;
    }
    else {
        iwr.u.essid.flags = 0; /* ESSID off */
    }

    if (ioctl(drv->ioctl_sock, SIOCSIWESSID, &iwr) < 0) {
        perror("ioctl[SIOCSIWESSID]");
        return -1;
    }
    return 0;
}

static int
ar6000_get_ssid(void *priv, u8 *buf, int len)
{
    struct ar6000_driver_data *drv = priv;
    struct iwreq iwr;
    int ret = 0;

    memset(&iwr, 0, sizeof(iwr));
    os_strlcpy(iwr.ifr_name, drv->iface, IFNAMSIZ);
    iwr.u.essid.pointer = (caddr_t) buf;
    iwr.u.essid.length = len;

    if (ioctl(drv->ioctl_sock, SIOCGIWESSID, &iwr) < 0) {
        perror("ioctl[SIOCGIWESSID]");
        /*ret = -1; */
        ret = 0;
    } else
        ret = iwr.u.essid.length;

    return ret;
}

static int
ar6000_set_countermeasures(void *priv, int enabled)
{
    struct ar6000_driver_data *drv = priv;
    wpa_printf(MSG_DEBUG, "%s: enabled=%d", __FUNCTION__, enabled);
    return set80211param(drv, IEEE80211_PARAM_COUNTERMEASURES, enabled);
}

int
ar6000_set_hidden_ssid(void *priv, const u8 hid)
{
    struct ar6000_driver_data *drv = priv;
    char buf[16];
    struct ifreq ifr;
    WMI_AP_HIDDEN_SSID_CMD *pHidden = (WMI_AP_HIDDEN_SSID_CMD *)(buf + 4);

    memset(&ifr, 0, sizeof(ifr));
    pHidden->hidden_ssid = hid;

    ((int *)buf)[0] = AR6000_XIOCTL_AP_HIDDEN_SSID;
    os_strlcpy(ifr.ifr_name, drv->iface, sizeof(ifr.ifr_name));
    ifr.ifr_data = buf;
    if (ioctl(drv->ioctl_sock, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
        perror("ioctl[AP_HIDDEN_SSID]");
        return -1;
    }

    return 0;
}

static int
ar6000_set_mac_acl(void *priv)
{
    struct ar6000_driver_data *drv = priv;
    struct hostapd_bss_config *conf = drv->hapd->conf;
    struct ifreq ifr;
    int i, abuf[(sizeof(WMI_AP_ACL_MAC_CMD)/sizeof(A_INT32))+2];
    WMI_AP_ACL_MAC_CMD *pAcl = (WMI_AP_ACL_MAC_CMD*)(abuf + 1);
    WMI_AP_ACL_POLICY_CMD *pAclPolicy = (WMI_AP_ACL_POLICY_CMD*)(abuf+1);
    int totalAcl = 0;
    struct mac_acl_entry *macAcl = NULL;

    if(!conf->macaddr_acl) return 0;

    memset(&ifr, 0, sizeof(ifr));
    memset(&abuf, 0, sizeof(abuf));

    if (conf->macaddr_acl == ACCEPT_UNLESS_DENIED) {
        totalAcl = conf->num_deny_mac;
        macAcl = conf->deny_mac;
        pAclPolicy->policy = (AP_ACL_DENY_MAC|0);
    } else if (conf->macaddr_acl == DENY_UNLESS_ACCEPTED) {
        totalAcl = conf->num_accept_mac;
        macAcl = conf->accept_mac;
        pAclPolicy->policy = (AP_ACL_ALLOW_MAC|0);
    } else {
        pAclPolicy->policy = (AP_ACL_DISABLE|0);
    }

    strncpy(ifr.ifr_name, drv->iface, IFNAMSIZ);
    abuf[0] = AR6000_XIOCTL_AP_SET_ACL_POLICY;
    ifr.ifr_data = (char *)abuf;
    if (ioctl(drv->ioctl_sock, AR6000_IOCTL_EXTENDED, &ifr)<0) {
        wpa_printf(MSG_ERROR, "Failed to set mac ACL policy %d\n", pAclPolicy->policy);
        return 1;
    }

    if (totalAcl > 0) {
        for (i=0; i<totalAcl; ++i) {
            memset(&ifr, 0, sizeof(ifr));
            memset(&abuf, 0, sizeof(abuf));
            strncpy(ifr.ifr_name, drv->iface, IFNAMSIZ);
            abuf[0] = AR6000_XIOCTL_AP_SET_ACL_MAC;
            pAcl->action = ADD_MAC_ADDR;
            memcpy(pAcl->mac, macAcl[i].addr, 6);
            ifr.ifr_data = (char *)abuf;
            if (ioctl(drv->ioctl_sock, AR6000_IOCTL_EXTENDED, &ifr)<0) {
                wpa_printf(MSG_ERROR, "Failed to add mac to ACL\n");
                return 1;
            }
        }
    }

    return 0;
}

static int
ar6000_commit(void *priv)
{
    struct ar6000_driver_data *drv = priv;
    struct iwreq iwr;

    memset(&iwr, 0, sizeof(iwr));
    strncpy(iwr.ifr_name, drv->iface, IFNAMSIZ);

    if (ioctl(drv->ioctl_sock, SIOCSIWCOMMIT, &iwr) < 0) {
        perror("ioctl[SIOCSIWCOMMIT]");
        return -1;
    }
    return 0;
}

const struct wpa_driver_ops wpa_driver_ar6000_ops = {
    .name               = "ar6000",
    .set_key        = ar6000_set_key,
    .hapd_init          = ar6000_init,
    .hapd_deinit        = ar6000_deinit,
    .set_ieee8021x      = ar6000_set_ieee8021x,
    .set_privacy        = ar6000_set_privacy,
    .flush              = ar6000_flush,
    .set_generic_elem   = ar6000_set_opt_ie,
    .sta_set_flags      = ar6000_sta_set_flags,
    .hapd_send_eapol    = ar6000_send_eapol,
    .sta_disassoc       = ar6000_sta_disassoc,
    .sta_deauth         = ar6000_sta_deauth,
    .set_freq           = ar6000_set_freq,
    .hapd_set_ssid      = ar6000_set_ssid,
    .hapd_get_ssid      = ar6000_get_ssid,
    .hapd_set_countermeasures    = ar6000_set_countermeasures,
    .commit             = ar6000_commit,
    .set_ap_wps_ie = ar6000_set_ap_wps_ie,
    .hapd_set_hidden_ssid   = ar6000_set_hidden_ssid,
    .hapd_set_mac_acl   = ar6000_set_mac_acl,
    .hapd_set_hw_mode   = ar6000_set_hw_mode,
    .hapd_set_intra_bss = ar6000_driver_set_intra_bss,
};

static int ar6000_key_mgmt(int key_mgmt, int auth_alg)
{
    switch (key_mgmt) {
        case WPA_KEY_MGMT_IEEE8021X:
            return IEEE80211_AUTH_WPA;
        case WPA_KEY_MGMT_PSK:
            return IEEE80211_AUTH_WPA_PSK;
            /*case KEY_MGMT_NONE:
              if (auth_alg == AUTH_ALG_OPEN_SYSTEM)
              return IEEE80211_AUTH_OPEN;
              if (auth_alg == AUTH_ALG_SHARED_KEY)
              return IEEE80211_AUTH_SHARED;
              */
        default:
            return IEEE80211_AUTH_OPEN;
    }
}

int
ar6000_set_max_num_sta(void *priv, const u8 num_sta)
{
    struct ar6000_driver_data *drv = priv;
    char buf[16];
    struct ifreq ifr;
    WMI_AP_NUM_STA_CMD *pNumSta = (WMI_AP_NUM_STA_CMD *)(buf + 4);

    memset(&ifr, 0, sizeof(ifr));
    pNumSta->num_sta = num_sta;

    ((int *)buf)[0] = AR6000_XIOCTL_AP_SET_NUM_STA;
    os_strlcpy(ifr.ifr_name, drv->iface, sizeof(ifr.ifr_name));
    ifr.ifr_data = buf;
    if (ioctl(drv->ioctl_sock, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
        perror("ioctl[SET_NUM_STA]");
        return -1;
    }

    return 0;
}
static int ar6000_get_wmm(struct ar6000_driver_data *drv)
{
    char buf[16];
    struct ifreq ifr;
    A_BOOL wmmenabled;


    memset(&ifr, 0, sizeof(ifr));
    memset(buf, 0, sizeof(buf));

    ((int *)buf)[0] = AR6000_XIOCTL_WMI_GET_WMM;
    os_strlcpy(ifr.ifr_name, drv->iface, sizeof(ifr.ifr_name));
    ifr.ifr_data = buf;
    if (ioctl(drv->ioctl_sock, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
        perror("ioctl[GET_WMM]");
        return -1;
    }

    wmmenabled = *(A_BOOL *)buf;

    return wmmenabled;
}

int ar6000_set_hw_mode(void *priv, int hw_mode)
{
    struct ar6000_driver_data *drv = (struct ar6000_driver_data *)priv;
    char buf[16] = {0};
    struct ifreq ifr;
    WMI_CHANNEL_PARAMS_CMD *chParamCmd = (WMI_CHANNEL_PARAMS_CMD *)buf;
    WMI_PHY_MODE phy_mode = WMI_11G_MODE;

    memset(&ifr, 0, sizeof(ifr));

    switch (hw_mode)
    {
        case HOSTAPD_MODE_IEEE80211A:
            phy_mode = WMI_11A_MODE;
			break;
        case HOSTAPD_MODE_IEEE80211B:
            phy_mode = WMI_11B_MODE;
			break;
        case HOSTAPD_MODE_IEEE80211G:
        default:
            /*invalid mode, set to defult to BG*/
            phy_mode = WMI_11G_MODE;
            break;
    }

    chParamCmd->phyMode   = phy_mode;

    os_strlcpy(ifr.ifr_name, drv->iface, sizeof(ifr.ifr_name));
    ifr.ifr_data = buf;
    if (ioctl(drv->ioctl_sock, AR6000_IOCTL_WMI_SET_CHANNELPARAMS, &ifr) < 0) {
        perror("ioctl[Set_Hw_Mode]");
        return -1;
    }
    return 0;
}


int ar6000_driver_set_intra_bss(void *priv, int ap_isolate)
{
    char buf[16];
    struct ifreq ifr;
    struct ar6000_driver_data *drv = (struct ar6000_driver_data *)priv;
    A_UINT8 *intra = (A_UINT8 *)(buf + 4);

    memset(&ifr, 0, sizeof(ifr));
    *intra = ap_isolate;
    *intra &= 0xF;

    ((int *)buf)[0] = AR6000_XIOCTL_AP_CTRL_BSS_COMM;
    os_strlcpy(ifr.ifr_name, drv->iface, sizeof(ifr.ifr_name));
    ifr.ifr_data = buf;
    if (ioctl(drv->ioctl_sock, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
        perror("ioctl[SET_ISOLATE_AP]");
        return -1;
    }
    return 0;

}

