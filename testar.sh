#!/bin/bash
# testar.sh — Executa os estudos de caso do Trabalho Prático 2
#
# Uso: ./testar.sh [cenario]
#   ./testar.sh        — executa todos os cenários em sequência
#   ./testar.sh 1      — apenas Cenário 1 (2 peers, 10 KB, 1 KB/bloco)
#   ./testar.sh 2      — apenas Cenário 2 (2 peers, 10 KB, 4 KB/bloco)
#   etc.

set -e  # parar em caso de erro de compilação

# ---- Cores para output ----
VERDE='\033[0;32m'
VERMELHO='\033[0;31m'
AMARELO='\033[1;33m'
AZUL='\033[0;34m'
SEM_COR='\033[0m'

ok()   { echo -e "${VERDE}[OK]${SEM_COR} $*"; }
erro() { echo -e "${VERMELHO}[ERRO]${SEM_COR} $*"; }
info() { echo -e "${AZUL}[INFO]${SEM_COR} $*"; }
titulo() { echo -e "\n${AMARELO}===== $* =====${SEM_COR}\n"; }

# ---- Compilar ----
titulo "Compilando..."
make clean > /dev/null 2>&1
make 2>&1
ok "Compilação concluída"

# ---- Função principal de teste ----
# Parâmetros: desc num_peers tamanho_kb tamanho_bloco
executar_cenario() {
    local DESCRICAO="$1"
    local NUM_PEERS="$2"
    local TAMANHO_KB="$3"
    local TAM_BLOCO="$4"

    titulo "Cenário: $DESCRICAO"
    info "Peers: $NUM_PEERS | Arquivo: ${TAMANHO_KB} KB | Bloco: ${TAM_BLOCO} B"

    # Limpar estado anterior
    rm -rf blocos_seeder blocos_leecher1 blocos_leecher2 blocos_leecher3
    rm -f arquivo_teste.bin baixado_leecher*.bin arquivo.meta

    # Criar arquivo de teste
    info "Criando arquivo de teste (${TAMANHO_KB} KB)..."
    ./criar_teste "$TAMANHO_KB" arquivo_teste.bin
    HASH_ORIGINAL=$(sha256sum arquivo_teste.bin | awk '{print $1}')
    info "SHA-256 original: $HASH_ORIGINAL"

    # Iniciar seeder em background
    info "Iniciando seeder na porta 5001..."
    ./par config_seeder.txt "$TAM_BLOCO" > log_seeder.txt 2>&1 &
    PID_SEEDER=$!
    sleep 1  # aguardar servidor subir

    # Iniciar leechers em background
    PIDS=()
    for i in $(seq 1 $((NUM_PEERS - 1))); do
        info "Iniciando leecher$i na porta $((5001 + i))..."
        ./par "config_leecher${i}.txt" "$TAM_BLOCO" > "log_leecher${i}.txt" 2>&1 &
        PIDS+=($!)
        sleep 0.5
    done

    # Aguardar leechers terminarem o download
    TIMEOUT=120  # segundos máximos de espera
    DECORRIDO=0
    TODOS_PRONTOS=false

    info "Aguardando downloads (timeout: ${TIMEOUT}s)..."
    while [ $DECORRIDO -lt $TIMEOUT ]; do
        PRONTOS=0
        for i in $(seq 1 $((NUM_PEERS - 1))); do
            if grep -q "Integridade OK" "log_leecher${i}.txt" 2>/dev/null; then
                PRONTOS=$((PRONTOS + 1))
            fi
        done

        if [ $PRONTOS -eq $((NUM_PEERS - 1)) ]; then
            TODOS_PRONTOS=true
            break
        fi

        sleep 2
        DECORRIDO=$((DECORRIDO + 2))
        echo -n "."
    done
    echo ""

    # Matar todos os processos
    kill $PID_SEEDER 2>/dev/null || true
    for PID in "${PIDS[@]}"; do
        kill "$PID" 2>/dev/null || true
    done
    sleep 1

    # Verificar resultados
    local SUCESSO=true
    for i in $(seq 1 $((NUM_PEERS - 1))); do
        local ARQUIVO_BAIXADO="baixado_leecher${i}.bin"
        if [ ! -f "$ARQUIVO_BAIXADO" ]; then
            erro "leecher${i}: arquivo '$ARQUIVO_BAIXADO' não gerado"
            SUCESSO=false
            continue
        fi

        local HASH_BAIXADO
        HASH_BAIXADO=$(sha256sum "$ARQUIVO_BAIXADO" | awk '{print $1}')

        if [ "$HASH_BAIXADO" = "$HASH_ORIGINAL" ]; then
            ok "leecher${i}: integridade verificada (SHA-256 correto)"
        else
            erro "leecher${i}: FALHA de integridade!"
            erro "  Esperado: $HASH_ORIGINAL"
            erro "  Obtido  : $HASH_BAIXADO"
            SUCESSO=false
        fi
    done

    if $SUCESSO && $TODOS_PRONTOS; then
        ok "CENÁRIO PASSOU em ${DECORRIDO}s"
    else
        erro "CENÁRIO FALHOU (decorrido: ${DECORRIDO}s)"
        info "Verifique os logs: log_seeder.txt, log_leecher*.txt"
    fi

    echo ""
}

# ---- Definir quais cenários executar ----
CENARIO=${1:-"todos"}

case "$CENARIO" in
    1) executar_cenario "2 peers | 10 KB  | bloco 1024 B" 2 10    1024 ;;
    2) executar_cenario "2 peers | 10 KB  | bloco 4096 B" 2 10    4096 ;;
    3) executar_cenario "2 peers | 20 KB  | bloco 1024 B" 2 20    1024 ;;
    4) executar_cenario "2 peers | 1 MB   | bloco 1024 B" 2 1024  1024 ;;
    5) executar_cenario "2 peers | 1 MB   | bloco 4096 B" 2 1024  4096 ;;
    6) executar_cenario "2 peers | 5 MB   | bloco 1024 B" 2 5120  1024 ;;
    7) executar_cenario "2 peers | 10 MB  | bloco 1024 B" 2 10240 1024 ;;
    8) executar_cenario "4 peers | 1 MB   | bloco 1024 B" 4 1024  1024 ;;
    9) executar_cenario "4 peers | 10 MB  | bloco 1024 B" 4 10240 1024 ;;
    todos)
        titulo "EXECUTANDO TODOS OS CENÁRIOS"
        executar_cenario "2 peers | 10 KB  | bloco 1024 B" 2 10    1024
        executar_cenario "2 peers | 10 KB  | bloco 4096 B" 2 10    4096
        executar_cenario "2 peers | 1 MB   | bloco 1024 B" 2 1024  1024
        executar_cenario "2 peers | 1 MB   | bloco 4096 B" 2 1024  4096
        executar_cenario "2 peers | 10 MB  | bloco 1024 B" 2 10240 1024
        executar_cenario "4 peers | 1 MB   | bloco 1024 B" 4 1024  1024
        executar_cenario "4 peers | 10 MB  | bloco 1024 B" 4 10240 1024
        titulo "TODOS OS CENÁRIOS CONCLUÍDOS"
        ;;
    *)
        echo "Cenário '$CENARIO' inválido. Use 1-9 ou 'todos'."
        exit 1
        ;;
esac
