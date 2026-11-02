import socket
import threading
import time

# Configurações do Servidor
HOST = '127.0.0.1'
PORTA = 8080

# Parâmetros do Teste
NUM_CLIENTES = 10  # Número de threads enviando pedidos simultaneamente
ITEM_PEDIDO = "CAIPIRINHA"  # Estoque inicial é 10!
QTD = 1

def cliente_worker(id_cliente):
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect((HOST, PORTA))
        
        # Lê a mensagem inicial (boas-vindas ou aviso de sem mesa)
        msg_inicial = s.recv(256).decode('utf-8')
        print(f"[Cliente {id_cliente:02d}] Conectou -> {msg_inicial.strip()}")
        
        # Se ficou sem mesa, lê a 2ª linha quando a mesa for liberada
        if "SEM MESA" in msg_inicial:
            msg_liberado = s.recv(256).decode('utf-8')
            print(f"[Cliente {id_cliente:02d}] Liberado -> {msg_liberado.strip()}")

        # Envia o pedido
        comando = f"PEDIR {ITEM_PEDIDO} {QTD}\n"
        s.sendall(comando.encode('utf-8'))
        
        # Recebe o resultado do pedido (OK, ESGOTADO, ERRO COZINHA)
        resposta = s.recv(256).decode('utf-8')
        print(f"[Cliente {id_cliente:02d}] Resposta: {resposta.strip()}")

        # Envia SAIR para liberar a comanda e a mesa no semáforo
        s.sendall(b"SAIR\n")
        s.recv(256)
        s.close()
        
    except Exception as e:
        print(f"[Cliente {id_cliente:02d}] Erro de conexão: {e}")

# Dispara todas as threads ao mesmo tempo
threads = []
print(f"--- INICIANDO TESTE DE CONCORRÊNCIA COM {NUM_CLIENTES} CLIENTES ---")
tempo_inicio = time.time()

for i in range(1, NUM_CLIENTES + 1):
    t = threading.Thread(target=cliente_worker, args=(i,))
    threads.append(t)
    t.start()

for t in threads:
    t.join()

print(f"--- TESTE FINALIZADO EM {time.time() - tempo_inicio:.2f}s ---")
