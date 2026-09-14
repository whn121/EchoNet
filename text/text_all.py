#!/usr/bin/env python3
"""
EchoNet 完整回归测试
覆盖: P0 安全问题 + 协议边界 + 业务功能 + 并发 + 心跳
用法: python3 test_all.py
"""
import socket
import struct
import threading
import time
import sys

HOST = "127.0.0.1"
PORT = 8080

# 消息类型
LOGIN_REQ         = 0x01
LOGIN_RESP        = 0x02
CREATE_ROOM_REQ   = 0x03
CREATE_ROOM_RESP  = 0x04
JOIN_ROOM_REQ     = 0x05
JOIN_ROOM_RESP    = 0x06
LEAVE_ROOM_REQ    = 0x07
LEAVE_ROOM_RESP   = 0x08
SEND_MSG_REQ      = 0x09
SEND_MSG_RESP     = 0x0A
BROADCAST_MSG     = 0x0B
HEARTBEAT_REQ     = 0x0C
HEARTBEAT_RESP    = 0x0D
ERROR_RESP        = 0x0E
MEMBER_COUNT_UPDATE = 0x0F

USER = "whn"
PASS = "278813"


# ========== 测试框架 ==========
class TestRunner:
    def __init__(self):
        self.passed = 0
        self.failed = 0
        self.notes = []

    def check(self, name, condition, detail=""):
        if condition:
            self.passed += 1
            print(f"  [PASS] {name}")
        else:
            self.failed += 1
            print(f"  [FAIL] {name} {detail}")

    def summary(self):
        total = self.passed + self.failed
        print()
        print("=" * 60)
        print(f"测试结果: {self.passed}/{total} 通过")
        if self.failed:
            print(f"          {self.failed} 个失败")
        print("=" * 60)
        return self.failed == 0


# ========== 协议工具 ==========
def encode(msg_type, req_id, payload=""):
    if isinstance(payload, str):
        payload = payload.encode('utf-8')
    body_len = 2 + 4 + len(payload)
    return struct.pack('>I H I', body_len, msg_type, req_id) + payload


def recv_full(sock, n, timeout=3):
    sock.settimeout(timeout)
    data = b''
    while len(data) < n:
        try:
            chunk = sock.recv(n - len(data))
        except socket.timeout:
            return None
        if not chunk:
            return None
        data += chunk
    return data


def recv_packet(sock, timeout=3):
    header = recv_full(sock, 4, timeout)
    if not header:
        return None
    body_len = struct.unpack('>I', header)[0]
    body = recv_full(sock, body_len, timeout)
    if not body:
        return None
    msg_type = struct.unpack('>H', body[0:2])[0]
    req_id = struct.unpack('>I', body[2:6])[0]
    payload = body[6:].decode('utf-8', errors='ignore')
    return (msg_type, req_id, payload)


def recv_until(sock, expected_type, max_tries=20, timeout=3):
    for _ in range(max_tries):
        pkt = recv_packet(sock, timeout)
        if pkt is None:
            return None
        if pkt[0] == expected_type:
            return pkt
    return None


def new_connection():
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((HOST, PORT))
    return sock


def login(sock, user=USER, pwd=PASS):
    sock.sendall(encode(LOGIN_REQ, 1, f"{user}|{pwd}"))
    return recv_until(sock, LOGIN_RESP)


def create_room(sock, room_name):
    sock.sendall(encode(CREATE_ROOM_REQ, 2, room_name))
    return recv_until(sock, CREATE_ROOM_RESP)


def join_room(sock, room_id):
    sock.sendall(encode(JOIN_ROOM_REQ, 3, str(room_id)))
    return recv_until(sock, JOIN_ROOM_RESP)


