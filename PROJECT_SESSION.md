# AsynthOsc Firmware Session

This file is a single entry point to start a new development session from this project folder with all key documentation references.

## Project

- Repository: `asynthosc_fw`
- Firmware target board: `asynthosc` (STM32H743VITx)
- Main app path: `app/`
- Build system: Zephyr + West + CMake/Ninja

## Canonical Paths

- Workspace root: `<workspace-root>`
- Firmware repo root: `<workspace-root>/asynthosc_fw`
- Active Zephyr source tree (single): `<workspace-root>/zephyr`
- Canonical build directory: `<workspace-root>/build`
- `asynthosc_fw/zephyr/module.yml` is module metadata, not a second Zephyr tree.

## Session Quick Start (Windows PowerShell)

```powershell
cd <workspace-root>
& ".\.venv\Scripts\Activate.ps1"
west build -p always -b asynthosc asynthosc_fw/app
west flash --skip-rebuild --build-dir build --runner blackmagicprobe -- --gdb-serial COM3
```

## Private Local Notes

- Put personal setup/build notes in `local_docs/` or `PROJECT_SESSION.local.md`.
- These paths are ignored by Git and will not be pushed to public repositories.

Alternative flash runner:

```powershell
west flash --build-dir build --runner=stm32cubeprogrammer
```

Native simulation:

```powershell
west build -b native_sim/native/64 app
.\build\zephyr\zephyr.exe -display_zoom_pct=200
```

Tests:

```powershell
west twister -T app --integration
west twister -T tests/lib/tinyosc/ --integration
```

## Source Map

- `app/src/main.c`: main firmware loop and display/UI behavior
- `app/src/osc_schema.c`: OSC schema handling
- `include/app/osc_schema.h`: OSC schema public interface
- `drivers/`: custom drivers (`blink/`, `sensor/`)
- `lib/tinyosc/`: TinyOSC library integration
- `boards/rederotech/asynthosc/`: custom board support files
- `dts/bindings/`: device tree bindings
- `tests/lib/`: test targets

## Documentation Index

Core project docs:

- `README.md`: project overview, probe/UART wiring, build/flash/test notes, docs generation
- `INSTALL.md`: Windows-first setup guide for Zephyr/West toolchain
- `LICENSE`: licensing terms

Application and board docs:

- `app/README.rst`: CFB sample-style app documentation
- `boards/rederotech/asynthosc/doc/index.rst`: board hardware and feature documentation

Documentation system files:

- `doc/index.rst`: Sphinx documentation entry point
- `doc/zephyr.rst`: Zephyr cross-reference page
- `doc/conf.py`: Sphinx configuration
- `doc/requirements.txt`: Python packages for Sphinx/doc tooling
- `doc/Makefile`: Sphinx build make targets
- `doc/make.bat`: Sphinx build helper for Windows
- `doc/Doxyfile`: Doxygen configuration
- `doc/_doxygen/main.md`: Doxygen main page
- `doc/_doxygen/groups.dox`: Doxygen group definitions (`drivers`, `lib`)
- `doc/OSC_dictionnary.csv`: OSC dictionary/reference data

Library and sample docs:

- `lib/tinyosc/README.md`: TinyOSC usage and API examples
- `samples/adc_sequence/README.rst`: ADC sequence sample notes
- `samples/lvgl/README.rst`: LVGL sample notes
- `samples/midi_usb/README.rst`: USB MIDI sample notes
- `samples/osc_sock_client/README.md`: UDP OSC socket sample notes

## Build Documentation

From the repository root:

```powershell
cd doc
pip install -r requirements.txt
doxygen
make html
```

Expected outputs:

- Doxygen: `doc/_build_doxygen/`
- Sphinx HTML: `doc/_build_sphinx/`

## Suggested Session Workflow

1. Activate the correct virtual environment.
2. Build `app` for board `asynthosc` from the workspace root.
3. Flash and validate serial logs/UI behavior.
4. Run Twister tests for app and TinyOSC library.
5. Regenerate docs after API or architecture changes.

## Active Carryover Session

- Topic: Network interface and OSC over UDP implementation.
- Repo memory summary: `/memories/repo/osc-udp-project-status.md`.
- Current blocking items:
	- Add `#include <stdint.h>` in `include/app/lib/tinyosc.h`.
	- Enable networking options in `app/prj.conf`.
	- Replace OSC send stubs in `app/src/main.c` with live transport calls.
- Current code default network values (`app/src/main.c`):
	- IP mode: `Static` (`ip_mode = 0`)
	- Base subnet bytes: `192.168.1` (`ip_b1=192`, `ip_b2=168`, `ip_b3=1`)
	- Local IP (device): `192.168.1.42` (`device_ip4 = 42`)
	- Target IP: `192.168.1.142` (`target_ip4 = 142`)
	- CIDR mask: `/24` (`cidr_mask = 24`)
	- Port base: `42000`
	- TX target port: `42010` (`target_port = 10`, effective = `42000 + 10`)
	- RX device port: `42011` (`device_port = 11`, effective = `42000 + 11`)

## Notes

- A few documentation files still use placeholder naming such as "Example Application" in `doc/conf.py` and `doc/Doxyfile`.
- If you want, these can be aligned to AsynthOsc naming in a follow-up cleanup pass.
