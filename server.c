/*
 * server.c - Servidor "O Boteco do Zé"
 *
 * Implementa o atendimento multi-threaded aos clientes via TCP e a
 * comunicação confiável com a cozinha via UDP Stop-and-Wait (rede capenga).
 */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "rede_capenga.h"

#define MAX_BUFFER 256
#define NUM_ITENS 5
#define MAX_TENTATIVAS_UDP 15
#define TIMEOUT_UDP_MS 200

/* Tabela global para broadcast de aviso de estoque */
int g_clientes_fd[MAX_CLIENTES_CONECTADOS];
pthread_mutex_t g_mutex_clientes = PTHREAD_MUTEX_INITIALIZER;

void inicializar_tabela_clientes(void) {
    for (int i = 0; i < MAX_CLIENTES_CONECTADOS; i++) {
        g_clientes_fd[i] = -1;
    }
}

void registrar_cliente(int fd) {
    pthread_mutex_lock(&g_mutex_clientes);
    for (int i = 0; i < MAX_CLIENTES_CONECTADOS; i++) {
        if (g_clientes_fd[i] == -1) {
            g_clientes_fd[i] = fd;
            break;
        }
    }
    pthread_mutex_unlock(&g_mutex_clientes);
}

void remover_cliente(int fd) {
    pthread_mutex_lock(&g_mutex_clientes);
    for (int i = 0; i < MAX_CLIENTES_CONECTADOS; i++) {
        if (g_clientes_fd[i] == fd) {
            g_clientes_fd[i] = -1;
            break;
        }
    }
    pthread_mutex_unlock(&g_mutex_clientes);
}

void broadcast_estoque_baixo(const char *nome_item, int estoque_atual) {
    char msg[128];
    snprintf(msg, sizeof(msg), "AVISO ESTOQUE BAIXO: %s (%d restantes)\n", nome_item, estoque_atual);

    pthread_mutex_lock(&g_mutex_clientes);
    for (int i = 0; i < MAX_CLIENTES_CONECTADOS; i++) {
        if (g_clientes_fd[i] != -1) {
            /* MSG_NOSIGNAL evita que o servidor caia se a conexão estiver fechada */
            send(g_clientes_fd[i], msg, strlen(msg), MSG_NOSIGNAL);
        }
    }
    pthread_mutex_unlock(&g_mutex_clientes);
}

/* Estrutura para cada item do cardápio */
typedef struct {
    const char *nome;
    float preco;
    float preco_original; // Guarda o preço normal
    int estoque;
    int estoque_inicial;
    pthread_mutex_t mutex; /* Mutex granular para este item específico */
} ItemCardapio;

/* Cardápio global com seus respetivos mutexes */
static ItemCardapio cardapio[NUM_ITENS] = {
    {"CERVEJA",     8.00f,  20, PTHREAD_MUTEX_INITIALIZER},
    {"CAIPIRINHA",  12.00f, 10, PTHREAD_MUTEX_INITIALIZER},
    {"GUARANA",     5.00f,  15, PTHREAD_MUTEX_INITIALIZER},
    {"AGUA",        3.00f,  30, PTHREAD_MUTEX_INITIALIZER},
    {"PAODEQUEIJO", 4.50f,  25, PTHREAD_MUTEX_INITIALIZER}
};

/* Semáforo para controle de capacidade de mesas */
static sem_t sem_mesas;

/* Dados passados para as threads de atendimento ao cliente */
typedef struct {
    int client_fd;
    struct sockaddr_in cozinha_addr;
} ClientThreadArgs;

/* Converte string para maiúsculas */
static void str_toupper(char *s) {
    for (; *s; ++s) *s = (char)toupper((unsigned char)*s);
}

/* Encontra o índice de um item pelo nome (-1 se não encontrar) */
static int encontrar_item(const char *nome) {
    for (int i = 0; i < NUM_ITENS; i++) {
        if (strcasecmp(cardapio[i].nome, nome) == 0) {
            return i;
        }
    }
    return -1;
}

/* 
 * Comunicação com a Cozinha via UDP Stop-and-Wait 
 * Retorna 1 em caso de sucesso (PRONTO recebido) ou 0 em caso de falha/timeout.
 */
