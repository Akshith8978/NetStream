# NetStream Protocol Specification

This document defines the wire formats, framing rules, endianness conventions, and parsing state machines for NetStream's TCP and UDP protocols.

---

## 1. TCP Framing Specification

NetStream uses a binary-safe length-prefixed protocol. No delimiters (such as newlines) are used, enabling arbitrary binary payloads containing embedded null bytes (`0x00`) and arbitrary binary data.

### 1.1 Wire Format

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                       Payload Length (N)                      |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                                                               |
+                        Payload Data                           +
|                         (N Bytes)                             |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

| Field | Size | Type | Endianness | Description |
| :--- | :--- | :--- | :--- | :--- |
| `Payload Length` | 4 bytes | `uint32_t` | Big-Endian (Network Byte Order) | Length in bytes of the subsequent payload |
| `Payload Data` | `N` bytes | `uint8_t[]` | Raw byte sequence | Opaque application payload |

- **Header Size**: Exactly 4 bytes (`sizeof(uint32_t)`).
- **Maximum Message Size**: Configurable on the server (default `16,777,216` bytes / 16MB). If `Payload Length` exceeds this limit, the connection is aborted immediately with `ParseStatus::FrameTooLarge`.

### 1.2 Incremental Parsing State Machine

Because TCP is a stream-oriented protocol, a single `recv()` call may return:
- Less than 4 bytes (partial header).
- A complete header but only a fraction of the payload (partial payload).
- Multiple complete frames coalesced into a single network buffer.

The NetStream `FrameParser` operates as follows:
1. Appends raw incoming bytes into an internal contiguous buffer.
2. If `buffered_bytes() < 4`, returns `ParseStatus::NeedMoreData`.
3. Reads 4-byte length prefix using `ntohl()`.
4. If `length > max_message_size`, returns `ParseStatus::FrameTooLarge`.
5. If `buffered_bytes() < 4 + length`, returns `ParseStatus::NeedMoreData`.
6. Extracts `length` bytes into `Frame::payload`, advances internal read offset, and returns `ParseStatus::Ok`.
7. Caller continues calling `next_frame()` in a loop until `NeedMoreData` is returned.

---

## 2. UDP Packet Specification

The UDP streaming protocol provides an RTP-inspired binary header that supports packet ordering, duplicate detection, media pacing, loss tracking, and non-linear file reconstruction.

### 2.1 Wire Format

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|    Version    |  Payload Type |             Flags             |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                           Stream ID                           |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                        Sequence Number                        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                           Timestamp                           |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                         Payload Length                        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                                                               |
+                          File Offset                          +
|                           (64-bit)                            |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                                                               |
+                         Payload Bytes                         +
|                     (Up to 1400 Bytes)                        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

### 2.2 Header Fields

| Field | Size | Type | Endianness | Description |
| :--- | :--- | :--- | :--- | :--- |
| `Version` | 1 byte | `uint8_t` | N/A | Protocol version. Constant `1`. |
| `Payload Type` | 1 byte | `uint8_t` | N/A | `1` = Media Chunk, `2` = Control / Metadata |
| `Flags` | 2 bytes | `uint16_t` | Big-Endian | Bit 0: `FLAG_START` (`0x0001`), Bit 1: `FLAG_END` (`0x0002`) |
| `Stream ID` | 4 bytes | `uint32_t` | Big-Endian | Unique session/file transfer identifier |
| `Sequence Number`| 4 bytes | `uint32_t` | Big-Endian | Monotonically increasing datagram counter |
| `Timestamp` | 4 bytes | `uint32_t` | Big-Endian | Elapsed milliseconds from stream start |
| `Payload Length`| 4 bytes | `uint32_t` | Big-Endian | Length in bytes of the attached payload |
| `File Offset` | 8 bytes | `uint64_t` | Big-Endian (`htobe64`) | Exact byte position of chunk in reconstructed file |

- **Header Size**: Exactly 28 bytes.
- **Maximum Payload Size**: 1400 bytes default. Total datagram: $28 + 1400 = 1428$ bytes $\le 1500$ MTU.

### 2.3 Modular Sequence Number Arithmetic
The 32-bit sequence number supports continuous transmission across wraparound boundaries ($2^{32}-1 \rightarrow 0$):
```cpp
bool seq_lt(uint32_t a, uint32_t b) {
    return static_cast<int32_t>(a - b) < 0;
}
```
This guarantees valid ordering over a sliding window of up to $2^{31}-1$ packets.
