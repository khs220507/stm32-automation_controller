import datetime
import socket
import statistics
import time
from pathlib import Path

endpoint = ('169.254.100.2', 5000)
evidence = Path(__file__).resolve().parents[4] / 'docs/측정/2026-09-10-TCP'
evidence.mkdir(parents=True, exist_ok=True)
lines = []

def log(message):
    line = f'{datetime.datetime.now().isoformat(timespec="milliseconds")} {message}'
    lines.append(line)
    print(line, flush=True)

def connect():
    client = socket.create_connection(endpoint, 3)
    client.settimeout(2)
    client.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    return client

def line(client):
    received = bytearray()
    while not received.endswith(b'\r\n'):
        byte = client.recv(1)
        if not byte:
            raise RuntimeError('Unexpected TCP EOF')
        received.extend(byte)
        assert len(received) <= 256
    return received.decode('ascii').strip()

try:
    log('NUCLEO-F401RE + WIZ550io; PC USB-LAN direct link; board 169.254.100.2:5000 /16')
    with connect() as client:
        log(f'PC local endpoint: {client.getsockname()}')
        for command in ['PING', 'CHECK_W5500', 'CHECK_MPU6050', 'WAKE_MPU6050', 'CONFIG_ACCEL', 'READ_ACCEL', 'CHECK_HCSR04']:
            client.sendall((command+'\r\n').encode('ascii'))
            answer = line(client)
            log(f'TX {command} -> RX {answer}')
            assert answer.startswith(('OK,', 'ERR,'))
            time.sleep(.1)
        client.sendall(b'PI'); time.sleep(.12); client.sendall(b'NG\r'); time.sleep(.12); client.sendall(b'\n')
        assert line(client) == 'OK,PING,PONG'
        log('PASS: fragmented PING with two 120 ms gaps')
        client.sendall(b'PING\r\nCHECK_W5500\r\nPING\r\n')
        assert [line(client) for _ in range(3)] == ['OK,PING,PONG', 'OK,CHECK_W5500,4', 'OK,PING,PONG']
        log('PASS: three commands coalesced in one write, ordered responses')
        times=[]
        for _ in range(300):
            start=time.perf_counter(); client.sendall(b'PING\r\n'); assert line(client)=='OK,PING,PONG'
            times.append((time.perf_counter()-start)*1000)
        log(f'PASS: 300 PING round trips; min={min(times):.3f}, mean={statistics.mean(times):.3f}, max={max(times):.3f} ms (PC clock, includes software/network)')
        client.sendall(b'PI')
    time.sleep(.1)
    for i in range(10):
        with connect() as client:
            client.sendall(b'PING\r\n'); assert line(client)=='OK,PING,PONG'
        time.sleep(.05)
    log('PASS: partial command discarded at disconnect; 10 reconnect/PING cycles')
    log('Not tested: physical cable removal, power loss, long-duration operation, multi-client support')
finally:
    (evidence/'TCP-LAN-실측-로그.txt').write_text('\n'.join(lines)+'\n',encoding='utf-8')
