/*
 * rede_capenga.h
 *
 * Biblioteca fornecida para o TP2 (O Boteco do Zé).
 * Simula um enlace de rede ruim (perda, corrupção, duplicação e atraso)
 * por cima de um socket UDP normal, para que a comunicação
 * servidor <-> cozinha exija uma camada de confiabilidade construída
 * por vocês (números de sequência, checksum, timeout e retransmissão).
 *
 * NÃO MODIFIQUE ESTE ARQUIVO. Ele é parte da infraestrutura fornecida
 * e será substituído pela versão original na correção.
 */
#ifndef REDE_CAPENGA_H
#define REDE_CAPENGA_H

#include <sys/types.h>
#include <sys/socket.h>
#include <stddef.h>

/*
 * Configura as probabilidades de falha da rede simulada.
 *   perda        - probabilidade (0.0 a 1.0) de um pacote enviado sumir
 *   corrupcao    - probabilidade (0.0 a 1.0) de um byte do pacote ser
 *                  corrompido antes do envio
 *   duplicacao   - probabilidade (0.0 a 1.0) de um pacote ser enviado
 *                  duas vezes
 *   atraso_max_ms - atraso aleatório (0 a atraso_max_ms) antes de cada
 *                   envio, simulando jitter
 *
 * Chame uma vez no início do programa, antes de enviar qualquer coisa.
 */
void capenga_config(double perda, double corrupcao, double duplicacao,
                     int atraso_max_ms);

/* Cria um socket UDP comum (equivalente a socket(AF_INET, SOCK_DGRAM, 0)). */
int capenga_socket(void);

/*
 * Envia dados simulando as falhas configuradas em capenga_config().
 * Mesma assinatura e mesmo comportamento de retorno de sendto():
 * do ponto de vista de quem chama, não há como saber se o pacote
 * realmente chegou ou não — exatamente como uma rede de verdade.
 */
ssize_t capenga_sendto(int sockfd, const void *buf, size_t len, int flags,
                        const struct sockaddr *dest_addr, socklen_t addrlen);

/*
 * Recebe dados normalmente. A simulação de falhas ocorre inteiramente
 * no envio (capenga_sendto); esta função é um repasse direto para
 * recvfrom().
 */
ssize_t capenga_recvfrom(int sockfd, void *buf, size_t len, int flags,
                          struct sockaddr *src_addr, socklen_t *addrlen);

/*
 * Checksum simples (soma dos bytes módulo 256) para os dados fornecidos.
 * Utilitário livre para uso na detecção de corrupção — vocês decidem
 * onde e como aplicá-lo no protocolo.
 */
unsigned char capenga_checksum(const void *dados, size_t len);

#endif /* REDE_CAPENGA_H */
