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

  // Forget the learned MeshCore route to this peer (out_path_len back to
  // OUT_PATH_UNKNOWN), so the next DIRECT send flood-routes and the ACK
  // teaches a fresh path. Called when a DIRECT send's ACK times out. Without
  // it a path learned at close range — an empty path, "direct neighbour" —
  // is kept for ever: after the devices move apart every direct send is a
  // zero-hop unicast into nothing, waits the full ACK timeout, then falls
  // back to the channel, and the tunnel crawls at one timeout per fragment
  // no matter what the flood setting says. Found 2026-09-05: "works close,
  // fails far, flood on both ends". MeshCore's companion app resets the
  // path after failed attempts for the same reason.
  virtual bool resetDirectPath(const uint8_t* pub_key) = 0;

  // Our own MeshCore public key, hex, lowercase — the identity advertised in
  // RNSBIND. Empty until the mesh identity is loaded.
  virtual const char* selfPubKeyHex() = 0;

  // Seconds since epoch from MeshCore's RTC, or 0 if the clock is unset.
  virtual uint32_t nowEpoch() = 0;
};
