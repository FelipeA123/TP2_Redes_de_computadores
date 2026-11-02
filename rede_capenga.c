/*
 * rede_capenga.c
 *
 * Implementação da rede capenga do Boteco do Zé. Veja rede_capenga.h
 * para a documentação da API. NÃO MODIFIQUE ESTE ARQUIVO.
 */
#define _DEFAULT_SOURCE

#include "rede_capenga.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

static double g_perda = 0.0;
static double g_corrupcao = 0.0;
static double g_duplicacao = 0.0;
static int g_atraso_max_ms = 0;
static int g_seeded = 0;

static void seed_uma_vez(void) {
    if (!g_seeded) {
        srand((unsigned int)time(NULL) ^ (unsigned int)getpid());
        g_seeded = 1;
    }
}

static double sorteio(void) {
    return (double)rand() / (double)RAND_MAX;
}

void capenga_config(double perda, double corrupcao, double duplicacao,
                     int atraso_max_ms) {
    g_perda = perda;
    g_corrupcao = corrupcao;
    g_duplicacao = duplicacao;
    g_atraso_max_ms = atraso_max_ms;
    seed_uma_vez();
}

int capenga_socket(void) {
    return socket(AF_INET, SOCK_DGRAM, 0);
}

static ssize_t enviar_de_verdade(int sockfd, const void *buf, size_t len,
                                  int flags, const struct sockaddr *dest_addr,
                                  socklen_t addrlen) {
    return sendto(sockfd, buf, len, flags, dest_addr, addrlen);
}

ssize_t capenga_sendto(int sockfd, const void *buf, size_t len, int flags,
                        const struct sockaddr *dest_addr, socklen_t addrlen) {
    seed_uma_vez();

    /* Atraso simulado (jitter) */
    if (g_atraso_max_ms > 0) {
        int atraso = rand() % (g_atraso_max_ms + 1);
        if (atraso > 0) {
            usleep((useconds_t)atraso * 1000);
        }
    }

    /* Perda: finge que enviou, mas nao envia nada de verdade.
     * Quem chamou nao tem como distinguir isso de um envio real que
     * se perdeu na rede depois -- e essa e a gracinha. */
    if (sorteio() < g_perda) {
        return (ssize_t)len;
    }

    /* Corrupcao: inverte os bits de um byte aleatorio antes de enviar */
    if (g_corrupcao > 0.0 && sorteio() < g_corrupcao && len > 0) {
        char *copia = malloc(len);
        if (copia != NULL) {
            memcpy(copia, buf, len);
            size_t pos = (size_t)rand() % len;
            copia[pos] ^= 0xFF;
            ssize_t n = enviar_de_verdade(sockfd, copia, len, flags,
                                           dest_addr, addrlen);
            free(copia);
            return n;
        }
    }

    ssize_t n = enviar_de_verdade(sockfd, buf, len, flags, dest_addr, addrlen);

    /* Duplicacao: manda de novo, de proposito */
    if (sorteio() < g_duplicacao) {
        enviar_de_verdade(sockfd, buf, len, flags, dest_addr, addrlen);
    }

    return n;
}

ssize_t capenga_recvfrom(int sockfd, void *buf, size_t len, int flags,
                          struct sockaddr *src_addr, socklen_t *addrlen) {
    return recvfrom(sockfd, buf, len, flags, src_addr, addrlen);
}

unsigned char capenga_checksum(const void *dados, size_t len) {
    const unsigned char *p = dados;
    unsigned char soma = 0;
    for (size_t i = 0; i < len; i++) {
        soma = (unsigned char)(soma + p[i]);
    }
    return soma;
}
