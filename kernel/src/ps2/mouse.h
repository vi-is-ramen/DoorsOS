#ifndef MOUSE_H
#define MOUSE_H

#include <stdint.h>
#include <stdbool.h>

static inline int32_t mouse_x = 0, mouse_y = 0;
// Mouse buttons bitmask
#define MOUSE_LEFT   0x01
#define MOUSE_RIGHT  0x02
#define MOUSE_MIDDLE 0x04

// Mouse event types
typedef enum {
    MOUSE_EVENT_MOVE,
    MOUSE_EVENT_BUTTON_DOWN,
    MOUSE_EVENT_BUTTON_UP,
    MOUSE_EVENT_WHEEL,
} MouseEventType;

// Mouse event data
typedef struct {
    MouseEventType type;
    int32_t x;           // absolute or relative x position
    int32_t y;           // absolute or relative y position
    uint8_t buttons;     // bitmask of buttons pressed
    int32_t wheel_delta; // positive or negative for wheel scroll
} MouseEvent;

// Initialize mouse driver / handler
void mouse_init(void);

// Poll or wait for next mouse event
bool mouse_get_event(MouseEvent* event);

// Or alternatively a callback you can register
typedef void (*MouseEventCallback)(const MouseEvent* event);
void mouse_set_callback(MouseEventCallback cb);

#endif // MOUSE_H
