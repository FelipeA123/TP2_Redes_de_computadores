/*
 * client.c - Cliente interativo para o "Boteco do Zé"
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

    char mensagem_inicial[MAX_BUFFER];
    if (fgets(mensagem_inicial, sizeof(mensagem_inicial), stream) != NULL) {
        printf("%s", mensagem_inicial);

        /* O servidor envia outra mensagem quando uma mesa é liberada. */
        if (strstr(mensagem_inicial, "SEM MESA") != NULL) {
            if (fgets(mensagem_inicial, sizeof(mensagem_inicial), stream) != NULL) {
                printf("%s", mensagem_inicial);
            }
        }
    }

    printf("Digite os comandos (CARDAPIO, PEDIR <ITEM> <QTD>, CONTA, SAIR):\n");

    char input[MAX_BUFFER];
    char resp[MAX_BUFFER];

    while (1) {
        printf("> ");
        fflush(stdout);

        if (fgets(input, sizeof(input), stdin) == NULL) {
            break;
        }

        input[strcspn(input, "\r\n")] = '\0';
        if (strlen(input) == 0) continue;

        fprintf(stream, "%s\n", input);
        fflush(stream);

        char copia_comando[MAX_BUFFER];
        snprintf(copia_comando, sizeof(copia_comando), "%s", input);
        char *comando = strtok(copia_comando, " ");

        if (comando && strcasecmp(comando, "CARDAPIO") == 0) {
            /* Quantidade de linhas definida pelo protocolo do servidor. */
            for (int i = 0; i < 5; i++) {
                if (fgets(resp, sizeof(resp), stream) != NULL) {
                    printf("%s", resp);
                }
            }
        } else {
            if (fgets(resp, sizeof(resp), stream) != NULL) {
                printf("%s", resp);

                if (comando && strcasecmp(comando, "SAIR") == 0) {
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
