#ifndef GRP_DATA_TUNNEL_CODEC_H
#define GRP_DATA_TUNNEL_CODEC_H

// RNS tunnel over MeshCore GRP_DATA, wire format v1. Normative spec:
// docs/GRP_DATA_TUNNEL.md (agreed 2026-09-26). The Ratspeak handheld fork carries
// the same logic in src/meshcore/TunnelCodec.h; both test the spec example.
// Pure and header-only: host-tested in test/host/test_grp_data_codec.cpp.
//
// These functions build and parse the GRP_DATA *body* only. MeshCore adds the
// data_type (GrpDataType::RnsTunnel) and length, and encrypts with the channel key.
//
// SPDX-License-Identifier: MIT

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace MeshCoreTunnel::grpdata {

// GRP_DATA data_type for tunnel traffic (spec section 1). Change the value here
// and nowhere else; it must match the handheld.
enum class GrpDataType : uint16_t {
    RnsTunnel = 0xFFFF,   // MeshCore DATA_TYPE_DEV until an assigned value is agreed
};

enum class Kind : uint8_t { Fragment = 0x01, Bind = 0x02, BindRequest = 0x03 };

inline constexpr size_t kMaxBody = 165;              // MAX_GROUP_DATA_LENGTH
inline constexpr size_t kSenderPrefix = 4;
inline constexpr size_t kPublicKeySize = 32;
inline constexpr size_t kFragmentHeader = 11;        // kind, sender(4), pkt_id(4), idx, total
inline constexpr size_t kMaxFragmentPayload = kMaxBody - kFragmentHeader;   // 154
inline constexpr size_t kBindHeader = 35;            // kind, key(32), flags, name_len
inline constexpr size_t kMaxNameBytes = 31;
inline constexpr uint8_t kFlagRouter = 0x01;

struct Fragment {
    uint8_t sender[kSenderPrefix] = {};
    uint32_t packetId = 0;
    uint8_t index = 0;
    uint8_t total = 0;
    const uint8_t* payload = nullptr;   // points into the decoded body
    size_t payloadLength = 0;
};

struct Bind {
    bool request = false;
    uint8_t publicKey[kPublicKeySize] = {};
    bool router = false;
    uint8_t nameLength = 0;
    char name[kMaxNameBytes + 1] = {};   // NUL-terminated copy
};

inline bool kindOf(const uint8_t* body, size_t length, Kind& kind) {
    if (!body || length == 0) return false;
    switch (body[0]) {
    case 0x01: kind = Kind::Fragment; return true;
    case 0x02: kind = Kind::Bind; return true;
    case 0x03: kind = Kind::BindRequest; return true;
    default: return false;   // reserved kinds are dropped (spec section 2)
    }
}

// Fragments needed for an RNS packet of `length` bytes; 0 if empty or if it
// would need more than 255 fragments.
inline constexpr uint8_t fragmentCount(size_t length) {
    return length == 0 || length > 255 * kMaxFragmentPayload
        ? 0 : static_cast<uint8_t>((length + kMaxFragmentPayload - 1) / kMaxFragmentPayload);
}

// Writes fragment `index` of `packet` into `out`. Returns the body length, or 0.
inline size_t encodeFragment(const uint8_t sender[kSenderPrefix], uint32_t packetId, uint8_t index,
                             const uint8_t* packet, size_t packetLength, uint8_t* out, size_t capacity) {
    const uint8_t total = fragmentCount(packetLength);
    if (!sender || !packet || !out || total == 0 || index >= total) return 0;
    const size_t offset = size_t(index) * kMaxFragmentPayload;
    const size_t chunk = packetLength - offset < kMaxFragmentPayload ? packetLength - offset : kMaxFragmentPayload;
    if (capacity < kFragmentHeader + chunk) return 0;
    out[0] = static_cast<uint8_t>(Kind::Fragment);
    memcpy(out + 1, sender, kSenderPrefix);
    out[5] = uint8_t(packetId >> 24); out[6] = uint8_t(packetId >> 16);
    out[7] = uint8_t(packetId >> 8);  out[8] = uint8_t(packetId);
    out[9] = index;
    out[10] = total;
    memcpy(out + kFragmentHeader, packet + offset, chunk);
    return kFragmentHeader + chunk;
}

inline bool decodeFragment(const uint8_t* body, size_t length, Fragment& out) {
    if (!body || length <= kFragmentHeader || length > kMaxBody) return false;
    if (body[0] != static_cast<uint8_t>(Kind::Fragment)) return false;
    const uint8_t index = body[9], total = body[10];
    if (total == 0 || index >= total) return false;
    memcpy(out.sender, body + 1, kSenderPrefix);
    out.packetId = uint32_t(body[5]) << 24 | uint32_t(body[6]) << 16 | uint32_t(body[7]) << 8 | body[8];
    out.index = index;
    out.total = total;
    out.payload = body + kFragmentHeader;
    out.payloadLength = length - kFragmentHeader;
    return true;
}

// Returns the body length, or 0 if the name is too long or does not fit.
inline size_t encodeBind(const Bind& bind, uint8_t* out, size_t capacity) {
    if (!out || bind.nameLength > kMaxNameBytes || capacity < kBindHeader + bind.nameLength) return 0;
    out[0] = static_cast<uint8_t>(bind.request ? Kind::BindRequest : Kind::Bind);
    memcpy(out + 1, bind.publicKey, kPublicKeySize);
    out[33] = bind.router ? kFlagRouter : 0;
    out[34] = bind.nameLength;
    memcpy(out + kBindHeader, bind.name, bind.nameLength);
    return kBindHeader + bind.nameLength;
}

inline bool decodeBind(const uint8_t* body, size_t length, Bind& out) {
    if (!body || length < kBindHeader || length > kMaxBody) return false;
    if (body[0] != static_cast<uint8_t>(Kind::Bind) && body[0] != static_cast<uint8_t>(Kind::BindRequest))
        return false;
    const uint8_t nameLength = body[34];
    if (nameLength > kMaxNameBytes || length != kBindHeader + nameLength) return false;
    out.request = body[0] == static_cast<uint8_t>(Kind::BindRequest);
    memcpy(out.publicKey, body + 1, kPublicKeySize);
    out.router = (body[33] & kFlagRouter) != 0;   // reserved bits ignored
    out.nameLength = nameLength;
    memcpy(out.name, body + kBindHeader, nameLength);
    out.name[nameLength] = '\0';
    return true;
}

} // namespace MeshCoreTunnel::grpdata
#endif // GRP_DATA_TUNNEL_CODEC_H
