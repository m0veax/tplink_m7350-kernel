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

#include <sys/types.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/types.h>
#include <linux/if.h>
#include <linux/wireless.h>
#include <arpa/inet.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "qmi.h"
#include "qmi_client.h"
#include "user_identity_module_v01.h"
#include "qmi_uim_srvc.h"
#include "qmi_idl_lib.h"

#include "qcom_wlan_eap.h"
#include "pcsc_funcs.h"

#define AKA_RAND_LEN 16
#define AKA_AUTN_LEN 16
#define AKA_AUTS_LEN 14
#define RES_MAX_LEN 16
#define IK_LEN 16
#define CK_LEN 16

#define UNKNOWN				0
#define PIN_ENABLED_NOT_VERIFIED	1
#define PIN_ENABLED_VERIFIED		2
#define PIN_DISABLED			3
#define SIM_BLOCKED			4
#define SIM_PERM_BLOCKED		5

typedef struct
{
  uim_card_state_enum_v01			card_state;
  uim_card_error_code_enum_v01			card_error_code;
  u8						app_state;
  u8						app_type;
  u8						pin1_state;
} uim_card_info_type;

typedef struct
{
  int                                   card_ready_idx;
  uim_card_info_type                card_info[QMI_UIM_CARDS_MAX_V01];
  qmi_client_type                       qmi_uim_svc_client_ptr;
  int                                   qmi_msg_lib_handle;
} uim_struct_type;

/* Global variable with the card status */
static uim_struct_type   uim;
static qmi_idl_service_object_type qp_uim_service_object;
static int qmi_handle, card_mnc_len;

int qcom_wlan_eap_verify_pin(const char *pin)
{
	int length;
	unsigned char                      *data;
	int                                 src  = 0, dst  = 0,i;
	Boolean                             qmi_status = TRUE;
	qmi_client_error_type               qmi_err_code      = 0;
	uim_verify_pin_req_msg_v01	   *qmi_verify_pin_req_ptr = NULL;
	uim_verify_pin_resp_msg_v01	   *qmi_verify_pin_resp_ptr = NULL;

	if(PIN_DISABLED == uim.card_info[uim.card_ready_idx].pin1_state){
		return 0;
	}

	if(PIN_ENABLED_NOT_VERIFIED == uim.card_info[uim.card_ready_idx].pin1_state){

		if(NULL == pin){
			wpa_printf(MSG_DEBUG, "No PIN configured for SIM access");
			return -1;
		}

		qmi_verify_pin_req_ptr = (uim_verify_pin_req_msg_v01 *)
						malloc(sizeof(uim_verify_pin_req_msg_v01));
		if (qmi_verify_pin_req_ptr == NULL) {
			wpa_printf(MSG_WARNING,
				"Couldn't allocate memory for qmi_read_trans_req_ptr !\n");
			return -1;
		}

		qmi_verify_pin_resp_ptr = (uim_verify_pin_resp_msg_v01 *)
						malloc(sizeof(uim_verify_pin_resp_msg_v01));
		if (qmi_verify_pin_resp_ptr == NULL) {
			wpa_printf(MSG_WARNING,
				"Couldn't allocate memory for qmi_read_trans_resp_ptr !\n");

			if (qmi_verify_pin_req_ptr)
				free(qmi_verify_pin_req_ptr);

			return -1;
		}

		memset(qmi_verify_pin_req_ptr, 0,
				sizeof(uim_verify_pin_req_msg_v01));
		memset(qmi_verify_pin_resp_ptr , 0,
				sizeof(uim_verify_pin_resp_msg_v01));

		qmi_verify_pin_req_ptr->session_information.session_type =
					QMI_UIM_SESSION_TYPE_PRI_GW_PROV;
		qmi_verify_pin_req_ptr->session_information.aid_len = 0;

		qmi_verify_pin_req_ptr->verify_pin.pin_id = UIM_PIN_ID_PIN_1_V01;
		qmi_verify_pin_req_ptr->verify_pin.pin_value_len = sizeof(pin);
		memcpy(qmi_verify_pin_req_ptr->verify_pin.pin_value,pin,sizeof(pin));

		qmi_err_code = qmi_client_send_msg_sync(uim.qmi_uim_svc_client_ptr,
						QMI_UIM_VERIFY_PIN_REQ_V01,
						(void *)qmi_verify_pin_req_ptr,
						sizeof(*qmi_verify_pin_req_ptr),
						(void *) qmi_verify_pin_resp_ptr,
						sizeof(*qmi_verify_pin_resp_ptr),
						WPA_UIM_QMI_DEFAULT_TIMEOUT);

		if (QMI_NO_ERR != qmi_err_code) {
			wpa_printf(MSG_WARNING, "Unable to verify the pin from UIM service qmi_err_code=%x\n",qmi_err_code);

			/* Free the allocated read request buffer */
			if (qmi_verify_pin_req_ptr)
				free(qmi_verify_pin_req_ptr);

			/* Free the allocated read response buffer */
			if (qmi_verify_pin_resp_ptr)
				free(qmi_verify_pin_resp_ptr);

			return -1;
		}

		/* Free the allocated read request buffer */
		if (qmi_verify_pin_req_ptr)
			free(qmi_verify_pin_req_ptr);

		/* Free the allocated read response buffer */
		if (qmi_verify_pin_resp_ptr)
			free(qmi_verify_pin_resp_ptr);

	}
	return 0;
}

