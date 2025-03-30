#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#if defined(_MSC_VER)
#define strdup _strdup
#endif

#define PRECISION 16
#define SCALE (1 << PRECISION)
#define RANS_FLUSH_THRESHOLD ((uint64_t)1 << 48) // seuil pour flush

typedef struct {
    uint32_t freq0;
    uint32_t freq1;
} rans_ctx;

//void rans_flush(uint64_t* state, FILE* out) {
//    uint32_t lower = (uint32_t)(*state & 0xFFFFFFFF);
//    fwrite(&lower, sizeof(lower), 1, out);
//    *state >>= 32;
//}

//void rans_encode(const uint8_t* data, size_t length, rans_ctx* ctx, FILE* out) {
//    uint64_t state = 1;
//
//    // Encodage à l'envers
//    for (int i = length - 1; i >= 0; i--) {
//        uint8_t bit = data[i];
//        if (bit == 0) {
//            state = (state / ctx->freq0) * SCALE + (state % ctx->freq0);
//        }
//        else {
//            state = (state / ctx->freq1) * SCALE + (state % ctx->freq1) + ctx->freq0;
//        }
//
//        printf("Après symbole %d (bit=%d) : state = %llu\n", (int)i, bit, (unsigned long long)state);
//        // Flush si dépassement du seuil
//        if (state > RANS_FLUSH_THRESHOLD) {
//            rans_flush(&state, out);
//            printf("---- \n");
//        }
//    }
//
//    // Écrire l'état final sous forme de deux uint32_t pour garantir une récupération fiable
//    uint32_t final_low = (uint32_t)(state & 0xFFFFFFFF);
//    uint32_t final_high = (uint32_t)(state >> 32);
//    fwrite(&final_low, sizeof(final_low), 1, out);
//    fwrite(&final_high, sizeof(final_high), 1, out);
//}
uint64_t rans_refill(FILE* in) {
    uint64_t state = 0;
    uint32_t part;
    fread(&part, sizeof(part), 1, in);
    state = part;
    return state;
}

//void rans_decode(FILE* in, uint8_t* data, size_t length, rans_ctx* ctx) {
//    fseek(in, 0, SEEK_END);
//    long file_size = ftell(in);
//    rewind(in);
//
//    uint8_t* compressed_data = (uint8_t*)malloc(file_size);
//    fread(compressed_data, 1, file_size, in);
//
//    // Lire l'état final complet depuis la fin du flux
//    uint64_t state;
//    memcpy(&state, compressed_data + file_size - sizeof(uint64_t), sizeof(uint64_t));
//
//    size_t pos = file_size - sizeof(uint64_t);
//
//    // Décodage limité à length symboles
//    for (size_t i = 0; i < length; i++) {
//        if (state < SCALE && pos >= sizeof(uint32_t) || (state == 80430)) {
//            pos -= sizeof(uint32_t);
//            uint32_t part;
//            memcpy(&part, compressed_data + pos, sizeof(uint32_t));
//            state = (state << 32) | part;
//            printf("---- \n");
//        }
//
//        uint32_t val = state % SCALE;
//        uint8_t bit = (val >= ctx->freq0) ? 1 : 0;
//        data[i] = bit;
//        uint64_t before = state;
//
//        if (bit == 0) {
//            state = ctx->freq0 * (state / SCALE) + val;
//        }
//        else {
//            state = ctx->freq1 * (state / SCALE) + (val - ctx->freq0);
//        }
//        printf("Décodé bit=%d, état avant = %llu, nouvel état = %llu\n", bit, before, state);
//    }
//
//    free(compressed_data);
//}
void rans_flush(uint64_t* state, FILE* out) {
    uint32_t lower = (uint32_t)(*state & 0xFFFFFFFF);
    fwrite(&lower, sizeof(lower), 1, out);
    *state >>= 32;
}

void rans_encode(const uint8_t* data, size_t length, rans_ctx* ctx, FILE* out) {
    uint64_t state = 1;

    for (int i = length - 1; i >= 0; i--) {
        uint8_t bit = data[i];
        if (bit == 0) {
            state = (state / ctx->freq0) * SCALE + (state % ctx->freq0);
        }
        else {
            state = (state / ctx->freq1) * SCALE + (state % ctx->freq1) + ctx->freq0;
        }
    //    printf("Après symbole %d (bit=%d) : state = %llu\n", (int)i, bit, (unsigned long long)state);
        if (state > RANS_FLUSH_THRESHOLD) {
            rans_flush(&state, out); //printf("---- \n");
        }
    }

    uint32_t final_low = (uint32_t)(state & 0xFFFFFFFF);
    uint32_t final_high = (uint32_t)(state >> 32);
    fwrite(&final_low, sizeof(final_low), 1, out);
    fwrite(&final_high, sizeof(final_high), 1, out);
}

