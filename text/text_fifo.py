import socket
import struct
import concurrent.futures
import time


HOST = "127.0.0.1"
PORT = 8080


# ==============================
# MyType
# ==============================

HEARTBEAT_REQ = 0x0C
HEARTBEAT_RESP = 0x0D


# ==============================
# 编码
#
# 协议：
#
# 4 bytes length
# 2 bytes type
# 4 bytes request id
# payload
#
# length 表示：
# type + request_id + payload
# ==============================

def encode_message(msg_type, msg_id, payload=b""):
    body = struct.pack(
        "!HI",
        msg_type,
        msg_id
    ) + payload

    return struct.pack(
        "!I",
        len(body)
    ) + body


# ==============================
# recv_exact
# ==============================

def recv_exact(sock, size):
    data = b""

    while len(data) < size:
        chunk = sock.recv(size - len(data))

        if not chunk:
            raise ConnectionError(
                "服务器提前关闭连接"
            )

        data += chunk

    return data


# ==============================
# 接收一个完整消息
# ==============================

def recv_message(sock):

    # 4 字节长度
    header = recv_exact(sock, 4)

    length = struct.unpack(
        "!I",
        header
    )[0]

    # 防止异常长度导致测试程序申请巨大内存
    if length < 6 or length > 10 * 1024 * 1024:
        raise RuntimeError(
            f"非法消息长度: {length}"
        )

    body = recv_exact(
        sock,
        length
    )

    # 2 字节 type
    msg_type = struct.unpack(
        "!H",
        body[0:2]
    )[0]

    # 4 字节 request id
    msg_id = struct.unpack(
        "!I",
        body[2:6]
    )[0]

    payload = body[6:]

    return msg_type, msg_id, payload


# ============================================================
# TEST 1
# 单连接 FIFO
# ============================================================

def test_single_connection(count=1000):

    print("=" * 60)
    print(f"[TEST 1] 单连接 FIFO")
    print(f"消息数量: {count}")
    print("=" * 60)

    sock = socket.socket(
        socket.AF_INET,
        socket.SOCK_STREAM
    )

    sock.settimeout(10)

    sock.connect(
        (HOST, PORT)
    )

    print("[INFO] connected")

    # ------------------------------
    # 连续快速发送
    # ------------------------------

    start = time.time()

    packet = b""

    for i in range(1, count + 1):

        packet += encode_message(
            HEARTBEAT_REQ,
            i
        )

    # 一次性发送
    sock.sendall(packet)

    send_time = time.time() - start

    print(
        f"[INFO] 已发送 {count} 条消息"
    )

    print(
        f"[INFO] 发送耗时: {send_time:.6f}s"
    )

    # ------------------------------
    # 接收响应
    # ------------------------------

    results = []

    for _ in range(count):

        msg_type, msg_id, payload = recv_message(sock)

        if msg_type != HEARTBEAT_RESP:

            print(
                "[FAIL] 收到错误消息类型:",
                hex(msg_type)
            )

            sock.close()

            return False

        results.append(msg_id)

    sock.close()

    # ------------------------------
    # 检查 FIFO
    # ------------------------------

    expected = list(
        range(1, count + 1)
    )

    if results == expected:

        print()
        print(
            f"[PASS] 单连接 FIFO 正确"
        )

        print(
            f"[PASS] {count} 条消息顺序完全一致"
        )

        return True

    # ------------------------------
    # 找第一次错误
    # ------------------------------

    print()
    print("[FAIL] FIFO 顺序错误")

    for index, (expected_id, actual_id) in enumerate(
        zip(expected, results),
        start=1
    ):

        if expected_id != actual_id:

            print(
                f"[FAIL] 第 {index} 个响应错误"
            )

            print(
                f"       expected = {expected_id}"
            )

            print(
                f"       actual   = {actual_id}"
            )

            break

    return False


# ============================================================
# TEST 2
# 多连接并发
# ============================================================

