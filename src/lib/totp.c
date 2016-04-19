#include "totp.h"

/* TOTP calculation is fun.
 *
 * At its core, a TOTP token can be defined via the following:
 * K is predefined as a series of arbitrary bytes forming a secret
 * C = ( TIME() - T0 ) / Ti, given that TIME() returns the current unix epoch
 * HS = HMAC(K,C)
 * D = Truncate(HS)
 *
 * It starts off with HS = HMAC(K,C).
 * We first have K, our authentication key, which is a bytestring typically stored as base32 text
 * The configure script converts this back to bytes as part of generating the header
 * We then have T0 and Ti (epoch and validity interval). These are almost always defined as T0 = 0, Ti = 30
 * We can then derive C, where C = ( TIME() - T0 ) / Ti
 * From HMAC(K,C) we then derive HS, which is the resulting 160-bit message digest.
 * We then compute the HOTP, which takes HS and applies dynamic truncation.
 * Dynamic truncation involves calculating first the offset, defined as O.
 * We calculate O as being the lower four bits of the last byte in HS.
 * Thus, O = HS[DIGEST_LENGTH - 1] & 0x0F
 * From this, we truncate HS to four bytes beginning at byte O, which may be anywhere from 0 to F (15), to obtain P.
 * Thus, P = HS[O+0..3]
 * After truncating, we strip the most significant bit in order to prevent P from being interpreted as signed.
 * Thus, P = P & 0x7FFFFFFF
 * Finally, in order to derive D as a number below 1000000, we modulo P against this.
 * Thus, D = P % 1000000.
 * With D now known, we have our TOTP token.
 *
 * This formula makes the assumption that the desired token length is 6 digits, however this is a fairly safe assumption to make.
 */

uint32_t get_token(time_t time_utc, unsigned char key[], uint32_t ksize) {
	// Get the current epoch time and store it in a reasonable way for sha1 operations
	long epoch = (time(NULL) - TOTP_T0) / TOTP_Ti;
	uint8_t sha1time[8];
	for(int i=8;i--;epoch >>= 8) sha1time[i] = epoch;
	
	// Our hash function is SHA1
	sha1nfo hfunc;
	
	// We first get HMAC(K,C) where K is our secret and C is our message (the time)
	sha1_initHmac(&hfunc, key, ksize);
	sha1_write(&hfunc, (char*)sha1time, 8);
	sha1_resultHmac(&hfunc);
	
	// Now that we have the message digest (HS), we can move on to HOTP.
	// HOTP is HMAC with dynamic truncation. An explanation of dynamic truncation with HMAC is below:
	// Since HS (the result of HMAC(K,C)) is a 160-bit (20-byte) hash, we take lower four bits of the last byte (HS[DIGEST_LENGTH-1] & 0x0F)
	// And use that as the beginning of our truncated bytes. We truncate to four bytes, so this method allows for truncation anywhere in HS
	// While guaranteeing that there will be three bytes to the right of the offset.
	// We first obtain the offset, O.
	uint8_t offset = hfunc.state.b[HASH_LENGTH-1] & 0x0F;
	
	// We then truncate to the four bytes beginning at O, to obtain P.
	uint32_t otp = hfunc.state.b[offset] << 24 | hfunc.state.b[offset + 1] << 16 | hfunc.state.b[offset + 2] << 8 | hfunc.state.b[offset + 3];
	// Then strip the topmost bit to prevent it being handled as a signed integer.
	otp &= 0x7FFFFFFF;
	// To obtain D as something we can display as a six-digit integer, modulo by 1000000
	otp %= (unsigned int)pow(10,TOTP_LEN);
	// Return the result.
	return otp;
}