void rans_decode(FILE* in, uint8_t* data, size_t length, rans_ctx* ctx) {
    fseek(in, 0, SEEK_END);
    long file_size = ftell(in);
    rewind(in);

    uint8_t* compressed_data = (uint8_t*)malloc(file_size);
    fread(compressed_data, 1, file_size, in);

    uint32_t final_low, final_high;
    memcpy(&final_low, compressed_data + file_size - 2 * sizeof(uint32_t), sizeof(uint32_t));
    memcpy(&final_high, compressed_data + file_size - sizeof(uint32_t), sizeof(uint32_t));
    uint64_t state = ((uint64_t)final_high << 32) | final_low;

    size_t pos = file_size - 2 * sizeof(uint32_t);

    for (size_t i = 0; i < length; i++) {
        if (state < SCALE && pos >= sizeof(uint32_t)) {
            pos -= sizeof(uint32_t);
            uint32_t part;
            memcpy(&part, compressed_data + pos, sizeof(uint32_t));
            state = (state << 32) | part; 
    //        printf("---- \n");
     //       printf("nouvel état = %llu\n", state);
        }

        uint32_t val = state % SCALE;
        uint8_t bit = (val >= ctx->freq0) ? 1 : 0;
        data[i] = bit;
        uint64_t before = state;
        if (bit == 0) {
            state = ctx->freq0 * (state / SCALE) + val;
        }
        else {
            state = ctx->freq1 * (state / SCALE) + (val - ctx->freq0);
        }//printf("Décodé bit=%d, état avant = %llu, nouvel état = %llu\n", bit, before, state);
    }

    free(compressed_data);
}

int main() {
    int i = 1;
    char inputFileName[100], compFileName[100], decompFileName[100];

//    while (1) {
        for (int iname = 0; iname <= 4; iname++) {

            snprintf(inputFileName, sizeof(inputFileName), "C:/Users/donde/source/test/wwwwwX.txt");
            snprintf(compFileName, sizeof(inputFileName), "C:/Users/donde/source/test/wwwwwX.enc");
            snprintf(decompFileName, sizeof(inputFileName), "C:/Users/donde/source/test/wwwwwX.dec");

//        sprintf(inputFileName, "C:/Users/donde/source/test/random/%dbook.txt", i);
//        snprintf(inputFileName, sizeof(inputFileName), "C:/Users/donde/source/test/random/alice%d.txt", 29/*numberoffile*/);
        //snprintf(inputFileName, sizeof(inputFileName), "C:/Users/donde/source/test/random/plrabn%d.txt", 12/*numberoffile*/);
//        snprintf(inputFileName, sizeof(inputFileName), "C:/Users/donde/source/test/random/lcet%d.txt", 10/*numberoffile*/);
//        snprintf(inputFileName, sizeof(inputFileName), "C:/Users/donde/source/test/random/asyoulik.txt"/*numberoffile*/);
       //         snprintf(inputFileName, sizeof(inputFileName), "C:/Users/donde/source/test/random/ptt5.txt");        
  //      snprintf(inputFileName, sizeof(inputFileName), "C:/Users/donde/source/test/random/fields.c");        

// Lecture du fichier
            FILE* f = fopen(inputFileName, "rb");
            printf("yo");
            if (!f) {
                perror("Erreur ouverture fichier");
                return 1;
            }
            printf("yo");

            fseek(f, 0, SEEK_END);
            size_t file_size = ftell(f);
            printf("file_size  %d ", file_size);
            rewind(f);

            uint8_t* file_data = (uint8_t*)malloc(file_size * sizeof(uint8_t));
            fread(file_data, 1, file_size, f);
            fclose(f);
            
/*            uint8_t file_data[128];
            size_t file_size = 128;
            srand(42);
            for (int i = 0; i < 128; i++) {
                file_data[i] = (rand() % 100 < 10) ? 1 : 0; // 10% de 1, 90% de 0
            }*/
//            uint8_t file_data[8] = { 0, 1, 0, 0, 1, 0, 1, 1 };
  //          size_t file_size = 8;
        // Vérifier que ce sont des données binaires (0 ou 1)
        for (size_t i = 0; i < file_size; i++) {
            if (file_data[i] != 0 && file_data[i] != 1) {
                fprintf(stderr, "Erreur : Le fichier doit contenir uniquement des 0 ou des 1.\n");
                free(file_data);
                return 1;
            }
        }

        // Compter proba et créer contexte
        size_t count1 = 0;
        for (size_t i = 0; i < file_size; i++) {
            if (file_data[i] == 1) count1++;
            else if (file_data[i] != 0) {
                fprintf(stderr, "Fichier non binaire.");
                return 1;
            }
        }
        double prob1 = (double)count1 / file_size;
        rans_ctx ctx;
        ctx.freq0 = (uint32_t)((1.0 - prob1) * SCALE);
        ctx.freq1 = SCALE - ctx.freq0;

        // Encodage avec flush
        FILE* fout = fopen(compFileName, "wb");
        rans_encode(file_data, file_size, &ctx, fout);
        fclose(fout);

        // Décodage pour vérif
        fout = fopen(compFileName, "rb");
        if (!fout) {
            perror("Erreur ouverture fichier de décompression");
            // ... gestion de l'erreur ...
            return 0; // Ou une autre action appropriée
        }
        uint8_t* decoded = (uint8_t*)malloc(file_size);
        uint8_t* decoded_reversed = (uint8_t*)malloc(file_size);
        rans_decode(fout, decoded, file_size, &ctx);
        fclose(fout);

        printf("----------------");

        //return
        for (size_t i = 0; i < file_size; i++) {
            decoded_reversed[file_size - 1 - i] = decoded[i];
        }


        int ok = (memcmp(file_data, decoded_reversed, file_size) == 0);
        printf("Vérification : %s\n", ok ? "OK" : "ECHEC");

//        free(file_data);
  //      free(decoded);
        if (i++ >= 1)
            break;
    }
    if (i == 0)
        printf("Aucun fichier à compresser/décompresser.\n");
    return 0;
}
