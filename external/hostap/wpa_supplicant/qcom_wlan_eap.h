/*
 * Copyright (c) 2012 The Linux Foundation. All rights reserved
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

#ifndef EAP_AR6K_SIM_H
#define EAP_AR6K_SIM_H

#include "common.h"
#include "common/defs.h"
#include "pcsc_funcs.h"

#define IMSI_LENGTH 15
#define BUF_LEN 16

#define AKA_RAND_LEN 16
#define AKA_AUTN_LEN 16
#define AKA_AUTS_LEN 14
#define RES_MAX_LEN 16
#define IK_LEN 16
#define CK_LEN 16

/* Default timeout (in milli-seconds) for synchronous QMI message */
#define WPA_UIM_QMI_DEFAULT_TIMEOUT               5000

/*qmi message library handle*/
#define QMI_INVALID_CLIENT_HANDLE -1

typedef enum {
	SCARD_GSM_SIM=1,
	SCARD_USIM,
	SCARD_NOT_SUPPORTED
} sim_types;

struct scard_data {
	sim_types sim_type;
	int pin1_required;
};

int qcom_wlan_eap_umts_auth(const unsigned char *_rand,
			    const unsigned char *autn,
			    unsigned char *res, size_t *res_len,
			    unsigned char *ik, unsigned char *ck,
			    unsigned char *auts);
Boolean qcom_wlan_eap_gsm_auth(const unsigned char *_rand, unsigned char *sres, unsigned char *kc);
Boolean qcom_wlan_eap_read_card_imsi(unsigned char *identity, int *identity_len);
int qcom_wlan_eap_init(struct scard_data *scard,scard_sim_type sim_type);
int qcom_wlan_eap_deinit();
int qcom_wlan_eap_verify_pin(const char *pin);

#endif
