# ASynthOSC User Manual

## Overview

ASynthOSC is a hardware bridge that combines:

- 4 analog CV inputs
- 2 trigger inputs
- MIDI input
- OSC output over Ethernet UDP
- A small OLED user interface with inputs monitoring, cue recall controller, messenger and a settings menu

The firmware continuously samples the CV inputs, watches the trigger inputs, parses incoming MIDI, and publishes the resulting data as OSC messages.

The firmware also accepts a small set of incoming OSC commands for remote interaction.

## Main Features

- 4 CV inputs (16 bits ADC, two ranges (-5v,+5v) or (-12v,+12v) by DIP switch selector) sent as normalized OSC floating-point values
- 2 trigger inputs sent as OSC boolean values (`F`=low, `T`=high)
- MIDI to OSC bridge for note, CC, program change, pitch bend, transport (clock/start/stop/continue), song position, and MTC
- Incoming OSC parser with on screen messenger
- On-screen network monitor (`N`) with link state and packet activity blink
- Cue recall system with direct front-panel control
- Persistent settings storage for network, ADC, MIDI note forwarding, and current cue
- OLED input monitor, CV metering and status line with automatic scrolling for long messages

## Front Panel UI

## OLED Layout

The OLED is divided into several functional areas:

- Top-left: 3-digit cue number
- Right side: 4 vertical CV bar meters
- Top-right: activity indicators labeled `N 1 M A 2`
- Middle/lower area: scrolling status text
- Bottom row: soft labels for left, center, and right controls

### Activity Indicators

The top-right label `N 1 M A 2` corresponds to:

- `N`: Network monitor. Lit when Ethernet link is up, blinks on OSC traffic activity.
- `1`: Trigger 1 activity
- `M`: MIDI activity
- `A`: DAC output monitor. Lit while `/aout` is greater than `0`.
- `2`: Trigger 2 activity

### Status Message Area

- The status message area shows up to 8 visible characters at a time.
- Longer text scrolls automatically.
- Incoming OSC display messages use this area.
- Menu item labels are also shown here while in menu mode.

## Controls

The front panel exposes three user controls:

- Left button, labeled `Prev`
- Right button, labeled `Next`
- Rotary encoder with push button, labeled `Recl` in normal mode and `Menu` in menu mode

## Normal Mode : Show control (send cues numbers)

Normal mode is the default operating mode after boot.

### Left / Right Buttons

- `Prev`: decrement the current cue immediately and send the new cue as OSC
- `Next`: increment the current cue immediately and send the new cue as OSC

The cue value wraps:

- below `000` -> `999`
- above `999` -> `000`

### Rotary Encoder

In normal mode, turning the encoder edits a pending cue value instead of sending immediately.

- The pending cue blinks on the OLED
- User should press the encoder to validate the cue change
- If after 5 seconds the encoder isn't pressed, the cue recall canceled and the cue number is not changed


### Rotary Push Button

- Short press: confirm the pending cue and send it
- Short press with no pending edit: resend the current cue
- Long press for 2 seconds: enter menu mode

## Menu Mode : Edit settings

Menu mode is entered by holding the rotary button for 2 seconds.

When the menu is active:

- The center label changes from `Recl` to `Menu`
- The status line shows the current menu item name
- The cue area shows the current value for the selected item

### Menu Navigation

- Left button: previous menu item
- Right button: next menu item
- Rotary encoder: change current item value
- Short press on encoder: reset current item to its default value
- Long press on encoder for 2 seconds: leave menu mode

Menu navigation wraps around from first to last item and from last to first item.

### Important Behavior

- Menu changes are saved immediately to persistent storage
- The OSC target network settings are changed only when user exit the menu

## Menu Items

The firmware currently supports the following menu items.

### 1. CV period (ms)

- Meaning: CV sampling period
- Default: `20 ms`
- Range: `10 .. 200 ms`
- Step: `5 ms`

### 2. CV Hysteresis (mV)

- Meaning: minimum normalized CV change before a new OSC CV value is sent, to reduce OSC traffic to meaningful changes
- Internal storage: permille of full scale
- Default: `10/1000`
- Displayed approximately as: `33 mV`
- Range: `5/1000 .. 50/1000`
- Step: `5/1000`
- Approximate voltage range: `17 .. 165 mV`