static Boolean qcom_wlan_eap_read_card_status(struct scard_data *scard, scard_sim_type sim_type)
{
	unsigned int                        i = 0, j = 0;
	Boolean                             card_found = FALSE;
	qmi_client_error_type               qmi_err_code      = 0;
	uim_get_card_status_resp_msg_v01   *qmi_response_ptr  = NULL;

	qmi_response_ptr = (uim_get_card_status_resp_msg_v01 *)
		malloc(sizeof(uim_get_card_status_resp_msg_v01));
	if (qmi_response_ptr == NULL) {
		wpa_printf(MSG_ERROR,
			"Couldn't allocate memory for qmi_response_ptr !\n");
		return FALSE;
	}

	os_memset(qmi_response_ptr,
				0,
				sizeof(uim_get_card_status_resp_msg_v01));
	qmi_err_code = qmi_client_send_msg_sync(uim.qmi_uim_svc_client_ptr,
						QMI_UIM_GET_CARD_STATUS_REQ_V01,
						NULL,
						0,
						(void *) qmi_response_ptr,
						sizeof(*qmi_response_ptr),
						WPA_UIM_QMI_DEFAULT_TIMEOUT);
	wpa_printf(MSG_ERROR,
				"QMI_UIM_GET_CARD_STATUS_REQ_V01, qmi_err_code: 0x%x\n",
				qmi_err_code);
	if (qmi_err_code != QMI_NO_ERR) {
		wpa_printf(MSG_ERROR,
			"Error for QMI_UIM_GET_CARD_STATUS_REQ_V01, qmi_err_code: 0x%x\n",
			qmi_err_code);
		free(qmi_response_ptr);
		return FALSE;
	}

	/* Updated global card status if needed */
	if (!qmi_response_ptr->card_status_valid ||
			(qmi_response_ptr->resp.result != QMI_RESULT_SUCCESS_V01)) {
		wpa_printf(MSG_ERROR, "card_status is not valid !\n");
		free(qmi_response_ptr);
		return FALSE;
	}
	/* Update global in case of new card state or error code */
	for (i = 0;
		i < QMI_UIM_CARDS_MAX_V01 &&
		i < qmi_response_ptr->card_status.card_info_len; i++) {
		wpa_printf(MSG_ERROR, "card_info[i].card_state: 0x%x\n",
			qmi_response_ptr->card_status.card_info[i].card_state);
		wpa_printf(MSG_ERROR, "card_info[i].error_code: 0x%x\n",
			qmi_response_ptr->card_status.card_info[i].error_code);

		uim.card_info[i].card_state =
			qmi_response_ptr->card_status.card_info[i].card_state;

		uim.card_info[i].card_error_code =
			qmi_response_ptr->card_status.card_info[i].error_code;


		if (qmi_response_ptr->card_status.card_info[i].card_state ==
			UIM_CARD_STATE_PRESENT_V01) {
			for (j = 0 ; j < QMI_UIM_APPS_MAX_V01 ; j++) {
				uim.card_info[i].app_type =
					qmi_response_ptr->card_status.card_info[i].app_info[j].app_type;

				uim.card_info[i].app_state =
					qmi_response_ptr->card_status.card_info[i].app_info[j].app_state;

				uim.card_info[i].pin1_state =
					qmi_response_ptr->card_status.card_info[i].app_info[j].pin1.pin_state;

				if (((qmi_response_ptr->card_status.card_info[i].app_info[j].app_type == UIM_APP_TYPE_SIM_V01) ||
				     (qmi_response_ptr->card_status.card_info[i].app_info[j].app_type == UIM_APP_TYPE_USIM_V01)) &&
				    ((qmi_response_ptr->card_status.card_info[i].app_info[j].app_state ==
				      UIM_APP_STATE_READY_V01) ||
				     (qmi_response_ptr->card_status.card_info[i].app_info[j].app_state ==
				      UIM_APP_STATE_PIN1_OR_UPIN_REQ_V01))) {

					wpa_printf(MSG_ERROR, "card READY\n");
					wpa_printf(MSG_ERROR, "card_info[i].app_type : 0x%x\n",
					qmi_response_ptr->card_status.card_info[i].app_info[j].app_type);
					wpa_printf(MSG_ERROR, "card_info[i].app_state : 0x%x\n",
					qmi_response_ptr->card_status.card_info[i].app_info[j].app_state);
					wpa_printf(MSG_ERROR, "card_info[i].pin1_state : 0x%x\n",
					qmi_response_ptr->card_status.card_info[i].app_info[j].pin1.pin_state);

					scard->sim_type = SCARD_GSM_SIM;
					if (sim_type == SCARD_USIM_ONLY || sim_type == SCARD_TRY_BOTH) {
						wpa_printf(MSG_DEBUG, "SCARD: verifying USIM support");
						if (qmi_response_ptr->card_status.card_info[i].app_info[j].app_type != UIM_APP_TYPE_USIM_V01) {
							 wpa_printf(MSG_ERROR, "SCARD: USIM is not supported");
							 if (sim_type == SCARD_USIM_ONLY)
								 scard->sim_type = SCARD_NOT_SUPPORTED;
							 wpa_printf(MSG_DEBUG, "SCARD: Trying to use GSM SIM");
							 scard->sim_type = SCARD_GSM_SIM;
						}else{
							wpa_printf(MSG_DEBUG, "SCARD: USIM is supported");
							scard->sim_type = SCARD_USIM;
						}
					}
					scard->pin1_required = qmi_response_ptr->card_status.card_info[i].app_info[j].pin1.pin_state;
					card_found = TRUE;
					break;
				}
			}
		}

		if (card_found) {
			wpa_printf(MSG_ERROR, "card found\n");
			break;
		}
	}

	if ((!card_found) || (i ==QMI_UIM_CARDS_MAX_V01) ||
		(j == QMI_UIM_APPS_MAX_V01)) {

		if (qmi_response_ptr)
			free(qmi_response_ptr);

		wpa_printf(MSG_ERROR, "SIM/USIM not ready\n");
		return FALSE;
	}

	uim.card_ready_idx = i;

	/* Free the allocated response buffer */
	if (qmi_response_ptr)
		free(qmi_response_ptr);

	return TRUE;
} /* qcom_wlan_eap_read_card_status */



