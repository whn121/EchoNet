import socket
import struct
import time
import sys

HOST = '127.0.0.1'
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


def encode(msg_type, req_id, payload=""):
    payload_b = payload.encode('utf-8')
    body_len = 2 + 4 + len(payload_b)
    return struct.pack('>I H I', body_len, msg_type, req_id) + payload_b


def recv_full(sock, n, timeout=5):
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


def recv_one(sock, timeout=5):
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


def recv_until(sock, expected_type, max_tries=10, timeout=5):
    """循环读取，直到收到指定类型或超时"""
    for _ in range(max_tries):
        msg = recv_one(sock, timeout)
        if msg is None:
            return None
        if msg[0] == expected_type:
            return msg
    return None


def print_sep(title):
    print()
    print("=" * 60)
    print(f"  {title}")
    print("=" * 60)


def test_normal_flow():
    print_sep("测试 1：正常业务流程")

    s = socket.socket()
    s.connect((HOST, PORT))
    print("[OK] 连接服务器")

    # 登录
    s.sendall(encode(LOGIN_REQ, 1, "whn|278813"))
    resp = recv_until(s, LOGIN_RESP)
    assert resp and resp[2] == "OK", f"登录失败: {resp}"
    print(f"[OK] 登录成功: {resp[2]}")

    # 创建房间
    s.sendall(encode(CREATE_ROOM_REQ, 2, "test_room_p0"))
    resp = recv_until(s, CREATE_ROOM_RESP)
    assert resp and resp[2].startswith("OK"), f"创建房间失败: {resp}"
    parts = resp[2].split('|')
    room_id = int(parts[1])
    member_count = int(parts[2])
    print(f"[OK] 创建房间成功: room_id={room_id}, 人数={member_count}")

    # 加入房间
    s.sendall(encode(JOIN_ROOM_REQ, 3, str(room_id)))
    resp = recv_until(s, JOIN_ROOM_RESP)
    assert resp and resp[2].startswith("OK"), f"加入房间失败: {resp}"
    print(f"[OK] 加入房间成功: {resp[2]}")

    # 发送消息
    test_content = "P0_test_message_20260912"
    s.sendall(encode(SEND_MSG_REQ, 4, test_content))
    resp = recv_until(s, SEND_MSG_RESP)
    assert resp and resp[2] == "OK", f"发送消息失败: {resp}"
    print(f"[OK] 发送消息成功: {resp[2]}")

    # 心跳
    s.sendall(encode(HEARTBEAT_REQ, 5, ""))
    resp = recv_until(s, HEARTBEAT_RESP)
    assert resp, f"心跳失败: {resp}"
    print(f"[OK] 心跳响应成功")

    s.close()
    print("\n[PASS] 正常业务流程全部通过")
    print(f"      测试消息内容: {test_content}")
    return test_content


def test_sql_injection_login():
    print_sep("测试 2：SQL 注入 - 登录接口")

    s = socket.socket()
    s.connect((HOST, PORT))

    # 尝试 SQL 注入
    malicious = "admin' OR '1'='1"
    payload = malicious + "|whatever"
    s.sendall(encode(LOGIN_REQ, 1, payload))

    resp = recv_until(s, LOGIN_RESP)
    assert resp, "服务器无响应"
    print(f"响应类型: 0x{resp[0]:02x}")
    print(f"响应内容: {resp[2]}")

    if resp[2] == "FAIL":
        print("[PASS] SQL 注入被正确拦截（返回 FAIL）")
    elif resp[2] == "OK":
        print("[FAIL] SQL 注入成功，服务器存在漏洞！")
        s.close()
        sys.exit(1)
    else:
        print(f"[WARN] 未知响应: {resp[2]}")

    s.close()


def test_sql_injection_drop():
    print_sep("测试 3：SQL 注入 - DROP TABLE")

    s = socket.socket()
    s.connect((HOST, PORT))

    # 尝试 DROP TABLE
    malicious = "'; DROP TABLE users; --"
    payload = malicious + "|x"
    s.sendall(encode(LOGIN_REQ, 1, payload))

    resp = recv_until(s, LOGIN_RESP)
    if resp:
        print(f"响应: {resp[2]}")

    time.sleep(0.5)
    s.close()

    # 再次尝试正常登录，验证 users 表还在
    s2 = socket.socket()
    s2.connect((HOST, PORT))
    s2.sendall(encode(LOGIN_REQ, 2, "whn|278813"))
    resp = recv_until(s2, LOGIN_RESP)
    if resp and resp[2] == "OK":
        print("[PASS] users 表未被破坏，正常登录仍可用")
    else:
        print(f"[FAIL] users 表可能被删除: {resp}")
        sys.exit(1)
    s2.close()


