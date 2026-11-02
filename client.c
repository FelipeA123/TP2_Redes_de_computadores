/*
 * client.c - Cliente interativo para o "Boteco do Zé"
 *
 * Conecta ao servidor via TCP, lê comandos via stdin e imprime respostas de stdout.
 */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define MAX_BUFFER 256

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Uso: %s <endereço_ip> <porta>\n", argv[0]);
        return 1;
    }

    const char *ip_servidor = argv[1];
    int porta = atoi(argv[2]);

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)porta);

    if (inet_pton(AF_INET, ip_servidor, &addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(sockfd);
        return 1;
    }

    if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            perror("connect");
            close(sockfd);
            return 1;
        }

    FILE *stream = fdopen(sockfd, "r+");
    if (!stream) {
        perror("fdopen");
        close(sockfd);
        return 1;
    }

    setvbuf(stdout, NULL, _IOLBF, 0);

    printf("Conectado ao Boteco do Zé!\n");

    /* 
     * Lê e exibe avisos do servidor ao conectar:
     * - Se não houver mesas, exibe "SEM MESA DISPONIVEL..."
     * - Assim que a mesa for liberada pelo servidor, lê a 2ª linha ("MESA LIBERADA!...")
     */
    char msg_inicial[MAX_BUFFER];
    
    /* 1. Tenta ler a mensagem inicial (aviso de sem mesa ou confirmação) */
    if (fgets(msg_inicial, sizeof(msg_inicial), stream) != NULL) {
        printf("%s", msg_inicial);

        /* Se recebeu aviso de espera, bloqueia no fgets aguardando a liberação */
        if (strstr(msg_inicial, "SEM MESA") != NULL) {
            if (fgets(msg_inicial, sizeof(msg_inicial), stream) != NULL) {
                printf("%s", msg_inicial);
            }
        }
    }

    printf("Digite os comandos (CARDAPIO, PEDIR <ITEM> <QTD>, CONTA, SAIR):\n");

    /* Segue para o loop principal de comandos (fgets do stdin) */
    char input[MAX_BUFFER];
    char resp[MAX_BUFFER];

    while (1) {
        printf("> ");
        fflush(stdout);

        if (fgets(input, sizeof(input), stdin) == NULL) {
            break; /* EOF ou Ctrl+D */
        }

        /* Remove quebra de linha */
        input[strcspn(input, "\r\n")] = '\0';
        if (strlen(input) == 0) continue;

        /* Envia o comando ao servidor */
        fprintf(stream, "%s\n", input);
        fflush(stream);

        /* Extrai primeiro token do comando para saber como tratar a resposta */
        char cmd_copy[MAX_BUFFER];
        strncpy(cmd_copy, input, sizeof(cmd_copy) - 1);
        char *cmd = strtok(cmd_copy, " ");

        if (cmd && strcasecmp(cmd, "CARDAPIO") == 0) {
            /* O cardápio retorna exatamente 5 linhas */
            for (int i = 0; i < 5; i++) {
                if (fgets(resp, sizeof(resp), stream) != NULL) {
                    printf("%s", resp);
                }
            }
        } else {
            /* Outros comandos retornam exatamente 1 linha */
            if (fgets(resp, sizeof(resp), stream) != NULL) {
                printf("%s", resp);

                if (cmd && strcasecmp(cmd, "SAIR") == 0) {
                    break;
                }
            } else {
                printf("Conexão encerrada pelo servidor.\n");
                break;
            }
        }
    }

    fclose(stream);
    return 0;
}
