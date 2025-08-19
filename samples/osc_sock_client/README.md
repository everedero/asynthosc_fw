# OpenOSC test app

This app is based on Echo Client, a simple app to return a network package to the sender.

But this time, instead of sending "Lorem Ipsum", we send OSC formatted data.

Upon receiving echoed data, the message is parsed too.

Change the IPV4 IP addresses in prj.conf (MY\_IPV4 = the board’s, PEER\_IPV4 = the computer it talks to).

Peer port is set up to be 4242.

## Interact with echo server
Netcat is a small Linux utility to echo data back to the sender.
The -u switch allows to use UDP.

### UDP echo server

    ncat -e /bin/cat -k -u -l 4242

### Server display, not echo

    nc -lu 4242
