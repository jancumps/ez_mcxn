#!/usr/bin/env python3
"""MCXN236 USB Ethernet Demo Test Script.

Tests:
1. Bidirectional TCP Echo (short message and bulk binary data) on port 5000.
2. Interactive commands (stats, uptime, ping) on port 5000.
3. HTTP Status Web Server on port 80.

No manual IP configuration is required: the MCXN236 device runs an embedded
DHCP server that automatically assigns 192.168.7.1 to the connected host
without advertising a gateway, preserving existing internet connectivity.
"""
import argparse
import os
import socket
import sys
import threading
import time

def test_echo(host, port):
    print(f"\n[1/3] Testing Bidirectional TCP Echo on {host}:{port}...")
    
    # Test 1: Short text message
    short_msg = b"Hello from Host to MCXN236 USB Ethernet!"
    with socket.create_connection((host, port), timeout=5) as s:
        s.settimeout(5)
        s.sendall(short_msg)
        rx = bytearray()
        while len(rx) < len(short_msg):
            chunk = s.recv(len(short_msg) - len(rx))
            if not chunk:
                raise RuntimeError("Connection closed prematurely during short echo")
            rx.extend(chunk)
        assert rx == short_msg, f"Echo mismatch: {rx!r} != {short_msg!r}"
        print(f"  PASS: Short message echoed ({len(short_msg)} bytes)")

    # Test 2: Bulk binary data transfer (64 KB)
    bulk_size = 65536
    bulk_data = os.urandom(bulk_size)
    print(f"  Sending {bulk_size:,} bytes of bulk binary data...")
    t0 = time.monotonic()
    with socket.create_connection((host, port), timeout=10) as s:
        s.settimeout(10)
        rx = bytearray()
        recv_err = []

        def receiver():
            try:
                while len(rx) < bulk_size:
                    chunk = s.recv(min(4096, bulk_size - len(rx)))
                    if not chunk:
                        break
                    rx.extend(chunk)
            except Exception as e:
                recv_err.append(e)

        t = threading.Thread(target=receiver, daemon=True)
        t.start()

        chunk_size = 4096
        for offset in range(0, bulk_size, chunk_size):
            s.sendall(bulk_data[offset:offset + chunk_size])

        t.join(timeout=15)
        if recv_err:
            raise recv_err[0]

        elapsed = time.monotonic() - t0
        assert rx == bulk_data, f"Bulk data echo mismatch! (Received {len(rx)} / {bulk_size} bytes)"
        speed_kb = (bulk_size * 2 / 1024) / elapsed
        print(f"  PASS: Bulk bidirectional echo verified in {elapsed:.2f}s (~{speed_kb:.1f} KB/s total)")

def test_commands(host, port):
    print(f"\n[2/3] Testing Interactive Commands on {host}:{port}...")
    for cmd in [b"stats\r\n", b"uptime\r\n", b"ping\r\n"]:
        with socket.create_connection((host, port), timeout=5) as s:
            s.settimeout(5)
            s.sendall(cmd)
            resp = s.recv(1024)
            print(f"  Sent: {cmd.strip().decode()} -> Received: {resp.strip().decode()}")
            assert len(resp) > 0
    print("  PASS: Command responses verified")

def test_http(host, port=80):
    print(f"\n[3/3] Testing HTTP Web Server on http://{host}:{port}/ ...")
    with socket.create_connection((host, port), timeout=5) as s:
        s.settimeout(5)
        s.sendall(b"GET / HTTP/1.1\r\nHost: " + host.encode() + b"\r\nConnection: close\r\n\r\n")
        resp = bytearray()
        while True:
            chunk = s.recv(1024)
            if not chunk:
                break
            resp.extend(chunk)
        resp_text = resp.decode('utf-8', errors='replace')
        assert "HTTP/1.1 200 OK" in resp_text, "HTTP 200 OK not found"
        assert "MCXN236 USB Ethernet" in resp_text, "Expected title not found"
        print("  PASS: HTTP 200 OK received with HTML status page")

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="192.168.7.2", help="Device IP (default: 192.168.7.2)")
    parser.add_argument("--echo-port", type=int, default=5000, help="TCP Echo port (default: 5000)")
    parser.add_argument("--http-port", type=int, default=80, help="HTTP port (default: 80)")
    args = parser.parse_args()

    print("==================================================")
    print(" MCXN236 USB Ethernet Demo Test")
    print(f" Target: {args.host}")
    print("==================================================")

    try:
        test_echo(args.host, args.echo_port)
        test_commands(args.host, args.echo_port)
        test_http(args.host, args.http_port)
        print("\n==================================================")
        print(" ALL TESTS PASSED SUCCESSFULLY!")
        print("==================================================")
        return 0
    except Exception as exc:
        print(f"\nTEST FAILED: {exc}")
        return 1

if __name__ == "__main__":
    sys.exit(main())
