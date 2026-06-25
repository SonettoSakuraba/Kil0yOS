#ifndef MOUSE_H
#define MOUSE_H

#include "lib/types.h"
#include "core/isr.h"

#define MOUSE_IRQ      12
#define MOUSE_PORT     0x60
#define MOUSE_STATUS   0x64

#define MOUSE_CMD_ENABLE     0xA8
#define MOUSE_CMD_DISABLE    0xA7
#define MOUSE_CMD_READ_CFG   0x20
#define MOUSE_CMD_WRITE_CFG  0x60
#define MOUSE_CMD_SEND_TO_MOUSE 0xD4

#define MOUSE_CMD_ENABLE_STREAMING 0xF4
#define MOUSE_CMD_DISABLE_STREAMING 0xF5
#define MOUSE_CMD_SET_DEFAULTS 0xF6
#define MOUSE_CMD_RESEND       0xFE
#define MOUSE_CMD_RESET        0xFF

typedef struct {
    int x;
    int y;
    uint8_t buttons;
    int available;
} mouse_state_t;

void mouse_init();
void mouse_handler(interrupt_frame_t* frame);
int mouse_get_state(mouse_state_t* state);

#endif