static int enviar_pedido_cozinha(int seq, const char *item_nome, int qtd, const struct sockaddr_in *cozinha_addr) {
    int udp_fd = capenga_socket();
    if (udp_fd < 0) {
        perror("capenga_socket");
        return 0;
    }

    /* Configura timeout no socket UDP */
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = TIMEOUT_UDP_MS * 1000;
    if (setsockopt(udp_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        perror("setsockopt SO_RCVTIMEO");
        close(udp_fd);
        return 0;
    }

    /* Prepara a carga útil da mensagem: "PEDIDO <ITEM> <QTD>" */
    char corpo_envio[128];
    snprintf(corpo_envio, sizeof(corpo_envio), "PEDIDO %s %d", item_nome, qtd);

    /* Calcula o checksum sobre o corpo */
    unsigned char cksum_envio = capenga_checksum(corpo_envio, strlen(corpo_envio));

    /* Prepara o pacote completo: "<SEQ> <CHECKSUM> PEDIDO <ITEM> <QTD>" */
    char msg_envio[MAX_BUFFER];
    snprintf(msg_envio, sizeof(msg_envio), "%d %u %s", seq, (unsigned int)cksum_envio, corpo_envio);

    int sucesso = 0;

    for (int tentativa = 1; tentativa <= MAX_TENTATIVAS_UDP; tentativa++) {
        /* Envia pela rede capenga */
        ssize_t s = capenga_sendto(udp_fd, msg_envio, strlen(msg_envio), 0,
                                   (const struct sockaddr *)cozinha_addr, sizeof(*cozinha_addr));
        if (s < 0) {
            perror("capenga_sendto");
        }

        /* Aguarda resposta com timeout */
        char buf_resp[MAX_BUFFER];
        struct sockaddr_in src_addr;
        socklen_t src_len = sizeof(src_addr);

        ssize_t n = capenga_recvfrom(udp_fd, buf_resp, sizeof(buf_resp) - 1, 0,
                                     (struct sockaddr *)&src_addr, &src_len);
        if (n <= 0) {
            /* Timeout ou erro de leitura: tenta novamente no próximo loop */
            continue;
        }

        buf_resp[n] = '\0';

        /* Parse da resposta: "<SEQ> <CHECKSUM> PRONTO" */
        int seq_resp;
        unsigned int cksum_resp;
        char corpo_resp[MAX_BUFFER];

        if (sscanf(buf_resp, "%d %u %[^\n]", &seq_resp, &cksum_resp, corpo_resp) != 3) {
            /* Mensagem mal formatada: descarte silencioso */
            continue;
        }

        /* Valida o checksum do corpo recebido */
        unsigned char cksum_calc = capenga_checksum(corpo_resp, strlen(corpo_resp));
        if (cksum_calc != (unsigned char)cksum_resp) {
            /* Checksum inválido: descarte silencioso */
            continue;
        }

        /* Valida se o SEQ bate com o esperado e a resposta é "PRONTO" */
        if (seq_resp == seq && strcmp(corpo_resp, "PRONTO") == 0) {
            sucesso = 1;
            break;
        }
    }

    close(udp_fd);
    return sucesso;
}

/* Thread dedicada ao atendimento de cada cliente TCP */
static void *client_handler(void *arg) {
    ClientThreadArgs args = *(ClientThreadArgs *)arg;
    free(arg);

    int fd = args.client_fd;
    float conta_total = 0.0f;
    int seq_atual = 0; /* Alterna entre 0 e 1 */

    FILE *stream = fdopen(fd, "r+");
    if (!stream) {
        perror("fdopen");
        close(fd);
        sem_post(&sem_mesas);
        return NULL;
    }

    /* Garante bufferização por linha no envio TCP */
    setvbuf(stream, NULL, _IOLBF, 0);

    char linha[MAX_BUFFER];

    while (fgets(linha, sizeof(linha), stream) != NULL) {
        /* Remove \r e \n do final */
        linha[strcspn(linha, "\r\n")] = '\0';

        if (strlen(linha) == 0) continue;

        char cmd[MAX_BUFFER];
        char arg1[MAX_BUFFER];
        int qtd = 0;

        int num_args = sscanf(linha, "%s %s %d", cmd, arg1, &qtd);
        str_toupper(cmd);

        if (strcmp(cmd, "CARDAPIO") == 0) {
            for (int i = 0; i < NUM_ITENS; i++) {
                pthread_mutex_lock(&cardapio[i].mutex);
                fprintf(stream, "%s %.2f %d\n", cardapio[i].nome, cardapio[i].preco, cardapio[i].estoque);
                pthread_mutex_unlock(&cardapio[i].mutex);
            }
            fflush(stream);
        } 
        else if (strcmp(cmd, "PEDIR") == 0) {
            if (num_args < 3 || qtd <= 0) {
                fprintf(stream, "ERRO PROTOCOLO\n");
                fflush(stream);
                continue;
            }

            int idx = encontrar_item(arg1);
            if (idx == -1) {
                fprintf(stream, "ERRO PROTOCOLO\n");
                fflush(stream);
                continue;
            }

            /* Passo 1: Reserva atômica no estoque granular */
            pthread_mutex_lock(&cardapio[idx].mutex);
            if (cardapio[idx].estoque < qtd) {
                pthread_mutex_unlock(&cardapio[idx].mutex);
                fprintf(stream, "ESGOTADO %s\n", cardapio[idx].nome);
                fflush(stream);
                continue;
            }

            cardapio[idx].estoque -= qtd;
            pthread_mutex_unlock(&cardapio[idx].mutex);

            /* Passo 2: Tenta enviar o pedido para a cozinha via UDP Stop-and-Wait */
            int ok_cozinha = enviar_pedido_cozinha(seq_atual, cardapio[idx].nome, qtd, &args.cozinha_addr);

            if (!ok_cozinha) {
                /* Rollback atômico no estoque caso a cozinha falhe */
                pthread_mutex_lock(&cardapio[idx].mutex);
                cardapio[idx].estoque += qtd;
                pthread_mutex_unlock(&cardapio[idx].mutex);

                fprintf(stream, "ERRO COZINHA\n");
                fflush(stream);
            } else {
                /* Sucesso: atualiza conta, alterna o número de sequência (0 -> 1 -> 0) */
                float subtotal = cardapio[idx].preco * (float)qtd;
                conta_total += subtotal;
                fprintf(stream, "OK %s %d %.2f\n", cardapio[idx].nome, qtd, subtotal);
                fflush(stream);
                seq_atual = 1 - seq_atual;
            }
        } 
        else if (strcmp(cmd, "CONTA") == 0) {
            fprintf(stream, "CONTA %.2f\n", conta_total);
            fflush(stream);
        } 
        else if (strcmp(cmd, "SAIR") == 0) {
            fprintf(stream, "TCHAU %.2f\n", conta_total);
            fflush(stream);
            break;
        } 
        else {
            fprintf(stream, "ERRO PROTOCOLO\n");
            fflush(stream);
        }
    }

    /* Encerramento limpo da conexão do cliente */
    fclose(stream);
    sem_post(&sem_mesas);
    return NULL;
}

void *happy_hour_thread(void *arg) {
    int intervalo_segundos = 60; // Exemplo: ativa o Happy Hour a cada 60s
    (void)arg;

    while (1) {
        sleep(intervalo_segundos);

        printf("\n[HAPPY HOUR] *** INICIADO! Todos os itens com 50%% de desconto por 30s! ***\n");

        /* 1. Aplica o desconto de 50% sob mutex granular */
        for (int i = 0; i < NUM_ITENS; i++) {
            pthread_mutex_lock(&cardapio[i].mutex);
            cardapio[i].preco = cardapio[i].preco_original * 0.5f;
            pthread_mutex_unlock(&cardapio[i].mutex);
        }

        /* 2. Mantém o desconto ativo por 30 segundos */
        sleep(30);

        printf("\n[HAPPY HOUR] *** FIM DO HAPPY HOUR! Precos voltaram ao normal. ***\n");

        /* 3. Restaura os preços originais sob mutex granular */
        for (int i = 0; i < NUM_ITENS; i++) {
            pthread_mutex_lock(&cardapio[i].mutex);
            cardapio[i].preco = cardapio[i].preco_original;
            pthread_mutex_unlock(&cardapio[i].mutex);
        }
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 5) {
        fprintf(stderr, "Uso: %s <porta_clientes> <num_mesas> <ip_cozinha> <porta_cozinha>\n", argv[0]);
        return 1;
    }

    int porta_clientes = atoi(argv[1]);
    int num_mesas = atoi(argv[2]);
    const char *ip_cozinha = argv[3];
    int porta_cozinha = atoi(argv[4]);

    if (porta_clientes <= 0 || porta_clientes > 65535) {
        fprintf(stderr, "Erro: porta_clientes deve estar entre 1 e 65535 (recebido: %d)\n", porta_clientes);
        return 1;
    }

    if (porta_cozinha <= 0 || porta_cozinha > 65535) {
        fprintf(stderr, "Erro: porta_cozinha deve estar entre 1 e 65535 (recebido: %d)\n", porta_cozinha);
        return 1;
    }

    if (num_mesas <= 0) {
        fprintf(stderr, "Erro: num_mesas deve ser maior que 0.\n");
        return 1;
    }

    /* Inicializa semáforo de mesas */
    sem_init(&sem_mesas, 0, num_mesas);

/* Validação do IP da cozinha, evita inet_pton cozinha: Success*/
    struct sockaddr_in cozinha_addr;
    memset(&cozinha_addr, 0, sizeof(cozinha_addr));
    cozinha_addr.sin_family = AF_INET;
    cozinha_addr.sin_port = htons((uint16_t)porta_cozinha);

    int res = inet_pton(AF_INET, ip_cozinha, &cozinha_addr.sin_addr);
    if (res <= 0) {
        if (res == 0) {
            fprintf(stderr, "Erro: IP da cozinha '%s' possui formato invalido.\n", ip_cozinha);
        } else {
            perror("inet_pton cozinha");
        }
        return 1;
    }

    /* Cria socket TCP escuta */
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket escuta TCP");
        return 1;
    }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons((uint16_t)porta_clientes);

    if (bind(listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind TCP");
        close(listen_fd);
        return 1;
    }

    if (listen(listen_fd, 128) < 0) {
        perror("listen");
        close(listen_fd);
        return 1;
    }
    /* Inicia a thread do Happy Hour */
    pthread_t th_hh;
    pthread_create(&th_hh, NULL, happy_hour_thread, NULL);
    pthread_detach(th_hh);

    printf("[SERVIDOR] Boteco do Zé rodando na porta %d (%d mesas disponíveis)...\n", 
           porta_clientes, num_mesas);

    while (1) {
        struct sockaddr_in cli_addr;
        socklen_t cli_len = sizeof(cli_addr);

        int client_fd = accept(listen_fd, (struct sockaddr *)&cli_addr, &cli_len);
        if (client_fd < 0) {
            perror("accept");
            continue;
        }

        /* Tenta reservar uma mesa sem bloquear imediatamente */
        if (sem_trywait(&sem_mesas) != 0) {
            if (errno == EAGAIN) {
                /* Não há mesas livres no momento: avisa o cliente sobre a espera */
                const char *msg_espera = "SEM MESA DISPONIVEL no momento. Aguarde na fila...\n";
                send(client_fd, msg_espera, strlen(msg_espera), 0);

                /* Bloqueia até uma mesa ser liberada por outro cliente */
                sem_wait(&sem_mesas);

                const char *msg_liberado = "MESA LIBERADA! Seja bem-vindo ao Boteco do Ze.\n";
                send(client_fd, msg_liberado, strlen(msg_liberado), 0);
            }
        } else {
            /* Entrou direto: envia confirmação de mesa */
            const char *msg_ok = "BEM-VINDO! Mesa reservada com sucesso.\n";
            send(client_fd, msg_ok, strlen(msg_ok), 0);
        }

        ClientThreadArgs *args = malloc(sizeof(ClientThreadArgs));
        if (!args) {
            perror("malloc");
            close(client_fd);
            sem_post(&sem_mesas);
            continue;
        }

        args->client_fd = client_fd;
        args->cozinha_addr = cozinha_addr;

        pthread_t tid;
        if (pthread_create(&tid, NULL, client_handler, args) != 0) {
            perror("pthread_create");
            close(client_fd);
            free(args);
            sem_post(&sem_mesas);
        } else {
            pthread_detach(tid);
        }
    }

    close(listen_fd);
    sem_destroy(&sem_mesas);
    return 0;
}
