/*
 /*
 * par.c — Sistema P2P de Transferência de Arquivos
 * Sistemas Distribuídos — CEFET-MG — 2025/2
 */

#include "par.h"
#include "sha256.h"

/* Estado global único do par */
EstadoPar estado_global;

/* LOGGING */

/*
 * log_msg: imprime mensagem formatada com timestamp e identificação do par
 * Thread-safe: usa mutex_log
 */
void log_msg(EstadoPar *estado, const char *formato, ...) {
    pthread_mutex_lock(&estado->mutex_log);

    /* Timestamp HH:MM:SS */
    time_t agora = time(NULL);
    struct tm *ti = localtime(&agora);
    char hora[16];
    strftime(hora, sizeof(hora), "%H:%M:%S", ti);

    printf("[%s][%s:%d] ", hora, estado->ip_proprio, estado->porta_propria);

    va_list args;
    va_start(args, formato);
    vprintf(formato, args);
    va_end(args);

    printf("\n");
    fflush(stdout);

    pthread_mutex_unlock(&estado->mutex_log);
}

/* METADADOS */

/*
 * criar_metadado: gera o arquivo .meta a partir de um arquivo existente
 * Calcula número de blocos, tamanho e SHA-256
 */
void criar_metadado(const char *caminho_arq, int tam_bloco,
                    const char *caminho_meta, MetadadoArquivo *meta) {
    /* Nome do arquivo */
    const char *barra = strrchr(caminho_arq, '/');
    strncpy(meta->nome_arquivo, barra ? barra + 1 : caminho_arq, 255);
    meta->nome_arquivo[255] = '\0';

    /* Tamanho total em bytes */
    FILE *fp = fopen(caminho_arq, "rb");
    if (!fp) { perror("criar_metadado: fopen"); return; }
    fseek(fp, 0, SEEK_END);
    meta->tamanho_total = ftell(fp);
    fclose(fp);

    /* Configurações de fragmentação */
    meta->tamanho_bloco = tam_bloco;
    meta->num_blocos    = (int)((meta->tamanho_total + tam_bloco - 1) / tam_bloco);

    /* Hash SHA-256 do arquivo original */
    sha256_arquivo(caminho_arq, meta->hash_sha256);

    /* Persistir metadados */
    salvar_metadado(caminho_meta, meta);
}

/*
 * salvar_metadado: escreve metadados em formato texto legível
 */
void salvar_metadado(const char *caminho, const MetadadoArquivo *meta) {
    FILE *fp = fopen(caminho, "w");
    if (!fp) { perror("salvar_metadado: fopen"); return; }

    fprintf(fp, "nome=%s\n",         meta->nome_arquivo);
    fprintf(fp, "tamanho=%ld\n",     meta->tamanho_total);
    fprintf(fp, "tamanho_bloco=%d\n", meta->tamanho_bloco);
    fprintf(fp, "num_blocos=%d\n",   meta->num_blocos);
    fprintf(fp, "sha256=%s\n",       meta->hash_sha256);

    fclose(fp);
}

/*
 * carregar_metadado: lê arquivo .meta e preenche a estrutura
 * Retorna 1 em sucesso, 0 em falha
 */
int carregar_metadado(const char *caminho, MetadadoArquivo *meta) {
    FILE *fp = fopen(caminho, "r");
    if (!fp) return 0;

    char linha[512];
    while (fgets(linha, sizeof(linha), fp)) {
        linha[strcspn(linha, "\r\n")] = '\0';
        if (linha[0] == '#' || linha[0] == '\0') continue;

        char chave[64], valor[448];
        if (sscanf(linha, "%63[^=]=%447s", chave, valor) != 2) continue;

        if      (strcmp(chave, "nome")          == 0)
            strncpy(meta->nome_arquivo, valor, 255);
        else if (strcmp(chave, "tamanho")        == 0)
            meta->tamanho_total = atol(valor);
        else if (strcmp(chave, "tamanho_bloco")  == 0)
            meta->tamanho_bloco = atoi(valor);
        else if (strcmp(chave, "num_blocos")     == 0)
            meta->num_blocos = atoi(valor);
        else if (strcmp(chave, "sha256")         == 0) {
            strncpy(meta->hash_sha256, valor, 64);
            meta->hash_sha256[64] = '\0';
        }
    }
    fclose(fp);
    return (meta->num_blocos > 0 && meta->tamanho_total > 0);
}

