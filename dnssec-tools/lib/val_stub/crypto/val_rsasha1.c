/*
 * Copyright 2005 Sparta, Inc.  All rights reserved.
 * See the COPYING file distributed with this software for details.
 *
 * Author: Abhijit Hayatnagarkar
 *
 * This is the implementation for the RSA/SHA-1 algorithm signature
 * verification
 *
 * See RFC 3110
 */

#include <openssl/bn.h>
#include <openssl/sha.h>
#include <openssl/rsa.h>
#include <openssl/err.h>
#include <openssl/objects.h> /* For NID_sha1 */
#include <strings.h>
#include <string.h>
#include <ctype.h>

#include <val_errors.h>
#include "val_rsasha1.h"

/* Returns VALIDATE_SUCCESS on success, other values on failure */
static int rsasha1_parse_public_key (const unsigned char *buf,
				      int buflen,
				      RSA *rsa)
{
    int index = 0;
    u_char *cp;
    u_int16_t exp_len = 0x0000;
    BIGNUM *bn_exp;
    BIGNUM *bn_mod;

    if (!rsa) return INDETERMINATE;

    cp = (u_char *) buf;

    if ((u_int8_t)(buf[index]) == (u_int8_t) 0) {
	index += 1;
	cp = (u_char *) (buf + index);
	NS_GET16(exp_len, cp);
	index += 2;
    }
    else {
	exp_len += (u_int8_t)(buf[index]);
	index += 1;
    }
    
    printf("\texponent length [2] = %d\n", exp_len);

    /* Extract the exponent */
    bn_exp = BN_bin2bn(buf + index, exp_len, NULL);
    index += exp_len;

    /* Extract the modulus */
    bn_mod = BN_bin2bn(buf + index, buflen - index, NULL);

    rsa->e = bn_exp;
    rsa->n = bn_mod;

    return VALIDATE_SUCCESS; /* success */
}

int rsasha1_sigverify (const unsigned char *data,
		       int data_len,
		       const val_dnskey_rdata_t dnskey,
		       const val_rrsig_rdata_t rrsig)
{
    RSA *rsa = NULL;
    unsigned char sha1_hash[SHA_DIGEST_LENGTH];
    unsigned char *sig_data;
    int i;

    printf("rsasha1_sigverify(): parsing the public key...\n");
    if ((rsa = RSA_new()) == NULL) {
	printf("rsasha1_sigverify could not allocate rsa structure.\n");
	return INDETERMINATE;
    };

    if (rsasha1_parse_public_key(dnskey.public_key, dnskey.public_key_len,
				 rsa) != VALIDATE_SUCCESS) {
	printf("rsasha1_sigverify(): Error in parsing public key.  Returning INDETERMINATE\n");
	RSA_free(rsa);
	return INDETERMINATE;
    }

    printf("rsasha1_sigverify(): computing SHA-1 hash...\n");
    bzero(sha1_hash, SHA_DIGEST_LENGTH);
    SHA1(data, data_len, (unsigned char *) sha1_hash);
    printf("hash = 0x");
    for (i=0; i<SHA_DIGEST_LENGTH; i++) {
	printf("%02x", sha1_hash[i]);
    }
    printf("\n");

    printf("rsasha1_sigverify(): verifying RSA signature...\n");

    if (RSA_verify(NID_sha1, (unsigned char *) sha1_hash, SHA_DIGEST_LENGTH,
		   rrsig.signature, rrsig.signature_len, rsa)) {
	printf("RSA_verify returned SUCCESS\n");
	RSA_free(rsa);
	free(sig_data);
	return VALIDATE_SUCCESS;
    }
    else {
	printf("RSA_verify returned FAILURE\n");
	RSA_free(rsa);
	free(sig_data);
	return INDETERMINATE;
    }   
}
