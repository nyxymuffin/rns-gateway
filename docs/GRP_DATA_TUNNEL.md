# RNS tunnel over MeshCore GRP_DATA — wire format v1

Normative description of how Reticulum (RNS) packets are carried between
MeshCore nodes as MeshCore group datagrams (`GRP_DATA`, payload type `0x06`).
Both implementations follow this document and test against it:

- this gateway (`examples/rns_gateway/`, `tunnel_format = data`)
- the Ratspeak handheld fork (`nyxymuffin/ratspeak-handheld`, `src/meshcore/`)

It replaces the text framing (`RNS:<base64url>` in `GRP_TXT` channel
messages) for nodes configured to use it. The two formats are not
wire-compatible; a channel must carry one or the other.

Status: agreed 2026-09-26. Version 1.

## 1. Transport

Every message is one MeshCore `GRP_DATA` packet on the tunnel's private
group channel (MeshCore Protocol Specification, section 8). MeshCore
encrypts and authenticates it with the channel key and adds nothing that
identifies the sender; that is why the sender is carried inside the body.

The decrypted `GRP_DATA` plaintext is `data_type (uint16 LE) | len (uint8) |
body`. For this tunnel:

| Field | Value |
|---|---|
| `data_type` | `RnsTunnel`. Currently `0xFFFF` (MeshCore's `DATA_TYPE_DEV` developer namespace). Defined once in each implementation as an enum member so it can be changed in one place. |
| `len` | Length of `body`, 1..165 (`MAX_GROUP_DATA_LENGTH`). |
| `body` | One tunnel message, section 2. |

Packets with any other `data_type` are not tunnel traffic and are ignored
by the tunnel.

Channel reach (zero-hop or flood) and path hash size are local settings,
not part of the format. The Ratspeak handheld defaults to flood.

## 2. Messages

The first byte of `body` is the message kind. All multi-byte integers are
big-endian unless stated otherwise. A receiver drops a message whose kind
it does not know, or whose length does not match its kind.

| Kind | Name |
|---|---|
| `0x01` | Fragment |
| `0x02` | Bind |
| `0x03` | Bind request |
| other | reserved — drop |

### 2.1 Fragment (`0x01`)

One piece of one RNS packet.

| Offset | Size | Field | Notes |
|---|---|---|---|
| 0 | 1 | kind | `0x01` |
| 1 | 4 | sender | First 4 bytes of the sender's MeshCore public key |
| 5 | 4 | pkt_id | Chosen at random by the sender for each RNS packet, big-endian |
| 9 | 1 | frag_idx | 0-based, `< frag_total` |
| 10 | 1 | frag_total | 1..255 |
| 11 | 1..154 | payload | Consecutive bytes of the RNS packet |

- Header is 11 bytes, so a fragment carries at most **154** payload bytes.
- The sender splits an RNS packet of `n` bytes into `ceil(n / 154)`
  fragments, all but the last full. A 500-byte packet (the RNS MTU) is 4
  fragments.
- A fragment shorter than 12 bytes, or with `frag_total == 0`, or with
  `frag_idx >= frag_total`, is dropped.
- The receiver reassembles by the key **(sender, pkt_id)** and delivers the
  packet once all `frag_total` indexes are present. Duplicate fragments
  are ignored. Incomplete assemblies expire (implementation-defined
  timeout; the gateway uses 5 minutes).
- Order of arrival is not significant.

### 2.2 Bind (`0x02`) and Bind request (`0x03`)

Peer discovery: announces the sender's full MeshCore identity so that
fragment `sender` prefixes can be resolved, and (in a later version)
direct routing can target it. Same layout for both kinds.

| Offset | Size | Field | Notes |
|---|---|---|---|
| 0 | 1 | kind | `0x02` Bind, `0x03` Bind request |
| 1 | 32 | public_key | Sender's full MeshCore Ed25519 public key |
| 33 | 1 | flags | bit 0: router (1) or edge (0). Bits 1-7 reserved: send 0, ignore on receipt |
| 34 | 1 | name_len | 0..31 |
| 35 | name_len | name | Node name, UTF-8, no NUL. For logs and display only |

- Total length must equal `35 + name_len`; otherwise drop.
- A node that receives a **Bind request** answers with a **Bind** after a
  short random delay (the gateway's IGMP-style deferred response), unless
  it has already sent one within that window.
- Peers are keyed by `public_key`, never by name. A Bind from a known key
  with a different name updates the name only.
- A message whose `public_key` equals the receiver's own key is its own
  echo and is ignored.

## 3. Versioning

There is no version field. An incompatible change to any layout above is
published under a **new `data_type` value**; version 1 receivers then
ignore it (section 1). New message kinds may be added under the same
`data_type` because unknown kinds are dropped.

## 4. Receiver behaviour: MeshCore hops count as Reticulum hops

Reticulum sizes link-establishment and packet-receipt timeouts at 6 s per hop
from a path's hop count (RNS `Link.py`, `Packet.py`), but the tunnel is one
Reticulum hop however many MeshCore repeaters it spans. A receiver therefore
adds the MeshCore hops a packet crossed (the flood path's hash count, the
highest across the packet's fragments; 0 when heard directly) to the RNS hop
byte (byte 1) before handing the packet to Reticulum, clamped at 126 because
Reticulum drops packets over 127 after its own +1 (`PATHFINDER_M` = 128).
The hop byte is outside the packet hash, so no hash or signature changes.
This is local behaviour, not wire format, but both implementations do it so
timeouts cover the distance in both directions.

## 5. Not in version 1

- **Direct (unicast) leg.** Everything is sent on the channel. How unicast
  RNS traffic is carried (MeshCore `TXT_MSG`, `REQ`/`RESPONSE`, or
  `GRP_DATA` along a known path) is decided separately.
- Airtime policy (announce and path-request throttles, hourly budget) is
  local to each implementation and not part of the wire format.

## 6. Example

A Bind request from a node whose public key begins `3a 7c 01 e9 …`, edge
role, named `Nyx T-Pager` (11 bytes):

```
03                                              kind: Bind request
3a 7c 01 e9 .. (32 bytes total) ..              public_key
00                                              flags: edge
0b                                              name_len = 11
4e 79 78 20 54 2d 50 61 67 65 72                "Nyx T-Pager"
```

Body length 46; `GRP_DATA` plaintext `ff ff 2e 03 3a 7c …`.

The second fragment of a 500-byte RNS packet from the same node,
`pkt_id = 0x0badf00d`:

```
01                  kind: Fragment
3a 7c 01 e9         sender (public key prefix)
0b ad f0 0d         pkt_id
01                  frag_idx = 1
04                  frag_total = 4
.. 154 bytes ..     payload = RNS packet bytes 154..307
```
