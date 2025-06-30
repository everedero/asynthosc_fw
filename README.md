# AsynthOsc Zephyr RTOS firmware

This repository contains the firmware application for the AsynthOsc project.
The hardware design for the board is available [here](https://github.com/everedero/asynth2osc).

This board is based upon STM32H743VITx.

This repository depends on the Zephyr project.
The [manifest](west.yml) is used the select the compatible Zephyr version.

[zephyr]: https://github.com/zephyrproject-rtos/zephyr

Note: do not "git clone" this project, see installation instructions to run
"west init" in order to initialize the project.

## Probe connection

### Connect ST-Link
You will need an ST-Link probe to flash the firmware to the board.
Two different connectors allow to flash the board:

* J4, a standard 2.54mm header, to be soldered on bottom side
* J3 TagConnect, a pogo pin solution, no soldering needed but you will have to make a custom cable.

#### Pinout
##### DUT connectors
###### J4 JTAG connector
This pinout follows the ST-Link connector pinout, with an additional row for JTAG JTDI.
The second column in only GND, it was added for connector mechanical stability and easy GND access.
<!--
             +-------+
      3V3    | 1 | 2 | GND
             |   |   |
SWCLK JTCK   | 3 | 4 | GND
             |   |   |
      GND    | 5 | 6 | GND
             |   |   |
SWDIO JTMS   | 7 | 8 | GND
             |   |   |
      NRST   | 9 | 10| GND
             |   |   |
SWO   JTDO   | 11| 12| GND
             |   |   |
      JTDI   | 13| 14| GND
             |   |   |
             +-------+
-->
![Kroki generated Ditaa](https://kroki.io/ditaa/svg/eNpTUEAC2roQoM0F4RuHGYOoGgVDIDYCYnc_Fy5kDUAhKOYKDnf28VbwCnH2BgsZA7EJfh0QPlABRNgUiM0I2eHi6Q-0wzcYLGQOxBbE2OEXFBwCFrIE-cWAgB3-QNorxMUf4nNDIDYiwg6gDk-IDmMgNiFCB3qgAwBQM0uR)

###### J3 TagConnect TC2030
TBD: Not sure about this pinout. The J3 pinout follows the manufacturers's recommendation for JTAG.
<!--
1 3V3
2 JTMS
3 NRST
4 JTCK
5 GND
6 JTDO


          O

  1   o      o   2

  3   o      o   4

  5   o      o   6

   O            O

         o
        +-------+
        | 1 | 2 |
        |   |   +-+
        | 3 | 4   |
        |   |   +-+
        | 5 | 6 |
        |   |   |
        +-------+

1 VTREF
2 SWDIO
3 nRST
4 SWCLK
5 GND
-->

<!--
          O

   9  o      o  2

  10  o      o  1

   7  o      o  4

   8  o      o  3

   5  o      o  6


   O            O

         o
        +--------+
        | 1 | 2  |
        |   |    |
        | 3 | 4  |
        |   |    +-+
        | 5 | 6    |
        |   |    +-+
        | 7 | 8  |
        |   |    |
        | 9 | 10 |
        |   |    |
        +--------+
-->

##### ST-Link connectors
###### Nucleo ST-Link/V2 pinout CN2
If you are using the ST-Link embedded on a Nucleo board as your debugger, do not forget to remove the 2 jumpers CN2, as shown on the overlay.

* Jumpers equipped: flash the Nucleo
* Jumpers removed: flash an external board connected on CN2

You can even separate the ST-Link part from the Nucleo part with the breakout pads!
<!--
        o
     +---+
     | 1 | VDD    3V3
     |   |
     | 2 | SWCLK  JTCK
     |   |
     | 3 | GND
     |   |
     | 4 | SWDIO  JTMS
     |   |
     | 5 | NRST   NRST
     |   |
     | 6 | SWO    JTDO
     |   |
     +---+
-->
![Kroki generated Ditaa](https://kroki.io/ditaa/svg/eNpTUICAfC4wpa2rq6sNYdYoGAJxmIsLiGMcZgwTBWIY0wiIg8OdfbwVFLxCnL2xqDAGYnc_FywyJmC9Lp7-IL2-wVhUmAKxX1BwCJADorCoMAOb4Q_ieIW4-GOogPgGAGM1LFQ=)

###### BlackMagic Probe on STM32F103RCT6 breakout board

This adapter was made following instructions [here](https://stm32world.com/wiki/DIY_STM32_Programmer_(ST-Link/V2-1))
using [a STM32F103 breakout board](https://fr.aliexpress.com/item/1005008154652916.html)

* In order to flash the BlackMagicProbe on the STM32F106 board

| ST-Link connector pin | Name | STM32F106 breakout |
|-----------------------|------|--------------------|
| 1                     | 3V3  | 3V3                |
| 2                     | JTCK | A14                |
| 3                     | GND  | G                  |
| 4                     | JTMS | A13                |
| 5                     | nRST | RST                |

* Once BlackMagic is flashed, the STM32F106 becomes the probe with the following pinout

| ST-Link connector pin | Name | STM32F106 breakout |
|-----------------------|------|--------------------|
| 1                     | 3V3  | NE (R bridge to A0)|
| 2                     | JTCK | A5                 |
| 3                     | GND  | G                  |
| 4                     | JTMS | B14 (100o B12)     |
| 5                     | nRST | B0                 |
| 6                     | JTDO | A6                 |
| 7                     | JTDI | A7                 |

* Pin 4 (SWDIO) needs to be connected to B14 and also to B12 via a 100 ohms resistor
* 3V3 is the target voltage detection, it needs a divide-by-two resistor bridge to A0 to be working correctly

The adapter also provides a target debug UART:

| UART Target | STM32F106 UART | STM32F106 breakout |
|-------------|----------------|--------------------|
| TX          | RX             | A3                 |
| RX          | TX             | A2                 |

In order to install the adapter, you will have to configure udev rules (on Linux) or install
a custom driver (on Windows) so that the BlackMagic probe serial connections will appear with
aliases "/dev/ttyBmpTarg" and "/dev/ttyBmpGdb".

The configuration files are available [here](https://github.com/blackmagic-debug/blackmagic/tree/main/driver).

###### Official ST-LinK/V2
Oh yeah, you can actually by the probe too. Be careful when buying those, there are quite a fair amount of ST-Link counterfeits and not all of them are fully functional.
<!--
                   o
                  +-------+
             VAPP | 1 | 2 | VAPP
                  |   |   |
             TRST | 3 | 4 | GND
                  |   |   |
              TDI | 5 | 6 | GND
                  |   |   |
      SWDIO   TMS | 7 | 8 | GND
                  |   |   |
      SWCLK   TCK | 9 | 10| GND
                  +   |   |
               NC   11| 12| GND
                  +   |   |
      SWO     TDO | 13| 14| GND
                  |   |   |
             NRST | 15| 16| GND
                  |   |   |
               NC | 17| 18| GND
                  |   |   |
              VDD | 19| 20| GND
                  +-------+
-->
![Kroki generated Ditaa](https://kroki.io/ditaa/svg/eNqVklsKwyAQRf-7ivmXQs07n8WBEtJqqJIsoxvI4nuHtIWQCiocwTscdAaJDut1OmbqvC21r83XaaKVNCiAHP_I65d9LTx9QF6CCtwsp7sUeEBegybZ9QsPTtyHR96CLsM191FcMyLvpelLzFWRN5M12LSGW6S6fnGffp3cWYIqc1Z2m7OuQZM7Z3kzvBZ0ue7MLG6PzxGf1e9jvQFbdnNG)

##### Pinout recap

| JTAG Signal | SWIO Signal | Board J4 | Board TagConnect J3  | Nucleo ST-Link CN2 | ST-Link 2x10 |
|-------------|-------------|----------|----------------------|--------------------|--------------|
| JTCK        | SWCLK       | 3        | 4                    | 2                  | 9            |
| JTMS        | SWDIO       | 7        | 2                    | 4                  | 7            |
| JTDI        |             | 13       | NC                   | NC                 | 5            |
| JTDO        | SWO         | 11       | 6                    | 6                  | 13           |
| nRST        | nRST        | 9        | 3                    | 5                  | 15           |
| VDD         | VDD         | 1        | 1                    | 1                  | 19           |
| GND         | GND         | 5, 2-14  | 5                    | 3                  | 2-20         |

### Connect UART

No USB-UART chip is provided onboard, you will need to get a third party 3V3 USB-UART gizmo such as the [FTDI one](https://ftdichip.com/products/ttl-232r-3v3/) or any equivalent.

The Zephyr debug UART is set up to be RX2 and TX2, and 115200 2N1.

<!--

                   o
                  +-------+
              RX2 | 1 | 2 | TX3
                  |   |   |
               NC | 3 | 4 | GND
                  |   |   |
              GND | 5 | 6 | NC
                  |   |   |
              TX2 | 7 | 8 | RX3
                  |   |   |
                  +-------+
-->
![Kroki generated Ditaa](https://kroki.io/ditaa/svg/eNpTUMAA-Qq0B1yYQtq6EKBNR3uDIowUahQMgRhEh0QY08-7NTCMLufnDBQ3BmITIHb3c1EYeDeBXFGjYArEZkAMdODAOykEHHPmQGwBxEE0izmSoo7WqRgAh8qCxA==)

## Software compilation

Before getting started, make sure you have a proper Zephyr development
environment. Follow the official
[Zephyr Getting Started Guide](https://docs.zephyrproject.org/latest/getting_started/index.html).

Quick summary:

* Install a venv and activate it
* Then install west meta-tool:

```
pip3 install west
```

### Initialization

The first step is to initialize the workspace folder (``my-workspace``) where
the ``asynthosc-application`` and all Zephyr modules will be cloned. Run the following
command:

* Initialize my-workspace for the asynthosc-application (main branch)
```shell
west init -m https://github.com/everedero/asynthosc_fw --mr main my-workspace
```
* Update Zephyr modules
```shell
cd my-workspace
west update
```
### Git key magic
By default west configures the repository to be http identification, if you want
to get back to ssh, you can do it with:
```
git config remote.origin.url git@github.com:everedero/asynthosc_fw.git
```

### Building and running

To build the application, run the following command:

```shell
cd asynthosc_fw
west build -b asynthosc app
```

You can use the `asynthosc` board found in this
repository. Note that Zephyr sample boards may be used if an
appropriate overlay is provided (see `app/boards`).

Once you have built the application, run the following command to flash it:

```shell
west flash
```

### Emulation

This project supports emulation.
Left and right pushbuttons are emulated by keyboard left and right.
Rotary encoder click is by keyboard enter.

```shell
west build -b native_sim/native/64 app
```

Then, execute:

```shell
./build/zephyr/zephyr.exe -display_zoom_pct=200
```

### Testing

To execute Twister integration tests, run the following command:

```shell
west twister -T tests --integration
```

### Notes for USB to OSC

#### Interact with echo server
##### UDP echo server

    ncat -e /bin/cat -k -u -l 4242

##### Server display, not echo

    nc -lu 4242

### Compile Shell over USB support

It means we don’t need an FTDI

    cd ~/zephyrproject/zephyr/samples/subsys/shell

    west build -p always -b asynthosc ./shell_module -DOVERLAY_CONFIG=overlay-usb.conf -DDTC_OVERLAY_FILE=usb.overlay

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