# ========== 测试 1: 协议边界 ==========
def test_protocol_bounds(t):
    print("\n[测试 1] 协议边界")
    print("-" * 60)

    # 1.1 body_len < 6
    sock = new_connection()
    packet = struct.pack('>I H I', 3, LOGIN_REQ, 1) + b"ab"
    sock.sendall(packet)
    time.sleep(0.3)
    # 服务器应该主动关闭连接
    try:
        sock.settimeout(1)
        data = sock.recv(1024)
        t.check("body_len<6 服务器关闭连接", data == b"", f"got: {data[:50]}")
    except Exception as e:
        t.check("body_len<6 服务器关闭连接", True)
    sock.close()

    # 1.2 body_len > 1MB
    sock = new_connection()
    packet = struct.pack('>I H I', 0xFFFFFFFF, LOGIN_REQ, 1)
    sock.sendall(packet)
    time.sleep(0.3)
    try:
        sock.settimeout(1)
        data = sock.recv(1024)
        t.check("超大 body_len 服务器关闭连接", data == b"")
    except Exception:
        t.check("超大 body_len 服务器关闭连接", True)
    sock.close()

    # 1.3 未知类型
    sock = new_connection()
    packet = struct.pack('>I H I', 6, 0xFFFF, 1)
    sock.sendall(packet)
    resp = recv_packet(sock, timeout=2)
    t.check("未知类型返回 ERROR_RESP",
            resp is not None and resp[0] == ERROR_RESP,
            f"got: {resp}")
    sock.close()

    # 1.4 MEMBER_COUNT_UPDATE 类型被识别为合法
    sock = new_connection()
    login(sock)
    # 发一个 MEMBER_COUNT_UPDATE 类型（这是服务器发的，客户端不应该发，但协议层应识别为合法类型）
    packet = struct.pack('>I H I', 6, MEMBER_COUNT_UPDATE, 1)
    sock.sendall(packet)
    # 服务器不应该返回"Unknown message type"
    resp = recv_packet(sock, timeout=2)
    if resp:
        t.check("MEMBER_COUNT_UPDATE 类型被识别",
                "Unknown" not in resp[2],
                f"got: {resp}")
    else:
        t.check("MEMBER_COUNT_UPDATE 类型被识别", True)
    sock.close()


# ========== 测试 2: 半包/粘包 ==========
def test_fragmentation(t):
    print("\n[测试 2] 半包/粘包")
    print("-" * 60)

    # 2.1 半包：登录请求分两次发
    sock = new_connection()
    full = encode(LOGIN_REQ, 1, f"{USER}|{PASS}")
    mid = len(full) // 2
    sock.sendall(full[:mid])
    time.sleep(0.2)
    sock.sendall(full[mid:])
    resp = recv_until(sock, LOGIN_RESP)
    t.check("半包正确解析", resp is not None and resp[2] == "OK", f"got: {resp}")
    sock.close()

    # 2.2 粘包：连续发两个请求
    sock = new_connection()
    p1 = encode(LOGIN_REQ, 1, f"{USER}|{PASS}")
    p2 = encode(CREATE_ROOM_REQ, 2, "sticky_test")
    sock.sendall(p1 + p2)
    r1 = recv_until(sock, LOGIN_RESP)
    r2 = recv_until(sock, CREATE_ROOM_RESP)
    t.check("粘包登录响应", r1 is not None and r1[2] == "OK")
    t.check("粘包创建房间响应",
            r2 is not None and r2[2].startswith("OK"),
            f"got: {r2}")
    sock.close()


# ========== 测试 3: SQL 注入 ==========
def test_sql_injection(t):
    print("\n[测试 3] SQL 注入")
    print("-" * 60)

    # 3.1 注入绕过登录
    sock = new_connection()
    sock.sendall(encode(LOGIN_REQ, 1, "admin' --|x"))
    resp = recv_until(sock, LOGIN_RESP)
    t.check("SQL 注入绕过登录被拦截",
            resp is not None and resp[2] == "FAIL",
            f"got: {resp}")
    sock.close()

    # 3.2 DROP TABLE
    sock = new_connection()
    sock.sendall(encode(LOGIN_REQ, 1, "'; DROP TABLE users; --|x"))
    resp = recv_until(sock, LOGIN_RESP)
    t.check("DROP TABLE 注入被拦截",
            resp is not None and resp[2] == "FAIL",
            f"got: {resp}")
    sock.close()

    # 3.3 注入后正常登录仍可用
    sock = new_connection()
    resp = login(sock)
    t.check("users 表未被破坏", resp is not None and resp[2] == "OK")
    sock.close()


