#!/usr/bin/env python3
"""TCP ASCII controller simulator for the Data Extraction Dashboard.

The dashboard connects to this script as a TCP client. Each frame follows the
controller protocol: STX + "humidity,temperature" + ETX.
"""

from __future__ import annotations

import argparse
import socket
import threading
import time
from datetime import datetime


STX = b"\x02"
ETX = b"\x03"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1", help="local interface/IP to listen on (default: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=20108, help="TCP port (default: 20108)")
    parser.add_argument("--interval", type=float, default=5.0, help="seconds between frames (default: 5)")
    parser.add_argument("--humidity", type=float, default=37.3, help="humidity value to send")
    parser.add_argument("--temperature", type=float, default=32.5, help="temperature value to send")
    return parser.parse_args()


def frame(humidity: float, temperature: float) -> bytes:
    return STX + f"{humidity:g},{temperature:g}".encode("ascii") + ETX


def main() -> int:
    args = parse_args()
    if not 0 < args.port < 65536:
        raise SystemExit("port must be between 1 and 65535")
    if args.interval <= 0:
        raise SystemExit("interval must be greater than zero")

    clients: set[socket.socket] = set()
    clients_lock = threading.Lock()

    def accept_clients(server: socket.socket) -> None:
        while True:
            try:
                client, address = server.accept()
            except socket.timeout:
                continue
            except OSError:
                # The listener is being closed during shutdown.
                return
            client.settimeout(2.0)
            with clients_lock:
                clients.add(client)
            print(f"[{datetime.now().isoformat(timespec='seconds')}] connected: {address[0]}:{address[1]}", flush=True)

    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
            server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            server.bind((args.host, args.port))
            server.listen()
            server.settimeout(0.5)
            print(f"Listening on {args.host}:{args.port}; sending every {args.interval:g}s. Ctrl+C to stop.", flush=True)
            accept_thread = threading.Thread(target=accept_clients, args=(server,), daemon=True)
            accept_thread.start()

            payload = frame(args.humidity, args.temperature)
            while True:
                started = time.monotonic()
                with clients_lock:
                    current_clients = list(clients)
                for client in current_clients:
                    try:
                        client.sendall(payload)
                    except OSError:
                        with clients_lock:
                            clients.discard(client)
                        try:
                            peer = client.getpeername()
                        except OSError:
                            peer = "unknown peer"
                        print(f"[{datetime.now().isoformat(timespec='seconds')}] disconnected: {peer}", flush=True)
                        client.close()
                if current_clients:
                    print(f"[{datetime.now().isoformat(timespec='seconds')}] sent {payload.hex(' ')} to {len(current_clients)} client(s)", flush=True)
                time.sleep(max(0.0, args.interval - (time.monotonic() - started)))
    except KeyboardInterrupt:
        print("\nController simulator stopped.", flush=True)
    except OSError as exc:
        raise SystemExit(f"Cannot listen on {args.host}:{args.port}: {exc}") from exc
    finally:
        with clients_lock:
            for client in clients:
                client.close()
            clients.clear()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
