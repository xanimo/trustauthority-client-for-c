/*
 * Copyright (C) 2024 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <tdx_adapter.h>
#include <openssl/evp.h>
#include <jansson.h>
#include <rest.h>
#include <json.h>
#include <base64.h>
#include <log.h>
#include <tss2/tss2_esys.h>

int azure_tdx_adapter_new(evidence_adapter **adapter)
{
	tdx_adapter_context *ctx = NULL;
	if (NULL == adapter)
	{
		return STATUS_TDX_ERROR_BASE | STATUS_NULL_ADAPTER;
	}

	*adapter = (evidence_adapter *)malloc(sizeof(evidence_adapter));
	if (NULL == *adapter)
	{
		return STATUS_TDX_ERROR_BASE | STATUS_ALLOCATION_ERROR;
	}

	ctx = (tdx_adapter_context *)calloc(1, sizeof(tdx_adapter_context));
	if (NULL == ctx)
	{
		free(*adapter);
		*adapter = NULL;
		return STATUS_TDX_ERROR_BASE | STATUS_ALLOCATION_ERROR;
	}

	(*adapter)->ctx = ctx;
	(*adapter)->collect_evidence = tdx_collect_evidence_azure;

	return STATUS_OK;
}

int tdx_adapter_free(evidence_adapter *adapter)
{
	if (NULL == adapter)
	{
		return STATUS_NULL_ADAPTER;
	}

	if (NULL != adapter->ctx)
	{
		free(adapter->ctx);
		adapter->ctx = NULL;
	}

	free(adapter);
	adapter = NULL;
	return STATUS_OK;
}

int tdx_collect_evidence_azure(void *ctx,
		evidence *evidence,
		nonce *nonce,
		uint8_t *user_data,
		uint32_t user_data_len)
{
	// Duplicate code till line EVP_MD_CTX_free(mdctx)
	tdx_adapter_context *tdx_ctx = NULL;
	if (NULL == ctx)
	{
		return STATUS_TDX_ERROR_BASE | STATUS_NULL_ADAPTER_CTX;
	}

	if (NULL == evidence)
	{
		return STATUS_TDX_ERROR_BASE | STATUS_NULL_EVIDENCE;
	}

	if (user_data_len > 0 && user_data == NULL)
	{
		return STATUS_TDX_ERROR_BASE | STATUS_INVALID_USER_DATA;
	}

	tdx_ctx = (tdx_adapter_context *)ctx;
	uint32_t nonce_data_len = 0;
	uint8_t *nonce_data = NULL;
	uint8_t *tpm_report;
	uint8_t *td_report;
	uint8_t *runtime_data;
	uint32_t runtime_data_len;
	uint8_t *td_quote;
	json_t *runtime_data_json;
	json_t *user_data_json;
	char *user_data_string;
	int status = STATUS_OK;

	if (NULL != nonce)
	{
		if (nonce->val == NULL)
		{
			return STATUS_TDX_ERROR_BASE | STATUS_NULL_NONCE;
		}
		// append nonce->val and nonce->iat
		nonce_data_len = nonce->val_len + nonce->iat_len;
		nonce_data = (uint8_t *)calloc(1, (nonce_data_len + 1) * sizeof(uint8_t));
		if (NULL == nonce_data)
		{
			status = STATUS_ALLOCATION_ERROR;
			goto ERROR;
		}

		memcpy(nonce_data, nonce->val, nonce->val_len);
		memcpy(nonce_data + nonce->val_len, nonce->iat, nonce->iat_len);
	}

	uint8_t report_data[TDX_REPORT_DATA_SIZE] = {0};
	if (nonce_data != NULL || user_data != NULL)
	{
		// Hashing Nonce and UserData
		unsigned char md_value[EVP_MAX_MD_SIZE];
		unsigned int md_len;
		const EVP_MD *md = EVP_get_digestbyname("sha512");
		EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
		EVP_DigestInit_ex(mdctx, md, NULL);
		EVP_DigestUpdate(mdctx, nonce_data, nonce_data_len);
		EVP_DigestUpdate(mdctx, user_data, user_data_len);
		EVP_DigestFinal_ex(mdctx, md_value, &md_len);
		EVP_MD_CTX_free(mdctx);
		memcpy(report_data, md_value, TDX_REPORT_DATA_SIZE);
	}

	DEBUG("Report data generated: %s", report_data);

	status = get_td_report(report_data, &tpm_report);
	if (status != 0)
	{
		ERROR("TD report fetch from TPM NV index failed");
		goto ERROR;
	}

	td_report = (uint8_t *)malloc(TD_REPORT_SIZE * sizeof(uint8_t));
	if (td_report == NULL)
	{
		ERROR("Failed to allocate memory for TD report");
		status = STATUS_ALLOCATION_ERROR;
		goto ERROR;
	}
	// Copy the actual TD report from the response recieved from TPM
	memcpy(td_report, tpm_report + TD_REPORT_OFFSET, TD_REPORT_SIZE);

	uint16_t quote_size;
	uint8_t tmp[4];
	memcpy(tmp, tpm_report + RUNTIME_DATA_SIZE_OFFSET, 4);
	// Convert to little endian format
	runtime_data_len = (uint32_t)tmp[0] | (uint32_t)tmp[1] << 8 | (uint32_t)(tmp[2]) << 16 | (uint32_t)(tmp[3]) << 24;

	runtime_data = (uint8_t *)calloc(runtime_data_len, sizeof(uint8_t));
	if (runtime_data == NULL)
	{
		ERROR("Failed to allocate memory for runtime data");
		status = STATUS_ALLOCATION_ERROR;
		goto ERROR;
	}
	memcpy(runtime_data, tpm_report + RUNTIME_DATA_OFFSET, runtime_data_len);

	DEBUG("Runtime data size: %d", runtime_data_len);
	DEBUG("Runtime data: %s", runtime_data);

	status = get_td_quote(td_report, &td_quote, &quote_size);
	if (status != 0)
	{
		ERROR("TD quote generation failed");
		goto ERROR;
	}

	json_error_t error;
	runtime_data_json = json_loads((const char *)runtime_data, 0, &error);
	if (!runtime_data_json)
	{
		status = STATUS_JSON_DECODING_ERROR;
		goto ERROR;
	}

	user_data_json = json_object_get(runtime_data_json, "user-data");
	if (NULL == user_data_json || !json_is_string(user_data_json))
	{
		status = STATUS_JSON_DECODING_ERROR;
		goto ERROR;
	}
	user_data_string = (char*)json_string_value(user_data_json);

	// Convert report data bytes to hex format
	char *report_data_hex = (char *)calloc(1, strlen(user_data_string));
	if (report_data_hex == NULL)
	{
		ERROR("Failed to allocate memory for hex encoded report data");
		status = STATUS_ALLOCATION_ERROR;
		goto ERROR;
	}
	char tmp_hex[3];
	for (int i = 0; i < sizeof(report_data); i++)
	{
		sprintf(tmp_hex, "%02X", report_data[i]);
		strcat(report_data_hex, tmp_hex);
	}

	if (strcmp(user_data_string, report_data_hex) != 0)
	{
		ERROR("User data calculated does not match the same received from TPM");
		status = STATUS_USER_DATA_MISMATCH_ERROR;
		goto ERROR;
	}

	evidence->type = EVIDENCE_TYPE_TDX;

	// Populating Evidence with TDQuote
	evidence->evidence = (uint8_t *)calloc(quote_size, sizeof(uint8_t));
	if (NULL == evidence->evidence)
	{
		status = STATUS_TDX_ERROR_BASE | STATUS_ALLOCATION_ERROR;
		goto ERROR;
	}
	memcpy(evidence->evidence, td_quote, quote_size);
	evidence->evidence_len = quote_size;

	// Populating Evidence with UserData
	evidence->user_data = (uint8_t *)calloc(user_data_len, sizeof(uint8_t));
	if (NULL == evidence->user_data)
	{
		free(evidence->evidence);
		evidence->evidence = NULL;
		status = STATUS_ALLOCATION_ERROR;
		goto ERROR;
	}
	memcpy(evidence->user_data, user_data, user_data_len);
	evidence->user_data_len = user_data_len;

	evidence->runtime_data = (uint8_t *)calloc(runtime_data_len, sizeof(uint8_t));
	if (NULL == evidence->runtime_data)
	{
		free(evidence->user_data);
		free(evidence->evidence);
		evidence->user_data = NULL;
		evidence->evidence = NULL;
		status = STATUS_ALLOCATION_ERROR;
		goto ERROR;
	}
	memcpy(evidence->runtime_data, runtime_data, runtime_data_len);
	evidence->runtime_data_len = runtime_data_len;
	evidence->event_log = NULL;
	evidence->event_log_len = 0;

ERROR:
	if (nonce_data)
	{
		free(nonce_data);
		nonce_data = NULL;
	}

	if (tpm_report)
	{
		free(tpm_report);
		tpm_report = NULL;
	}

	if (td_report)
	{
		free(td_report);
		td_report = NULL;
	}

	if (runtime_data)
	{
		free(runtime_data);
		runtime_data = NULL;
	}

	if (report_data_hex)
	{
		free(report_data_hex);
		report_data_hex = NULL;
	}

	if (td_quote)
	{
		free(td_quote);
		td_quote = NULL;
	}

	if (runtime_data_json)
	{
		free(runtime_data_json);
		runtime_data_json = NULL;
	}

	if (user_data_json)
	{
		free(user_data_json);
		user_data_json = NULL;
	}

	if (user_data_string)
	{
		free(user_data_string);
		user_data_string = NULL;
	}

	return 0;
}

int get_td_report(uint8_t *report_data, uint8_t **tpm_report)
{
	char command[COMMAND_LEN];
	FILE *output;
	ESYS_CONTEXT *esys_context;
	ESYS_TR nvIndex = 0;
	TPM2B_NV_PUBLIC *nvPublic = NULL;
	TPM2B_NAME *nvName = NULL;
	TSS2_TCTI_CONTEXT *tcti = NULL;
	/*Application binary interface version. Set it to NULL and let it be auto-calculated*/
	TSS2_ABI_VERSION *abiVersion = NULL;
	TRUST_AUTHORITY_STATUS status = STATUS_OK;
	uint8_t* report_string;

	/*Initialize to get the ESYS Context*/
	TSS2_RC rval = Esys_Initialize(&esys_context, tcti, abiVersion);
	if (rval != TPM2_RC_SUCCESS)
	{
		ERROR("Failed to set ESYS context. Error:0x%x", rval);
		status = STATUS_TPM_CONTEXT_INIT_ERROR;
		goto ERROR;
	}

	/* Create/Fetch ESAPI Handle from TPM public area of the index */
	rval = Esys_TR_FromTPMPublic(
			esys_context,
			REPORT_DATA_NVINDEX,
			ESYS_TR_NONE,
			ESYS_TR_NONE,
			ESYS_TR_NONE,
			&nvIndex);

	if (rval != TSS2_RC_SUCCESS)
	{
		TPM2B_NV_PUBLIC pub_templ = {
			/* this is counterintuitive, but it tells the TSS2 library to calculate this for us */
			.size = 0,
			/* The things that define what NV index we are creating */
			.nvPublic = {
				/* uses sha256 to identify the tpm object by name */
				.nameAlg = TPM2_ALG_SHA256,
				/* allows the owner password or index password r/w access */
				.attributes = TPMA_NV_OWNERWRITE |
					TPMA_NV_OWNERREAD |
					TPMA_NV_AUTHWRITE |
					TPMA_NV_AUTHREAD,
				/* can hold 64 bytes of data */
				.dataSize = 64,
				/* Create at NV Index  */
				.nvIndex = REPORT_DATA_NVINDEX},
		};

		/* Create the NV Index space */
		rval = Esys_NV_DefineSpace(
				esys_context,
				ESYS_TR_RH_OWNER, /* create an NV index in the owner hierarchy */
				ESYS_TR_PASSWORD, /* auth as the owner with a password, which is empty */
				ESYS_TR_NONE,
				ESYS_TR_NONE,
				NULL,
				&pub_templ,
				&nvIndex);
		if (rval != TSS2_RC_SUCCESS)
		{
			ERROR("Error defining NV space: 0x%x\n", rval);
			status = STATUS_TPM_NV_DEFINE_FAILED_ERROR;
			goto ERROR;
		}

		DEBUG("Created NV Index: 0x%x\n", pub_templ.nvPublic.nvIndex);
	}

	// Get a random number to be appended to the end of file name to make it random
	int rand_num = rand();
	char filename[50];
	// Create file name
	sprintf(filename, "/tmp/report_azure_%d.txt", rand_num);
	FILE *tmpFile = fopen(filename, "w");
	fwrite(report_data, 1, TDX_REPORT_DATA_SIZE, tmpFile);
	fclose(tmpFile);

	/*Write report data to nv Index 0x01400002*/
	char tpm_write_command[100];
	/*TODO Replace file based implementation with stdin buffer or use tpm-tss library*/
	sprintf(tpm_write_command, "tpm2_nvwrite -C o 0x1400002 -i %s", filename);
	output = popen(tpm_write_command, "r");
	if (output == NULL || pclose(output) == -1)
	{
		ERROR("Unable to write to index 0x01400002");
		status = STATUS_TPM_NV_WRITE_FAILED_ERROR;
		goto ERROR;
	}
	remove(filename);

	/*Convert the NVIndex from TPM2_HR_NV_INDEX to ESYS_TR*/
	rval = Esys_TR_FromTPMPublic(
			esys_context,
			TD_REPORT_NVINDEX,
			ESYS_TR_NONE,
			ESYS_TR_NONE,
			ESYS_TR_NONE,
			&nvIndex);
	if (rval != TSS2_RC_SUCCESS)
	{
		ERROR("Error fetching ESAPI handle for index 0x%x: 0x%x\n", TD_REPORT_NVINDEX, rval);
		status = STATUS_TPM_NV_READ_PUBLIC_FAILED_ERROR;
		goto ERROR;
	}

	/*Read the public area of the index to get the data size*/
	rval = Esys_NV_ReadPublic(esys_context, nvIndex, ESYS_TR_NONE, ESYS_TR_NONE, ESYS_TR_NONE, &nvPublic, &nvName);
	if (rval != TPM2_RC_SUCCESS && rval != TSS2_ESYS_RC_BAD_REFERENCE)
	{
		ERROR("Failed to read NVRAM public at index 0x%x (%d). Error:0x%x", nvIndex, nvIndex, rval);
		status = STATUS_TPM_NV_READ_PUBLIC_FAILED_ERROR;
		goto ERROR;
	}
	DEBUG("NV Public Area size: %d\n", nvPublic->nvPublic.dataSize);

	output = popen("tpm2_nvread -C o 0x01400001", "r");
	if (output == NULL)
	{
		ERROR("Unable to read index 0x01400001");
		status = STATUS_TPM_NV_READ_FAILED_ERROR;
		goto ERROR;
	}

	int ch;
	*tpm_report = (uint8_t *)calloc(nvPublic->nvPublic.dataSize, sizeof(uint8_t));
	if (tpm_report == NULL)
	{
		ERROR("Failed to allocate memory for report received from tpm");
		status = STATUS_ALLOCATION_ERROR;
		goto ERROR;
	}

	report_string = (uint8_t*)calloc(1, nvPublic->nvPublic.dataSize);
	if (NULL == report_string)
	{
		ERROR("Failed to allocate memory for report received from tpm");
		status = STATUS_ALLOCATION_ERROR;
		goto ERROR;
	}
	int report_index = 0;
	while ((ch = fgetc(output)) != EOF)
	{
		report_string[report_index] = (uint8_t)ch;
		report_index++;
	}
	pclose(output);
	memcpy(*tpm_report, report_string, nvPublic->nvPublic.dataSize);
	free(report_string);