### 3. IP Mode

- Meaning: Ethernet address assignment mode
- Values:
  - `0`: Static
  - `1`: DHCP+LL
  - `2`: DHCP+FB
- Default: `0`

### 4. Target IP

- Meaning: last octet of the OSC target IP address
- Shared subnet bytes are configured separately with `IP byte 1`, `IP byte 2`, and `IP byte 3`
- Default: `142`
- Effective default target IP: `192.168.1.142`

### 5. Target Port

- Meaning: OSC transmit port offset
- Effective port = `42000 + offset`
- Default offset: `10`
- Effective default TX port: `42010`
- Range: `0 .. 999`

### 6. Device IP

- Meaning: last octet of the board IP address
- Default: `42`
- Effective default device IP: `192.168.1.42`

### 7. Device Port

- Meaning: OSC receive port offset
- Effective port = `42000 + offset`
- Default offset: `11`
- Effective default RX port: `42011`
- Range: `0 .. 999`

### 8. CIDR Mask

- Meaning: subnet mask size
- Default: `/24`
- Range: `0 .. 32`

### 9. IP byte 1

- Meaning: first shared subnet byte
- Default: `192`

### 10. IP byte 2

- Meaning: second shared subnet byte
- Default: `168`

### 11. IP byte 3

- Meaning: third shared subnet byte
- Default: `1`

### 12. MIDI Note (0:ignore 1:thru)

- Meaning: controls whether incoming MIDI note on/off messages are forwarded to OSC
- Default: `0`
- Values:
  - `0`: do not forward note on/off to OSC
  - `1`: forward note on/off to OSC

This setting affects note on/off only. Other supported MIDI-to-OSC messages remain active.

## Default Network Configuration

Out of the box, the firmware defaults to:

- Mode: `Static`
- Device IP: `192.168.1.42`
- Netmask: `/24` (`255.255.255.0`)
- OSC target IP: `192.168.1.142`
- OSC TX port: `42010`
- OSC RX port: `42011`

## Cue Behavior

The cue value is a 3-digit number from `000` to `999`.

There are two ways to change it:

- Immediate change with `Prev` / `Next`
- Pending edit with the rotary encoder, then confirm with a short press

The cue is sent as an OSC message whenever it is recalled.

## OSC Commands

## Incoming OSC

The board listens for incoming OSC on the configured device port.

Default receive endpoint:

- IP: `192.168.1.42`
- Port: `42011`

Supported incoming commands:

| Path | Arguments | Description |
|------|-----------|-------------|
| `/ping` | none | Sends `/pong` to the configured OSC target endpoint |
| `/msg` | `string` | Displays the provided text on the OLED status line |
| `/cue` | `integer` | Remote change the current cue number on device's interface |
| `/aout` | `float` | Set DAC_OUT1 normalized value (`0.0 .. 1.0`) |
| `/idle` | none or `bool` | Blink Next button LED at ~4 Hz until Next is pressed |

Notes:

- The incoming parser accepts both single OSC messages and OSC bundles
- `/aout` values are clamped to `0.0 .. 1.0`
- OLED `A` indicator is lit while `/aout` value is greater than `0`
- If an unsupported OSC path is received, the serial log prints:
  - `OSC RX: unhandled path '/path'`

## Outgoing OSC

Outgoing OSC is sent to the configured target IP and target port.

Default transmit endpoint:

- IP: `192.168.1.142`
- Port: `42010`

### CV Output

| Path | Argument Type | Meaning |
|------|---------------|---------|
| `/cv/1` | `float` | CV input 1 normalized to `0.0 .. 1.0` |
| `/cv/2` | `float` | CV input 2 normalized to `0.0 .. 1.0` |
| `/cv/3` | `float` | CV input 3 normalized to `0.0 .. 1.0` |
| `/cv/4` | `float` | CV input 4 normalized to `0.0 .. 1.0` |

### Trigger Output

| Path | Argument Type | Meaning |
|------|---------------|---------|
| `/trig/1` | `bool` | Trigger 1 state, `F` (low) or `T` (high) |
| `/trig/2` | `bool` | Trigger 2 state, `F` (low) or `T` (high) |