def test_oversized_packet():
    print_sep("测试 4：超大长度字段（DoS 防护）")

    s = socket.socket()
    s.connect((HOST, PORT))

    # 发送一个超大长度字段
    body_len = 0xFFFFFFFF
    packet = struct.pack('>I H I', body_len, LOGIN_REQ, 1)
    s.sendall(packet)
    print(f"已发送超大长度字段: 0x{body_len:08x} ({body_len} 字节)")

    time.sleep(0.5)

    # 尝试再发数据，如果连接被关闭，会失败或返回 0
    try:
        s.settimeout(2)
        result = s.send(b"test")
        if result == 0:
            print("[PASS] 服务器主动关闭连接（send 返回 0）")
        else:
            print(f"[WARN] send 返回 {result}，连接可能还活着")
            # 尝试读响应
            data = s.recv(1024)
            if not data:
                print("[PASS] 服务器关闭了连接")
            else:
                print(f"[FAIL] 服务器未关闭连接: {data}")
    except (BrokenPipeError, ConnectionResetError) as e:
        print(f"[PASS] 连接已被服务器关闭: {e}")
    except socket.timeout:
        print("[WARN] 服务器未在 2 秒内响应")

    s.close()


def test_heartbeat_timeout():
    print_sep("测试 5：心跳超时踢人")
    print("说明：需要等待 90 秒，请耐心等待...")

    s = socket.socket()
    s.connect((HOST, PORT))

    # 登录
    s.sendall(encode(LOGIN_REQ, 1, "whnzs|278813"))
    resp = recv_until(s, LOGIN_RESP)
    assert resp and resp[2] == "OK", f"登录失败: {resp}"
    print(f"[OK] 登录成功")

    # 不发任何心跳，模拟"拔网线"
    print("已停止发送心跳，等待服务器主动断开...")
    start_time = time.time()

    try:
        s.settimeout(120)
        while True:
            data = s.recv(1024)
            if not data:
                elapsed = time.time() - start_time
                print(f"[PASS] 服务器主动关闭了连接，耗时 {elapsed:.1f} 秒")
                break
    except socket.timeout:
        print("[FAIL] 服务器在 120 秒内未关闭连接")
    except (ConnectionResetError, BrokenPipeError) as e:
        elapsed = time.time() - start_time
        print(f"[PASS] 连接被服务器断开: {e}，耗时 {elapsed:.1f} 秒")

    s.close()


def main():
    print("\n" + "#" * 60)
    print("#  EchoNet P0 验证测试")
    print("#" * 60)

    # 检查服务器是否运行
    try:
        s = socket.socket()
        s.settimeout(2)
        s.connect((HOST, PORT))
        s.close()
    except Exception as e:
        print(f"[ERROR] 无法连接服务器 {HOST}:{PORT}: {e}")
        print("请先启动 EchoNet 服务器")
        sys.exit(1)

    print(f"[OK] 服务器 {HOST}:{PORT} 可访问")

    # 记录测试前的消息总数
    msg_content = test_normal_flow()

    test_sql_injection_login()
    test_sql_injection_drop()
    test_oversized_packet()

    # 心跳超时测试可选（需要 90 秒）
    print()
    ans = input("是否运行心跳超时测试？需要 90 秒 [y/N]: ").strip().lower()
    if ans == 'y':
        test_heartbeat_timeout()
    else:
        print("[SKIP] 跳过心跳超时测试")

    # 总结
    print_sep("测试完成")
    print("请在另一个终端执行以下命令验证 MySQL 数据：")
    print(f"  mysql -u root -p -e \"SELECT * FROM echonet.messages ORDER BY id DESC LIMIT 5;\"")
    print()
    print(f"应该能看到 content='{msg_content}' 的新记录")


if __name__ == '__main__':
    main()
