#pragma once

#include <Arduino.h>
#include <stdint.h>

/**
 * The seam where MeshCoreInterface used to talk to a companion radio over UART.
 *
 * In the two-board bridge this was MeshCoreCompanion (SH3D/meshcore_c) driving
 * a Heltec V4 across Serial1. On one device the radio is in the same binary, so
 * everything that existed only to cross that wire — the framed protocol, the
 * owned drain state machine, SyncNextMessage, the handshake and liveness
 * watchdogs, parser resync — is gone. What remains is the four operations the
 * tunnel actually needed.
 *
 * Implemented by MyMesh. Calls arrive on the RNS task (core 0) and are queued
 * across to the MeshCore task (core 1), because MeshCore is not thread-safe.
 */
class MeshCoreLink {
public:
  virtual ~MeshCoreLink() { }

  // Broadcast one tunnel fragment on the bridge channel. MeshCore prepends
  // "<our name>: " itself, which is the same framing the companion protocol
  // produced and what the Python reference expects.
  virtual bool sendChannelText(const char* text, uint32_t timestamp) = 0;

  // Unicast one tunnel fragment to a known MeshCore contact (direct routing).
  // 'pub_key' is PUB_KEY_SIZE bytes. Returns false when the contact is unknown,
  // which the caller treats as a direct-send failure and falls back to channel.
  virtual bool sendDirectText(const uint8_t* pub_key, const char* text, uint32_t timestamp) = 0;

  // Guarantee a MeshCore contact exists for this peer, so a later DIRECT send
  // has an encryption target. The tunnel's peer map (RNSBIND) and MeshCore's
  // contact list are separate stores: contacts normally form only from a peer's
  // advert, so a peer restored from flash — or bound before its advert arrived
  // — is routable in the map but unsendable in MeshCore. A contact created here
  // is pathless (out_path_len = OUT_PATH_UNKNOWN): the first send flood-routes,
  // still peer-encrypted, and the ACK teaches the path. 'pub_key' is
  // PUB_KEY_SIZE bytes; 'name' is the peer's node name.
  virtual bool ensureContact(const uint8_t* pub_key, const char* name) = 0;

  // Our own MeshCore public key, hex, lowercase — the identity advertised in
  // RNSBIND. Empty until the mesh identity is loaded.
  virtual const char* selfPubKeyHex() = 0;

  // Seconds since epoch from MeshCore's RTC, or 0 if the clock is unset.
  virtual uint32_t nowEpoch() = 0;

  // On-air time, in ms, of one CHANNEL fragment of this text length on the
  // radio's current parameters; 0 when the tunnel is in zero-hop mode (no
  // repeater echo to clear). The tunnel multiplies it by
  // Config::flood_gap_airtimes to space fragments past the flood's echo:
  // every repeater re-transmits a fragment after a random 0-2 airtimes
  // (Mesh::getRetransmitDelay), and each further hop adds the same again,
  // so echoes of fragment 0 keep arriving for several airtimes. A second
  // fragment sent inside that window collides with an echo at the receiver
  // and both are lost — observed 2026-09-06 as "fragment 0 arrives,
  // fragment 1 never does", both directions, only with flood on.
  virtual uint32_t channelFragAirtimeMs(size_t text_len) = 0;

  // ── GRP_DATA tunnel format (docs/GRP_DATA_TUNNEL.md) ───────────────────
  // Broadcast one GRP_DATA tunnel body (1..165 bytes) on the bridge channel,
  // with data_type GrpDataType::RnsTunnel. Same queueing as sendChannelText.
  virtual bool sendChannelData(const uint8_t* body, size_t len) = 0;

  // As channelFragAirtimeMs, for a GRP_DATA body of this length.
  virtual uint32_t channelDataAirtimeMs(size_t body_len) = 0;

  // Our MeshCore node name, carried in GRP_DATA Bind messages.
  virtual const char* selfName() = 0;
};
