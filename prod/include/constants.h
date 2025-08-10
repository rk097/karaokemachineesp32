#ifndef CONSTANTS_H
#define CONSTANTS_H

#define FRAME_SIZE 256 // size per DMA buffer
#define DMA_BUFFER_COUNT 8 // number of dma buffers
#define SAMPLE_RATE 44100 //in hz
#define RINGBUFFER_CAPACITY sizeof(int32_t) * FRAME_SIZE * DMA_BUFFER_COUNT * 2

#endif