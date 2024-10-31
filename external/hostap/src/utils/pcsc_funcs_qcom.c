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

#include "includes.h"
#include "common.h"
#include "pcsc_funcs.h"
#include "qcom_wlan_eap.h"

/**
 * scard_init - Initialize SIM/USIM connection using PC/SC
 * @sim_type: Allowed SIM types (SIM, USIM, or both)
 * @reader: Reader name prefix to search for
 * Returns: Pointer to private data structure, or %NULL on failure
 *
 * This function is used to initialize SIM/USIM connection. PC/SC is used to
 * open connection to the SIM/USIM card and the card is verified to support the
 * selected sim_type. In addition, local flag is set if a PIN is needed to
 * access some of the card functions. Once the connection is not needed
 * anymore, scard_deinit() can be used to close it.
 */
struct scard_data * scard_init(scard_sim_type sim_type, const char *reader)
{
	int ret;
	struct scard_data *scard;

	wpa_printf(MSG_DEBUG, "SCARD: initializing smart card interface");

	scard = os_zalloc(sizeof(*scard));

	if (scard == NULL) {
		wpa_printf(MSG_ERROR, "SCARD: memory allocation"
			   "failed for scard");
		return NULL;
	}

	ret = qcom_wlan_eap_init(scard,sim_type);

	if (ret != 0) {
		wpa_printf(MSG_WARNING, "SCARD: %s failed", __func__);
		goto failed_card_init;
	}

	return scard;

failed_card_init:
	scard_deinit(scard);
	return NULL;
}

/**
 * scard_set_pin - Set PIN (CHV1/PIN1) code for accessing SIM/USIM commands
 * @scard: Pointer to private data from scard_init()
 * @pin: PIN code as an ASCII string (e.g., "1234")
 * Returns: 0 on success, -1 on failure
 */
int scard_set_pin(struct scard_data *scard, const char *pin)
{
	if (scard == NULL)
		return -1;

	return (qcom_wlan_eap_verify_pin(pin));
}


/**
 * scard_deinit - Deinitialize SIM/USIM connection
 * @scard: Pointer to private data from scard_init()
 *
 * This function closes the SIM/USIM connect opened with scard_init().
 */
void scard_deinit(struct scard_data *scard)
{
	int ret = 0;
	ret = qcom_wlan_eap_deinit();

	if (ret != -1)
		wpa_printf(MSG_INFO, "Deinitialized SIM/USIM connection");
	else
		wpa_printf(MSG_INFO, "SIM/USIM connection not Deinitialzed");

	os_free(scard);
}


/**
 * scard_get_imsi - Read IMSI from SIM/USIM card
 * @scard: Pointer to private data from scard_init()
 * @imsi: Buffer for IMSI
 * @len: Length of imsi buffer; set to IMSI length on success
 * Returns: 0 on success, -1 if IMSI file cannot be selected
 *
 * This function can be used to read IMSI from the SIM/USIM card. If the IMSI
 * file is PIN protected, scard_set_pin() must have been used to set the
 * correct PIN code before calling scard_get_imsi().
 */
int scard_get_imsi(struct scard_data *scard, char *imsi, size_t *len)
{
	Boolean qmi_ret;
	char *pos;

	qmi_ret = qcom_wlan_eap_read_card_imsi(imsi, len);

	if (qmi_ret == FALSE) {
		wpa_printf(MSG_WARNING, "Reading IMSI failed");
		return -1;
	}

	wpa_printf(MSG_DEBUG, "IMSI value read");

	return 0;
}

/**
 * scard_get_mnc_len - Read length of MNC in the IMSI from SIM/USIM card
 * @scard: Pointer to private data from scard_init()
 * Returns: length (>0) on success, -1 if administrative data file cannot be
 * selected, -2 if administrative data file selection returns invalid result
 * code, -3 if parsing FSP template file fails (USIM only), -4 if length of
 * the file is unexpected, -5 if reading file fails, -6 if MNC length is not
 * in range (i.e. 2 or 3), -7 if MNC length is not available.
 *
 */
int scard_get_mnc_len(struct scard_data *scard)
{
	int qmi_ret = 0;
	char *pos;

	qmi_ret = qcom_wlan_eap_read_mnc();

	if (qmi_ret == -1) {
		wpa_printf(MSG_WARNING, "Reading mnc failed");
		return -1;
	}

	wpa_printf(MSG_DEBUG, "mnc value read");

	return qmi_ret;
}


/**
 * scard_gsm_auth - Run GSM authentication command on SIM card
 * @scard: Pointer to private data from scard_init()
 * @_rand: 16-byte RAND value from HLR/AuC
 * @sres: 4-byte buffer for SRES
 * @kc: 8-byte buffer for Kc
 * Returns: 0 on success, -1 if SIM/USIM connection has not been initialized,
 *
 * This function performs GSM authentication using SIM/USIM card and the
 * provided RAND value from HLR/AuC. If authentication command can be completed
 * successfully, SRES and Kc values will be written into sres and kc buffers.
 */
int scard_gsm_auth(struct scard_data *scard, const unsigned char *_rand,
		   unsigned char *sres, unsigned char *kc)
{
	Boolean qmi_ret;

	qmi_ret = qcom_wlan_eap_gsm_auth(_rand, sres, kc);

	if (qmi_ret == FALSE)
		return -1;

	return 0;
}

/**
 * scard_umts_auth - Run UMTS authentication command on USIM card
 * @scard: Pointer to private data from scard_init()
 * @_rand: 16-byte RAND value from HLR/AuC
 * @autn: 16-byte AUTN value from HLR/AuC
 * @res: 16-byte buffer for RES
 * @res_len: Variable that will be set to RES length
 * @ik: 16-byte buffer for IK
 * @ck: 16-byte buffer for CK
 * @auts: 14-byte buffer for AUTS
 * Returns: 0 on success, -1 on failure, or -2 if USIM reports synchronization
 * failure
 *
 * This function performs AKA authentication using USIM card and the provided
 * RAND and AUTN values from HLR/AuC. If authentication command can be
 * completed successfully, RES, IK, and CK values will be written into provided
 * buffers and res_len is set to length of received RES value. If USIM reports
 * synchronization failure, the received AUTS value will be written into auts
 * buffer. In this case, RES, IK, and CK are not valid.
 */
int scard_umts_auth(struct scard_data *scard, const unsigned char *_rand,
		    const unsigned char *autn,
		    unsigned char *res, size_t *res_len,
		    unsigned char *ik, unsigned char *ck, unsigned char *auts)
{
	int qmi_ret;

	if (scard == NULL)
                return -1;

        if (scard->sim_type == SCARD_GSM_SIM) {
                wpa_printf(MSG_ERROR, "SCARD: Non-USIM card - cannot do"
                           "UMTS auth");
                return -1;
        }

	qmi_ret = qcom_wlan_eap_umts_auth(_rand, autn, res, res_len,
					  ik, ck, auts);
	return qmi_ret;
}

int scard_supports_umts(struct scard_data *scard)
{
	return scard->sim_type == SCARD_USIM;
}
