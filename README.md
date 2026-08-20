# IDCCP — IoT Device Control Command Protocol

> **IDCCP** = **I**oT **D**evice **C**ontrol **C**ommand **P**rotocol

A **UDP**-based, ultra-lightweight application-layer protocol for sending control commands one-way to IoT devices. The protocol is designed around the "**fire-and-forget**" principle: after sending a command, the client **does not wait for, nor receive, any reply packet**.

## ✨ Features

- **Ultra-lightweight**: a fixed 8-byte header; with no payload the entire frame is just 8 bytes, and the payload is at most 255 bytes.
- **No reply packets**: purely one-way communication; the server produces no response, minimizing device-side overhead.
- **Connectionless**: built on UDP, requiring no handshake or connection maintenance, ideal for IoT scenarios with unstable networks and limited resources.
- **Configuration-driven**: the mapping from command numbers to concrete actions is entirely defined by the configuration file, so it can be extended without changing code.
- **Built-in deduplication**: duplicate packets (such as UDP retransmissions) are filtered by request ID and client address, preventing commands from being executed more than once.

## 📦 Directory Structure

```
IDCCP/
├── idccp.h       # Protocol header: frame structure, constants, exit codes, function declarations
├── idccpd.c      # Server daemon implementation (device side)
├── idccp.conf    # Sample configuration file
├── LICENSE       # GNU GPL v3 license
└── README.md     # This file
```

## 🧭 Protocol Design

### Frame Format

Every IDCCP packet consists of a **fixed 8-byte header** plus an **optional payload**, laid out in host byte order with a compact (packed) memory layout:

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|    magic_0    |    magic_1    |    magic_2    |   ver_flag    |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|     length    |    command    |            req_id             |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                            payload                           ...
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

| Offset | Field | Length | Description |
|:----:|:----:|:----:|:-----|
| 0 | `magic_0` | 1 byte | Magic character `'I'` (`0x49`) |
| 1 | `magic_1` | 1 byte | Magic character `'D'` (`0x44`) |
| 2 | `magic_2` | 1 byte | Magic character `'C'` (`0x43`) |
| 3 | `ver_flag` | 1 byte | The upper 4 bits are the protocol version number, and `bit0` is the payload flag |
| 4 | `length` | 1 byte | Payload length, from `0` to `255` bytes |
| 5 | `command` | 1 byte | Command number, from `0` to `255` |
| 6-7 | `req_id` | 2 bytes | Request ID, used for deduplication |
| 8+ | `payload` | variable | Payload, whose length is given by `length` (present only when the payload flag is 1) |

### `ver_flag` Field Bit Layout

| Bit | Meaning |
|:--:|:-----|
| 7-4 | Protocol version number (currently `1`) |
| 3-1 | Reserved |
| 0 | `payload_flag`: `0` = no payload, `1` = payload present |

### Server Processing Flow

1. After receiving a UDP datagram, check that its length is at least 8 bytes (the header length).
2. Verify that the magic number is `"IDC"`.
3. Verify that the version number is `1`.
4. Perform **deduplication** based on `req_id` + client IP + client port; duplicate packets are dropped directly.
5. Look up the rule matching `command` in the configuration table.
6. Verify that the packet's `payload_flag` matches the configuration.
7. Execute the command bound in the configuration via `fork()` + `exec()` calling `/bin/sh -c`.
8. **Send no response packet**.

## 🚀 Quick Start

### Building

The server is a single-file C program that depends only on the standard library and system calls:

```bash
gcc -o idccpd idccpd.c
```

> `idccp.h` and `idccpd.c` must be in the same directory.

### Configuration

The default configuration file path is `/etc/idccp.conf`, and it can be overridden with the `-c` option. Configuration uses `[Command]` sections to describe command rules:

```ini
# Command without payload: directly execute the bound shell command
[Command]
cmdnum=0x1
payload_flag=0
cli_str=/bin/echo "hello"

# Command with payload: the payload content replaces the {payload} placeholder before execution
[Command]
cmdnum=0x2
cli_str=/bin/echo "hello {payload}"
payload_flag=1
```

Each `[Command]` rule contains three fields:

| Key | Meaning | Value |
|:--:|:-----|:-----|
| `cmdnum` | Command number | `0` to `255` (supports `0x` hexadecimal notation) |
| `cli_str` | Shell command to execute | Any string, up to 255 characters |
| `payload_flag` | Whether a payload is carried | `0`/`false` or `1`/`true` |

