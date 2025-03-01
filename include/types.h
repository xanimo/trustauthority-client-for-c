/*
 * Copyright (C) 2023 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef __TYPES_H__
#define __TYPES_H__

#include <stdint.h>
#include <stddef.h>

#define SHA512_LEN 64
#define API_KEY_MAX_LEN 256
#define API_URL_MAX_LEN 128
#define MAX_USER_DATA_LEN 1024	  // 1k
#define MAX_EVIDENCE_LEN 8 * 1024 // 8k
#define MAX_ATS_CERT_CHAIN_LEN 10
#define DEFAULT_RETRY_MAX 2;
#define DEFAULT_RETRY_WAIT_TIME 2;
#define COMMAND_LEN 1000
#define TPM_OUTPUT_BUFFER 10000

//Intel Trust Authority supported token signing algorithms
#define RS256 "RS256"
#define PS384 "PS384"

typedef struct token
{
	char *jwt;
} token;

typedef struct response_headers
{
	char *headers;
} response_headers;

typedef struct evidence
{
	uint32_t type;
	uint8_t *evidence;
	uint32_t evidence_len;
	uint8_t *user_data;
	uint32_t user_data_len;
	uint8_t *runtime_data;
	uint32_t runtime_data_len;
	uint8_t *event_log;
	uint32_t event_log_len;
} evidence;

typedef struct nonce
{
	uint8_t *val;
	uint32_t val_len;
	uint8_t *iat;
	uint32_t iat_len;
	uint8_t *signature;
	uint32_t signature_len;
} nonce;

typedef struct quote_request {
	char * report;
	uint32_t report_len;
} quote_request;

typedef struct policies
{
	char **ids;
	uint32_t count;
} policies;

typedef struct retry_config
{
	int retry_wait_time;
	int retry_max;
} retry_config;

typedef struct trust_authority_connector
{
	char api_key[API_KEY_MAX_LEN + 1]; /* character array containing API KEY use to authenticate to Intel trust Authority */
	char api_url[API_URL_MAX_LEN + 1]; /* character array containing URL of Intel Trust Authority */
	retry_config *retries;
} trust_authority_connector;

typedef struct jwks
{
	char *keytype;
	char *n;
	char *e;
	char *alg;
	char *kid;
	char **x5c;
	size_t num_of_x5c;
} jwks;

typedef struct jwk_set {
	jwks **keys;
	size_t key_cnt;
}jwk_set;

// The evidence_adapter defines an abstraction for collecting evidence
// for difference implementations (ex. SGX, TDX, TPM, SPDM, etc.).
#define EVIDENCE_TYPE_SGX 0x53475800 // 'SGX0'
#define EVIDENCE_TYPE_TDX 0x54445800 // 'TDX0'

typedef int (*evidence_callback)(void *ctx,
		evidence *evidence,
		nonce *nonce,
		uint8_t *user_data,
		uint32_t user_data_len);

typedef struct evidence_adapter
{
	void *ctx;
	evidence_callback collect_evidence;
} evidence_adapter;

