// Host golden tests for examples/rns_gateway/GrpDataTunnelCodec.h against the
// normative spec docs/GRP_DATA_TUNNEL.md. The example in spec section 5 is
// reproduced byte for byte; the Ratspeak handheld runs the same vectors.
// Run:  ./scripts/run_host_tests.sh

#include <cstdio>
#include <cstring>

#include "GrpDataTunnelCodec.h"

using namespace MeshCoreTunnel::grpdata;

static int failures = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

static const uint8_t kSender[4] = {0x3a, 0x7c, 0x01, 0xe9};

static void constantsMatchSpec() {
    static_assert(kMaxFragmentPayload == 154, "spec 2.1");
    static_assert(kBindHeader == 35, "spec 2.2");
    CHECK(fragmentCount(0) == 0);
    CHECK(fragmentCount(1) == 1);
    CHECK(fragmentCount(154) == 1);
    CHECK(fragmentCount(155) == 2);
    CHECK(fragmentCount(500) == 4);                        // RNS MTU (spec 2.1)
    CHECK(fragmentCount(255 * 154) == 255);
    CHECK(fragmentCount(255 * 154 + 1) == 0);
}

static void bindRequestMatchesSpecExample() {
    Bind bind;
    bind.request = true;
    memcpy(bind.publicKey, kSender, 4);
    for (int i = 4; i < 32; ++i) bind.publicKey[i] = uint8_t(i);
    bind.router = false;
    bind.nameLength = 11;
    memcpy(bind.name, "Nyx T-Pager", 11);

    uint8_t body[kMaxBody];
    const size_t length = encodeBind(bind, body, sizeof(body));
    CHECK(length == 46);                                   // spec 5: body length 46
    CHECK(body[0] == 0x03);
    CHECK(memcmp(body + 1, kSender, 4) == 0);
    CHECK(body[33] == 0x00);
    CHECK(body[34] == 0x0b);
    CHECK(memcmp(body + 35, "\x4e\x79\x78\x20\x54\x2d\x50\x61\x67\x65\x72", 11) == 0);

    Bind decoded;
    CHECK(decodeBind(body, length, decoded));
    CHECK(decoded.request);
    CHECK(!decoded.router);
    CHECK(memcmp(decoded.publicKey, bind.publicKey, 32) == 0);
    CHECK(std::strcmp(decoded.name, "Nyx T-Pager") == 0);
}

static void bindRejectsBadLengthsAndIgnoresReservedFlags() {
    Bind bind;
    bind.router = true;
    uint8_t body[kMaxBody];
    const size_t length = encodeBind(bind, body, sizeof(body));
    CHECK(length == 35);
    CHECK(body[0] == 0x02 && body[33] == 0x01);
    Bind decoded;
    body[33] = 0xFF;                                       // reserved bits set
    CHECK(decodeBind(body, length, decoded) && decoded.router);
    CHECK(!decodeBind(body, length - 1, decoded));         // truncated
    body[34] = 1;                                          // name_len disagrees with length
    CHECK(!decodeBind(body, length, decoded));
    body[34] = 32;                                         // name too long
    CHECK(!decodeBind(body, 35 + 32, decoded));
    bind.nameLength = 32;
    CHECK(encodeBind(bind, body, sizeof(body)) == 0);
}

static void fragmentRoundTripMatchesSpecExample() {
    uint8_t packet[500];
    for (size_t i = 0; i < sizeof(packet); ++i) packet[i] = uint8_t(i * 7);
    uint8_t body[kMaxBody];
    // Spec 5: second fragment of a 500-byte packet, pkt_id 0x0badf00d.
    const size_t length = encodeFragment(kSender, 0x0badf00d, 1, packet, sizeof(packet), body, sizeof(body));
    CHECK(length == 11 + 154);
    const uint8_t header[11] = {0x01, 0x3a, 0x7c, 0x01, 0xe9, 0x0b, 0xad, 0xf0, 0x0d, 0x01, 0x04};
    CHECK(memcmp(body, header, 11) == 0);
    CHECK(memcmp(body + 11, packet + 154, 154) == 0);      // bytes 154..307

    Fragment fragment;
    CHECK(decodeFragment(body, length, fragment));
    CHECK(fragment.packetId == 0x0badf00d);
    CHECK(fragment.index == 1 && fragment.total == 4);
    CHECK(memcmp(fragment.sender, kSender, 4) == 0);
    CHECK(fragment.payloadLength == 154 && fragment.payload == body + 11);

    // Last fragment carries the 38-byte remainder.
    CHECK(encodeFragment(kSender, 1, 3, packet, sizeof(packet), body, sizeof(body)) == 11 + 38);
    CHECK(encodeFragment(kSender, 1, 4, packet, sizeof(packet), body, sizeof(body)) == 0);   // past the end
    CHECK(encodeFragment(kSender, 1, 0, packet, sizeof(packet), body, 100) == 0);            // no room
}

static void fragmentRejectsMalformed() {
    uint8_t body[kMaxBody] = {0x01, 0, 0, 0, 0, 0, 0, 0, 0, /*idx*/ 0, /*total*/ 1, 0xAA};
    Fragment fragment;
    CHECK(decodeFragment(body, 12, fragment));
    CHECK(!decodeFragment(body, 11, fragment));            // no payload
    body[10] = 0;
    CHECK(!decodeFragment(body, 12, fragment));            // total 0
    body[10] = 2; body[9] = 2;
    CHECK(!decodeFragment(body, 12, fragment));            // idx >= total
    CHECK(!decodeFragment(body, kMaxBody + 1, fragment));  // too long
    body[0] = 0x02;
    CHECK(!decodeFragment(body, 12, fragment));            // wrong kind
}

static void unknownKindsAreDropped() {
    const uint8_t reserved[] = {0x04, 0x00};
    Kind kind;
    CHECK(!kindOf(reserved, sizeof(reserved), kind));
    CHECK(!kindOf(reserved, 0, kind));
    const uint8_t fragment[] = {0x01};
    CHECK(kindOf(fragment, 1, kind) && kind == Kind::Fragment);
}

int main() {
    constantsMatchSpec();
    bindRequestMatchesSpecExample();
    bindRejectsBadLengthsAndIgnoresReservedFlags();
    fragmentRoundTripMatchesSpecExample();
    fragmentRejectsMalformed();
    unknownKindsAreDropped();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("grp_data codec: PASS\n");
    return 0;
}
