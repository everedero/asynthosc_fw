# AsynthOsc Zephyr firmware

This repository contains the firmware application for the AsynthOsc project.

This repository is versioned together with the [Zephyr main tree][zephyr]. This
means that every time that Zephyr is tagged, this repository is tagged as well
with the same version number, and the [manifest](west.yml) entry for `zephyr`
will point to the corresponding Zephyr tag. For example, the `asynthosc-application`
v2.6.0 will point to Zephyr v2.6.0. Note that the `main` branch always
points to the development branch of Zephyr, also `main`.

## Getting Started

Before getting started, make sure you have a proper Zephyr development
environment. Follow the official
[Zephyr Getting Started Guide](https://docs.zephyrproject.org/latest/getting_started/index.html).

### Initialization

The first step is to initialize the workspace folder (``my-workspace``) where
the ``asynthosc-application`` and all Zephyr modules will be cloned. Run the following
command:

```shell
# initialize my-workspace for the asynthosc-application (main branch)
west init -m https://github.com/everedero/asynthosc_fw --mr main my-workspace
# update Zephyr modules
cd my-workspace
west update
```

### Building and running

To build the application, run the following command:

```shell
cd asynthosc-fw
BOARD=asynthosc
west build -b $BOARD app
```

You can use the `asynthosc` board found in this
repository. Note that Zephyr sample boards may be used if an
appropriate overlay is provided (see `app/boards`).

A sample debug configuration is also provided. To apply it, run the following
command:

```shell
west build -b $BOARD app -- -DEXTRA_CONF_FILE=debug.conf
```

Once you have built the application, run the following command to flash it:

```shell
west flash
```

### Testing

To execute Twister integration tests, run the following command:

```shell
west twister -T tests --integration
```

### Documentation

A minimal documentation setup is provided for Doxygen and Sphinx. To build the
documentation first change to the ``doc`` folder:

```shell
cd doc
```

Before continuing, check if you have Doxygen installed. It is recommended to
use the same Doxygen version used in [CI](.github/workflows/docs.yml). To
install Sphinx, make sure you have a Python installation in place and run:

```shell
pip install -r requirements.txt
```

API documentation (Doxygen) can be built using the following command:

```shell
doxygen
```

The output will be stored in the ``_build_doxygen`` folder. Similarly, the
Sphinx documentation (HTML) can be built using the following command:

```shell
make html
```

The output will be stored in the ``_build_sphinx`` folder. You may check for
other output formats other than HTML by running ``make help``.

## Bringup todo list
* Debug LEDs: OK
* Buttons: OK
* Buttons LEDs: OK, make markings for equipment
* UART2: OK
* Enable 10V: OK
* USB data: OK, make markings for equipment
* QSPI flash: OK
* MDIO bus / RMII / Ethernet: KO, wrong ldo retroalim
* Display: OK, verify grey lines, maybe voltage bias issue
* DAC: TBD
* Analog: TBD
* MIDI: TBD
