# Makefile — Sistema P2P de Transferência de Arquivos
# Disciplina: Sistemas Distribuídos — CEFET-MG

CC      = gcc
CFLAGS  = -Wall -Wextra -pthread -O2 -g
LDFLAGS = -lpthread

# Alvos principais
.PHONY: all clean limpar_blocos limpar_tudo ajuda

all: par criar_teste

# Compilar o par P2P (depende de par.c, par.h, sha256.h)
par: par.c par.h sha256.h
	$(CC) $(CFLAGS) -o par par.c $(LDFLAGS)
	@echo "✓ Compilado: par"

# Compilar o criador de arquivos de teste
criar_teste: criar_teste.c
	$(CC) $(CFLAGS) -o criar_teste criar_teste.c
	@echo "✓ Compilado: criar_teste"

# Remover binários compilados
clean:
	rm -f par criar_teste
	@echo "✓ Binários removidos"

# Remover diretórios de blocos temporários
limpar_blocos:
	rm -rf blocos_seeder blocos_leecher1 blocos_leecher2 blocos_leecher3 blocos_leecher4
	@echo "✓ Diretórios de blocos removidos"

# Remover tudo (binários, blocos, arquivos baixados e de teste)
limpar_tudo: clean limpar_blocos
	rm -f *.bin *.meta baixado_*.bin
	@echo "✓ Todos os arquivos temporários removidos"

ajuda:
	@echo ""
	@echo "Alvos disponíveis:"
	@echo "  make            - Compila par e criar_teste"
	@echo "  make clean      - Remove binários"
	@echo "  make limpar_blocos  - Remove diretórios de blocos"
	@echo "  make limpar_tudo    - Remove tudo"
	@echo ""
	@echo "Uso rápido:"
	@echo "  ./criar_teste 1024 arquivo.bin"
	@echo "  ./par config_seeder.txt"
	@echo "  ./par config_leecher1.txt"
	@echo ""
