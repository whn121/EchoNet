import socket, struct, time

s = socket.socket()
s.connect(("127.0.0.1", 8080))
print("connected to gateway:8080")

payload = b"userA|passA"
body = struct.pack(">HI", 0x01, 1) + payload
packet = struct.pack(">I", len(body)) + body

s.sendall(packet)
print("sent LOGIN_REQ")

time.sleep(2)
s.close()
print("closed")
