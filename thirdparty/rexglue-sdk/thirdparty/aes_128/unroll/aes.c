/*
 * Full unroll implementation of aes-128
 * 
 * Author: LuoPeng (luopengxq@gmail.com)
 * Time:   2017.08.02
 */
#include <stdint.h>
#include <stdio.h>

#include "aes.h"

/*
 * round constants
 */
static uint8_t RC[] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36};

/*
 * Sbox
 */
static uint8_t SBOX[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16};

/*
 * Inverse Sboxs
 */
static uint8_t INV_SBOX[256] = {
    0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38, 0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3, 0xd7, 0xfb,
    0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87, 0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb,
    0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23, 0x3d, 0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e,
    0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2, 0x76, 0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25,
    0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16, 0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92,
    0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda, 0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84,
    0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a, 0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06,
    0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02, 0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b,
    0x3a, 0x91, 0x11, 0x41, 0x4f, 0x67, 0xdc, 0xea, 0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73,
    0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85, 0xe2, 0xf9, 0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e,
    0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89, 0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b,
    0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2, 0x79, 0x20, 0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4,
    0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31, 0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f,
    0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d, 0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef,
    0xa0, 0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0, 0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61,
    0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26, 0xe1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0c, 0x7d};

/**
 * https://en.wikipedia.org/wiki/Finite_field_arithmetic
 * Multiply two numbers in the GF(2^8) finite field defined
 * by the polynomial x^8 + x^4 + x^3 + x + 1 = 0
 * We do use mul2(int8_t a) but not mul(uint8_t a, uint8_t b)
 * just in order to get a higher speed.
 */
static inline uint8_t mul2(uint8_t a) {
    return (a&0x80) ? ((a<<1)^0x1b) : (a<<1);
}

void aes_key_schedule_128(const uint8_t *key, uint8_t *roundkeys) {

    uint8_t temp[4];
    uint8_t *last4bytes; // point to the last 4 bytes of one round
    uint8_t *lastround;
    uint8_t i;

    for (i = 0; i < 16; ++i) {
        *roundkeys++ = *key++;
    }

    last4bytes = roundkeys-4;
    for (i = 0; i < AES_ROUNDS; ++i) {
        // k0-k3 for next round
        temp[3] = SBOX[*last4bytes++];
        temp[0] = SBOX[*last4bytes++];
        temp[1] = SBOX[*last4bytes++];
        temp[2] = SBOX[*last4bytes++];
        temp[0] ^= RC[i];
        lastround = roundkeys-16;
        *roundkeys++ = temp[0] ^ *lastround++;
        *roundkeys++ = temp[1] ^ *lastround++;
        *roundkeys++ = temp[2] ^ *lastround++;
        *roundkeys++ = temp[3] ^ *lastround++;
        // k4-k7 for next round        
        *roundkeys++ = *last4bytes++ ^ *lastround++;
        *roundkeys++ = *last4bytes++ ^ *lastround++;
        *roundkeys++ = *last4bytes++ ^ *lastround++;
        *roundkeys++ = *last4bytes++ ^ *lastround++;
        // k8-k11 for next round
        *roundkeys++ = *last4bytes++ ^ *lastround++;
        *roundkeys++ = *last4bytes++ ^ *lastround++;
        *roundkeys++ = *last4bytes++ ^ *lastround++;
        *roundkeys++ = *last4bytes++ ^ *lastround++;
        // k12-k15 for next round
        *roundkeys++ = *last4bytes++ ^ *lastround++;
        *roundkeys++ = *last4bytes++ ^ *lastround++;
        *roundkeys++ = *last4bytes++ ^ *lastround++;
        *roundkeys++ = *last4bytes++ ^ *lastround++;
    }
}

// encryption

#define sub_bytes(ct, tmp)  \
	*tmp      = SBOX[*ct],      *(tmp+1)  = SBOX[*(ct+1)],  *(tmp+2)  = SBOX[*(ct+2)],  *(tmp+3)  = SBOX[*(ct+3)];  \
	*(tmp+4)  = SBOX[*(ct+4)],  *(tmp+5)  = SBOX[*(ct+5)],  *(tmp+6)  = SBOX[*(ct+6)],  *(tmp+7)  = SBOX[*(ct+7)];  \
	*(tmp+8)  = SBOX[*(ct+8)],  *(tmp+9)  = SBOX[*(ct+9)],  *(tmp+10) = SBOX[*(ct+10)], *(tmp+11) = SBOX[*(ct+11)]; \
	*(tmp+12) = SBOX[*(ct+12)], *(tmp+13) = SBOX[*(ct+13)], *(tmp+14) = SBOX[*(ct+14)], *(tmp+15) = SBOX[*(ct+15)];

