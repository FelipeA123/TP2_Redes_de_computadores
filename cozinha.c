/*
 * cozinha.c
 *
 * Processo de referência da "cozinha" do Boteco do Zé, fornecido para
 * o TP2. Fica esperando pedidos do servidor (Zé) pela rede capenga e
 * confirma cada um com PRONTO. Implementa o lado RECEPTOR do
 * protocolo de confiabilidade (validação de checksum e deduplicação
 * por número de sequência) — o lado que caberá a vocês implementar,
 * no servidor, é o lado EMISSOR (timeout e retransmissão).
 *
 * Formato das mensagens (texto, campos separados por espaço):
 *   Servidor -> Cozinha:  "<SEQ> <CHECKSUM> PEDIDO <ITEM> <QTD>"
 *   Cozinha  -> Servidor: "<SEQ> <CHECKSUM> PRONTO"
 *
 * SEQ é 0 ou 1 (Stop-and-Wait). CHECKSUM é o valor de
 * capenga_checksum() (0-255) sobre o restante da mensagem após o
 * campo de checksum, formatado em decimal.
 *
 * NÃO É NECESSÁRIO ENTREGAR ESTE ARQUIVO — ele já está disponível no
 * ambiente de correção. Ele é dado para que vocês possam testar o
 * servidor localmente.
 *
 * A cozinha também aplica sua própria rede capenga nas respostas que
 * manda de volta (o enlace é ruim nos dois sentidos) — por isso seu
 * servidor precisa tratar tanto pedido perdido quanto confirmação
 * perdida/corrompida/duplicada.
 *
 * Uso: ./cozinha <porta> [perda] [corrupcao] [duplicacao] [atraso_max_ms]
 * Padrão se os últimos 4 argumentos forem omitidos: 0.2 0.1 0.1 50
 */
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "rede_capenga.h"

#define BUF_SIZE 256

/* Mantém, por cliente (endereço:porta de origem), qual foi o último
 * SEQ processado e qual foi a última resposta enviada — assim, se o
 * mesmo pedido chegar duplicado (rede capenga adora fazer isso), a
 * cozinha reenvia a resposta antiga em vez de "fritar o pastel" de
 * novo. */
#define MAX_CLIENTES 64

typedef struct {
    int em_uso;
    struct sockaddr_in endereco;
    int ultimo_seq;
    char ultima_resposta[BUF_SIZE];
} EstadoCliente;

static EstadoCliente clientes[MAX_CLIENTES];

static int mesmo_endereco(const struct sockaddr_in *a, const struct sockaddr_in *b) {
    return a->sin_addr.s_addr == b->sin_addr.s_addr && a->sin_port == b->sin_port;
}

static EstadoCliente *busca_ou_cria_cliente(const struct sockaddr_in *addr) {
    int livre = -1;
    for (int i = 0; i < MAX_CLIENTES; i++) {
        if (clientes[i].em_uso && mesmo_endereco(&clientes[i].endereco, addr)) {
            return &clientes[i];
        }
        if (!clientes[i].em_uso && livre == -1) {
            livre = i;
        }
    }
    if (livre == -1) {
        return NULL; /* capacidade esgotada; caso improvável em testes */
    }
    clientes[livre].em_uso = 1;
    clientes[livre].endereco = *addr;
    clientes[livre].ultimo_seq = -1; /* nenhum pedido processado ainda */
    clientes[livre].ultima_resposta[0] = '\0';
    return &clientes[livre];
}

int main(int argc, char *argv[]) {
    if (argc != 2 && argc != 6) {
        fprintf(stderr, "Uso: %s <porta> [perda] [corrupcao] [duplicacao] [atraso_max_ms]\n", argv[0]);
        return 1;
    }
    int porta = atoi(argv[1]);

    /* Saida com buffer por linha, para que o log apareca em tempo real
     * mesmo quando redirecionado para um arquivo (util para acompanhar
     * o comportamento da cozinha durante os testes). */
    setvbuf(stdout, NULL, _IOLBF, 0);

    double perda = 0.2, corrupcao = 0.1, duplicacao = 0.1;
    int atraso_max_ms = 50;
    if (argc == 6) {
        perda = atof(argv[2]);
        corrupcao = atof(argv[3]);
        duplicacao = atof(argv[4]);
        atraso_max_ms = atoi(argv[5]);
    }
    capenga_config(perda, corrupcao, duplicacao, atraso_max_ms);

    int sockfd = capenga_socket();
    if (sockfd < 0) {
        perror("capenga_socket");
        return 1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)porta);

    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }

    printf("[COZINHA] Aguardando pedidos na porta %d...\n", porta);

    char buf[BUF_SIZE];
    while (1) {
        struct sockaddr_in cli;
        socklen_t cli_len = sizeof(cli);

        ssize_t n = capenga_recvfrom(sockfd, buf, sizeof(buf) - 1, 0,
                                      (struct sockaddr *)&cli, &cli_len);
        if (n <= 0) {
            continue;
        }
        buf[n] = '\0';

        int seq_recebido;
        unsigned int checksum_recebido;
        char resto[BUF_SIZE];

        if (sscanf(buf, "%d %u %[^\n]", &seq_recebido, &checksum_recebido, resto) != 3) {
            /* mensagem mal formatada -- trate como corrompida: ignore */
            continue;
        }

        unsigned char checksum_calculado = capenga_checksum(resto, strlen(resto));
        if (checksum_calculado != (unsigned char)checksum_recebido) {
            /* checksum não bate: pacote corrompido, descartado
             * silenciosamente -- exatamente como uma rede de
             * verdade faria. O servidor vai perceber pelo timeout. */
            printf("[COZINHA] Pacote corrompido descartado (seq=%d).\n", seq_recebido);
            continue;
        }

        EstadoCliente *ec = busca_ou_cria_cliente(&cli);
        if (ec == NULL) {
            continue;
        }

        if (seq_recebido == ec->ultimo_seq) {
            /* Pedido duplicado (retransmissão por timeout, ACK que se
             * perdeu no caminho de volta, etc.): não processa de
             * novo, só reenvia a resposta que já tínhamos mandado. */
            printf("[COZINHA] Pedido duplicado (seq=%d), reenviando resposta.\n", seq_recebido);
            capenga_sendto(sockfd, ec->ultima_resposta, strlen(ec->ultima_resposta), 0,
                            (struct sockaddr *)&cli, cli_len);
            continue;
        }

        /* Pedido novo: "prepara" (aqui, só imprime) e responde PRONTO */
        printf("[COZINHA] Preparando pedido (seq=%d): %s\n", seq_recebido, resto);

        char corpo_resposta[] = "PRONTO";
        unsigned char cksum_resp = capenga_checksum(corpo_resposta, strlen(corpo_resposta));

        char resposta[BUF_SIZE];
        snprintf(resposta, sizeof(resposta), "%d %u %s",
                 seq_recebido, (unsigned int)cksum_resp, corpo_resposta);

        ec->ultimo_seq = seq_recebido;
        strncpy(ec->ultima_resposta, resposta, sizeof(ec->ultima_resposta) - 1);

        capenga_sendto(sockfd, resposta, strlen(resposta), 0,
                        (struct sockaddr *)&cli, cli_len);
    }

    return 0;
}