def single_client(client_id, count):

    sock = socket.socket(
        socket.AF_INET,
        socket.SOCK_STREAM
    )

    sock.settimeout(10)

    sock.connect(
        (HOST, PORT)
    )

    # 每个客户端使用独立 ID 范围
    #
    # client 1:
    # 100001 ~ 101000
    #
    # client 2:
    # 200001 ~ 201000
    #

    base = client_id * 100000

    packet = b""

    for i in range(1, count + 1):

        msg_id = base + i

        packet += encode_message(
            HEARTBEAT_REQ,
            msg_id
        )

    sock.sendall(packet)

    results = []

    for _ in range(count):

        msg_type, msg_id, payload = recv_message(sock)

        if msg_type != HEARTBEAT_RESP:

            sock.close()

            return False, "错误响应类型"

        results.append(msg_id)

    sock.close()

    expected = [
        base + i
        for i in range(1, count + 1)
    ]

    if results != expected:

        for index, (e, a) in enumerate(
            zip(expected, results),
            start=1
        ):

            if e != a:

                return False, (
                    f"第 {index} 个错误: "
                    f"expected={e}, "
                    f"actual={a}"
                )

    return True, ""


def test_multiple_connections(
    client_count=4,
    count=1000
):

    print()
    print("=" * 60)
    print("[TEST 2] 多连接并发 FIFO")
    print(
        f"客户端: {client_count}"
    )
    print(
        f"每个客户端消息: {count}"
    )
    print("=" * 60)

    start = time.time()

    with concurrent.futures.ThreadPoolExecutor(
        max_workers=client_count
    ) as executor:

        futures = [
            executor.submit(
                single_client,
                client_id,
                count
            )

            for client_id
            in range(1, client_count + 1)
        ]

        results = [
            future.result()
            for future in futures
        ]

    elapsed = time.time() - start

    all_pass = True

    for client_id, result in enumerate(
        results,
        start=1
    ):

        ok, error = result

        if ok:

            print(
                f"[PASS] Client {client_id}"
            )

        else:

            all_pass = False

            print(
                f"[FAIL] Client {client_id}: "
                f"{error}"
            )

    print()
    print(
        f"[INFO] 总耗时: {elapsed:.6f}s"
    )

    if all_pass:

        print(
            "[PASS] 多连接 FIFO 全部正确"
        )

    return all_pass


# ============================================================
# TEST 3
# 高频压力
# ============================================================

def test_stress():

    print()
    print("=" * 60)
    print("[TEST 3] 高频压力测试")
    print("=" * 60)

    count = 10000

    sock = socket.socket(
        socket.AF_INET,
        socket.SOCK_STREAM
    )

    sock.settimeout(30)

    sock.connect(
        (HOST, PORT)
    )

    packet = b""

    for i in range(
        1,
        count + 1
    ):

        packet += encode_message(
            HEARTBEAT_REQ,
            i
        )

    print(
        f"[INFO] 准备发送 {count} 条"
    )

    start = time.time()

    sock.sendall(packet)

    send_elapsed = time.time() - start

    print(
        f"[INFO] sendall 耗时: "
        f"{send_elapsed:.6f}s"
    )

    # ------------------------------
    # 接收并检查
    # ------------------------------

    for expected_id in range(
        1,
        count + 1
    ):

        msg_type, msg_id, payload = recv_message(sock)

        if msg_type != HEARTBEAT_RESP:

            print(
                "[FAIL] 消息类型错误"
            )

            sock.close()

            return False

        if msg_id != expected_id:

            print()
            print(
                "[FAIL] FIFO 错误"
            )

            print(
                f"expected={expected_id}"
            )

            print(
                f"actual={msg_id}"
            )

            sock.close()

            return False

    sock.close()

    elapsed = time.time() - start

    print(
        f"[PASS] {count} 条消息全部按顺序返回"
    )

    print(
        f"[INFO] 总耗时: {elapsed:.6f}s"
    )

    print(
        f"[INFO] QPS: "
        f"{count / elapsed:.2f}"
    )

    return True


# ============================================================
# main
# ============================================================

def main():

    print()
    print("=" * 60)
    print("EchoNet Connection FIFO Test")
    print("=" * 60)
    print(
        f"Server: {HOST}:{PORT}"
    )
    print()

    results = []

    # Test 1
    results.append(
        test_single_connection(
            count=1000
        )
    )

    # Test 2
    results.append(
        test_multiple_connections(
            client_count=4,
            count=1000
        )
    )

    # Test 3
    results.append(
        test_stress()
    )

    print()
    print("=" * 60)

    if all(results):

        print(
            "ALL TESTS PASSED"
        )

    else:

        print(
            "TEST FAILED"
        )

    print("=" * 60)


if __name__ == "__main__":
    main()
