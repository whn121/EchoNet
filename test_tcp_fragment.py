import socket
import time

def test_half_packet():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect(('127.0.0.1', 8080))
    # 发送请求前半部分
    s.send(b'GET / HTTP/1.1\r\nHo')
    time.sleep(0.5)
    # 发送剩余部分
    s.send(b'st: localhost\r\n\r\n')
    time.sleep(0.5)
    data = s.recv(4096)
    print('Half-packet response:\n', data.decode())
    s.close()

def test_sticky_packet():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect(('127.0.0.1', 8080))
    req = b'GET / HTTP/1.1\r\nHost: localhost\r\n\r\nGET / HTTP/1.1\r\nHost: localhost\r\n\r\n'
    s.send(req)
    time.sleep(0.5)
    data = s.recv(8192)
    print('Sticky-packet response:\n', data.decode())
    s.close()

if __name__ == '__main__':
    test_half_packet()
    test_sticky_packet()
