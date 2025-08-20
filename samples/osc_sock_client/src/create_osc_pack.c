#include <app/lib/tinyosc.h>
#include <zephyr/kernel.h>

//extern char* tx_buffer;//[2048]; // declare a 2Kb buffer to read packet data into

//extern const int tx_buf_len; // = sizeof(tx_buffer) - 1;
//const struct tosc_message msg;

/**
 * A basic program to listen to port 9000 and print received OSC packets.
 */
int create_msg(char *tx_buffer, int max_len) {
      tosc_bundle bundle;
    int len = 0;
      // Test timestamp 0xC007CAFE
      tosc_writeBundle(&bundle, 0xC007CAFE, tx_buffer, max_len);
      tosc_writeNextMessage(&bundle, "/a", "i", 1);
      tosc_writeNextMessage(&bundle, "/toto", "s", "hello!");

      len = tosc_getBundleLength(&bundle);
      printk("Starting write tests:\n");
      printk("Print osc:\n");
      tosc_printOscBuffer(tx_buffer, len);
      return(len);
}
void read_msg(char* buffer, int len) {
      tosc_bundle bundle;
      tosc_message msg;
      bool is_msg = true;
      tosc_parseBundle(&bundle, buffer, len);
      is_msg = tosc_getNextMessage(&bundle, &msg);
      while (is_msg) {
          tosc_parseMessage(&msg, buffer, len);
          printk("Print msg:\n");
          tosc_printMessage(&msg);
          is_msg = tosc_getNextMessage(&bundle, &msg);
    }
}
