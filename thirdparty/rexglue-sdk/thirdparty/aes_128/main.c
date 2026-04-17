/*
 *
 * Chinese Academy of Sciences
 * State Key Laboratory of Information Security
 * Institute of Information Engineering
 *
 * Copyright (C) 2016 Chinese Academy of Sciences
 *
 * LuoPeng, luopeng@iie.ac.cn
 * Updated in May 2016
 *
 */

#include <stdio.h>

#include "aes.h"

int main(int argc, char *argv[]) {

	uint8_t i, r;

	/* 128 bit key */
	uint8_t key[] = {
		//0x0f, 0x15, 0x71, 0xc9, 0x47, 0xd9, 0xe8, 0x59, 
		//0x0c, 0xb7, 0xad, 0xd6, 0xaf, 0x7f, 0x67, 0x98,
		0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
		0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 

	};

	uint8_t plaintext[] = {
		//0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
		//0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10,
		0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
		0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,
	};

 
	uint8_t ciphertext[AES_BLOCK_SIZE];

	const uint8_t const_cipher[AES_BLOCK_SIZE] = {
		//0xff, 0x0b, 0x84, 0x4a, 0x08, 0x53, 0xbf, 0x7c,
		//0x69, 0x34, 0xab, 0x43, 0x64, 0x14, 0x8f, 0xb9,
		0x69, 0xc4, 0xe0, 0xd8, 0x6a, 0x7b, 0x04, 0x30,
		0xd8, 0xcd, 0xb7, 0x80, 0x70, 0xb4, 0xc5, 0x5a,
	};
	
	uint8_t roundkeys[AES_ROUND_KEY_SIZE];

	printf("\n--------------------------------------------------------\n");
	printf("Plain text:\n");
	for (i = 0; i < AES_BLOCK_SIZE; i++) {
		printf("%2x ", plaintext[i]);
	}
	printf("\n\n");	

	// key schedule
	aes_key_schedule_128(key, roundkeys);
	printf("Round Keys:\n");
	for ( r = 0; r <= AES_ROUNDS; r++ ) {
		for (i = 0; i < AES_BLOCK_SIZE; i++) {
			printf("%2x ", roundkeys[r*AES_BLOCK_SIZE+i]);
		}
		printf("\n");
	}
	printf("\n");

	// encryption
	aes_encrypt_128(roundkeys, plaintext, ciphertext);
	printf("Cipher text:\n");
	for (i = 0; i < AES_BLOCK_SIZE; i++) {
		printf("%2x ", ciphertext[i]);
	}
	for (i = 0; i < AES_BLOCK_SIZE; i++) {
		if ( ciphertext[i] != const_cipher[i] ) { break; }
	}
	if ( AES_BLOCK_SIZE != i ) { printf("\nENCRYPT WRONG\n\n"); }
	else { printf("\nENCRYPT CORRECT\n\n"); }


	// decryption
	aes_decrypt_128(roundkeys, ciphertext, ciphertext);
	printf("Plain text:\n");
	for (i = 0; i < AES_BLOCK_SIZE; i++) {
		printf("%2x ", ciphertext[i]);
	}
	for (i = 0; i < AES_BLOCK_SIZE; i++) {
		if ( ciphertext[i] != plaintext[i] ) { break; }
	}
	if ( AES_BLOCK_SIZE != i ) { printf("\nDECRYPT WRONG\n\n"); }
	else { printf("\nDECRYPT CORRECT\n\n"); }

	return 0;

}
