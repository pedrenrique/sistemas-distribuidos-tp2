/*
 * criar_teste.c — Cria arquivos de teste com dados aleatórios
 *
 * Uso: ./criar_teste <tamanho_kb> <nome_arquivo>
 *
 * Exemplos:
 *   ./criar_teste 10    arquivo_10kb.bin     # File A base
 *   ./criar_teste 20    arquivo_20kb.bin     # File A variação
 *   ./criar_teste 1024  arquivo_1mb.bin      # File B base
 *   ./criar_teste 5120  arquivo_5mb.bin      # File B variação
 *   ./criar_teste 10240 arquivo_10mb.bin     # File C base
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr,
            "Uso: %s <tamanho_kb> <nome_arquivo>\n\n"
            "Exemplos:\n"
            "  %s 10    arquivo_10kb.bin\n"
            "  %s 1024  arquivo_1mb.bin\n"
            "  %s 10240 arquivo_10mb.bin\n",
            argv[0], argv[0], argv[0], argv[0]);
        return 1;
    }

    int tamanho_kb = atoi(argv[1]);
    const char *nome = argv[2];

    if (tamanho_kb <= 0) {
        fprintf(stderr, "Erro: tamanho deve ser positivo\n");
        return 1;
    }

    FILE *fp = fopen(nome, "wb");
    if (!fp) {
        perror("fopen");
        return 1;
    }

    /* Semente aleatória baseada no tempo para garantir dados únicos */
    srand((unsigned int)time(NULL));

    uint8_t buffer[1024];
    long total_escrito = 0;

    for (int i = 0; i < tamanho_kb; i++) {
        /* Preencher buffer com bytes pseudo-aleatórios */
        for (int j = 0; j < 1024; j++) {
            buffer[j] = (uint8_t)(rand() % 256);
        }
        fwrite(buffer, 1, sizeof(buffer), fp);
        total_escrito += 1024;

        /* Mostrar progresso a cada 10% para arquivos grandes */
        if (tamanho_kb >= 100 && (i + 1) % (tamanho_kb / 10) == 0) {
            printf("  %d%% concluído...\n", (int)(((i + 1) * 100L) / tamanho_kb));
            fflush(stdout);
        }
    }

    fclose(fp);

    printf("Arquivo '%s' criado: %d KB (%ld bytes) de dados aleatórios\n",
           nome, tamanho_kb, total_escrito);
    return 0;
}
