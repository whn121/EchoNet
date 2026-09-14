import socket, struct, time

HOST = '127.0.0.1'
PORT = 8080

def encode(msg_type, req_id, payload=""):
    payload_b = payload.encode()
    body_len = 2 + 4 + len(payload_b)
    return struct.pack('>I H I', body_len, msg_type, req_id) + payload_b

def recv_full(sock, n):
    data = b''
    while len(data) < n:
        chunk = sock.recv(n - len(data))
        if not chunk:
            break
        data += chunk
    return data

s = socket.socket()
s.connect((HOST, PORT))

# 登录 whn
s.sendall(encode(0x01, 1, "whn|278813"))
header = recv_full(s, 4)
if header:
    body_len = struct.unpack('>I', header)[0]
    body = recv_full(s, body_len)
    print("登录响应:", body.decode())

print("连接保持中，请去另一个终端查询 Redis...")
time.sleep(60)   # 保持 60 秒
s.close()
