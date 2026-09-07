import socket
import struct
import time

# 消息类型枚举
class MsgType:
    LOGIN_REQ = 0x01
    LOGIN_RESP = 0x02
    CREATE_ROOM_REQ = 0x03
    CREATE_ROOM_RESP = 0x04
    JOIN_ROOM_REQ = 0x05
    JOIN_ROOM_RESP = 0x06
    LEAVE_ROOM_REQ = 0x07
    LEAVE_ROOM_RESP = 0x08
    SEND_MSG_REQ = 0x09
    SEND_MSG_RESP = 0x0A
    BROADCAST_MSG = 0x0B
    HEARTBEAT_REQ = 0x0C
    HEARTBEAT_RESP = 0x0D
    ERROR_RESP = 0x0E

def encode_message(msg_type, request_id, payload=""):
    payload_bytes = payload.encode('utf-8')
    body_len = 2 + 4 + len(payload_bytes)
    # 大端打包
    header = struct.pack('>I H I', body_len, msg_type, request_id)
    return header + payload_bytes

def decode_message(data):
    if len(data) < 4:
        return None
    body_len = struct.unpack('>I', data[:4])[0]
    if len(data) < 4 + body_len:
        return None
    msg_type = struct.unpack('>H', data[4:6])[0]
    request_id = struct.unpack('>I', data[6:10])[0]
    payload = data[10:4+body_len].decode('utf-8', errors='ignore')
    return (msg_type, request_id, payload)

def recv_full(sock, expected_len):
    """确保收满 expected_len 字节"""
    data = b''
    while len(data) < expected_len:
        chunk = sock.recv(expected_len - len(data))
        if not chunk:
            break
        data += chunk
    return data

def test_chat():
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect(('127.0.0.1', 8080))

    # 1. 登录
    req_id = 1
    sock.sendall(encode_message(MsgType.LOGIN_REQ, req_id, "whn|278813"))
    header = recv_full(sock, 4)
    if not header:
        print("连接关闭")
        return
    body_len = struct.unpack('>I', header)[0]
    body = recv_full(sock, body_len)
    msg_type, _, payload = decode_message(header + body)
    print(f"登录响应: type={msg_type:#x}, payload={payload}")
    assert msg_type == MsgType.LOGIN_RESP and payload == "OK"

    # 2. 创建房间
    req_id += 1
    sock.sendall(encode_message(MsgType.CREATE_ROOM_REQ, req_id, "room1"))
    header = recv_full(sock, 4)
    body_len = struct.unpack('>I', header)[0]
    body = recv_full(sock, body_len)
    msg_type, _, payload = decode_message(header + body)
    print(f"创建房间响应: type={msg_type:#x}, payload={payload}")
    room_id = int(payload)
    assert msg_type == MsgType.CREATE_ROOM_RESP

    # 3. 加入房间
    req_id += 1
    sock.sendall(encode_message(MsgType.JOIN_ROOM_REQ, req_id, str(room_id)))
    header = recv_full(sock, 4)
    body_len = struct.unpack('>I', header)[0]
    body = recv_full(sock, body_len)
    msg_type, _, payload = decode_message(header + body)
    print(f"加入房间响应: type={msg_type:#x}, payload={payload}")
    assert msg_type == MsgType.JOIN_ROOM_RESP

    # 4. 第二个客户端登录并加入同一房间
    sock2 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock2.connect(('127.0.0.1', 8080))
    req_id2 = 1
    sock2.sendall(encode_message(MsgType.LOGIN_REQ, req_id2, "whnzs|278813"))
    header = recv_full(sock2, 4)
    body_len = struct.unpack('>I', header)[0]
    body = recv_full(sock2, body_len)
    msg_type, _, payload = decode_message(header + body)
    print(f"客户端2登录响应: {payload}")
    req_id2 += 1
    sock2.sendall(encode_message(MsgType.JOIN_ROOM_REQ, req_id2, str(room_id)))
    header = recv_full(sock2, 4)
    body_len = struct.unpack('>I', header)[0]
    body = recv_full(sock2, body_len)
    msg_type, _, payload = decode_message(header + body)
    print(f"客户端2加入房间响应: {payload}")

    # 5. 客户端1发送消息
    req_id += 1
    sock.sendall(encode_message(MsgType.SEND_MSG_REQ, req_id, "Hello from whn"))
    # 客户端1收到确认
    header = recv_full(sock, 4)
    body_len = struct.unpack('>I', header)[0]
    body = recv_full(sock, body_len)
    msg_type, _, payload = decode_message(header + body)
    print(f"发送消息确认: type={msg_type:#x}, payload={payload}")
    # 客户端1收到广播（自己也会收到）
    header = recv_full(sock, 4)
    body_len = struct.unpack('>I', header)[0]
    body = recv_full(sock, body_len)
    msg_type, _, payload = decode_message(header + body)
    print(f"客户端1收到广播: type={msg_type:#x}, payload={payload}")

    # 客户端2收到广播
    header = recv_full(sock2, 4)
    body_len = struct.unpack('>I', header)[0]
    body = recv_full(sock2, body_len)
    msg_type, _, payload = decode_message(header + body)
    print(f"客户端2收到广播: type={msg_type:#x}, payload={payload}")

    # 6. 心跳测试
    req_id += 1
    sock.sendall(encode_message(MsgType.HEARTBEAT_REQ, req_id, ""))
    header = recv_full(sock, 4)
    body_len = struct.unpack('>I', header)[0]
    body = recv_full(sock, body_len)
    msg_type, _, payload = decode_message(header + body)
    print(f"心跳响应: type={msg_type:#x}")

    # 关闭连接
    sock.close()
    sock2.close()

if __name__ == '__main__':
    test_chat()