static char bin_to_hexchar(unsigned char ch)
{
	if (ch < 0x0a) {
		return ch + '0';
	}
	return ch + 'a' - 10;
}

int qcom_wlan_eap_umts_auth(const unsigned char *_rand,
			    const unsigned char *autn,
			    unsigned char *res, size_t *res_len,
			    unsigned char *ik, unsigned char *ck,
			    unsigned char *auts)
{
	unsigned char *buf, *pos, *end;
	int src = 0, dst = 0, qmi_ret = 0, i, len;
	qmi_client_error_type qmi_err_code = 0;
	uim_authenticate_req_msg_v01 *qmi_auth_req_ptr = NULL;
	uim_authenticate_resp_msg_v01 *qmi_auth_resp_ptr = NULL;

	wpa_hexdump(MSG_DEBUG, "SCARD: UMTS auth - RAND", _rand, AKA_RAND_LEN);
	wpa_hexdump(MSG_DEBUG, "SCARD: UMTS auth - AUTN", autn, AKA_AUTN_LEN);

	qmi_auth_req_ptr = (uim_authenticate_req_msg_v01 *)
			    malloc(sizeof(uim_authenticate_req_msg_v01));

	if (qmi_auth_req_ptr == NULL) {
		wpa_printf(MSG_WARNING, "memory allocation failed for"
			   "qmi_auth_req_ptr");
		qmi_ret = -1;
		goto fail_umts_auth;
	}

	qmi_auth_resp_ptr = (uim_authenticate_resp_msg_v01 *)
			     malloc(sizeof(uim_authenticate_resp_msg_v01));

	if (qmi_auth_resp_ptr == NULL) {
		wpa_printf(MSG_WARNING, "memory allocation failed for"
			   "qmi_auth_resp_ptr");
		qmi_ret = -1;
		goto fail_umts_auth;
	}

	memset(qmi_auth_req_ptr , 0,
	       sizeof(uim_authenticate_req_msg_v01));
	memset(qmi_auth_resp_ptr , 0,
	       sizeof(uim_authenticate_resp_msg_v01));

	qmi_auth_req_ptr->session_information.session_type =
					QMI_UIM_SESSION_TYPE_PRI_GW_PROV;
	qmi_auth_req_ptr->session_information.aid_len = 0;
	qmi_auth_req_ptr->authentication_data.context =
					UIM_AUTH_CONTEXT_3G_SEC_V01;

	/*
		From 3GPP TS 31.102

		-------------------------------------------------------------------------------
		Byte(s)             Description                 Length
		-------------------------------------------------------------------------------
		  1                 Length of RAND (L1)             1
		  2 to (L1+1)           RAND                        L1
		(L1+2)              Length of AUTN (L2) (see note)  1
		(L1+3) to (L1+L2+2)     AUTN            (see note)  L2
		-------------------------------------------------------------------------------
		Note: Parameter present if and only if in 3G security context.
		-------------------------------------------------------------------------------

			data[0] = rand value length
			data[1-16] =random value
			data[17] = AUTN value length
			data[18-33] AUTN value
	*/

	qmi_auth_req_ptr->authentication_data.data_len = AKA_RAND_LEN + AKA_AUTN_LEN + 2;

	qmi_auth_req_ptr->authentication_data.data[0] = AKA_RAND_LEN;
	memcpy(qmi_auth_req_ptr->authentication_data.data + 1, _rand,
	       AKA_RAND_LEN);
	qmi_auth_req_ptr->authentication_data.data[AKA_RAND_LEN + 1] = AKA_AUTN_LEN;
	memcpy((qmi_auth_req_ptr->authentication_data.data + AKA_RAND_LEN + 2),
		autn, AKA_AUTN_LEN);

	qmi_err_code = qmi_client_send_msg_sync(uim.qmi_uim_svc_client_ptr,
						QMI_UIM_AUTHENTICATE_REQ_V01,
						(void *)qmi_auth_req_ptr,
						sizeof(*qmi_auth_req_ptr),
						(void *)qmi_auth_resp_ptr,
						sizeof(*qmi_auth_resp_ptr),
						WPA_UIM_QMI_DEFAULT_TIMEOUT);
	if (qmi_err_code != QMI_NO_ERR) {
		wpa_printf(MSG_WARNING, "SCARD: Unable to get UMTS auth from"
			   "UIM service qmi_err_code = %x", qmi_err_code);
		qmi_ret = -1;
		goto fail_umts_auth;
	}

	if (qmi_auth_resp_ptr->content_valid == 0) {
		wpa_printf(MSG_WARNING, "SCARD: UMTS auth content is invalid");
		qmi_ret = -1;
		goto fail_umts_auth;
	}

	len = qmi_auth_resp_ptr->content_len;
	buf = qmi_auth_resp_ptr->content;

	wpa_printf(MSG_EXCESSIVE,"SCARD: UMTS auth content is valid");
	wpa_hexdump(MSG_DEBUG, "SCARD: UMTS auth response result", buf, len);

	if ((len >= 2 + AKA_AUTS_LEN) && (buf[0] == 0xdc) &&
	    (buf[1] == AKA_AUTS_LEN)) {

		wpa_printf(MSG_WARNING, "SCARD: UMTS Synchronization-Failure");
		os_memcpy(auts, buf + 2, AKA_AUTS_LEN);
		wpa_hexdump(MSG_WARNING, "SCARD: AUTS", auts, AKA_AUTS_LEN);

		/*
		 * Only when UMTS synchronization failure happens, propogate
		 * error value of -2 all other cases return -1
		 */
		qmi_ret = -2;
		goto fail_umts_auth;
	} else if ((len >= 6 + IK_LEN + CK_LEN) && (buf[0] == 0xdb)) {
		pos = buf + 1;
		end = buf + len;

		/* RES */
		if ((pos[0] > RES_MAX_LEN) || (pos + pos[0] > end)) {
			wpa_printf(MSG_WARNING, "SCARD: UMTS auth"
				   "invalid RES");
			qmi_ret = -1;
			goto fail_umts_auth;
		}

		*res_len = *pos++;
		os_memcpy(res, pos, *res_len);
		pos += *res_len;
		wpa_hexdump(MSG_DEBUG, "SCARD: RES", res, *res_len);

		/* CK */
		if ((pos[0] != CK_LEN) || (pos + CK_LEN > end)) {
			wpa_printf(MSG_WARNING, "SCARD: Invalid CK");
			qmi_ret = -1;
			goto fail_umts_auth;
		}

		pos++;
		os_memcpy(ck, pos, CK_LEN);
		pos += CK_LEN;
		wpa_hexdump(MSG_DEBUG, "SCARD: CK", ck, CK_LEN);

		/* IK */
		if ((pos[0] != IK_LEN) || (pos + IK_LEN > end)) {
			wpa_printf(MSG_WARNING, "SCARD: Invalid IK");
			qmi_ret = -1;
			goto fail_umts_auth;
		}

		pos++;
		os_memcpy(ik, pos, IK_LEN);
		pos += IK_LEN;
		wpa_hexdump(MSG_DEBUG, "SCARD: IK", ik, IK_LEN);
	}

fail_umts_auth:

	/* Free the allocated read request buffer */
	if (qmi_auth_req_ptr)
		free(qmi_auth_req_ptr);

	/* Free the allocated read response buffer */
	if (qmi_auth_resp_ptr)
		free(qmi_auth_resp_ptr);

	return qmi_ret;
}