/* MANIPULAÇÃO DE BLOCOS E ARQUIVO */

/*
 * caminho_bloco: monta o caminho do arquivo de bloco número 
 * Exemplo: ./blocos_seeder/bloco_00042.bin
 */
char *caminho_bloco(EstadoPar *estado, int num, char *buf, size_t sz) {
    snprintf(buf, sz, "%s/bloco_%05d.bin", estado->dir_blocos, num);
    return buf;
}

/*
 * fragmentar_arquivo: divide arquivo_origem em blocos no dir_blocos
 * Usado somente pelo seeder inicial
 */
void fragmentar_arquivo(EstadoPar *estado) {
    FILE *fp_orig = fopen(estado->arquivo_origem, "rb");
    if (!fp_orig) {
        perror("fragmentar_arquivo: fopen");
        return;
    }

    /* Criar diretório de blocos */
    mkdir(estado->dir_blocos, 0755);

    uint8_t *buf = malloc(estado->meta.tamanho_bloco);
    if (!buf) { perror("malloc"); fclose(fp_orig); return; }

    int i;
    for (i = 0; i < estado->meta.num_blocos; i++) {
        char caminho[512];
        caminho_bloco(estado, i, caminho, sizeof(caminho));

        size_t lido = fread(buf, 1, estado->meta.tamanho_bloco, fp_orig);
        if (lido == 0) break;

        FILE *fp_bloco = fopen(caminho, "wb");
        if (!fp_bloco) {
            perror("fragmentar_arquivo: fopen bloco");
            continue;
        }
        fwrite(buf, 1, lido, fp_bloco);
        fclose(fp_bloco);

        /* Seeder começa com todos os blocos */
        estado->bitmap[i] = 1;
        estado->blocos_recebidos++;
    }

    free(buf);
    fclose(fp_orig);

    log_msg(estado, "Arquivo fragmentado: %d blocos em '%s'",
            estado->meta.num_blocos, estado->dir_blocos);
}

/*
 * escanear_blocos_existentes: verifica quais blocos já existem no disco
 * Usado quando um leecher reinicia sem ter terminado o download
 */
void escanear_blocos_existentes(EstadoPar *estado) {
    int encontrados = 0;
    for (int i = 0; i < estado->meta.num_blocos; i++) {
        char caminho[512];
        caminho_bloco(estado, i, caminho, sizeof(caminho));
        if (access(caminho, F_OK) == 0) {
            estado->bitmap[i] = 1;
            estado->blocos_recebidos++;
            encontrados++;
        }
    }
    if (encontrados > 0) {
        log_msg(estado, "Retomada: %d/%d blocos já presentes no disco",
                encontrados, estado->meta.num_blocos);
    }
}

/*
 * remontar_arquivo: une todos os blocos em arquivo_destino e verifica hash
 * Retorna 1 se o arquivo foi remontado e a integridade confirmada, 0 caso contrário
 */
int remontar_arquivo(EstadoPar *estado) {
    /* Verificar se todos os blocos estão disponíveis */
    for (int i = 0; i < estado->meta.num_blocos; i++) {
        if (!estado->bitmap[i]) {
            log_msg(estado, "ERRO remontagem: bloco %d ausente", i);
            return 0;
        }
    }

    FILE *fp_dest = fopen(estado->arquivo_destino, "wb");
    if (!fp_dest) {
        perror("remontar_arquivo: fopen destino");
        return 0;
    }

    uint8_t *buf = malloc(estado->meta.tamanho_bloco);
    if (!buf) { perror("malloc"); fclose(fp_dest); return 0; }

    for (int i = 0; i < estado->meta.num_blocos; i++) {
        char caminho[512];
        caminho_bloco(estado, i, caminho, sizeof(caminho));

        FILE *fp_bloco = fopen(caminho, "rb");
        if (!fp_bloco) {
            log_msg(estado, "ERRO remontagem: não abriu bloco %d", i);
            free(buf);
            fclose(fp_dest);
            return 0;
        }

        size_t lido = fread(buf, 1, estado->meta.tamanho_bloco, fp_bloco);
        fwrite(buf, 1, lido, fp_dest);
        fclose(fp_bloco);
    }

    free(buf);
    fclose(fp_dest);

    log_msg(estado, "Arquivo remontado em '%s'", estado->arquivo_destino);

    /* Verificar integridade pelo SHA-256 */
    char hash_obtido[TAMANHO_HASH];
    sha256_arquivo(estado->arquivo_destino, hash_obtido);

    if (strcmp(hash_obtido, estado->meta.hash_sha256) == 0) {
        log_msg(estado, "Integridade OK — SHA-256: %s", hash_obtido);
        return 1;
    } else {
        log_msg(estado, "FALHA de integridade!");
        log_msg(estado, "  Esperado: %s", estado->meta.hash_sha256);
        log_msg(estado, "  Obtido  : %s", hash_obtido);
        return 0;
    }
}

