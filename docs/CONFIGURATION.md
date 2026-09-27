# Configuration reference

Every user-facing setting of the RNS Gateway firmware, what it does, its
default, and where it is stored. Read [First-time setup](FIRST_TIME_SETUP.md)
first if the device is new; this page is the reference you come back to.

Applies to all release variants on both boards (Heltec WiFi LoRa 32 V4 and
LilyGo T-Beam Supreme SX1262): Stationary, Mobile WiFi, Mobile BLE, and the
prop-restricted builds. The firmware is identical; variants differ only in
first-boot defaults, which are listed at the end.

> **The tunnel only connects between gateways on the same MeshCore
> channel.** Every site you want to reach must have the identical channel
> name and PSK entered in its own gateway's *MeshCore bridge channel*
> section. Radio parameters must match too. A gateway on a different
> channel is invisible, with no error on either side.

## Where settings live

There is no serial configuration step. Everything is set in the **web
portal** and stored on the device's flash filesystem, where it survives
reflashes (the filesystem is a separate partition from the firmware).

| Store | File | Holds | Cleared by |
|---|---|---|---|
| Gateway config | `/rns_gateway.json` | Everything in this document except the node name, radio and clock | Portal **Factory reset** |
| MeshCore prefs | `/prefs.json` | Node name, LoRa radio parameters | MeshCore `erase` CLI, or a full flash erase |
| Reticulum identity | `/transport_identity` and friends | The gateway's Reticulum transport identity | Full flash erase only |

Two consequences worth knowing:

- **Reflashing does not reset the device.** Passwords, channel PSK, radio
  parameters and client-access mode all persist across a firmware update.
  To return to shipped defaults, use Factory reset in the portal, or erase
  the whole flash with `esptool.py erase_flash` before reflashing.
- **A corrupt or missing config never bricks the device.** If the file is
  unreadable the firmware logs it and boots on the variant's defaults with
  the setup access point up, so the portal is always reachable.

Stored values always win over the build-time defaults. The defaults only
apply until the first save.

## Reaching the portal

| Situation | Address | Login |
|---|---|---|
| On the device's own access point | `http://192.168.4.1/` (captive portal should open automatically) | user `admin`, portal password |
| On a network the device has joined | `http://<hostname>.local/` or the device's IP | same |
| Bluetooth mode | No portal. Hold PRG 10 s and release for a one-off WiFi setup session, then as above | same |

The portal password ships as `password`. Change it on first login.

The portal has one form. **Save & reboot** writes every section at once and
restarts the device; the only exception is the Clock, which has its own
button and applies immediately without a reboot.

## Settings by portal section

### Client access

| Field | Values | Default | Notes |
|---|---|---|---|
| Radio clients connect over | WiFi / Bluetooth LE | WiFi (Mobile BLE variant: Bluetooth LE) | One mode per boot, never both |

**WiFi** runs the access point and station described below plus the RNS
TCP server. **Bluetooth LE** runs a `ble-reticulum` peripheral for Columba
(Android and iOS) and Linux clients; the WiFi radio is never started, so the
portal is unreachable until you hold PRG for a setup session. The device
advertises as `RNS-` followed by eight hex characters derived from its
identity. No pairing is needed.

A Bluetooth-mode device with **no channel PSK saved** boots into the WiFi
setup session on its own, every boot, until it has one. Details and the
reasoning are in [Bluetooth LE client access](BLE_CLIENT_ACCESS.md).

### WiFi station

| Field | Default | Notes |
|---|---|---|
| Join an existing network | off | Stationary use: join the site network so clients on that network reach the gateway |
| SSID | empty | up to 32 characters |
| Password | empty | up to 64 characters |

When a station is joined, the clock is set from NTP automatically and the
portal reports the station's DHCP address. The device keeps retrying the
network every 10 seconds if it drops.

Do not join two mesh-linked gateways to networks that can reach each other.
Reticulum will route between them over IP instead of the mesh. Everything
still works, but the tunnel carries nothing.

### Access point

| Field | Default | Range | Notes |
|---|---|---|---|
| Host an access point | on | | |
| AP SSID | variant name, see below | up to 32 chars | |
| AP password | `rnsgateway` | up to 64 chars, blank = open | Change it |
| AP channel | 1 | 1 to 13 | Only used when no station is joined |

The ESP32 has one WiFi radio. Once the station joins a network, the AP
moves to that network's channel and drops its clients while doing so. If
every client is on the site network, the cleanest setup is station on, AP
off.

The AP is raised regardless of this checkbox in two cases, so the portal
can always be reached: during a setup session, and whenever neither station
nor AP is enabled.

### RNS TCP server

| Field | Default | Range |
|---|---|---|
| Accept Reticulum clients | on | |
| Listen port | 4242 | 1 to 65535 |

This is what client apps connect to as a `TCPClientInterface`. The portal's
top section prints a ready-made config stanza with the current hostname and
port. Limits: **4 simultaneous clients**; a client that sends nothing for
**10 minutes** is dropped and must reconnect, which healthy apps do on their
own. The server is always off in Bluetooth mode.

