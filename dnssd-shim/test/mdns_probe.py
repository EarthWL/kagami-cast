# SPDX-License-Identifier: GPL-3.0-or-later
"""Watch mDNS traffic on the LAN and print the records that match a filter.

Joins 224.0.0.251:5353 on the given interface addresses, optionally sends
queries every few seconds, optionally runs a command (for example shimtest)
once listening has started, and prints each distinct record once, with the
time it was first seen and the source address (which tells the interface).

  python -I mdns_probe.py --iface 192.168.0.89 --seconds 15 \
      --match KagamiShimTest --query _airplay._tcp.local:PTR \
      --run "shimtest.exe reg dnssd.dll KagamiShimTest 6"
"""
import argparse
import ipaddress
import shlex
import socket
import struct
import subprocess
import sys
import threading
import time

TYPES = {1: "A", 12: "PTR", 16: "TXT", 28: "AAAA", 33: "SRV", 47: "NSEC", 255: "ANY"}
TYPE_IDS = {v: k for k, v in TYPES.items()}


def label_text(raw):
    s = raw.decode("utf-8", "replace")
    return s.replace("\\", "\\\\").replace(".", "\\.")


def read_name(buf, off):
    labels, end, hops = [], None, 0
    while True:
        n = buf[off]
        if n & 0xC0 == 0xC0:
            if end is None:
                end = off + 2
            off = ((n & 0x3F) << 8) | buf[off + 1]
            hops += 1
            if hops > 64:
                raise ValueError("name compression loop")
            continue
        off += 1
        if n == 0:
            break
        labels.append(label_text(buf[off:off + n]))
        off += n
    return ".".join(labels), (end if end is not None else off)


def encode_name(name):
    out, label, i = b"", "", 0
    while i < len(name):
        c = name[i]
        if c == "\\" and i + 1 < len(name):
            label += name[i + 1]
            i += 2
            continue
        if c == ".":
            out += bytes([len(label.encode())]) + label.encode()
            label = ""
        else:
            label += c
        i += 1
    if label:
        out += bytes([len(label.encode())]) + label.encode()
    return out + b"\x00"


def rdata_text(buf, rtype, off, rdlen):
    rd = buf[off:off + rdlen]
    if rtype == 12:
        return read_name(buf, off)[0]
    if rtype == 33:
        prio, weight, port = struct.unpack("!HHH", rd[:6])
        return f"prio={prio} weight={weight} port={port} target={read_name(buf, off + 6)[0]}"
    if rtype == 16:
        items, p = [], 0
        while p < len(rd):
            n = rd[p]
            items.append(rd[p + 1:p + 1 + n].decode("utf-8", "replace"))
            p += 1 + n
        return " | ".join(items) + f"  [hex {rd.hex()}]"
    if rtype == 1:
        return str(ipaddress.IPv4Address(rd))
    if rtype == 28:
        return str(ipaddress.IPv6Address(rd))
    if rtype == 47:
        nxt, p = read_name(buf, off)
        bitmap = buf[p:off + rdlen]
        kinds, q = [], 0
        while q + 2 <= len(bitmap):
            win, blen = bitmap[q], bitmap[q + 1]
            for bi, byte in enumerate(bitmap[q + 2:q + 2 + blen]):
                for bit in range(8):
                    if byte & (0x80 >> bit):
                        t = win * 256 + bi * 8 + bit
                        kinds.append(TYPES.get(t, str(t)))
            q += 2 + blen
        return f"next={nxt} types={','.join(kinds)}"
    return rd.hex()


def parse(buf):
    _id, flags, qd, an, ns, ar = struct.unpack("!6H", buf[:12])
    off, questions, records = 12, [], []
    for _ in range(qd):
        name, off = read_name(buf, off)
        qtype, qclass = struct.unpack("!HH", buf[off:off + 4])
        off += 4
        questions.append((name, TYPES.get(qtype, str(qtype)), "QU" if qclass & 0x8000 else "QM"))
    for section, count in (("AN", an), ("NS", ns), ("AR", ar)):
        for _ in range(count):
            name, off = read_name(buf, off)
            rtype, rclass, ttl, rdlen = struct.unpack("!HHIH", buf[off:off + 10])
            off += 10
            records.append((section, name, TYPES.get(rtype, str(rtype)),
                            "flush" if rclass & 0x8000 else "", ttl,
                            rdata_text(buf, rtype, off, rdlen)))
            off += rdlen
    return flags, questions, records


