#include <app/lib/tinyosc.h>
#include <zephyr/kernel.h>

//extern char* tx_buffer;//[2048]; // declare a 2Kb buffer to read packet data into

//extern const int tx_buf_len; // = sizeof(tx_buffer) - 1;
//const struct tosc_message msg;

/**
 * A basic program to listen to port 9000 and print received OSC packets.
 */
int create_msg(char *tx_buffer, int max_len) {

	  printk("Starting write tests:\n");
	  int len = 0;
	  char blob[8] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
	  //len = tosc_writeMessage(tx_buffer, max_len, "/address", "f", 1.0);
	  len = tosc_writeMessage(tx_buffer, max_len, "/address", "fs", 2.0, "Hello");
	  printk("Print osc:\n");
	  tosc_printOscBuffer(tx_buffer, len);
	  return(len);
}
void read_msg(char* buffer, int len) {
	  tosc_message osc;
	  tosc_parseMessage(&osc, buffer, len);
	  printk("Print msg:\n");
	  tosc_printMessage(&osc);
}