ERROR:

	if (esys_context) {
		free(esys_context);
	}

	if (nvPublic) {
		free(nvPublic);
	}

	if(nvName) {
		free(nvName);
	}

	return status;
}

int get_td_quote(uint8_t *td_report, uint8_t **td_quote, uint16_t *quote_size)
{
	char *response = NULL;
	char *headers = NULL;
	const char azure_tdquote_url[API_URL_MAX_LEN + 1] = "http://169.254.169.254/acc/tdquote";
	char *json_request;
	retry_config retryConfig = {0};
	char *quote;
	CURLcode status = CURLE_OK;
	char *report_b64;


	size_t output_length = ((TD_REPORT_SIZE + 2) / 3) * 4 + 1;
	report_b64 = (char *)malloc(output_length * sizeof(char));
	if (report_b64 == NULL)
	{
		ERROR("Failed to allocate memory for base64 encoded report");
		status = STATUS_ALLOCATION_ERROR;
		goto ERROR;
	}

	status = base64_encode(td_report, TD_REPORT_SIZE, report_b64, output_length, 0);
	if (status != 0)
	{
		ERROR("Base64 encoding of report data failed");
		goto ERROR;
	}

	quote_request quote_req = { .report = report_b64, .report_len = TD_REPORT_SIZE};

	status = json_marshal_quote_request(&quote_req, &json_request);
	if (STATUS_OK != status) {
		ERROR("Error: Error marshalling quote request to be sent to Azure server");
		goto ERROR;
	}

	retryConfig.retry_max = 0;
	retryConfig.retry_wait_time = 0;

	status = post_request(azure_tdquote_url, NULL, ACCEPT_APPLICATION_JSON, NULL, CONTENT_TYPE_APPLICATION_JSON, json_request, &response, &headers, &retryConfig);
	if (NULL == response || CURLE_OK != status)
	{
		ERROR("Error: POST request to %s failed", azure_tdquote_url);
		status = STATUS_GET_AZURE_TD_QUOTE_ERROR;
		goto ERROR;
	}

	status = json_unmarshal_quote_response(&quote, &response);
	if (STATUS_OK != status) {
		ERROR("Error: Error unmarshalling quote response received from Azure server");
		goto ERROR;
	}

	*quote_size = strlen(quote);
	DEBUG("Quote received: %s", quote);
	DEBUG("Quote size: %d", *quote_size);

	output_length = (*quote_size / 4) * 3; // Estimate the output length
	*td_quote = (uint8_t *)malloc((output_length + 1) * sizeof(uint8_t));
	if (td_quote == NULL)
	{
		ERROR("Failed to allocate memory for TD quote");
		status = STATUS_ALLOCATION_ERROR;
		goto ERROR;
	}

	status = base64_decode(quote, *quote_size, *td_quote, &output_length);
	if (BASE64_SUCCESS != status)
	{
		ERROR("Failed to decode base64 encoded TD quote");
		goto ERROR;
	}
	*quote_size = output_length;

ERROR:

	if (response)
	{
		free(response);
		response = NULL;
	}

	if (headers)
	{
		free(headers);
		headers = NULL;
	}

	if (json_request)
	{
		free(json_request);
		json_request = NULL;
	}

	if (quote)
	{
		free(quote);
		quote = NULL;
	}

	if (report_b64)
	{
		free(report_b64);
		report_b64 = NULL;
	}
	return status;
}