### mDNS

| Field | Default | Notes |
|---|---|---|
| Advertise over mDNS | on | |
| Hostname (without .local) | variant hostname, see below | up to 30 chars |

Gives clients a stable `<hostname>.local` name instead of a DHCP address.
Needs multicast to pass between the client and the gateway; some routers
and most guest networks block it. Fall back to the IP if the name does not
resolve.

### MeshCore node

| Field | Default | Notes |
|---|---|---|
| Node name | `RNS GW Stationary`, `RNS GW Mobile` or `RNS GW BLE` plus a unique suffix | up to 31 chars, stored in MeshCore prefs |

This is the name other MeshCore users see in their contact list. A device
still carrying the build default gets a suffix derived from its identity so
two gateways never show the same name. Give it a name of your own.

### MeshCore bridge channel

| Field | Default | Notes |
|---|---|---|
| Channel name | `RNSTesting` | up to 30 chars |
| Channel PSK (base64) | empty | base64 of a 16- or 32-byte key |

The gateway bridges nothing until a PSK is saved. Create a **private**
channel in the MeshCore app, dedicated to the tunnel, and paste its key
here.

**Recipients must be on the same channel.** The tunnel is carried as
messages on this channel, so a Reticulum client can only reach clients
behind gateways that have the identical channel name and PSK. Share the
channel with every site you intend to talk to, and with nobody else.
Channels match by PSK hash, not by slot index, so the channel can sit in a
different slot on each device. The public channel cannot be used.

### Tunnel policy

These control how much of the shared LoRa airtime the tunnel may consume.
The defaults are tuned for a roughly 300 bit/s mesh and should be left
alone unless you understand the cost.

| Field | Default | Range | What it does |
|---|---|---|---|
| Tunnel format | Text | Text or GRP_DATA | How RNS packets are framed on the bridge channel. Text is the original `RNS:<base64url>` channel-message format, compatible with the Python reference. GRP_DATA is the binary format in [GRP_DATA_TUNNEL.md](GRP_DATA_TUNNEL.md), used by the Ratspeak handheld: about twice the payload per fragment, channel only (no direct leg yet). Every node on the channel must use the same format; each ignores the other's traffic. Stored as `tunnel_format` (0 text, 1 GRP_DATA). |
| Path-request throttle, seconds per destination | 1800 | 0 to 86400 | Minimum interval between path requests for the same destination. 0 disables the throttle, which lets a freshly booted client storm hundreds of requests onto the channel. Bring-up only. |
| Announce throttle, seconds per destination | 600 | 0 to 86400 | Minimum interval between rebroadcasts of the same destination's announce. Lower it to reconverge faster after reboots, at the cost of airtime. |
| Route tunnel via mesh repeaters (flood) | off | | Off means zero-hop: repeaters do not re-flood tunnel traffic. Leave it off whenever the peer gateway is in direct radio range. On makes every repeater in the region retransmit tunnel traffic. |
| Flood scope | blank | blank, `*`, or a region name up to 30 characters | The MeshCore region the gateway's floods (tunnel traffic when flooding is on, and flood adverts) are sent in. Blank or `*` floods unscoped, MeshCore's wildcard region. A public region name (`name` or `#name`) sends transport floods for that region, keyed exactly as repeaters and the companion app key it, so repeaters restricted to regions still forward the traffic. Private `$` regions need a shared key and are not supported; they fall back to unscoped. Stored as `flood_scope`. |
| Airtime budget, KB per hour | 60 | 0 to 200 | Rolling one-hour cap on bytes the tunnel puts on the air. 0 is unlimited. |
| Prop destination hashes | empty | up to 4 hashes, 32 hex chars each, comma-separated | Whitelist for the prop-restricted builds. Other builds store and ignore it. |

**How the airtime budget sheds.** Above 80% of the budget the gateway stops
sending announces and path requests, since those are recoverable on demand.
At 100% it stops sending everything until the rolling window frees space.
Shed events are counted and logged. The default of 60 KB is about 45% of
what the link could theoretically carry in an hour, leaving room for
MeshCore's own users.

**Prop-restricted builds** (`…_stationary_prop`, `…_mobile_wifi_prop`)
carry only traffic addressed to the listed LXMF propagation-node
destinations, plus links established with them. Announces and path
requests still pass. With an empty list these builds block all link and
data traffic onto the mesh, so fill in the whitelist before relying on one.
Learned paths are RAM-only by design, so a gateway reconverges on its own
after a reboot; first contact with a destination can take up to a minute.

### Portal access

| Field | Default | Notes |
|---|---|---|
| New portal password | `password` | up to 32 chars, blank keeps the current one |

Login user is always `admin`. The password protects the portal, the `/log`
and `/lastlog` pages, and Factory reset.

### LoRa radio

| Field | Default | Range |
|---|---|---|
| Frequency (MHz) | 910.525 | |
| Bandwidth (kHz) | 62.5 | |
| SF | 7 | 5 to 12 |
| CR | 5 | 5 to 8 |
| TX dBm | board default | 0 to 30 |

