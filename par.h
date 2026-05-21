/*
 * par.h — Header principal do sistema P2P de transferência de arquivos
 *
 * Disciplina: Sistemas Distribuídos — CEFET-MG
 * Trabalho Prático 2 — 2025/2
 *
 * Cada par atua simultaneamente como Servidor (serve blocos) e
 * Cliente (baixa blocos de vizinhos configurados estaticamente).
 */
#ifndef PAR_H
#define PAR_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <signal.h>
#include <fcntl.h>

/* ================================================================
 * CONSTANTES DE CONFIGURAÇÃO
 * ================================================================ */

#define TAMANHO_BLOCO_PADRAO   1024   /* 1 KB por bloco (padrão) */
#define MAX_VIZINHOS           10     /* máximo de vizinhos por par */
#define MAX_CONEXOES_PENDENTES 20     /* backlog do listen() */
#define TAMANHO_HASH           65     /* 64 hex + '\0' */
#define TIMEOUT_SOCKET         15     /* timeout de recv/send em segundos */
#define INTERVALO_TENTATIVA    2      /* segundos entre rodadas de download */
#define TAMANHO_BUF_MSG        (16 * 1024 * 1024)  /* buffer max por bloco: 16 MB */

/* ================================================================
 * TIPOS DE MENSAGEM DO PROTOCOLO
 * ================================================================
 *
 * Protocolo binário simples:
 *   [1 byte: tipo] [4 bytes: num_bloco BE] [4 bytes: tam_dados BE] [dados...]
 *
 *  MSG_LISTAR_BLOCOS : cliente → servidor  (sem dados)
 *  MSG_RESP_BLOCOS   : servidor → cliente  (bitmap: 1 byte por bloco, 0 ou 1)
 *  MSG_PEDIR_BLOCO   : cliente → servidor  (num_bloco indica qual bloco)
 *  MSG_RESP_BLOCO    : servidor → cliente  (dados do bloco)
 *  MSG_SEM_BLOCO     : servidor → cliente  (não tenho esse bloco)
 */
#define MSG_LISTAR_BLOCOS  0x01
#define MSG_RESP_BLOCOS    0x02
#define MSG_PEDIR_BLOCO    0x03
#define MSG_RESP_BLOCO     0x04
#define MSG_SEM_BLOCO      0x05

/* ================================================================
 * ESTRUTURAS
 * ================================================================ */

/*
 * CabecalhoMsg — cabeçalho fixo de 9 bytes enviado em toda mensagem
 * packed garante que não haja padding de alinhamento
 */
typedef struct __attribute__((packed)) {
    uint8_t  tipo;        /* tipo da mensagem (MSG_*) */
    uint32_t num_bloco;   /* número do bloco (network byte order) */
    uint32_t tam_dados;   /* tamanho dos dados que seguem (network byte order) */
} CabecalhoMsg;

/*
 * MetadadoArquivo — informações sobre o arquivo sendo compartilhado
 * Salvo/carregado de um arquivo texto .meta
 */
typedef struct {
    char nome_arquivo[256];     /* nome original do arquivo */
    long tamanho_total;         /* tamanho em bytes */
    int  tamanho_bloco;         /* tamanho de cada bloco em bytes */
    int  num_blocos;            /* quantidade total de blocos */
    char hash_sha256[TAMANHO_HASH]; /* hash SHA-256 do arquivo completo */
} MetadadoArquivo;

/*
 * Vizinho — endereço de um par vizinho conhecido
 */
typedef struct {
    char ip[64];
    int  porta;
} Vizinho;

/*
 * EstadoPar — estado completo de um par P2P
 * Uma única instância global é usada (estado_global)
 */
typedef struct {
    /* ---- Identificação ---- */
    char ip_proprio[64];
    int  porta_propria;

    /* ---- Caminhos de arquivo ---- */
    char arquivo_origem[512];   /* arquivo a compartilhar (seeder) */
    char arquivo_destino[512];  /* onde salvar o arquivo baixado (leecher) */
    char dir_blocos[512];       /* diretório temporário para os blocos */
    char caminho_meta[512];     /* arquivo .meta com metadados */

    /* ---- Metadados e bitmap ---- */
    MetadadoArquivo meta;
    uint8_t        *bitmap;          /* bitmap[i]=1 → tenho bloco i */
    int             blocos_recebidos;
    int             eh_seeder;       /* 1 → inicia com arquivo completo */

    /* ---- Sincronização ---- */
    pthread_mutex_t mutex_bitmap; /* protege bitmap e blocos_recebidos */
    pthread_mutex_t mutex_log;    /* serializa saída no terminal */

    /* ---- Topologia ---- */
    Vizinho vizinhos[MAX_VIZINHOS];
    int     num_vizinhos;

    /* ---- Estado de execução ---- */
    int download_completo; /* 1 quando todos os blocos foram recebidos */
} EstadoPar;

/*
 * ArgConexao — argumento passado para thread_tratar_conexao
 * Alocado no heap e liberado pela própria thread
 */
typedef struct {
    int                socket_fd;
    struct sockaddr_in endereco_cliente;
    EstadoPar         *estado;
} ArgConexao;

/* ================================================================
 * PROTÓTIPOS DE FUNÇÕES
 * ================================================================ */

/* Logging */
void log_msg(EstadoPar *estado, const char *formato, ...);

/* Metadados */
void criar_metadado(const char *caminho_arq, int tam_bloco,
                    const char *caminho_meta, MetadadoArquivo *meta);
int  carregar_metadado(const char *caminho, MetadadoArquivo *meta);
void salvar_metadado(const char *caminho, const MetadadoArquivo *meta);

/* Blocos e arquivo */
char *caminho_bloco(EstadoPar *estado, int num, char *buf, size_t sz);
void  fragmentar_arquivo(EstadoPar *estado);
int   remontar_arquivo(EstadoPar *estado);
void  escanear_blocos_existentes(EstadoPar *estado);

/* Rede — primitivas */
int enviar_tudo(int fd, const void *buf, size_t tam);
int receber_tudo(int fd, void *buf, size_t tam);
int enviar_msg(int fd, uint8_t tipo, uint32_t num_bloco,
               const void *dados, uint32_t tam_dados);
int receber_cabecalho(int fd, CabecalhoMsg *cab);

/* Servidor */
void *thread_servidor(void *arg);
void *thread_tratar_conexao(void *arg);

/* Cliente */
int   pedir_blocos_vizinho(EstadoPar *estado, const char *ip, int porta);
void *thread_cliente(void *arg);

/* Inicialização */
int  carregar_config(const char *caminho, EstadoPar *estado);
void inicializar_estado(EstadoPar *estado);
void finalizar_estado(EstadoPar *estado);

/* Estado global (definido em par.c) */
extern EstadoPar estado_global;

#endif /* PAR_H */