Boolean qcom_wlan_eap_gsm_auth(const unsigned char *_rand, unsigned char *sres, unsigned char *kc)
{
	int length;
	unsigned char                      *data;
	int                                 src  = 0, dst  = 0,i;
	Boolean                             card_found = FALSE,qmi_status = TRUE;
	qmi_client_error_type               qmi_err_code      = 0;
	uim_authenticate_req_msg_v01	*qmi_auth_req_ptr = NULL;
	uim_authenticate_resp_msg_v01  *qmi_auth_resp_ptr = NULL;

	qmi_auth_req_ptr = (uim_authenticate_req_msg_v01 *)
					malloc(sizeof(uim_authenticate_req_msg_v01));
	if (qmi_auth_req_ptr == NULL) {
		wpa_printf(MSG_WARNING,
			"Couldn't allocate memory for qmi_read_trans_req_ptr !\n");
		return FALSE;
	}
	qmi_auth_resp_ptr = (uim_authenticate_resp_msg_v01 *)
					malloc(sizeof(uim_authenticate_resp_msg_v01));
	if (qmi_auth_resp_ptr == NULL) {
		wpa_printf(MSG_WARNING,
			"Couldn't allocate memory for qmi_read_trans_resp_ptr !\n");

		if (qmi_auth_req_ptr)
			free(qmi_auth_req_ptr);

		return FALSE;
	}

	memset(qmi_auth_req_ptr , 0,
			sizeof(uim_authenticate_req_msg_v01));
	memset(qmi_auth_resp_ptr , 0,
			sizeof(uim_authenticate_resp_msg_v01));

	qmi_auth_req_ptr->session_information.session_type =
				QMI_UIM_SESSION_TYPE_PRI_GW_PROV;
	qmi_auth_req_ptr->session_information.aid_len = 0;
	/*
		From 3GPP TS 31.102

		-------------------------------------------------------------------------------
		Byte(s)             Description                 Length
		-------------------------------------------------------------------------------
		  1                 Length of RAND (L1)             1
		  2 to (L1+1)           RAND                        L1
		(L1+2)              Length of AUTN (L2) (see note)  1
		(L1+3) to (L1+L2+2)     AUTN            (see note)  L2
		-------------------------------------------------------------------------------
		Note: Parameter present if and only if in 3G security context.
		-------------------------------------------------------------------------------

	*/
	if(uim.card_info[uim.card_ready_idx].app_type == UIM_APP_TYPE_SIM_V01)
	{
		qmi_auth_req_ptr->authentication_data.context = UIM_AUTH_CONTEXT_RUN_GSM_ALG_V01;
		qmi_auth_req_ptr->authentication_data.data_len = BUF_LEN;
		memcpy(qmi_auth_req_ptr->authentication_data.data,_rand,BUF_LEN);
	}
	else if(uim.card_info[uim.card_ready_idx].app_type == UIM_APP_TYPE_USIM_V01)
	{
		qmi_auth_req_ptr->authentication_data.context = UIM_AUTH_CONTEXT_GSM_SEC_V01;
		qmi_auth_req_ptr->authentication_data.data_len = BUF_LEN+1;
		qmi_auth_req_ptr->authentication_data.data[0]  = BUF_LEN;
		memcpy(qmi_auth_req_ptr->authentication_data.data+1,_rand,BUF_LEN);
	}
	else
		wpa_printf(MSG_WARNING,"Unsupprted SIM \n");


	qmi_err_code = qmi_client_send_msg_sync(uim.qmi_uim_svc_client_ptr,
					QMI_UIM_AUTHENTICATE_REQ_V01,
					(void *)qmi_auth_req_ptr,
					sizeof(*qmi_auth_req_ptr),
					(void *)qmi_auth_resp_ptr,
					sizeof(*qmi_auth_resp_ptr),
					WPA_UIM_QMI_DEFAULT_TIMEOUT);

	if (QMI_NO_ERR == qmi_err_code) {
		if (qmi_auth_resp_ptr->content_valid)
		{
			length  = qmi_auth_resp_ptr->content_len;
			data    = qmi_auth_resp_ptr->content;

			if(uim.card_info[uim.card_ready_idx].app_type == UIM_APP_TYPE_SIM_V01)
			{
				memcpy(sres, data, 4);
				memcpy(kc, data+4, 8);
			}
			else if(uim.card_info[uim.card_ready_idx].app_type == UIM_APP_TYPE_USIM_V01)
			{
				/*
				   From 3GPP TS 31.102

				   ----------------------------------------------
				   Byte(s)  Description             Length
				   ----------------------------------------------
				   1        Length of SRES (= 4)        1
				   2 to 5       SRES                    4
				   6        Length of KC (= 8)          1
				   7 to 14      KC                      8
				   ----------------------------------------------
				 */
				memcpy(sres, data+1, 4);
				memcpy(kc, data+6, 8);
			}
			else
				wpa_printf(MSG_WARNING, "Unsupprted SIM \n");
		}

		else
		{
			wpa_printf(MSG_WARNING, "Content is invalid Length %d\n", qmi_auth_resp_ptr->content_len);
			qmi_status =FALSE;
		}

	}
	/* Free the allocated read request buffer */
	if (qmi_auth_req_ptr)
		free(qmi_auth_req_ptr);

	/* Free the allocated read response buffer */
	if (qmi_auth_resp_ptr)
		free(qmi_auth_resp_ptr);

	return qmi_status;
}