#define shift_rows(tmp, t) \
	t = *(tmp+1),  *(tmp+1)  = *(tmp+5),  *(tmp+5)  = *(tmp+9), *(tmp+9)  = *(tmp+13), *(tmp+13) = t; \
	t = *(tmp+2),  *(tmp+2)  = *(tmp+10), *(tmp+10) = t;                                              \
	t = *(tmp+6),  *(tmp+6)  = *(tmp+14), *(tmp+14) = t;                                              \
	t = *(tmp+15), *(tmp+15) = *(tmp+11), *(tmp+11) = *(tmp+7), *(tmp+7)  = *(tmp+3),  *(tmp+3)  = t;

#define mix_columns(ct, tmp, t)                           \
	t      = tmp[0]       ^ tmp[1]   ^ tmp[2]  ^ tmp[3];  \
	ct[0]  = mul2(tmp[0]  ^ tmp[1])  ^ tmp[0]  ^ t;       \
	ct[1]  = mul2(tmp[1]  ^ tmp[2])  ^ tmp[1]  ^ t;       \
	ct[2]  = mul2(tmp[2]  ^ tmp[3])  ^ tmp[2]  ^ t;       \
	ct[3]  = mul2(tmp[3]  ^ tmp[0])  ^ tmp[3]  ^ t;       \
	t      = tmp[4]       ^ tmp[5]   ^ tmp[6]  ^ tmp[7];  \
	ct[4]  = mul2(tmp[4]  ^ tmp[5])  ^ tmp[4]  ^ t;       \
	ct[5]  = mul2(tmp[5]  ^ tmp[6])  ^ tmp[5]  ^ t;       \
	ct[6]  = mul2(tmp[6]  ^ tmp[7])  ^ tmp[6]  ^ t;       \
	ct[7]  = mul2(tmp[7]  ^ tmp[4])  ^ tmp[7]  ^ t;       \
	t      = tmp[8]       ^ tmp[9]   ^ tmp[10] ^ tmp[11]; \
	ct[8]  = mul2(tmp[8]  ^ tmp[9])  ^ tmp[8]  ^ t;       \
	ct[9]  = mul2(tmp[9]  ^ tmp[10]) ^ tmp[9]  ^ t;       \
	ct[10] = mul2(tmp[10] ^ tmp[11]) ^ tmp[10] ^ t;       \
	ct[11] = mul2(tmp[11] ^ tmp[8])  ^ tmp[11] ^ t;       \
	t      = tmp[12]      ^ tmp[13]  ^ tmp[14] ^ tmp[15]; \
	ct[12] = mul2(tmp[12] ^ tmp[13]) ^ tmp[12] ^ t;       \
	ct[13] = mul2(tmp[13] ^ tmp[14]) ^ tmp[13] ^ t;       \
	ct[14] = mul2(tmp[14] ^ tmp[15]) ^ tmp[14] ^ t;       \
	ct[15] = mul2(tmp[15] ^ tmp[12]) ^ tmp[15] ^ t;

#define add_round_key(in, out, rks) \
	*out      = *in      ^ *rks++, *(out+1)  = *(in+1)  ^ *rks++, *(out+2)  = *(in+2)  ^ *rks++, *(out+3)  = *(in+3)  ^ *rks++; \
	*(out+4)  = *(in+4)  ^ *rks++, *(out+5)  = *(in+5)  ^ *rks++, *(out+6)  = *(in+6)  ^ *rks++, *(out+7)  = *(in+7)  ^ *rks++; \
	*(out+8)  = *(in+8)  ^ *rks++, *(out+9)  = *(in+9)  ^ *rks++, *(out+10) = *(in+10) ^ *rks++, *(out+11) = *(in+11) ^ *rks++; \
	*(out+12) = *(in+12) ^ *rks++, *(out+13) = *(in+13) ^ *rks++, *(out+14) = *(in+14) ^ *rks++, *(out+15) = *(in+15) ^ *rks++;

#define enc_one_round(ct, rks, tmp, t) \
    sub_bytes(ct, tmp);                \
    shift_rows(tmp, t);                \
    mix_columns(ct, tmp, t);           \
    add_round_key(ct, ct, rks);

