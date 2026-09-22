#!/usr/bin/env python3
"""
Connection 生命周期测试
覆盖 4 种关闭场景，验证服务器不崩溃、无内存泄漏、日志正确
"""

import socket
import struct
import time
import sys

HOST = '127.0.0.1'
PORT = 8080

# 消息类型
LOGIN_REQ = 0x01
LOGIN_RESP = 0x02

def encode(msg_type, req_id, payload=""):
    payload_b = payload.encode('utf-8')
    body_len = 2 + 4 + len(payload_b)
    return struct.pack('>I H I', body_len, msg_type, req_id) + payload_b

def recv_full(sock, n, timeout=3):
    sock.settimeout(timeout)
    data = b''
    while len(data) < n:
        chunk = sock.recv(n - len(data))
        if not chunk:
            return None
        data += chunk
    return data

def login(sock):
    sock.sendall(encode(LOGIN_REQ, 1, "whn|278813"))
    header = recv_full(sock, 4)
    if not header:
        return False
    body_len = struct.unpack('>I', header)[0]
    body = recv_full(sock, body_len)
    return body is not None and body[0:2] == b'\x00\x02'

def test_1_client_close():
    """场景 1：客户端主动关闭"""
    print("\n[场景 1] 客户端主动关闭")
    sock = socket.socket()
    sock.connect((HOST, PORT))
    login(sock)
    print("  已登录，主动 close")
    sock.close()
    time.sleep(0.5)
    print("  [OK] 服务器应通过 recv 返回 0 触发 handleClose")

def test_2_server_close():
    """场景 2：服务器主动关闭（超时踢人或心跳超时）"""
    print("\n[场景 2] 服务器主动关闭（模拟心跳超时）")
    sock = socket.socket()
    sock.connect((HOST, PORT))
    login(sock)
    print("  已登录，停止发心跳，等待服务器踢人（60秒）")
    print("  [INFO] 此场景需要服务器端心跳超时触发，跳过实时等待")
    sock.close()

def test_3_rst_close():
    """场景 3：RST 强制关闭"""
    print("\n[场景 3] 客户端 RST 强制关闭")
    sock = socket.socket()
    sock.connect((HOST, PORT))
    login(sock)

    # 设置 SO_LINGER 为 0，close 时发送 RST
    l_onoff = 1
    l_linger = 0
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER,
                    struct.pack('ii', l_onoff, l_linger))

    print("  已登录，发送 RST")
    sock.close()
    time.sleep(0.5)
    print("  [OK] 服务器应通过 recv 返回 -1 (ECONNRESET) 触发 handleClose")

def test_4_half_close():
    """场景 4：半关闭 SHUT_WR"""
    print("\n[场景 4] 客户端半关闭 (shutdown SHUT_WR)")
    sock = socket.socket()
    sock.connect((HOST, PORT))
    login(sock)

    print("  已登录，调用 shutdown(SHUT_WR)")
    sock.shutdown(socket.SHUT_WR)
    time.sleep(0.5)
    print("  [OK] 服务器应通过 EPOLLRDHUP 感知半关闭")

    sock.close()

def main():
    print("=" * 60)
    print("Connection 生命周期测试")
    print("=" * 60)

    test_1_client_close()
    test_2_server_close()
    test_3_rst_close()
    test_4_half_close()

    print("\n" + "=" * 60)
    print("4 个场景测试完成")
    print("请检查服务器日志：")
    print("  - 每个场景触发一次 handleClose")
    print("  - 没有重复关闭")
    print("  - 没有崩溃")
    print("=" * 60)

if __name__ == '__main__':
    main()
