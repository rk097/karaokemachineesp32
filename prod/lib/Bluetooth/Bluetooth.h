#ifndef BLUETOOTH_H
#define BLUETOOTH_H

// initialize nvs and bluetooth. will also initialize a ringbuffer of the given size
void bt_init(RingbufHandle_t* ringbuf_handle_ptr, unsigned long ringbuf_size, bool* bt_playing);

#endif