int qcom_wlan_eap_read_mnc()
{
	wpa_printf(MSG_WARNING,"mnc value %d \n",card_mnc_len);
	return card_mnc_len;
}

Boolean qcom_wlan_eap_read_card_imsi(unsigned char *identity, int *identity_len)
{
	unsigned char *data, *imsi = NULL;
	int src  = 0, dst  = 0, i, length, imsi_len = 0;
	Boolean qmi_status = TRUE;
	qmi_client_error_type qmi_err_code = 0;
	uim_read_transparent_req_msg_v01 *qmi_read_trans_req_ptr = NULL;
	uim_read_transparent_resp_msg_v01 *qmi_read_trans_resp_ptr = NULL;

	if (identity == NULL) {
		wpa_printf(MSG_ERROR, "IMSI/identity pointer is NULL!");
		return FALSE;
	}

	qmi_read_trans_req_ptr = (uim_read_transparent_req_msg_v01 *)
				 malloc(sizeof(uim_read_transparent_req_msg_v01));

	if (qmi_read_trans_req_ptr == NULL) {
		wpa_printf(MSG_WARNING,
			   "Couldn't allocate memory for qmi_read_trans_req_ptr!");
		return FALSE;
	}

	qmi_read_trans_resp_ptr = (uim_read_transparent_resp_msg_v01 *)
				  malloc(sizeof(uim_read_transparent_resp_msg_v01));

	if (qmi_read_trans_resp_ptr == NULL) {

		wpa_printf(MSG_WARNING,
			   "Couldn't allocate memory for qmi_read_trans_resp_ptr!");

		qmi_status = FALSE;
		goto fail_imsi_read;

	}

	memset(qmi_read_trans_resp_ptr, 0,
	       sizeof(uim_read_transparent_resp_msg_v01));
	memset(qmi_read_trans_req_ptr, 0,
	       sizeof(uim_read_transparent_req_msg_v01));

	qmi_read_trans_req_ptr->read_transparent.length = 0;
	qmi_read_trans_req_ptr->read_transparent.offset = 0;
	qmi_read_trans_req_ptr->file_id.file_id = 0x6F07;
	qmi_read_trans_req_ptr->file_id.path_len = 4;
	qmi_read_trans_req_ptr->session_information.session_type =
				QMI_UIM_SESSION_TYPE_PRI_GW_PROV;
	qmi_read_trans_req_ptr->session_information.aid_len = 0;

	if (uim.card_info[uim.card_ready_idx].app_type ==
	    UIM_APP_TYPE_USIM_V01) {
		qmi_read_trans_req_ptr->file_id.path[0] = 0x00;
		qmi_read_trans_req_ptr->file_id.path[1] = 0x3F;
		qmi_read_trans_req_ptr->file_id.path[2] = 0xFF;
		qmi_read_trans_req_ptr->file_id.path[3] = 0x7F;
	} else
		if ((uim.card_info[uim.card_ready_idx].app_type ==
					UIM_APP_TYPE_SIM_V01)) {
			qmi_read_trans_req_ptr->file_id.path[0] = 0x00;
			qmi_read_trans_req_ptr->file_id.path[1] = 0x3F;
			qmi_read_trans_req_ptr->file_id.path[2] = 0x20;
			qmi_read_trans_req_ptr->file_id.path[3] = 0x7F;
		}
	else {
		wpa_printf(MSG_ERROR, "Invalid app_type to read IMSI!");
		qmi_status = FALSE;
		goto fail_imsi_read;
	}

	qmi_err_code = qmi_client_send_msg_sync(uim.qmi_uim_svc_client_ptr,
					QMI_UIM_READ_TRANSPARENT_REQ_V01,
					(void *)qmi_read_trans_req_ptr,
					sizeof(*qmi_read_trans_req_ptr),
					(void *) qmi_read_trans_resp_ptr,
					sizeof(*qmi_read_trans_resp_ptr),
					WPA_UIM_QMI_DEFAULT_TIMEOUT);

	if (qmi_err_code != QMI_NO_ERR) {
		wpa_printf(MSG_ERROR, "Unable to read IMSI from UIM service"
			   "qmi_err_code = %x", qmi_err_code);
		qmi_status = FALSE;
		goto fail_imsi_read;
	}

	if (qmi_read_trans_resp_ptr->read_result_valid == 0) {
		wpa_printf(MSG_ERROR, "IMSI read failure"
			   "read_result_valid = %d",
			   qmi_read_trans_resp_ptr->read_result_valid);
		qmi_status = FALSE;
		goto fail_imsi_read;
	}

	length  = qmi_read_trans_resp_ptr->read_result.content_len;
	data    = qmi_read_trans_resp_ptr->read_result.content;

	/*
	 * wpa_supplicant expects the IMSI in ASCII string format
	 * and usually its a 15 digit long number. Convert the recieved
	 * 3GPP format into ASCII string format and assign the appropriate
	 * length(imsi_len) for the destination IMSI pointer to hold the value.
	 */

	imsi_len = (length - 2) * 2 + 1;
	imsi = (unsigned char *)malloc((2 * length));

	if (imsi == NULL) {
		wpa_printf(MSG_ERROR, "Couldn't allocate memory for imsi!");
		qmi_status = FALSE;
		goto fail_imsi_read;
	}

	memset(imsi, 0, (2 * length));

	for (src = 1, dst = 0; (src <= length) && (dst < (length * 2));	src++) {
		if (src > 1) {
			imsi[dst] = bin_to_hexchar(data[src] & 0x0F);
			dst++;
		}
		/* Process upper part of byte for all bytes */
		imsi[dst] = bin_to_hexchar(data[src] >> 4);
		dst++;
	}

	if (*identity_len < imsi_len) {
		wpa_printf(MSG_ERROR, "IMSI length is too large = %d", imsi_len);
		qmi_status = FALSE;
		goto fail_imsi_read;
	}

	memcpy(identity, imsi, imsi_len);
	*identity_len = imsi_len;

	wpa_printf(MSG_DEBUG, "IMSI file length = %d"
		   "imsilen = %d\n", length, imsi_len);


	/* READ EF_AD */
	/* if qmi_status is FALSE, UIM read for mnc may not be required - To Do */
	qmi_read_trans_req_ptr->file_id.file_id = 0x6FAD;
	qmi_err_code = qmi_client_send_msg_sync(uim.qmi_uim_svc_client_ptr,
					QMI_UIM_READ_TRANSPARENT_REQ_V01,
					(void *)qmi_read_trans_req_ptr,
					sizeof(*qmi_read_trans_req_ptr),
					(void *) qmi_read_trans_resp_ptr,
					sizeof(*qmi_read_trans_resp_ptr),
					WPA_UIM_QMI_DEFAULT_TIMEOUT);

	card_mnc_len = -1;

	if (QMI_NO_ERR == qmi_err_code) {

		if (qmi_read_trans_resp_ptr->read_result_valid) {
			length  =
				qmi_read_trans_resp_ptr->read_result.content_len;
			data    =
				qmi_read_trans_resp_ptr->read_result.content;

			card_mnc_len = data[3];
		}
	}
	else {
		qmi_status = FALSE;
		wpa_printf(MSG_ERROR,
					"MNC read failed=%x\n",qmi_err_code);
	}

fail_imsi_read:

	/* Free the allocated read request buffer */
	if (qmi_read_trans_req_ptr)
		free(qmi_read_trans_req_ptr);

	/* Free the allocated read response buffer */
	if (qmi_read_trans_resp_ptr)
		free(qmi_read_trans_resp_ptr);

	if (imsi != NULL)
		free(imsi);

	return qmi_status;
} /* qmi_read_card_imsi */