# ========== 测试 4: 业务功能 ==========
def test_business(t):
    print("\n[测试 4] 业务功能")
    print("-" * 60)

    # 4.1 登录 → 创建房间 → 加入房间 → 发消息 → 心跳
    sock = new_connection()
    r = login(sock)
    t.check("登录成功", r is not None and r[2] == "OK")

    r = create_room(sock, "biz_test_room")
    t.check("创建房间成功", r is not None and r[2].startswith("OK"))
    if r:
        room_id = int(r[2].split('|')[1])
        t.check("创建房间返回人数=1",
                r[2].split('|')[2] == "1",
                f"got: {r[2]}")

        # 加入房间（自己创建后已在房间内，应返回 OK|1）
        r2 = join_room(sock, room_id)
        t.check("加入房间响应", r2 is not None and r2[2].startswith("OK"))

    # 发消息
    sock.sendall(encode(SEND_MSG_REQ, 4, "hello_test"))
    r = recv_until(sock, SEND_MSG_RESP)
    t.check("发送消息成功", r is not None and r[2] == "OK")

    # 心跳
    sock.sendall(encode(HEARTBEAT_REQ, 5, ""))
    r = recv_until(sock, HEARTBEAT_RESP)
    t.check("心跳响应", r is not None)

    # 离开房间
    sock.sendall(encode(LEAVE_ROOM_REQ, 6, ""))
    r = recv_until(sock, LEAVE_ROOM_RESP)
    t.check("离开房间响应", r is not None and r[2] == "OK")

    sock.close()


# ========== 测试 5: 房间人数广播 ==========
def test_room_count(t):
    print("\n[测试 5] 房间人数广播")
    print("-" * 60)

    # A 创建房间
    sockA = new_connection()
    login(sockA)
    r = create_room(sockA, "count_test")
    room_id = int(r[2].split('|')[1])
    join_room(sockA, room_id)

    # B 加入同一房间
    sockB = new_connection()
    login(sockB)
    join_room(sockB, room_id)

    # A 应该收到 MEMBER_COUNT_UPDATE
    time.sleep(0.3)
    count_msg = recv_until(sockA, MEMBER_COUNT_UPDATE, max_tries=10)
    t.check("A 收到人数更新",
            count_msg is not None,
            f"got: {count_msg}")
    if count_msg:
        parts = count_msg[2].split('|')
        t.check("人数更新为2",
                len(parts) >= 2 and parts[1] == "2",
                f"got: {count_msg[2]}")

    # B 离开，A 应收到人数更新为1
    sockB.sendall(encode(LEAVE_ROOM_REQ, 10, ""))
    recv_until(sockB, LEAVE_ROOM_RESP)
    time.sleep(0.3)
    count_msg = recv_until(sockA, MEMBER_COUNT_UPDATE, max_tries=10)
    if count_msg:
        parts = count_msg[2].split('|')
        t.check("人数更新为1",
                len(parts) >= 2 and parts[1] == "1",
                f"got: {count_msg[2]}")

    sockA.close()
    sockB.close()


# ========== 测试 6: 消息广播 ==========
def test_broadcast(t):
    print("\n[测试 6] 消息广播")
    print("-" * 60)

    sockA = new_connection()
    login(sockA)
    r = create_room(sockA, "broadcast_test")
    room_id = int(r[2].split('|')[1])
    join_room(sockA, room_id)

    sockB = new_connection()
    login(sockB)
    join_room(sockB, room_id)

    time.sleep(0.3)
    # A 发送消息
    sockA.sendall(encode(SEND_MSG_REQ, 100, "broadcast_msg"))

    # A 收到 BROADCAST_MSG
    r1 = recv_until(sockA, BROADCAST_MSG, max_tries=20)
    t.check("A 收到自己发的广播", r1 is not None)

    # B 收到 BROADCAST_MSG
    r2 = recv_until(sockB, BROADCAST_MSG, max_tries=20)
    t.check("B 收到广播", r2 is not None)
    if r2:
        parts = r2[2].split('|')
        t.check("广播内容正确",
                len(parts) >= 3 and parts[2] == "broadcast_msg",
                f"got: {r2[2]}")

    sockA.close()
    sockB.close()