#define enc_last_round(ct, rks, t)     \
    sub_bytes(ct, ct);                 \
    shift_rows(ct, t);                 \
    add_round_key(ct, ct, rks);

void aes_encrypt_128(const uint8_t *roundkeys, const uint8_t *plaintext, uint8_t *ciphertext) {

    uint8_t tmp[16], t;

    add_round_key(plaintext, ciphertext, roundkeys);

	enc_one_round(ciphertext, roundkeys, tmp, t);
	enc_one_round(ciphertext, roundkeys, tmp, t);
	enc_one_round(ciphertext, roundkeys, tmp, t);
	enc_one_round(ciphertext, roundkeys, tmp, t);
	enc_one_round(ciphertext, roundkeys, tmp, t);
	enc_one_round(ciphertext, roundkeys, tmp, t);
	enc_one_round(ciphertext, roundkeys, tmp, t);
	enc_one_round(ciphertext, roundkeys, tmp, t);
	enc_one_round(ciphertext, roundkeys, tmp, t);
    
	enc_last_round(ciphertext, roundkeys, t);

}


// decryption

#define inv_add_round_key(in, out, rks) \
	*out      = *in      ^ *rks,      *(out+1)  = *(in+1)  ^ *(rks+1),  *(out+2)  = *(in+2)  ^ *(rks+2),  *(out+3)  = *(in+3)  ^ *(rks+3);  \
	*(out+4)  = *(in+4)  ^ *(rks+4),  *(out+5)  = *(in+5)  ^ *(rks+5),  *(out+6)  = *(in+6)  ^ *(rks+6),  *(out+7)  = *(in+7)  ^ *(rks+7);  \
	*(out+8)  = *(in+8)  ^ *(rks+8),  *(out+9)  = *(in+9)  ^ *(rks+9),  *(out+10) = *(in+10) ^ *(rks+10), *(out+11) = *(in+11) ^ *(rks+11); \
	*(out+12) = *(in+12) ^ *(rks+12), *(out+13) = *(in+13) ^ *(rks+13), *(out+14) = *(in+14) ^ *(rks+14), *(out+15) = *(in+15) ^ *(rks+15);

#define inv_shift_rows(pt, t) \
    t = *(pt+13); *(pt+13) = *(pt+9); *(pt+9)  = *(pt+5);  *(pt+5)  = *(pt+1);  *(pt+1)  = t; \
    t = *(pt+14); *(pt+14) = *(pt+6); *(pt+6)  = t;                                           \
    t = *(pt+10); *(pt+10) = *(pt+2); *(pt+2)  = t;                                           \
    t = *(pt+3);  *(pt+3)  = *(pt+7); *(pt+7)  = *(pt+11); *(pt+11) = *(pt+15); *(pt+15) = t;

#define inv_sub_bytes(pt)     \
    *pt      = INV_SBOX[*pt],      *(pt+1)  = INV_SBOX[*(pt+1)],  *(pt+2)  = INV_SBOX[*(pt+2)],  *(pt+3)  = INV_SBOX[*(pt+3)];  \
    *(pt+4)  = INV_SBOX[*(pt+4)],  *(pt+5)  = INV_SBOX[*(pt+5)],  *(pt+6)  = INV_SBOX[*(pt+6)],  *(pt+7)  = INV_SBOX[*(pt+7)];  \
    *(pt+8)  = INV_SBOX[*(pt+8)],  *(pt+9)  = INV_SBOX[*(pt+9)],  *(pt+10) = INV_SBOX[*(pt+10)], *(pt+11) = INV_SBOX[*(pt+11)]; \
    *(pt+12) = INV_SBOX[*(pt+12)], *(pt+13) = INV_SBOX[*(pt+13)], *(pt+14) = INV_SBOX[*(pt+14)], *(pt+15) = INV_SBOX[*(pt+15)];