/**
 * Performs marshalling of quote request for Azure quote server
 * @param quote_req  quote_request structure to be marshalled to JSON format
 * @param json data to be marshalled
 * @return return status
 */
int json_marshal_quote_request(quote_request *quote_req,
		char **json)
{
	if (NULL == quote_req)
	{
		return STATUS_INVALID_PARAMETER;
	}

	if (NULL == json)
	{
		return STATUS_INVALID_PARAMETER;
	}

	json_t *jansson_request = json_object();
	int status = STATUS_OK;

	/*Create a JSON request for fetching the TD quote*/
	json_object_set(jansson_request, "report", json_string(quote_req->report));

	*json = json_dumps(jansson_request, JANSSON_ENCODING_FLAGS);
	if (NULL == json)
	{
		return STATUS_JSON_ENCODING_ERROR;
	}

	return status;
}

/**
 * Performs marshalling of quote request to Azure quote server
 * @param quote  quote string to be unmarshalled from json response
 * @param json data to be unmarshalled
 * @return return status
 */
int json_unmarshal_quote_response(char **quote,
		char **json) {

	if (NULL == quote)
	{
		return STATUS_INVALID_PARAMETER;
	}

	if (NULL == json)
	{
		return STATUS_INVALID_PARAMETER;
	}

	json_t *quote_json;
	json_t *tmp_obj;
	json_error_t error;
	int status = STATUS_OK;

	/*Parse quote response JSON*/
	quote_json = json_loads((const char *)*json, 0, &error);
	if (!quote_json)
	{
		status = STATUS_JSON_DECODING_ERROR;
		goto ERROR;
	}

	tmp_obj = json_object_get(quote_json, "quote");
	if (NULL == tmp_obj || !json_is_string(tmp_obj))
	{
		status = STATUS_JSON_DECODING_ERROR;
		goto ERROR;
	}

	char* tmp_string = (char *)json_string_value(tmp_obj);
	size_t quote_size;
	if (json_string_length(tmp_obj) % 4 != 0) {
		quote_size = json_string_length(tmp_obj) + (4 - (json_string_length(tmp_obj) % 4));
	} else {
		quote_size = json_string_length(tmp_obj);
	}

	*quote = (char*)calloc(quote_size, sizeof(char));
	if (quote == NULL)
	{
		ERROR("Failed to allocate memory for quote string");
		status = STATUS_ALLOCATION_ERROR;
		goto ERROR;
	}
	strcat(*quote, tmp_string);

	/*if base64 encoded data is not divisible by 4, add = as padding to make it a valid base64 encoding*/
	for (int i = 0; i < 4 - json_string_length(tmp_obj) % 4; i++)
	{
		strcat(*quote, "=");
	}

ERROR:
	if (quote_json) {
		free(quote_json);
		quote_json = NULL;
	}

	if (tmp_obj) {
		free(tmp_obj);
		tmp_obj = NULL;
	}

	return status;
}
