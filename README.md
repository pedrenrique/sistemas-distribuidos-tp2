# Sistema P2P de Transferência de Arquivos
**Disciplina:** Sistemas Distribuídos — CEFET-MG  
**Trabalho Prático 2 — 2025/2**

---

## Arquitetura

Cada **par** (peer) roda dois processos lógicos em paralelo:

| Thread | Papel | O que faz |
|--------|-------|-----------|
| `thread_servidor` | Servidor P2P | Aceita conexões, responde a pedidos de blocos |
| `thread_cliente`  | Leecher P2P  | Conecta a vizinhos e baixa blocos faltantes |

**Seeder** → inicia com o arquivo completo, fragmenta e serve blocos.  
**Leecher** → começa com zero blocos; à medida que baixa blocos, também os serve.

---

## Protocolo de Mensagens

Todas as mensagens têm um **cabeçalho fixo de 9 bytes** seguido de dados opcionais:

```
[1 byte: tipo] [4 bytes: num_bloco, big-endian] [4 bytes: tam_dados, big-endian] [dados...]
```

| Tipo | Valor | Direção | Significado |
|------|-------|---------|-------------|
| MSG_LISTAR_BLOCOS | 0x01 | cliente→servidor | "Quais blocos você tem?" |
| MSG_RESP_BLOCOS   | 0x02 | servidor→cliente | Bitmap de blocos disponíveis |
| MSG_PEDIR_BLOCO   | 0x03 | cliente→servidor | "Me manda o bloco N" |
| MSG_RESP_BLOCO    | 0x04 | servidor→cliente | Dados do bloco N |
| MSG_SEM_BLOCO     | 0x05 | servidor→cliente | "Não tenho o bloco N" |

---

## Arquivo de Configuração

```ini
ip=127.0.0.1          # IP deste par
porta=5001            # Porta TCP
seeder=1              # 1=seeder, 0=leecher
arquivo=./teste.bin   # Arquivo fonte (só seeder usa)
destino=./baixado.bin # Onde salvar o arquivo baixado
meta=./arquivo.meta   # Arquivo de metadados
blocos=./blocos_seeder # Diretório temporário para blocos
vizinhos=127.0.0.1:5002,127.0.0.1:5003  # Lista de vizinhos
```

---

## Compilação

```bash
# Pré-requisito: gcc e pthreads (padrão no Linux)
make

# Saída: executáveis 'par' e 'criar_teste'
```

---

## Teste Manual Passo a Passo

### Cenário 1 — 2 peers, arquivo de 1 MB, bloco de 1 KB

Abra **3 terminais** na pasta do projeto.

**Terminal 1 — Criar arquivo de teste:**
```bash
./criar_teste 1024 arquivo_teste.bin
sha256sum arquivo_teste.bin   # guarde este hash para comparar depois
```

**Terminal 2 — Iniciar o Seeder (Peer A):**
```bash
./par config_seeder.txt 1024
```
> O seeder fragmenta o arquivo em blocos e aguarda conexões.

**Terminal 3 — Iniciar o Leecher (Peer B):**
```bash
./par config_leecher1.txt 1024
```
> O leecher conecta ao seeder, baixa os blocos e remonta o arquivo.

**Verificar integridade:**
```bash
sha256sum baixado_leecher1.bin
# Deve ser igual ao hash do arquivo original
```

---

### Cenário 2 — 4 peers

Abra **5 terminais**:

```bash
# Terminal 1: criar arquivo
./criar_teste 10240 arquivo_teste.bin

# Terminal 2: seeder
./par config_seeder.txt 1024

# Terminal 3: leecher 1
./par config_leecher1.txt 1024

# Terminal 4: leecher 2 (baixa do seeder E do leecher1)
./par config_leecher2.txt 1024

# Terminal 5: leecher 3 (baixa de todos os anteriores)
./par config_leecher3.txt 1024
```

---

### Cenário 3 — Bloco de 4 KB

```bash
./criar_teste 1024 arquivo_teste.bin
./par config_seeder.txt  4096   # segundo argumento = tamanho do bloco
./par config_leecher1.txt 4096
```

---

## Teste Automático

O script `testar.sh` executa todos os cenários automaticamente:

```bash
chmod +x testar.sh

# Executar todos os cenários:
./testar.sh

# Executar cenário específico (1 a 9):
./testar.sh 1    # 2 peers, 10 KB, bloco 1024 B
./testar.sh 5    # 2 peers, 1 MB,  bloco 4096 B
./testar.sh 8    # 4 peers, 1 MB,  bloco 1024 B
```

---

## Tabela de Cenários do TP

| # | Peers | Arquivo | Bloco | Config |
|---|-------|---------|-------|--------|
| 1 | 2 | 10 KB  | 1024 B | `./testar.sh 1` |
| 2 | 2 | 10 KB  | 4096 B | `./testar.sh 2` |
| 3 | 2 | 20 KB  | 1024 B | `./testar.sh 3` |
| 4 | 2 | 1 MB   | 1024 B | `./testar.sh 4` |
| 5 | 2 | 1 MB   | 4096 B | `./testar.sh 5` |
| 6 | 2 | 5 MB   | 1024 B | `./testar.sh 6` |
| 7 | 2 | 10 MB  | 1024 B | `./testar.sh 7` |
| 8 | 4 | 1 MB   | 1024 B | `./testar.sh 8` |
| 9 | 4 | 10 MB  | 1024 B | `./testar.sh 9` |

---

## Estrutura de Arquivos

```
p2p/
├── sha256.h            ← SHA-256 autocontida (sem dependências externas)
├── par.h               ← Estruturas, constantes e protótipos
├── par.c               ← Implementação completa
├── criar_teste.c       ← Cria arquivos de teste aleatórios
├── Makefile
├── testar.sh           ← Script de testes automáticos
├── config_seeder.txt   ← Exemplo de config para seeder
├── config_leecher1.txt ← Peer B (vizinho: seeder)
├── config_leecher2.txt ← Peer C (vizinhos: seeder + leecher1)
└── config_leecher3.txt ← Peer D (vizinhos: seeder + leecher1 + leecher2)
```

---

## Decisões de Projeto

**Linguagem:** C padrão (C99) com pthreads — sem dependências externas.

**SHA-256:** Implementação própria em `sha256.h` (FIPS 180-4), sem OpenSSL.

**Protocolo:** TCP com cabeçalho binário de 9 bytes. A conexão fica aberta durante toda a sessão de download de um vizinho (reutilização de conexão).

**Bitmap de blocos:** Array de `uint8_t` — índice `i` = 1 se o par tem o bloco `i`. Protegido por `pthread_mutex`.

**Blocos em disco:** Cada bloco é um arquivo separado em `dir_blocos/bloco_NNNNN.bin`. Isso permite retomada em caso de reinicialização.

**Tornando-se seeder:** Assim que um leecher salva um bloco no disco e atualiza o bitmap, o servidor já pode servir esse bloco a outros pares. Não há sincronização adicional necessária.

**Configuração estática:** Vizinhos são definidos no arquivo de config, eliminando a necessidade de um tracker.

---

## Limpeza

```bash
make clean          # remove binários
make limpar_blocos  # remove diretórios de blocos temporários
make limpar_tudo    # remove tudo
```
