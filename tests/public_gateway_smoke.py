#!/usr/bin/env python3
"""TLS/WebSocket/gateway/UDP smoke test; creates no permanent binding."""

import asyncio
import base64
import hashlib
import json
import os
import sqlite3
import ssl
import struct
import time

DOMAIN = "8.166.124.212"


def crc16(data: bytes) -> int:
    value = 0xFFFF
    for byte in data:
        value ^= byte << 8
        for _ in range(8):
            value = ((value << 1) ^ 0x1021) & 0xFFFF if value & 0x8000 else (value << 1) & 0xFFFF
    return value


def control(message_type: int, device: int, request: int = 0, code: int = 0,
            binding: int = 0, token_low: int = 0, token_high: int = 0) -> bytes:
    packet = bytearray(48)
    packet[0:4] = bytes((ord("P"), ord("B"), 3, message_type))
    struct.pack_into("<IIIIII", packet, 4, device, request, code, binding,
                     token_low, token_high)
    struct.pack_into("<H", packet, 38, crc16(packet[:38]))
    return bytes(packet)


async def send_binary(writer: asyncio.StreamWriter, payload: bytes) -> None:
    mask = os.urandom(4)
    if len(payload) < 126:
        header = bytearray((0x82, 0x80 | len(payload)))
    elif len(payload) <= 0xFFFF:
        header = bytearray((0x82, 0xFE)) + struct.pack("!H", len(payload))
    else:
        raise ValueError("test frame is too large")
    encoded = bytearray(payload)
    for index in range(len(encoded)):
        encoded[index] ^= mask[index & 3]
    writer.write(header + mask + encoded)
    await writer.drain()


async def read_frame(reader: asyncio.StreamReader) -> bytes:
    first, second = await reader.readexactly(2)
    if first & 0x0F != 2 or second & 0x80:
        raise RuntimeError("unexpected WebSocket frame")
    length = second & 0x7F
    if length == 126:
        length = struct.unpack("!H", await reader.readexactly(2))[0]
    return await reader.readexactly(length)


async def open_tunnel():
    context = ssl.create_default_context()
    reader, writer = await asyncio.open_connection(
        "127.0.0.1", 32112, ssl=context, server_hostname=DOMAIN)
    token = os.urandom(32).hex()
    websocket_key = base64.b64encode(os.urandom(16)).decode("ascii")
    request = (
        f"GET /v1/tunnel HTTP/1.1\r\nHost: {DOMAIN}\r\n"
        "Upgrade: websocket\r\nConnection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {websocket_key}\r\nSec-WebSocket-Version: 13\r\n"
        f"Authorization: Bearer {token}\r\nX-Plane-Pet-Enroll: 1\r\n\r\n"
    )
    writer.write(request.encode("ascii"))
    await writer.drain()
    response = await reader.readuntil(b"\r\n\r\n")
    if not response.startswith(b"HTTP/1.1 101"):
        raise RuntimeError(response.decode("latin1", errors="replace"))
    return reader, writer, hashlib.sha256(token.encode("ascii")).hexdigest()


async def expect_status(reader: asyncio.StreamReader, device: int,
                        status: int) -> bytes:
    payload = await asyncio.wait_for(read_frame(reader), timeout=5)
    if (len(payload) != 48 or payload[:4] != b"PB\x03\x04" or
            struct.unpack_from("<I", payload, 4)[0] != device or
            payload[32] != status):
        raise RuntimeError(f"unexpected pairing status: {payload.hex()}")
    return payload


async def main() -> None:
    first_reader, first_writer, first_digest = await open_tunnel()
    second_reader, second_writer, second_digest = await open_tunnel()
    with open("/tmp/public_gateway_smoke_digests.txt", "w", encoding="ascii") as output:
        output.write(first_digest + "\n" + second_digest + "\n")
    first_device, second_device = 0x53A17E01, 0x53A17E02
    first_request, second_request, code = 0x18374629, 0x18374630, 918273
    telemetry = b"PPTELEM1\n" + json.dumps({
        "v": 1, "s": "0123456789abcdef", "q": 1,
        "t": int(time.time() * 1000), "a": "0.6.7",
        "i": "0000000000000000", "r": "0000000000000000",
        "e": "app_started", "x": 0,
    }, separators=(",", ":")).encode()
    await send_binary(first_writer, telemetry)
    await asyncio.sleep(0.2)
    with sqlite3.connect("/var/lib/plane-pet/telemetry.db") as database:
        stored = database.execute(
            "SELECT COUNT(*) FROM telemetry_events WHERE installation_id=? ",
            (first_digest[:24],)).fetchone()[0]
    if stored != 1:
        raise RuntimeError("anonymous telemetry did not reach SQLite")
    await send_binary(first_writer, control(1, first_device, first_request, code))
    await expect_status(first_reader, first_device, 1)
    await send_binary(second_writer, control(1, second_device, second_request, code))
    first_match = await expect_status(first_reader, first_device, 2)
    second_match = await expect_status(second_reader, second_device, 2)
    first_binding = struct.unpack_from("<I", first_match, 16)[0]
    second_binding = struct.unpack_from("<I", second_match, 16)[0]
    if first_binding == 0 or first_binding != second_binding:
        raise RuntimeError("two public clients did not receive one binding")
    token_low, token_high = struct.unpack_from("<II", first_match, 20)
    await send_binary(first_writer, control(
        6, first_device, binding=first_binding,
        token_low=token_low, token_high=token_high))
    await expect_status(first_reader, first_device, 7)
    print("PUBLIC_GATEWAY_SMOKE_OK tls=1 websocket=1 enrollment=1 "
          "telemetry=1 udp_roundtrip=1 dual_match=1 unbind_cleanup=1")
    first_writer.close()
    second_writer.close()
    await first_writer.wait_closed()
    await second_writer.wait_closed()


if __name__ == "__main__":
    asyncio.run(main())
