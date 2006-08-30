/*
 * Copyright 2005 SPARTA, Inc.  All rights reserved.
 * See the COPYING file distributed with this software for details.
 *
 * Author: Abhijit Hayatnagarkar
 *
 * This is the implementation for the RSA/MD5 algorithm signature
 * verification
 *
 * See RFC 2537, RFC 3110, RFC 4034 Appendix B.1
 */
#include "validator-config.h"

#include <openssl/bn.h>
#include <openssl/md5.h>
#include <openssl/rsa.h>
#include <openssl/err.h>
#include <openssl/objects.h> /* For NID_md5 */
#include <strings.h>
#include <string.h>

#include <validator.h>
#include <val_log.h>
#include "val_rsamd5.h"

/* Returns VAL_NO_ERROR on success, other values on failure */
static int rsamd5_parse_public_key (const unsigned char *buf,
				    int buflen,
				    RSA *rsa)
{
	int index = 0;
	const u_char *cp;
	u_int16_t exp_len = 0x0000;
	BIGNUM *bn_exp;
	BIGNUM *bn_mod;
	
	if (!rsa) return VAL_INTERNAL_ERROR;
	
	cp = buf;
	
	if ((u_int8_t)(buf[index]) == (u_int8_t) 0) {
		index += 1;
		cp = (buf + index);
		VAL_GET16(exp_len, cp);
		index += 2;
	}
	else {
		exp_len += (u_int8_t)(buf[index]);
		index += 1;
	}
	
	/* Extract the exponent */
	bn_exp = BN_bin2bn(buf + index, exp_len, NULL);
	
	index += exp_len;
	
	/* Extract the modulus */
	bn_mod = BN_bin2bn(buf + index, buflen - index, NULL);
	
	rsa->e = bn_exp;
	rsa->n = bn_mod;
	
	return VAL_NO_ERROR; /* success */
}

/* See RFC 4034, Appendix B.1 :
 *
 * " For a DNSKEY RR with algorithm 1, the key tag is defined to be the most
 *   significant 16 bits of the least significant 24 bits in the public
 *   key modulus (in other words, the 4th to last and 3rd to last octets
 *   of the public key modulus)."
 */
u_int16_t rsamd5_keytag (const unsigned char *pubkey,
			 int pubkey_len)
{
	RSA *rsa = NULL;
	BIGNUM *modulus;
	u_int16_t keytag = 0x0000;
	unsigned char *modulus_bin;
	int modulus_len;
	
	if ((rsa = RSA_new()) == NULL) {
		return VAL_OUT_OF_MEMORY;
	};
	
	if (rsamd5_parse_public_key(pubkey, pubkey_len,
				    rsa) != VAL_NO_ERROR) {
		RSA_free(rsa);
		return VAL_INTERNAL_ERROR;
	}
	
	modulus = rsa->n;
	modulus_len = BN_num_bytes(modulus);
	modulus_bin = (unsigned char *) MALLOC (modulus_len * sizeof(unsigned char));
	
	BN_bn2bin(modulus, modulus_bin);
	
	keytag = ((0x00ff & modulus_bin[modulus_len - 3]) << 8) |
		(0x00ff & modulus_bin[modulus_len - 2]);

	FREE(modulus_bin);
	RSA_free(rsa);
	return keytag;
}

int rsamd5_sigverify (val_context_t *ctx,
			const unsigned char *data,
		      int data_len,
		      const val_dnskey_rdata_t dnskey,
		      const val_rrsig_rdata_t rrsig)
{
	char buf[1028];
	int buflen = 1024;
	RSA *rsa = NULL;
	unsigned char md5_hash[MD5_DIGEST_LENGTH];
	
	val_log(ctx, LOG_DEBUG, "rsamd5_sigverify(): parsing the public key...\n");
	if ((rsa = RSA_new()) == NULL) {
		val_log(ctx, LOG_DEBUG, "rsamd5_sigverify could not allocate rsa structure.\n");
		return VAL_OUT_OF_MEMORY;
	};
	
	if (rsamd5_parse_public_key(dnskey.public_key, dnskey.public_key_len,
				    rsa) != VAL_NO_ERROR) {
		val_log(ctx, LOG_DEBUG, "rsamd5_sigverify(): Error in parsing public key.  Returning INDETERMINATE\n");
		RSA_free(rsa);
		return VAL_INTERNAL_ERROR;
	}
	
	bzero(md5_hash, MD5_DIGEST_LENGTH);
	MD5(data, data_len, (unsigned char *) md5_hash);
	val_log(ctx, LOG_DEBUG, "rsamd5_sigverify(): MD5 hash = %s", 
				get_hex_string(md5_hash, MD5_DIGEST_LENGTH, buf, buflen));
	
	val_log(ctx, LOG_DEBUG, "rsamd5_sigverify(): verifying RSA signature...\n");
	
	if (RSA_verify(NID_md5, (unsigned char *) md5_hash, MD5_DIGEST_LENGTH,
		       rrsig.signature, rrsig.signature_len, rsa)) {
		val_log(ctx, LOG_DEBUG, "RSA_verify returned SUCCESS\n");
		RSA_free(rsa);
		return VAL_A_RRSIG_VERIFIED;
	}
	else {
		val_log(ctx, LOG_DEBUG, "RSA_verify returned FAILURE\n");
		RSA_free(rsa);
		return VAL_A_RRSIG_VERIFY_FAILED;
	}   
}
