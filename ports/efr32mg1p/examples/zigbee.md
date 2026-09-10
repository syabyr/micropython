# Zigbee / IEEE 802.15.4 support notes

This port exposes raw IEEE 802.15.4 radio access through the native `radio` module and provides a set of experimental Python helpers under `modules/ZbPy`.

The current scripts are useful for packet parsing, sniffing, raw frame injection, and small experiments, but they are not a complete Zigbee coordinator/router/end-device stack.

## Native radio layer

`modules/Radio.py` is only a compatibility alias:

```python
from radio import *
```

The actual native module is `radio`, which provides:

```python
radio.init()
radio.promiscuous([value])
radio.pan([value])
radio.address([value])
radio.channel(value)
radio.mac()
radio.rx()
radio.tx(packet)
```

This is enough to send and receive raw IEEE 802.15.4 frames.

## Serial NIC bridge

`modules/NIC.py` turns the board into a simple serial radio bridge:

- receives radio packets with `Radio.rx()`
- prints received packets as hex
- reads hex from stdin
- sends decoded bytes with `Radio.tx()`

This is useful for:

- sniffing
- packet injection
- using a host-side tool to implement higher layers

It is not a Zigbee network stack by itself.

## ZbPy modules

### `ZbPy/IEEE802154.py`

Implements basic IEEE 802.15.4 MAC frame serialization/deserialization:

- beacon frames
- data frames
- ACK frames
- command frames
- short and long addressing
- PAN IDs
- ACK request flag
- basic command IDs:
  - join request
  - join response
  - data request
  - beacon request

This layer can build and parse raw 802.15.4 frames, but it does not implement a full MAC coordinator state machine.

### `ZbPy/ZigbeeNetwork.py`

Implements partial Zigbee NWK frame parsing/serialization:

- NWK data frames
- NWK command frames
- short source/destination addresses
- radius
- sequence numbers
- optional extended source/destination
- NWK security encrypt/decrypt using CCM*
- some command constants:
  - route request
  - route reply
  - network status
  - leave
  - route record
  - rejoin request/response
  - link status
  - network report/update

The file explicitly notes incomplete support:

```python
# Not properly supported:
# * Multicast
# * Route discovery
# * Source routing
```

These missing pieces are important for a real Zigbee mesh network.

### `ZbPy/ZigbeeApplication.py`

Implements partial APS frame parsing/serialization:

- unicast
- broadcast
- group mode
- endpoints
- cluster ID
- profile ID
- APS sequence
- payload

APS security is not implemented.

### `ZbPy/ZCL.py`

Implements a small subset of Zigbee Cluster Library helpers.

Currently includes partial definitions for clusters such as:

- Basic
- PowerConfig
- DeviceTemp
- Identify
- Groups
- Scenes
- OnOff
- LevelControl

This can be used to construct or parse some simple commands, for example `OnOff.On`, `OnOff.Off`, and some level control commands.

### `ZbPy/Parser.py`

A convenience parser that tries to decode a raw packet through:

```text
IEEE802154 -> ZigbeeNetwork -> ZigbeeApplication -> ZCL
```

Good for sniffing and debugging.

### `ZbPy/Device.py`

Contains experimental device logic:

- `IEEEDevice`
  - RX dispatch
  - duplicate filtering
  - ACK tracking/retry
  - beacon request
  - join request
  - data request
  - join response handling

- `NetworkDevice`
  - NWK parse/decrypt
  - NWK TX
  - NWK leave
  - partial handling/printing of route/link/status commands

It uses the well-known zigbee2mqtt network key by default:

```python
01030507090b0d0f00020406080a0c0d
```

The code comments note that joining networks with other keys, such as IKEA gateway keys, is not implemented.

## What can be implemented now

### 1. A simple custom IEEE 802.15.4 network

This is the most realistic use case with the current code.

You can use:

- `radio`
- `ZbPy.IEEE802154`

And fix these parameters manually:

- channel
- PAN ID
- short addresses
- payload format

This gives a small custom wireless network, but it is not Zigbee-compatible beyond the 802.15.4 MAC framing.

### 2. A Zigbee sniffer/parser

Use:

```python
import Radio
from ZbPy import Parser

Radio.init()
Radio.promiscuous(True)

while True:
    pkt = Radio.rx()
    if pkt:
        parsed, layer = Parser.parse(pkt, verbose=True)
        print(layer, parsed)
```

This is useful for analyzing traffic from existing Zigbee networks.

### 3. Limited Zigbee frame injection

If you already know the target network details, you can construct limited Zigbee frames:

- channel
- PAN ID
- source short address
- destination short address
- network key
- security frame counter
- endpoint
- cluster/profile

This can be used for experiments such as sending simple On/Off or LevelControl frames, but it is fragile because real Zigbee devices enforce security counters, addressing, routing, and APS/NWK state.

## What is not complete

The current code is not enough to implement a standards-compliant Zigbee coordinator.

Missing or incomplete areas include:

- coordinator network formation
- channel scan / PAN selection
- permit join
- association request/response handling
- full rejoin handling
- Trust Center behavior
- transport network key
- install-code/default-link-key handling
- network key update
- persistent frame counters
- neighbor table
- route discovery
- route reply
- route table
- link status processing
- source routing
- multicast
- APS ACK
- APS security
- binding table
- group table
- ZDO/ZDP server/client flows
- simple descriptor / active endpoint / match descriptor handling

## Recommended path

### For short-term experiments

Build a simple fixed-parameter 802.15.4 network first.

Example direction:

- node A: fixed PAN `0x1234`, address `0x0001`
- node B: fixed PAN `0x1234`, address `0x0002`
- both use one fixed channel
- payload is a custom application packet

This avoids Zigbee join/security/routing complexity.

### For Zigbee analysis

Use the board as a sniffer or serial NIC:

- `modules/NIC.py`
- `ZbPy/Parser.py`

### For real Zigbee coordinator support

Use a production Zigbee stack instead of implementing the complete stack in MicroPython scripts.

Practical options:

- Silicon Labs EmberZNet / Zigbee stack
- RCP/NCP mode with a host stack
- host software such as zigbee-herdsman or zigpy

## Conclusion

Current capability:

- raw IEEE 802.15.4 TX/RX
- simple serial radio bridge
- Zigbee packet parsing
- partial Zigbee NWK/APS/ZCL serialization
- limited experimental Zigbee-like node behavior

Not currently supported:

- complete Zigbee coordinator
- general-purpose commercial-device joining
- stable Zigbee mesh routing
- full Trust Center/security lifecycle

So this code is best viewed as a Zigbee/802.15.4 packet toolkit and experimental stack fragment, not a complete Zigbee network implementation.