def build_query(queries):
    body = b""
    for name, qtype in queries:
        body += encode_name(name) + struct.pack("!HH", TYPE_IDS[qtype], 1)
    return struct.pack("!6H", 0, 0, len(queries), 0, 0, 0) + body


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iface", action="append", required=True, help="local IPv4 address to listen on")
    ap.add_argument("--seconds", type=float, default=10)
    ap.add_argument("--match", action="append", default=[], help="case-insensitive substring")
    ap.add_argument("--query", action="append", default=[], help="name:TYPE, sent every --every s")
    ap.add_argument("--every", type=float, default=2.0)
    ap.add_argument("--run", action="append", default=[],
                    help="command to start once listening (repeatable; started --run-gap s apart)")
    ap.add_argument("--run-delay", type=float, default=1.0)
    ap.add_argument("--run-gap", type=float, default=0.3)
    ap.add_argument("--queries", action="store_true", help="also print matching queries")
    args = ap.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("0.0.0.0", 5353))
    for ip in args.iface:
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP,
                        socket.inet_aton("224.0.0.251") + socket.inet_aton(ip))
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_LOOP, 1)
    sock.settimeout(0.2)

    queries = []
    for q in args.query:
        name, qtype = q.rsplit(":", 1)
        queries.append((name, qtype.upper()))
    matches = [m.lower() for m in args.match]

    t0 = time.monotonic()
    seen = {}
    procs = []
    next_query = t0
    packets = 0

    def pump(stream, tag):
        for line in iter(stream.readline, b""):
            print(f"[+{time.monotonic() - t0:6.2f}s] {tag}> {line.decode(errors='replace').rstrip()}", flush=True)

    while time.monotonic() - t0 < args.seconds:
        now = time.monotonic()
        if len(procs) < len(args.run) and now - t0 >= args.run_delay + len(procs) * args.run_gap:
            # posix=True: use forward slashes in paths; quote names with spaces
            proc = subprocess.Popen(shlex.split(args.run[len(procs)]), stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT)
            threading.Thread(target=pump, args=(proc.stdout, f"run{len(procs) + 1}"),
                             daemon=True).start()
            procs.append(proc)
        if queries and now >= next_query:
            for ip in args.iface:
                sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton(ip))
                sock.sendto(build_query(queries), ("224.0.0.251", 5353))
            next_query = now + args.every
        try:
            data, (src, _port) = sock.recvfrom(9000)
        except socket.timeout:
            continue
        except ConnectionResetError:
            continue
        try:
            flags, questions, records = parse(data)
        except Exception as e:  # malformed or truncated packet
            continue
        is_response = bool(flags & 0x8000)
        packets += 1
        lines = []
        if not is_response and args.queries:
            for name, qtype, qu in questions:
                lines.append(f"query {qu} {qtype} {name}")
        for section, name, rtype, flush, ttl, rdata in records:
            kind = "resp" if is_response else "known-answer"
            lines.append(f"{kind} {section} {rtype:4} {name} ttl={ttl}{' flush' if flush else ''} -> {rdata}")
        for line in lines:
            if matches and not any(m in line.lower() for m in matches):
                continue
            key = (src, line)
            seen[key] = seen.get(key, 0) + 1
            if seen[key] == 1:
                print(f"[+{time.monotonic() - t0:6.2f}s] #{packets:<4} {src:15} {line}", flush=True)

    for proc in procs:
        if proc.poll() is None:
            proc.kill()  # TerminateProcess, like the app's QProcess::kill()
            proc.wait(timeout=10)
    if procs:
        # report goodbyes that follow the kill
        end = time.monotonic() + 1.0
        while time.monotonic() < end:
            try:
                data, (src, _port) = sock.recvfrom(9000)
                _flags, _q, records = parse(data)
            except (socket.timeout, ConnectionResetError, ValueError, IndexError, struct.error):
                continue
            for section, name, rtype, flush, ttl, rdata in records:
                line = f"after-exit {section} {rtype:4} {name} ttl={ttl} -> {rdata}"
                if (not matches or any(m in line.lower() for m in matches)) and (src, line) not in seen:
                    seen[(src, line)] = 1
                    print(f"[+{time.monotonic() - t0:6.2f}s] {src:15} {line}", flush=True)
    codes = ", ".join(str(p.returncode) for p in procs) or "-"
    print(f"--- {len(seen)} distinct lines; exit codes of --run: {codes}")


if __name__ == "__main__":
    sys.exit(main())
