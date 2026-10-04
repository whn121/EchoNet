import socket, struct, time

s = socket.socket()
s.connect(("127.0.0.1", 8080))
print("connected to gateway:8080")

# LOGIN_REQ = 0x01, id = 1, payload = "userA|passA"
payload = b"userA|passA"
body = struct.pack(">HI", 0x01, 1) + payload
packet = struct.pack(">I", len(body)) + body
s.sendall(packet)
print("sent LOGIN_REQ id=1")

s.settimeout(3)
try:
    data = s.recv(1024)
    print(f"received {len(data)} bytes: {data.hex()}")
    if len(data) >= 10:
        body_len = struct.unpack(">I", data[:4])[0]
        msg_type = struct.unpack(">H", data[4:6])[0]
        msg_id   = struct.unpack(">I", data[6:10])[0]
        print(f"  body_len={body_len} type=0x{msg_type:02x} id={msg_id}")
        if msg_id == 1:
            print("  PASS: id 还原正确")
        else:
            print(f"  FAIL: id 应该是 1, 实际 {msg_id}")
except Exception as e:
    print(f"recv error: {e}")

s.close()