/* REDE PRIMITIVAS DE ENVIO E RECEPÇÃO */

/*
 * enviar_tudo: garante que todos os bytes sejam enviados
 */
int enviar_tudo(int fd, const void *buf, size_t tam) {
    size_t enviado = 0;
    const uint8_t *ptr = (const uint8_t *)buf;
    while (enviado < tam) {
        ssize_t n = send(fd, ptr + enviado, tam - enviado, MSG_NOSIGNAL);
        if (n <= 0) return -1;
        enviado += (size_t)n;
    }
    return 0;
}

/*
 * receber_tudo: garante que todos os 'tam' bytes sejam recebidos
 * Trata retornos parciais de recv()
 */
int receber_tudo(int fd, void *buf, size_t tam) {
    size_t recebido = 0;
    uint8_t *ptr = (uint8_t *)buf;
    while (recebido < tam) {
        ssize_t n = recv(fd, ptr + recebido, tam - recebido, 0);
        if (n <= 0) return -1;
        recebido += (size_t)n;
    }
    return 0;
}

/*
 * enviar_msg: serializa e envia cabeçalho + dados opcionais
 */
int enviar_msg(int fd, uint8_t tipo, uint32_t num_bloco,
               const void *dados, uint32_t tam_dados) {
    CabecalhoMsg cab;
    cab.tipo      = tipo;
    cab.num_bloco = htonl(num_bloco);
    cab.tam_dados = htonl(tam_dados);

    if (enviar_tudo(fd, &cab, sizeof(cab)) < 0) return -1;
    if (dados && tam_dados > 0) {
        if (enviar_tudo(fd, dados, tam_dados) < 0) return -1;
    }
    return 0;
}

/*
 * receber_cabecalho: recebe 9 bytes e converte de network byte order
 */
int receber_cabecalho(int fd, CabecalhoMsg *cab) {
    if (receber_tudo(fd, cab, sizeof(CabecalhoMsg)) < 0) return -1;
    cab->num_bloco = ntohl(cab->num_bloco);
    cab->tam_dados = ntohl(cab->tam_dados);
    return 0;
}

/* SERVIDOR — TRATAR CONEXÃO */

/*
 * thread_tratar_conexao: processa todas as requisições de um cliente conectado
 * Roda em thread separada, o argumento é liberado aqui
 */
