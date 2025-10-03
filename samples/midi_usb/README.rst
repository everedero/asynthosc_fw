.. zephyr:code-sample:: usb-midi2-device
   :name: USB MIDI2 device
   :relevant-api: usbd_api usbd_midi2 input_events

   Implements a simple USB MIDI transmission from serial MIDI.

Overview
********

This sample demonstrates a basic MIDI serial to USB converter.
It transmits 0x8 and 0x9 note messages from an UART MIDI serial communication
to a USB MIDI.

Building and Running
********************

The code can be found in :zephyr_file:`samples/subsys/usb/midi`.

To build and flash the application:

.. zephyr-app-commands::
   :zephyr-app: samples/midi_usb
   :board: asynthosc
   :goals: build flash
   :compact:

Using the MIDI interface
************************

Once this sample is flashed, connect the device USB port to a host computer
with MIDI support. For example, on Linux, you can use alsa to access the device:

.. code-block:: console

  $ amidi -l
  Dir Device    Name
  IO  hw:2,1,0  Group 1 (USBD MIDI Sample)

On Mac OS you can use the system tool "Audio MIDI Setup" to view the device,
see https://support.apple.com/guide/audio-midi-setup/set-up-midi-devices-ams875bae1e0/mac

The "USBD MIDI Sample" interface should also appear in any program with MIDI
support; like your favorite Digital Audio Workstation or synthetizer. If you
don't have any such program at hand, there are some webmidi programs online,
for example: https://muted.io/piano/.

Testing loopback
****************

Open a first shell, and start dumping MIDI events:

.. code-block:: console

  $ amidi -p hw:2,1,0 -d


Then, in a second shell, send some MIDI events (for example note-on/note-off):

.. code-block:: console

  $ amidi -p hw:2,1,0 -S "90427f 804200"

These events should then appear in the first shell (dump)

On devboards with a user button, press it and observe that there are some note
on/note off events delivered to the first shell (dump)

.. code-block:: console

  $ amidi -p hw:2,1,0 -d

  90 40 7F
  80 40 7F
  90 40 7F
  80 40 7F
  [...]