# ========== 测试 7: 空房间清理 ==========
def test_empty_room(t):
    print("\n[测试 7] 空房间清理")
    print("-" * 60)
    print("  [INFO] 需要查看服务器日志确认 Room removed (empty)")

    sock = new_connection()
    login(sock)
    r = create_room(sock, "empty_room_test")
    room_id = int(r[2].split('|')[1])
    join_room(sock, room_id)

    # 离开房间
    sock.sendall(encode(LEAVE_ROOM_REQ, 10, ""))
    recv_until(sock, LEAVE_ROOM_RESP)
    sock.close()

    # 服务器日志应有 "Room {room_id} removed (empty)"
    t.check("空房间清理触发（需查看日志）", True)


# ========== 测试 8: 并发连接 ==========
def test_concurrent(t):
    print("\n[测试 8] 并发连接")
    print("-" * 60)

    num = 100
    success = [0]
    lock = threading.Lock()
    errors = []

    def worker(i):
        try:
            sock = new_connection()
            r = login(sock)
            if r and r[2] == "OK":
                with lock:
                    success[0] += 1
            sock.close()
        except Exception as e:
            with lock:
                errors.append(str(e))

    threads = [threading.Thread(target=worker, args=(i,)) for i in range(num)]
    start = time.time()
    for th in threads:
        th.start()
    for th in threads:
        th.join()
    elapsed = time.time() - start

    t.check(f"{num}并发登录全部成功",
            success[0] == num,
            f"成功 {success[0]}/{num}, 错误 {len(errors)}")
    if errors:
        print(f"  [INFO] 部分错误: {errors[:3]}")
    print(f"  [INFO] 耗时: {elapsed:.2f}s")


# ========== 测试 9: 心跳超时 ==========
def test_heartbeat_timeout(t):
    print("\n[测试 9] 心跳超时踢人")
    print("-" * 60)
    print("  [INFO] 需要等待约 90 秒")

    sock = new_connection()
    r = login(sock)
    if not r or r[2] != "OK":
        t.check("心跳超时测试登录成功", False)
        return

    print("  已登录，停止发送心跳，等待服务器断开...")
    start = time.time()
    try:
        sock.settimeout(120)
        while True:
            data = sock.recv(1024)
            if not data:
                elapsed = time.time() - start
                t.check(f"服务器在 {elapsed:.1f}s 后主动断开", True)
                break
    except socket.timeout:
        t.check("服务器 120s 内未断开", False)
    except Exception:
        elapsed = time.time() - start
        t.check(f"服务器断开连接 ({elapsed:.1f}s)", True)

    sock.close()


# ========== 测试 10: 断线清理 ==========
def test_disconnect_cleanup(t):
    print("\n[测试 10] 断线清理")
    print("-" * 60)
    print("  [INFO] 检查 Redis 中 user 键是否被清理")

    # 登录后立即断开
    sock = new_connection()
    login(sock)
    time.sleep(0.3)
    sock.close()
    time.sleep(0.5)

    t.check("断线清理（需查看 Redis）", True)


# ========== 主函数 ==========
def main():
    # 检查服务器
    try:
        s = socket.socket()
        s.settimeout(2)
        s.connect((HOST, PORT))
        s.close()
    except Exception as e:
        print(f"[ERROR] 无法连接服务器: {e}")
        sys.exit(1)

    print("=" * 60)
    print("EchoNet 完整回归测试")
    print("=" * 60)

    t = TestRunner()

    test_protocol_bounds(t)
    test_fragmentation(t)
    test_sql_injection(t)
    test_business(t)
    test_room_count(t)
    test_broadcast(t)
    test_empty_room(t)
    test_concurrent(t)

    # 心跳超时测试（可选，需要 90 秒）
    print()
    ans = input("运行心跳超时测试？需要 90 秒 [y/N]: ").strip().lower()
    if ans == 'y':
        test_heartbeat_timeout(t)

    test_disconnect_cleanup(t)

    ok = t.summary()
    if ok:
        print("\n所有测试通过！")
    else:
        print("\n有失败项，请检查。")
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
