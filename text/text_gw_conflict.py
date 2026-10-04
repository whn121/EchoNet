import socket, struct, time

def login(port, user, req_id):
    s = socket.socket()
    s.connect(("127.0.0.1", port))
    payload = f"{user}|pass".encode()
    body = struct.pack(">HI", 0x01, req_id) + payload
    s.sendall(struct.pack(">I", len(body)) + body)
    return s

a = login(8080, "userA", 1)
b = login(8080, "userB", 1)
time.sleep(1)

a.settimeout(2); b.settimeout(2)
for name, s in [("A", a), ("B", b)]:
    try:
        data = s.recv(1024)
        if len(data) >= 10:
            msg_id = struct.unpack(">I", data[6:10])[0]
            print(f"client {name} got response id={msg_id}")
        else:
            print(f"client {name} got empty/short response")
    except Exception as e:
        print(f"client {name} error: {e}")
    s.close()