### Cue Output

| Path | Argument Type | Meaning |
|------|---------------|---------|
| `/cue` | `int` | Current recalled cue value |

### Ping Response

| Path | Argument Type | Meaning |
|------|---------------|---------|
| `/pong` | none | Reply to incoming `/ping` |

### MIDI to OSC Output

The MIDI-to-OSC bridge conforms to the **OSC-MIDI Bridge Specification** (see `doc/OSC_bridge_spec.md`).
All MIDI data are passed as OSC arguments — no MIDI values are encoded in the OSC address.
Channels are exported as 1–16 to match visible MIDI conventions.

#### Notes and Controllers

| Path | Arguments | Meaning |
|------|-----------|---------|
| `/note` | `int channel, int pitch, int velocity` | Note On (velocity 1–127) **or** Note Off (velocity = 0) |
| `/control` | `int channel, int controller, int value` | Control Change |
| `/program` | `int channel, int program` | Program Change, program 0–127 |
| `/pitch` | `int channel, int value` | Pitch Bend, value 0–16383, 8192 = center |

#### Transport and Clock

| Path | Arguments | Meaning |
|------|-----------|---------|
| `/clock` | none | MIDI Timing Clock (24 per quarter note) |
| `/start` | none | MIDI Start / MMC Play |
| `/stop` | none | MIDI Stop / MMC Stop |
| `/continue` | none | MIDI Continue |
| `/songpos` | `int value` | Song Position Pointer (MIDI beats × 6 ticks, 0–16383) |

#### MIDI Time Code

| Path | Arguments | Meaning |
|------|-----------|---------|
| `/mtc` | `int hour, int minute, int second, int frame, int fps` | MTC Full Frame (fps: 24, 25, or 30) |
| `/mtc_qf` | `int piece, int value` | MTC Quarter Frame, piece 0–7, value nibble 0–15 |

Notes:

- MIDI note on/off forwarding depends on the `MIDI Note` menu setting
- CC, Program Change, Pitch Bend, Transport, and MTC forwarding are always active
- Note Off is sent as `/note` with velocity = 0; there is no separate `/note_off` message
- MMC Play/Deferred Play maps to `/start`; MMC Stop/Pause maps to `/stop`
- Pitch Bend value is the raw 14-bit MIDI value (0–16383); 8192 is the neutral center

## Serial Log Messages

Useful serial messages include:

- startup configuration summaries
- OSC RX bind confirmation
- unsupported OSC path logs
- OSC send failures
- MIDI diagnostics and dropped-byte warnings

Examples:

- `NETCFG[startup]: ...`
- `ADCCFG[startup]: ...`
- `MIDICFG[startup]: ...`
- `OSC RX: listening on UDP 42011`
- `OSC RX: unhandled path '/foo/bar'`

## Persistence

The following values are stored persistently and restored at startup:

- current cue
- CV sample period
- CV hysteresis
- network mode
- target IP last octet
- target port offset
- device IP last octet
- device port offset
- CIDR mask
- IP bytes 1 to 3
- MIDI note thru setting

## UI Messenger

- You can print messages on OLED screen by OSC. The text area shows 8 characters, and scrolls longer messages automatically.
This could be usefull to give confirmation or feedback from third show controller software to musician on stage.

## Quick Start

1. Power the board and wait for the splash screen to finish.
2. In normal mode, use `Prev` and `Next` to recall cues immediately.
3. Use the rotary encoder to preview a cue, then short-press to confirm it.
4. Hold the rotary button for 2 seconds to enter the menu.
5. Adjust network settings if required.
6. Send OSC commands to the board RX port.
7. Monitor outgoing OSC on the configured target port.

## Practical OSC Test Setup

With factory defaults:

- Board IP: `192.168.1.42`
- Incoming OSC port on board: `42011`
- OSC listener on host: `192.168.1.142:42010`

Example tests:

- Send `/ping` to `192.168.1.42:42011`
- Expect `/pong` on `192.168.1.142:42010`
- Send `/msg "Hello"` to `192.168.1.42:42011`
- Expect `Hello` on the OLED status line
