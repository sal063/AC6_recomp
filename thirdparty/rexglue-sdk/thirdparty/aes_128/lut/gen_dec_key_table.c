
#include <stdint.h>
#include <stdio.h>

static uint8_t Ie[256];
static uint8_t I9[256];
static uint8_t Ib[256];
static uint8_t Id[256];

static const uint8_t CONX = 0X1B;

// tables

static uint32_t IK0[256];
static uint32_t IK1[256];
static uint32_t IK2[256];
static uint32_t IK3[256];

void generate_mix();
void generate_tables();
void print_tables();

int main () {

    generate_mix();
    generate_tables();
    print_tables();

    return 0;
}


void generate_mix() {
    int i;
    uint8_t base, base2, base4, base8;
    for ( i = 0, base = 0; i < 256; ++i, ++base) {
        base2 = (base&0x80)  ? ((base<<1)^CONX)  : (base<<1);
        base4 = (base2&0x80) ? ((base2<<1)^CONX) : (base2<<1);
        base8 = (base4&0x80) ? ((base4<<1)^CONX) : (base4<<1);

        I9[i] = base8 ^ base;
        Ib[i] = I9[i] ^ base2;
        Id[i] = I9[i] ^ base4;
        Ie[i] = Id[i] ^ base2 ^ base;
    }
}

void print_tables() {

    int i;
    printf("IK0:\n");
    for ( i = 0; i < 256; ++i ) {
        printf("0x%8x, ", IK0[i]);
        if ( i % 4 == 3 ) {
            printf("\n");
        }
    }
    printf("\n\n");

    printf("IK1:\n");
    for ( i = 0; i < 256; ++i ) {
        printf("0x%8x, ", IK1[i]);
        if ( i % 4 == 3 ) {
            printf("\n");
        }
    }
    printf("\n\n");

    printf("IK2:\n");
    for ( i = 0; i < 256; ++i) {
        printf("0x%8x, ", IK2[i]);
        if ( i % 4 == 3 ) {
            printf("\n");
        }
    }
    printf("\n\n");
   
    printf("IK3:\n");
    for ( i = 0; i < 256; ++i ) {
        printf("0x%8x, ", IK3[i]);
        if ( i % 4 == 3 ) {
            printf("\n");
        }
    }
      
}

void generate_tables() {
      
    int i;
    for (i = 0; i < 256; ++i ) {
        IK0[i] = ((uint32_t)Ib[i]<<24) ^ ((uint32_t)Id[i]<<16)
                ^ ((uint32_t)I9[i]<<8) ^ ((uint32_t)Ie[i]);
        IK1[i] = ((uint32_t)Id[i]<<24)  ^ ((uint32_t)I9[i]<<16)
                ^ ((uint32_t)Ie[i]<<8)^ ((uint32_t)Ib[i]);
        IK2[i] = ((uint32_t)I9[i]<<24)  ^ ((uint32_t)Ie[i]<<16)
                ^ ((uint32_t)Ib[i]<<8)^ ((uint32_t)Id[i]);
        IK3[i] = ((uint32_t)Ie[i]<<24) ^ ((uint32_t)Ib[i]<<16)
                ^ ((uint32_t)Id[i]<<8) ^ ((uint32_t)I9[i]);
    }

}