void *thread_tratar_conexao(void *arg) {
    ArgConexao *conn   = (ArgConexao *)arg;
    EstadoPar  *estado = conn->estado;
    int fd             = conn->socket_fd;

    /* IP do cliente para logging */
    char ip_cliente[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &conn->endereco_cliente.sin_addr,
              ip_cliente, sizeof(ip_cliente));
    free(conn);

    log_msg(estado, "Servidor: conexão de %s", ip_cliente);

    CabecalhoMsg cab;
    /* Loop: mantém a conexão aberta para múltiplas requisições */
    while (receber_cabecalho(fd, &cab) == 0) {

        if (cab.tipo == MSG_LISTAR_BLOCOS) {
            /* Enviar bitmap de blocos disponíveis */
            pthread_mutex_lock(&estado->mutex_bitmap);
            int n = estado->meta.num_blocos;
            /* Cópia para não manter o mutex durante o envio */
            uint8_t *copia = malloc(n);
            memcpy(copia, estado->bitmap, n);
            pthread_mutex_unlock(&estado->mutex_bitmap);

            enviar_msg(fd, MSG_RESP_BLOCOS, 0, copia, (uint32_t)n);
            free(copia);

        } else if (cab.tipo == MSG_PEDIR_BLOCO) {
            /* Enviar bloco específico */
            int num = (int)cab.num_bloco;

            /* Verificar validade e disponibilidade */
            pthread_mutex_lock(&estado->mutex_bitmap);
            int tenho = (num >= 0 && num < estado->meta.num_blocos)
                        ? estado->bitmap[num] : 0;
            pthread_mutex_unlock(&estado->mutex_bitmap);

            if (!tenho) {
                enviar_msg(fd, MSG_SEM_BLOCO, num, NULL, 0);
                continue;
            }

            /* Ler bloco do disco */
            char caminho[512];
            caminho_bloco(estado, num, caminho, sizeof(caminho));

            FILE *fp = fopen(caminho, "rb");
            if (!fp) {
                enviar_msg(fd, MSG_SEM_BLOCO, num, NULL, 0);
                continue;
            }

            fseek(fp, 0, SEEK_END);
            long tam = ftell(fp);
            rewind(fp);

            uint8_t *dados = malloc(tam);
            size_t lido_bloco = fread(dados, 1, (size_t)tam, fp);
            (void)lido_bloco;  /* tamanho verificado via fseek */
            fclose(fp);

            enviar_msg(fd, MSG_RESP_BLOCO, (uint32_t)num, dados, (uint32_t)tam);
            free(dados);

            log_msg(estado, "Servidor: bloco %d enviado para %s", num, ip_cliente);

        } else {
            /* Tipo desconhecido, encerrar conexão */
            break;
        }
    }

    close(fd);
    log_msg(estado, "Servidor: conexão com %s encerrada", ip_cliente);
    return NULL;
}