typedef enum
{
	STATUS_OK = 0x0,
	STATUS_UNKNOWN_ERROR = 0x001,

	STATUS_INPUT_ERROR = 0x100,
	STATUS_NULL_CONNECTOR,
	STATUS_NULL_API_KEY,
	STATUS_INVALID_API_KEY,
	STATUS_NULL_API_URL,
	STATUS_INVALID_API_URL,
	STATUS_NULL_NONCE,
	STATUS_NULL_ADAPTER,
	STATUS_NULL_EVIDENCE,
	STATUS_NULL_VERSION,
	STATUS_NULL_TOKEN,
	STATUS_INVALID_USER_DATA,
	STATUS_NULL_CALLBACK,
	STATUS_NULL_ARGS,
	STATUS_INVALID_TOKEN_SIGNING_ALG,

	STATUS_CERTIFICATES_DECODE_ERROR = 0x200,
	STATUS_CREATE_STORE_ERROR,
	STATUS_CREATE_BIO_ERROR,
	STATUS_DECODE_CERTIFICATE_ERROR,
	STATUS_GET_COMMON_NAME_ERROR,
	STATUS_ADD_CERT_TO_STORE_ERROR,
	STATUS_GENERATE_PUBKEY_ERROR,
	STATUS_FORMAT_PUBKEY_ERROR,
	STATUS_VERIFYING_CERT_CHAIN_ERROR,
	STATUS_VERIFYING_CERT_CHAIN_LEAF_CERT_NOT_FOUND_ERROR,
	STATUS_VERIFYING_CERT_CHAIN_UNKNOWN_ERROR,

	STATUS_TOKEN_DECODE_ERROR = 0x300,
	STATUS_TOKEN_INVALID_ERROR,
	STATUS_TOKEN_KID_NULL_ERROR,
	STATUS_INVALID_KID_ERROR,
	STATUS_KID_NOT_MATCHING_ERROR,

	STATUS_INTERNAL_ERROR = 0x400,
	STATUS_ALLOCATION_ERROR,
	STATUS_INVALID_PARAMETER,
	STATUS_NULL_ADAPTER_CTX,
	STATUS_QUOTE_ERROR,
	STATUS_USER_DATA_MISMATCH_ERROR,

	STATUS_REST_ERROR = 0x500,
	STATUS_GET_VERSION_ERROR,
	STATUS_GET_NONCE_ERROR,
	STATUS_POST_TOKEN_ERROR,
	STATUS_GET_SIGNING_CERT_ERROR,
	STATUS_GET_AZURE_TD_QUOTE_ERROR,

	STATUS_JSON_ERROR = 0x600,
	STATUS_JSON_ENCODING_ERROR,
	STATUS_JSON_DECODING_ERROR,
	STATUS_JSON_VERSION_PARSING_ERROR,
	STATUS_JSON_TOKEN_PARSING_ERROR,
	STATUS_JSON_NONCE_PARSING_ERROR,
	STATUS_JSON_SIGN_CERT_PARSING_ERROR,
	STATUS_JSON_SIGN_CERT_PARSING_KEYS_FIELD_NOT_FOUND_ERROR,
	STATUS_JSON_SIGN_CERT_PARSING_MODULUS_MISSING_ERROR,
	STATUS_JSON_SIGN_CERT_PARSING_EXPONENT_MISSING_ERROR,
	STATUS_JSON_SIGN_CERT_PARSING_KEYS_FIELD_NOT_AN_ARRAY_ERROR,
	STATUS_JSON_SIGN_CERT_PARSING_KEYS_X5C_FIELD_NOT_AN_ARRAY_ERROR,
	STATUS_JSON_SIGN_CERT_PARSING_KEYS_X5C_OBJECT_ERROR,
	STATUS_JSON_SIGN_CERT_UNMARSHALING_ERROR,
	STATUS_JSON_NO_OF_SIGN_CERT_EXCEEDING_ERROR,
	STATUS_JSON_APPRAISAL_REQUEST_PARSING_ERROR,
	STATUS_JSON_INVALID_APPRAISAL_REQUEST_ERROR,
	STATUS_JSON_APPRAISAL_REQUEST_POLICIES_FIELD_NOT_FOUND_ERROR,
	STATUS_JSON_APPRAISAL_REQUEST_POLICIES_IDS_FIELD_NOT_FOUND_ERROR,

	STATUS_TOKEN_VERIFICATION_FAILED_ERROR = 0x700,

	STATUS_TPM_NV_ERROR = 0x800,
	STATUS_TPM_NV_DEFINE_FAILED_ERROR,
	STATUS_TPM_NV_READ_FAILED_ERROR,
	STATUS_TPM_NV_READ_PUBLIC_FAILED_ERROR,
	STATUS_TPM_NV_WRITE_FAILED_ERROR,
	STATUS_TPM_CONTEXT_INIT_ERROR,

	STATUS_MAX
} TRUST_AUTHORITY_STATUS;

enum BASE64STATUS
{
	BASE64_SUCCESS,
	BASE64_INVALID_INPUT,
	BASE64_INVALID_CHAR,
	BASE64_INVALID_OUTPUT_BUFFER_SIZE,
	BASE64_INVALID_PADDING,
	BASE64_DECODE_FAILED
};
#endif