- When `payload_flag=0`, the command is executed **as-is**.
- When `payload_flag=1`, the command **must** contain the `{payload}` placeholder; the payload content replaces that placeholder before execution.

### Running

```bash
# Run in the foreground, bound to 0.0.0.0:30052
./idccpd -c idccp.conf

# Specify the listen address and port, and run in the background as a daemon
./idccpd -a 0.0.0.0 -p 30052 -c /etc/idccp.conf -d
```

Command-line options:

| Option | Description |
|:----:|:-----|
| `-a <IP>` | Bind IP address (default `0.0.0.0`) |
| `-p <port>` | UDP port to listen on (default `30052`) |
| `-c <file>` | Configuration file path (default `/etc/idccp.conf`) |
| `-d` | Run in the background as a daemon |
| `-h` | Show help information |

### Sending a Command Example

The Python script below demonstrates how to construct a "with-payload" IDCCP packet and send it (corresponding to `cmdnum=0x2` in the sample configuration):

```python
import socket

IP, PORT = "127.0.0.1", 30052

magic = b"IDC"                      # Magic number
ver_flag = 0x10 | 0x01              # Version 1 (upper 4 bits) | payload present (bit0)
payload = b"world"                  # Payload content
command = 0x02                      # Command number
req_id = 0x0001                     # Request ID

frame = (
    magic
    + bytes([ver_flag])
    + bytes([len(payload)])
    + bytes([command])
    + req_id.to_bytes(2, "little")  # Little-endian, consistent with the implementation
    + payload
)

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.sendto(frame, (IP, PORT))
```

The server will execute `/bin/echo "hello world"` and will **not** send back any data.

## 🔁 Deduplication

Because UDP may retransmit or deliver duplicates, the server maintains a table of recent request records (default capacity `64`):

- Records the triple `(req_id, client IP, client port)`.
- On receiving a packet, the table is looked up first; a hit is treated as a duplicate and dropped.
- When the table is full, it wraps around and overwrites the oldest record (ring-buffer strategy).

## 🚪 Exit Codes

| Exit code | Macro | Meaning |
|:----:|:-----|:-----|
| 2 | `EXIT_NOCONF` | Failed to open the configuration file |
| 3 | `EXIT_UNKNOWN_KEY` | Unknown key in the configuration |
| 4 | `EXIT_UNSUPPORTED_CMDNUM` | Command number out of the `0`–`255` range |
| 5 | `EXIT_FULL_OF_CONF_TABLE` | Configuration table is full (more than 256 entries) |
| 6 | `EXIT_FORK_ERROR` | `fork()` failed |
| 7 | `EXIT_EXECL` | `exec()` failed |
| 8 | `EXIT_FLAG_DIFF` | Packet payload flag does not match the configuration |
| 9 | `EXIT_LENGTH_ERR` | Invalid payload length |
| 10 | `EXIT_HOLDER_MISSING` | The configured command is missing the `{payload}` placeholder |
| 11 | `EXIT_UNSUPPORTED_PAYLOAD_FLAG` | The configured payload flag has an invalid value |
| 12 | `EXIT_ERROR_IP` | Invalid IP address passed via the `-a` option |

## ⚠️ Security Considerations

IDCCP is designed to minimize communication overhead and **does not include any built-in security mechanism**. Please note the following:

- **No authentication**: any node that can send UDP packets to the port can trigger command execution.
- **No encryption**: packets are transmitted in plaintext and can be sniffed and forged.
- **Command injection risk**: the payload is directly concatenated into a shell command for execution; when client input is untrusted, you should perform strict validation yourself or use a controlled command whitelist instead.

> It is recommended to use it only on trusted intranets, isolated networks, or in experimental environments; for production deployment, add authentication and encryption at the network layer (e.g., VPN, VLAN, firewall) or the application layer.

## 🔍 Implementation Details

- The server executes commands via `fork()` + `exec()`; the parent does not wait for the child (`SIGCHLD` is ignored), thus avoiding blocking the main loop.
- `req_id` is read and written directly in host byte order (little-endian) without conversion to network byte order; take care when communicating across platforms with different byte orders.
- Command numbers that are **not found in the configuration table are silently ignored**; no error is sent back (consistent with the no-reply design).

## 📄 License

This project is licensed under the [GNU General Public License v3.0](LICENSE).