/* thread_servidor */
void *thread_servidor(void *arg) {
    EstadoPar *estado = (EstadoPar *)arg;

    /* Criar socket TCP */
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); return NULL; }

    /* Permitir reutilização de porta */
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    /* Bind */
    struct sockaddr_in end;
    memset(&end, 0, sizeof(end));
    end.sin_family      = AF_INET;
    end.sin_addr.s_addr = INADDR_ANY;
    end.sin_port        = htons((uint16_t)estado->porta_propria);

    if (bind(server_fd, (struct sockaddr *)&end, sizeof(end)) < 0) {
        perror("bind"); close(server_fd); return NULL;
    }

    if (listen(server_fd, MAX_CONEXOES_PENDENTES) < 0) {
        perror("listen"); close(server_fd); return NULL;
    }

    log_msg(estado, "Servidor ouvindo na porta %d", estado->porta_propria);

    while (1) {
        struct sockaddr_in end_cliente;
        socklen_t tam_end = sizeof(end_cliente);

        int cliente_fd = accept(server_fd,
                                (struct sockaddr *)&end_cliente, &tam_end);
        if (cliente_fd < 0) {
            if (errno == EINTR) continue; /* interrompido por sinal */
            perror("accept");
            continue;
        }

        /* Timeout de socket para o cliente */
        struct timeval tv = { TIMEOUT_SOCKET, 0 };
        setsockopt(cliente_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(cliente_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        /* Preparar argumento e disparar thread detached */
        ArgConexao *conn = malloc(sizeof(ArgConexao));
        conn->socket_fd        = cliente_fd;
        conn->endereco_cliente = end_cliente;
        conn->estado           = estado;

        pthread_t tid;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        pthread_create(&tid, &attr, thread_tratar_conexao, conn);
        pthread_attr_destroy(&attr);
    }

    close(server_fd);
    return NULL;
}

/* CLIENTE — BAIXAR BLOCOS */

/*
 * pedir_blocos_vizinho — conecta ao vizinho (ip:porta) e baixa blocos faltantes
 * Retorna quantidade de blocos baixados nesta sessão
 */
int pedir_blocos_vizinho(EstadoPar *estado, const char *ip, int porta) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 0;

    /* Timeout de socket */
    struct timeval tv = { TIMEOUT_SOCKET, 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in end_srv;
    memset(&end_srv, 0, sizeof(end_srv));
    end_srv.sin_family = AF_INET;
    end_srv.sin_port   = htons((uint16_t)porta);
    if (inet_pton(AF_INET, ip, &end_srv.sin_addr) <= 0) {
        close(fd); return 0;
    }

    if (connect(fd, (struct sockaddr *)&end_srv, sizeof(end_srv)) < 0) {
        /* Vizinho pode não estar online ainda  */
        close(fd); return 0;
    }

    log_msg(estado, "Cliente: conectado ao vizinho %s:%d", ip, porta);

    /* Passo 1: solicitar bitmap do vizinho */
    if (enviar_msg(fd, MSG_LISTAR_BLOCOS, 0, NULL, 0) < 0) {
        close(fd); return 0;
    }

    CabecalhoMsg cab;
    if (receber_cabecalho(fd, &cab) < 0 || cab.tipo != MSG_RESP_BLOCOS) {
        close(fd); return 0;
    }

    int n = (int)cab.tam_dados;
    if (n <= 0 || n > 10000000) { close(fd); return 0; }

    uint8_t *bitmap_vizinho = malloc(n);
    if (!bitmap_vizinho) { close(fd); return 0; }

    if (receber_tudo(fd, bitmap_vizinho, (size_t)n) < 0) {
        free(bitmap_vizinho); close(fd); return 0;
    }

    /* Passo 2: solicitar cada bloco que o vizinho tem e eu não tenho */
    int baixados = 0;
    for (int i = 0; i < estado->meta.num_blocos && i < n; i++) {

        /* Checar se preciso deste bloco (com lock mínimo) */
        pthread_mutex_lock(&estado->mutex_bitmap);
        int preciso = !estado->bitmap[i];
        pthread_mutex_unlock(&estado->mutex_bitmap);

        if (!preciso || !bitmap_vizinho[i]) continue;

        /* Solicitar bloco i */
        if (enviar_msg(fd, MSG_PEDIR_BLOCO, (uint32_t)i, NULL, 0) < 0) break;

        CabecalhoMsg resp;
        if (receber_cabecalho(fd, &resp) < 0) break;

        if (resp.tipo == MSG_SEM_BLOCO) continue;

        if (resp.tipo != MSG_RESP_BLOCO || resp.tam_dados == 0) break;

        /* Receber dados do bloco */
        if (resp.tam_dados > TAMANHO_BUF_MSG) break; 

        uint8_t *dados = malloc(resp.tam_dados);
        if (!dados) break;

        if (receber_tudo(fd, dados, resp.tam_dados) < 0) {
            free(dados); break;
        }

        /* Salvar bloco em disco */
        char caminho[512];
        caminho_bloco(estado, i, caminho, sizeof(caminho));
        mkdir(estado->dir_blocos, 0755);

        FILE *fp = fopen(caminho, "wb");
        if (fp) {
            fwrite(dados, 1, resp.tam_dados, fp);
            fclose(fp);

            /* Atualizar bitmap com lock */
            pthread_mutex_lock(&estado->mutex_bitmap);
            if (!estado->bitmap[i]) {
                estado->bitmap[i] = 1;
                estado->blocos_recebidos++;
                baixados++;
                log_msg(estado, "Cliente: bloco %d/%d recebido de %s:%d",
                        estado->blocos_recebidos,
                        estado->meta.num_blocos, ip, porta);
            }
            pthread_mutex_unlock(&estado->mutex_bitmap);
        }
        free(dados);
    }

    free(bitmap_vizinho);
    close(fd);

    if (baixados > 0) {
        log_msg(estado, "Cliente: %d blocos obtidos de %s:%d nesta sessão",
                baixados, ip, porta);
    }
    return baixados;
}

/*
 * thread_cliente — gerencia o ciclo de download
 * Itera sobre vizinhos até ter todos os blocos, então remonta o arquivo
 */
void *thread_cliente(void *arg) {
    EstadoPar *estado = (EstadoPar *)arg;

    if (estado->eh_seeder) {
        log_msg(estado, "Modo seeder: nenhum download necessário");
        return NULL;
    }

    log_msg(estado, "Download iniciado: '%s' (%ld bytes, %d blocos)",
            estado->meta.nome_arquivo,
            estado->meta.tamanho_total,
            estado->meta.num_blocos);

    while (1) {
        /* Verificar progresso atual */
        pthread_mutex_lock(&estado->mutex_bitmap);
        int recebidos = estado->blocos_recebidos;
        int total     = estado->meta.num_blocos;
        pthread_mutex_unlock(&estado->mutex_bitmap);

        if (recebidos >= total) {
            log_msg(estado, "Download completo: %d/%d blocos", recebidos, total);
            if (remontar_arquivo(estado)) {
                estado->download_completo = 1;
            }
            break;
        }

        /* Tentar baixar blocos de cada vizinho */
        int progresso_rodada = 0;
        for (int v = 0; v < estado->num_vizinhos; v++) {
            pthread_mutex_lock(&estado->mutex_bitmap);
            int ainda_falta = (estado->blocos_recebidos < estado->meta.num_blocos);
            pthread_mutex_unlock(&estado->mutex_bitmap);
            if (!ainda_falta) break;

            progresso_rodada += pedir_blocos_vizinho(estado,
                                                     estado->vizinhos[v].ip,
                                                     estado->vizinhos[v].porta);
        }

        if (progresso_rodada == 0) {
            /* Nenhum bloco novo nesta rodada — aguardar antes de tentar de novo */
            log_msg(estado, "Aguardando vizinhos (%d/%d blocos)...",
                    estado->blocos_recebidos, estado->meta.num_blocos);
            sleep(INTERVALO_TENTATIVA);
        }
    }

    return NULL;
}

/* CONFIGURAÇÃO */
int carregar_config(const char *caminho, EstadoPar *estado) {
    FILE *fp = fopen(caminho, "r");
    if (!fp) {
        fprintf(stderr, "Erro: não foi possível abrir '%s': %s\n",
                caminho, strerror(errno));
        return 0;
    }

    char linha[512];
    while (fgets(linha, sizeof(linha), fp)) {
        linha[strcspn(linha, "\r\n")] = '\0';
        if (linha[0] == '#' || linha[0] == '\0') continue;

        char chave[64], valor[448];
        /* Dividir na primeira '=' */
        char *igual = strchr(linha, '=');
        if (!igual) continue;
        *igual = '\0';
        strncpy(chave, linha, 63); chave[63] = '\0';
        strncpy(valor, igual + 1, 447); valor[447] = '\0';

        if      (strcmp(chave, "ip")      == 0)
            strncpy(estado->ip_proprio, valor, 63);
        else if (strcmp(chave, "porta")   == 0)
            estado->porta_propria = atoi(valor);
        else if (strcmp(chave, "seeder")  == 0)
            estado->eh_seeder = atoi(valor);
        else if (strcmp(chave, "arquivo") == 0)
            strncpy(estado->arquivo_origem, valor, 511);
        else if (strcmp(chave, "destino") == 0)
            strncpy(estado->arquivo_destino, valor, 511);
        else if (strcmp(chave, "meta")    == 0)
            strncpy(estado->caminho_meta, valor, 511);
        else if (strcmp(chave, "blocos")  == 0)
            strncpy(estado->dir_blocos, valor, 511);
        else if (strcmp(chave, "vizinhos") == 0 && valor[0] != '\0') {
            
            char copia[448];
            strncpy(copia, valor, 447);
            char *token = strtok(copia, ",");
            while (token && estado->num_vizinhos < MAX_VIZINHOS) {
                char ip[64]; int porta;
                if (sscanf(token, "%63[^:]:%d", ip, &porta) == 2) {
                    strncpy(estado->vizinhos[estado->num_vizinhos].ip, ip, 63);
                    estado->vizinhos[estado->num_vizinhos].porta = porta;
                    estado->num_vizinhos++;
                }
                token = strtok(NULL, ",");
            }
        }
    }
    fclose(fp);

    /* Validação básica */
    if (estado->porta_propria <= 0) {
        fprintf(stderr, "Erro: porta inválida no config\n"); return 0;
    }
    return 1;
}

/* inicializar_estado — zera estrutura e inicializa mutexes */
void inicializar_estado(EstadoPar *estado) {
    memset(estado, 0, sizeof(*estado));
    pthread_mutex_init(&estado->mutex_bitmap, NULL);
    pthread_mutex_init(&estado->mutex_log, NULL);
}

/* finalizar_estado — libera recursos alocados */
void finalizar_estado(EstadoPar *estado) {
    if (estado->bitmap) {
        free(estado->bitmap);
        estado->bitmap = NULL;
    }
    pthread_mutex_destroy(&estado->mutex_bitmap);
    pthread_mutex_destroy(&estado->mutex_log);
}

/* PONTO DE ENTRADA*/
int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr,
            "Uso: %s <config.txt> [tamanho_bloco_bytes]\n\n"
            "  config.txt          arquivo de configuração do par\n"
            "  tamanho_bloco_bytes tamanho do bloco em bytes (padrão: %d)\n\n"
            "Exemplos:\n"
            "  %s config_seeder.txt\n"
            "  %s config_leecher1.txt 4096\n",
            argv[0], TAMANHO_BLOCO_PADRAO, argv[0], argv[0]);
        return 1;
    }

    signal(SIGPIPE, SIG_IGN);

    /* Inicializar estado global */
    inicializar_estado(&estado_global);

    /* Carregar configuração */
    if (!carregar_config(argv[1], &estado_global)) {
        fprintf(stderr, "Falha ao carregar configuração '%s'\n", argv[1]);
        return 1;
    }

    /* Tamanho de bloco pode ser sobrescrito por argumento */
    int tamanho_bloco = TAMANHO_BLOCO_PADRAO;
    if (argc >= 3) tamanho_bloco = atoi(argv[2]);

    log_msg(&estado_global, "=== Par P2P iniciado ===");
    log_msg(&estado_global, "Modo: %s | Vizinhos: %d",
            estado_global.eh_seeder ? "SEEDER" : "LEECHER",
            estado_global.num_vizinhos);

    if (estado_global.eh_seeder) {
        /* SEEDER */
        criar_metadado(estado_global.arquivo_origem, tamanho_bloco,
                       estado_global.caminho_meta, &estado_global.meta);

        /* Alocar bitmap */
        estado_global.bitmap = calloc(estado_global.meta.num_blocos, 1);
        if (!estado_global.bitmap) { perror("calloc"); return 1; }

        fragmentar_arquivo(&estado_global);

        log_msg(&estado_global, "Arquivo: '%s' | %ld bytes | %d blocos de %d B",
                estado_global.meta.nome_arquivo,
                estado_global.meta.tamanho_total,
                estado_global.meta.num_blocos,
                estado_global.meta.tamanho_bloco);
        log_msg(&estado_global, "SHA-256: %s", estado_global.meta.hash_sha256);

    } else {
        /* LEECHER */
        if (!carregar_metadado(estado_global.caminho_meta, &estado_global.meta)) {
            fprintf(stderr,
                "Erro: metadado '%s' não encontrado.\n"
                "Copie o arquivo .meta do seeder antes de iniciar.\n",
                estado_global.caminho_meta);
            return 1;
        }

        /* Alocar bitmap zerado  */
        estado_global.bitmap = calloc(estado_global.meta.num_blocos, 1);
        if (!estado_global.bitmap) { perror("calloc"); return 1; }

        /* Criar diretório de blocos e verificar se há blocos de run anterior */
        mkdir(estado_global.dir_blocos, 0755);
        escanear_blocos_existentes(&estado_global);

        log_msg(&estado_global, "Meta: '%s' | %ld bytes | %d blocos",
                estado_global.meta.nome_arquivo,
                estado_global.meta.tamanho_total,
                estado_global.meta.num_blocos);
        log_msg(&estado_global, "SHA-256 esperado: %s", estado_global.meta.hash_sha256);
    }

    /* Iniciar threads */

    /* Thread do servidor */
    pthread_t tid_servidor;
    if (pthread_create(&tid_servidor, NULL, thread_servidor, &estado_global) != 0) {
        perror("pthread_create servidor"); return 1;
    }

    /* Thread do cliente */
    pthread_t tid_cliente;
    if (pthread_create(&tid_cliente, NULL, thread_cliente, &estado_global) != 0) {
        perror("pthread_create cliente"); return 1;
    }

    /* Aguardar download terminar */
    pthread_join(tid_cliente, NULL);

    if (estado_global.download_completo) {
        log_msg(&estado_global, "Download e verificação concluídos com sucesso!");
    }

    log_msg(&estado_global,
            "Servidor P2P ativo. Pressione Ctrl+C para encerrar.");

    /* Servidor continua rodando para servir outros pares */
    pthread_join(tid_servidor, NULL);

    finalizar_estado(&estado_global);
    return 0;
}