Stored in MeshCore prefs, and applied through MeshCore's own `set radio`
path so there is one source of truth. Values must match the target mesh
**exactly**. This is a silent failure: with wrong parameters nothing
errors, the node simply never hears the mesh. If two gateways cannot see
each other, compare these on both, character by character, before anything
else. The defaults are the US regional MeshCore parameters.

### Clock

| Control | Effect |
|---|---|
| Set clock from this browser | Sends the browser's current time to the device. Applies immediately, no reboot. |

MeshCore stamps every packet with this clock. With a station joined it is
set from NTP automatically. A Bluetooth-mode device has no network, so set
it here during the setup session, or from GPS on a board that has one. The
page shows the device's current time, or "NOT SET" if it is still counting
from boot.

### Factory reset

Erases `/rns_gateway.json` and reboots on the variant's defaults: shipped
AP name and password, portal password `password`, no station, no channel
PSK, WiFi client access. The node name, radio parameters and Reticulum
identity are **not** touched.

## The PRG button

All gestures act on **release**, because PRG is the boot-strapping pin and
resetting while it is held drops into the serial bootloader.

| Hold | On release | Feedback while holding |
|---|---|---|
| Short press | Advances the OLED page, if a display is fitted | |
| 5 to 10 s | **Powers off** (deep sleep). Press RST or power-cycle to start again | TX LED solid from 5 s; OLED says "Release: POWER OFF" |
| 10 s or more | Reboots into a one-off **WiFi setup session** with stored settings untouched | TX LED blinks from 10 s |

The setup session brings up the AP and portal for one boot only. The
request lives in RTC memory, so it survives the software reset but not a
power cycle. It is the way back to the portal from Bluetooth mode, and it
also works in WiFi mode if the AP or station settings were saved wrong.
Before rebooting, the firmware saves its log ring to flash so the setup
session's `/lastlog` page shows what happened before the button was held.

## OLED status screen

On boards with a display fitted, a short PRG press cycles through:

1. **STATUS**: variant name, address and port with TCP state (or the BLE
   name and advertising state), client and peer counts, radio parameters,
   tunnel TX/RX.
2. **PEERS**: bound peer gateways with name, capability flag and how long
   since each was last heard.
3. **TUNNEL**: direct versus fallback sends, announce drops, queue depth,
   routes, bind counts, routing mode (0HOP or FLOOD), free heap.
4. **BLE** (Bluetooth mode only): connected centrals, negotiated MTU,
   packet and fragment counters each way, and every drop cause.

While no channel PSK is configured a **SETUP** page replaces these and
shows the AP to join, or the PRG hold to perform. The display blanks after
60 seconds; any PRG press wakes it. A display is optional; the firmware
runs the same without one.

## Logs

Opening or closing USB serial **reboots this board** (native USB CDC), so
do not diagnose over the serial port. Instead:

| Page | Contents |
|---|---|
| `/log` | The live log ring (kept in PSRAM), same login as the portal |
| `/lastlog` | The log saved to flash just before a PRG-triggered reboot into a setup session |

The boot log prints the **active** radio parameters as loaded from prefs.
Trust that line over anything in a build file, since a `/prefs.json` left
behind by earlier firmware survives a reflash and correctly wins.

## Variant defaults

Same firmware, different first-boot identity, so two gateways in one
household never collide on SSID or hostname. Image names follow
`rns-gateway-<variant>-<board>-vX.Y.Z.bin`.

| | Stationary | Mobile WiFi | Mobile BLE |
|---|---|---|---|
| Intended life | Lives at a site, joined to the local network | Travels; phones join its AP | Travels with a phone over Bluetooth |
| Client access | WiFi | WiFi | Bluetooth LE |
| AP SSID | `RNSGateway-Stationary` | `RNSGateway-Mobile` | `RNSGateway-BLE` (setup session only) |
| AP password | `rnsgateway` | `rnsgateway` | `rnsgateway` |
| Hostname | `rnsgateway-stationary` | `rnsgateway-mobile` | `rnsgateway-ble` |
| Mesh node name | `RNS GW Stationary` | `RNS GW Mobile` | `RNS GW BLE` |
| Boards | `heltec_v4`, `tbeam_supreme` | `heltec_v4`, `tbeam_supreme` | `heltec_v4`, `tbeam_supreme` |
| Prop-restricted build | yes | yes | no |

Everything else is identical across variants: portal user `admin`, portal
password `password`, TCP port 4242, no station credentials, no channel PSK,
US radio defaults, and the tunnel policy defaults above.

## Copyable checklist for a new site

1. Flash, power-cycle, join the setup AP, open `http://192.168.4.1/`.
2. Change the AP password and the portal password.
3. Paste the private channel name and PSK. Use the same channel on every
   gateway you want to reach.
4. Confirm the radio parameters match your mesh.
5. Rename the node.
6. Stationary only: enter the site network credentials.
7. Mobile BLE only: set the clock from the browser, then switch Client
   access to Bluetooth LE if it is not already.
8. Save & reboot, then check `/log` for the active radio line and the
   `RNSBIND` exchange with the peer gateway.