#define inv_mix_columns(pt, tmp, t, u, v) \
    t     =  tmp[0] ^ tmp[1] ^ tmp[2] ^ tmp[3];                                                      \
    pt[0] =  t ^ tmp[0] ^ mul2(tmp[0] ^ tmp[1]), pt[1] = t ^ tmp[1] ^ mul2(tmp[1] ^ tmp[2]);         \
    pt[2] =  t ^ tmp[2] ^ mul2(tmp[2] ^ tmp[3]), pt[3] = t ^ tmp[3] ^ mul2(tmp[3] ^ tmp[0]);         \
    u     =  mul2(mul2(tmp[0] ^ tmp[2])), v = mul2(mul2(tmp[1] ^ tmp[3])), t = mul2(u ^ v);          \
    pt[0] ^= t ^ u, pt[1] ^= t ^ v, pt[2] ^= t ^ u, pt[3] ^= t ^ v;                                  \
    t     =  tmp[4] ^ tmp[5] ^ tmp[6] ^ tmp[7];                                                      \
    pt[4] =  t ^ tmp[4] ^ mul2(tmp[4] ^ tmp[5]), pt[5] = t ^ tmp[5] ^ mul2(tmp[5] ^ tmp[6]);         \
    pt[6] =  t ^ tmp[6] ^ mul2(tmp[6] ^ tmp[7]), pt[7] = t ^ tmp[7] ^ mul2(tmp[7] ^ tmp[4]);         \
    u     =  mul2(mul2(tmp[4] ^ tmp[6])), v = mul2(mul2(tmp[5] ^ tmp[7])), t = mul2(u ^ v);          \
    pt[4] ^= t ^ u, pt[5] ^= t ^ v, pt[6] ^= t ^ u, pt[7] ^= t ^ v;                                  \
    t     =  tmp[8] ^ tmp[9] ^ tmp[10] ^ tmp[11];                                                    \
    pt[8] =  t ^ tmp[8] ^ mul2(tmp[8] ^ tmp[9]), pt[9] = t ^ tmp[9] ^ mul2(tmp[9] ^ tmp[10]);        \
    pt[10] =  t ^ tmp[10] ^ mul2(tmp[10] ^ tmp[11]), pt[11] = t ^ tmp[11] ^ mul2(tmp[11] ^ tmp[8]);  \
    u     =  mul2(mul2(tmp[8] ^ tmp[10])), v = mul2(mul2(tmp[9] ^ tmp[11])), t = mul2(u ^ v);        \
    pt[8] ^= t ^ u, pt[9] ^= t ^ v, pt[10] ^= t ^ u, pt[11] ^= t ^ v;                                \
    t     =  tmp[12] ^ tmp[13] ^ tmp[14] ^ tmp[15];                                                  \
    pt[12] =  t ^ tmp[12] ^ mul2(tmp[12] ^ tmp[13]), pt[13] = t ^ tmp[13] ^ mul2(tmp[13] ^ tmp[14]); \
    pt[14] =  t ^ tmp[14] ^ mul2(tmp[14] ^ tmp[15]), pt[15] = t ^ tmp[15] ^ mul2(tmp[15] ^ tmp[12]); \
    u     =  mul2(mul2(tmp[12] ^ tmp[14])), v = mul2(mul2(tmp[13] ^ tmp[15])), t = mul2(u ^ v);      \
    pt[12] ^= t ^ u, pt[13] ^= t ^ v, pt[14] ^= t ^ u, pt[15] ^= t ^ v;

#define dec_first_round(ct, pt, rks, t) \
    inv_add_round_key(ct, pt, rks);     \
    rks -= 16;                          \
    inv_shift_rows(pt, t);              \
    inv_sub_bytes(pt);

#define dec_one_round(pt, rks, tmp, t, u, v) \
    inv_add_round_key(pt, tmp, rks);         \
    roundkeys -= 16;                         \
    inv_mix_columns(pt, tmp, t, u, v);       \
    inv_shift_rows(pt, t);                   \
    inv_sub_bytes(pt);
    
void aes_decrypt_128(const uint8_t *roundkeys, const uint8_t *ciphertext, uint8_t *plaintext) {

    uint8_t tmp[16];
    uint8_t t, u, v;

    roundkeys += 160;
    dec_first_round(ciphertext, plaintext, roundkeys, t);

    dec_one_round(plaintext, roundkeys, tmp, t, u, v);
    dec_one_round(plaintext, roundkeys, tmp, t, u, v);
    dec_one_round(plaintext, roundkeys, tmp, t, u, v);
    dec_one_round(plaintext, roundkeys, tmp, t, u, v);
    dec_one_round(plaintext, roundkeys, tmp, t, u, v);
    dec_one_round(plaintext, roundkeys, tmp, t, u, v);
    dec_one_round(plaintext, roundkeys, tmp, t, u, v);
    dec_one_round(plaintext, roundkeys, tmp, t, u, v);
    dec_one_round(plaintext, roundkeys, tmp, t, u, v);

    inv_add_round_key(plaintext, plaintext, roundkeys);
    
}