int qcom_wlan_eap_init(struct scard_data *scard, scard_sim_type sim_type)
{
	qmi_client_error_type rc;
	int i;

	/* Initialize the qmi datastructure(Once per process ) */
	qmi_handle = qmi_init(NULL, NULL);

	if (qmi_handle < 0) {
		wpa_printf(MSG_WARNING, "qmi message library not initialized");
		return qmi_handle;
	}

	qp_uim_service_object = uim_get_service_object_v01();

	if (qp_uim_service_object == NULL) {

		wpa_printf(MSG_WARNING, "Error: UIM service object is NULL");

		if (qmi_handle >= 0)
			qmi_release(qmi_handle);

		return -1;
	}

	/**
	 * Initialize a QMI UIM connection to first QMI control port
	 * qo_uim_service_object is defined in library header
	 */

	rc = qmi_client_init(QMI_PORT_RMNET_0,
			     qp_uim_service_object,
			     NULL,
			     qp_uim_service_object,
			     &uim.qmi_uim_svc_client_ptr);

	if (rc != QMI_NO_ERR) {

		if (qmi_handle >= 0)
			qmi_release(qmi_handle);

		wpa_printf(MSG_WARNING, "QMI Client is not initialized,"
			   " Error Code : %d", rc);
		return rc;
	}
	else {
		if (!qcom_wlan_eap_read_card_status(scard, sim_type)) {
			wpa_printf(MSG_WARNING, "Read card status failure");
			return -1;
		}
	}

	wpa_printf(MSG_DEBUG, "Read card status pass");

	return 0;
}

int qcom_wlan_eap_deinit()
{
	int ret = 0;

	if (qmi_handle >= 0)
		ret = qmi_release(qmi_handle);

	if (ret == -1) {
		wpa_printf(MSG_WARNING, "%s not Deinitialized", __func__);
		return -1;
	}

	wpa_printf(MSG_INFO, "%s Deinitialized", __func__);
	return 0;
}
