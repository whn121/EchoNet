#!/usr/bin/env python3
"""
Connection 生命周期测试
覆盖 4 种关闭场景, 验证服务器不崩溃、无内存泄漏、日志正确
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

HEARTBEAT_REQ = 0x0C
HEARTBEAT_RESP = 0x0D

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
    """场景 1: 客户端主动关闭"""
    print("\n[场景 1] 客户端主动关闭")
    sock = socket.socket()
    sock.connect((HOST, PORT))
    login(sock)
    print("  已登录, 主动 close")
    sock.close()
    time.sleep(0.5)
    print("  [OK] 服务器应通过 recv 返回 0 触发 handleClose")

def test_2_server_close():
    """场景 2: 服务器主动关闭(超时踢人或心跳超时)"""
    print("\n[场景 2] 服务器主动关闭(模拟心跳超时)")
    sock = socket.socket()
    sock.connect((HOST, PORT))
    login(sock)
    print("  已登录, 停止发心跳, 等待服务器踢人(60秒)")
    print("  [INFO] 此场景需要服务器端心跳超时触发, 跳过实时等待")
    sock.close()

def test_3_rst_close():
    """场景 3: RST 强制关闭"""
    print("\n[场景 3] 客户端 RST 强制关闭")
    sock = socket.socket()
    sock.connect((HOST, PORT))
    login(sock)

    # 设置 SO_LINGER 为 0, close 时发送 RST
    l_onoff = 1
    l_linger = 0
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER,
                    struct.pack('ii', l_onoff, l_linger))

    print("  已登录, 发送 RST")
    sock.close()
    time.sleep(0.5)
    print("  [OK] 服务器应通过 recv 返回 -1 (ECONNRESET) 触发 handleClose")

def test_4_half_close():
    """场景 4: 半关闭 SHUT_WR"""
    print("\n[场景 4] 客户端半关闭 (shutdown SHUT_WR)")
    sock = socket.socket()
    sock.connect((HOST, PORT))
    login(sock)

    print("  已登录, 调用 shutdown(SHUT_WR)")
    sock.shutdown(socket.SHUT_WR)
    time.sleep(0.5)
    print("  [OK] 服务器应通过 EPOLLRDHUP 感知半关闭")

    sock.close()

def test_5_half_close_before_response():
    """发送完整请求后立即半关闭, 再等待服务端响应"""
    print("\n[场景 5] 收到响应前立即半关闭")

    sock = socket.socket()
    sock.settimeout(3)

    try:
        sock.connect((HOST, PORT))

        # 发送完整请求, 但先不等待服务端响应。
        sock.sendall(encode(LOGIN_REQ, 5001, "whn|278813"))

        # 立即关闭客户端发送方向。
        sock.shutdown(socket.SHUT_WR)

        # 服务端仍应处理已接收的请求并返回响应。
        header = recv_full(sock, 4, timeout=3)

        if header is None:
            raise AssertionError("服务端在返回响应前关闭了连接")

        body_len = struct.unpack(">I", header)[0]
        body = recv_full(sock, body_len, timeout=3)

        if body is None or len(body) != body_len:
            raise AssertionError("服务端响应不完整")

        msg_type = struct.unpack(">H", body[:2])[0]

        if msg_type != LOGIN_RESP:
            raise AssertionError(
                f"响应类型错误：期望 {LOGIN_RESP:#x}, "
                f"实际 {msg_type:#x}"
            )

        print("[PASS] 半关闭后仍收到完整登录响应")

        # 响应收完后, 检查服务端是否完成关闭。
        tail = sock.recv(1)

        if tail != b"":
            raise AssertionError(
                "服务端发送完响应后仍有额外数据, "
                "尚未观察到有序 EOF"
            )

        print("[PASS] 收到完整响应后, 服务端最终返回 EOF")

    finally:
        sock.close()


def test_6_multiple_requests_then_half_close():
    """一次发送两个完整请求，再立即关闭发送方向。"""
    print("\n[场景 6] 多个完整请求后立即半关闭")

    sock = socket.socket()
    sock.settimeout(3)

    try:
        sock.connect((HOST, PORT))

        login_request_id = 6101
        heartbeat_request_id = 6102

        # 先登录，再发送心跳请求。
        # 两个完整协议帧一次性发出，测试拆包与单连接 FIFO。
        requests = (
            encode(LOGIN_REQ, login_request_id, "whn|278813")
            + encode(HEARTBEAT_REQ, heartbeat_request_id, "")
        )
        sock.sendall(requests)

        # 两个完整请求都已发送，之后不再发送任何数据。
        sock.shutdown(socket.SHUT_WR)

        expected = [
            (LOGIN_RESP, login_request_id),
            (HEARTBEAT_RESP, heartbeat_request_id),
        ]

        for expected_type, expected_id in expected:
            header = recv_full(sock, 4, timeout=3)

            if header is None:
                raise AssertionError(
                    f"等待 request_id={expected_id} 的响应时提前 EOF"
                )

            body_len = struct.unpack(">I", header)[0]
            body = recv_full(sock, body_len, timeout=3)

            if body is None or len(body) != body_len:
                raise AssertionError(
                    f"request_id={expected_id} 的响应不完整"
                )

            if len(body) < 6:
                raise AssertionError("响应帧长度不足")

            msg_type = struct.unpack(">H", body[:2])[0]
            request_id = struct.unpack(">I", body[2:6])[0]

            if msg_type != expected_type:
                raise AssertionError(
                    f"响应类型错误：期望 {expected_type:#x}，"
                    f"实际 {msg_type:#x}"
                )

            if request_id != expected_id:
                raise AssertionError(
                    f"响应 ID 错误：期望 {expected_id}，"
                    f"实际 {request_id}"
                )

        print("[PASS] 两个完整请求均收到对应响应，且顺序正确")

        # 所有响应读取完成后，应观察到服务端有序关闭。
        tail = sock.recv(1)

        if tail != b"":
            raise AssertionError("响应结束后未观察到服务端 EOF")

        print("[PASS] 两个响应发送完毕后，服务端返回 EOF")

    finally:
        sock.close()


def test_7_incomplete_tail_frame_then_half_close():
    """完整请求后附带不完整尾帧，再立即半关闭。"""
    print("\n[场景 7] 完整请求 + 不完整尾帧 + 半关闭")

    sock = socket.socket()
    sock.settimeout(3)

    login_request_id = 7101

    try:
        sock.connect((HOST, PORT))

        # 一个完整登录请求。
        login_frame = encode(
            LOGIN_REQ, login_request_id, "whn|278813"
        )

        # 一个不完整的心跳请求帧：故意截掉最后 2 字节。
        heartbeat_frame = encode(
            HEARTBEAT_REQ, 7102, ""
        )
        incomplete_frame = heartbeat_frame[:-2]

        # 连续发送：完整登录请求 + 不完整心跳帧。
        sock.sendall(login_frame + incomplete_frame)

        # 告知服务端不会再有更多数据。
        sock.shutdown(socket.SHUT_WR)

        # 完整登录请求仍然必须得到响应。
        header = recv_full(sock, 4, timeout=3)

        if header is None:
            raise AssertionError(
                "服务端在处理完整登录请求前就关闭了连接"
            )

        body_len = struct.unpack(">I", header)[0]
        body = recv_full(sock, body_len, timeout=3)

        if body is None or len(body) != body_len:
            raise AssertionError("登录响应不完整")

        if len(body) < 6:
            raise AssertionError("登录响应帧长度不足")

        msg_type = struct.unpack(">H", body[:2])[0]
        request_id = struct.unpack(">I", body[2:6])[0]

        if msg_type != LOGIN_RESP:
            raise AssertionError(
                f"响应类型错误：期望 {LOGIN_RESP:#x}，"
                f"实际 {msg_type:#x}"
            )

        if request_id != login_request_id:
            raise AssertionError(
                f"请求 ID 错误：期望 {login_request_id}，"
                f"实际 {request_id}"
            )

        print("[PASS] 完整登录请求仍然得到正确响应")

        # 不完整的心跳帧不应产生响应；服务端应完成关闭。
        tail = sock.recv(1)

        if tail != b"":
            raise AssertionError(
                "关闭前收到额外数据，检查不完整尾帧处理"
            )

        print("[PASS] 不完整尾帧未阻止连接正常关闭")

    finally:
        sock.close()

def test_8_rst_during_business():
    print("\n[场景 8] 业务执行中发生 RST")

    sock = socket.socket()

    try:
        sock.connect((HOST, PORT))

        # 发完整请求，但不等待业务响应。
        sock.sendall(encode(LOGIN_REQ, 8101, "whn|278813"))

        # 留出时间让服务端收到请求并开始执行慢业务。
        time.sleep(0.5)

        # 强制 RST 关闭。
        sock.setsockopt(
            socket.SOL_SOCKET,
            socket.SO_LINGER,
            struct.pack("ii", 1, 0)
        )
        sock.close()
        sock = None

        # 等待服务端完成延迟业务和最终关闭。
        time.sleep(3)

        print("[INFO] 请检查服务端日志是否触发延迟关闭分支")

    finally:
        if sock is not None:
            sock.close()


def main():
    print("=" * 60)
    print("Connection 生命周期测试")
    print("=" * 60)

    test_1_client_close()
    test_2_server_close()
    test_3_rst_close()
    test_4_half_close()
    test_5_half_close_before_response()  # 新增: 响应返回前立即半关闭
    test_6_multiple_requests_then_half_close()
    test_7_incomplete_tail_frame_then_half_close()
    test_8_rst_during_business()

    print("\n" + "=" * 60)
    print("7 个场景（场景 2 跳过实时等待）")
    print("请检查服务器日志：")
    print("  - 没有重复关闭")
    print("  - 没有崩溃")
    print("  - 半关闭后完整请求仍能收到响应")
    print("  - 不完整尾帧不会阻止连接完成关闭")
    print("=" * 60)


if __name__ == '__main__':
    main()
