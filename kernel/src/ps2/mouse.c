#include "mouse.h"
#include "../gfx/printf.h" // For printf
#include "io.h" // inb,outb,inw,outw,inl,outl
#include "../interrupts/isr.h"// For register_irq_handler
#include "../bootloader.h"

extern volatile struct limine_framebuffer_request framebuffer_request;
struct limine_framebuffer* fb = NULL;

#define PS2_MOUSE_DATA_PORT 0x60
#define PS2_MOUSE_STATUS_PORT 0x64

static MouseEventCallback mouse_callback = 0;
static MouseEvent last_event;
static uint8_t mouse_cycle = 0;

static uint8_t mouse_buttons = 0;
static int32_t mouse_wheel = 0;
static uint8_t packet[4];
static bool has_wheel = false;

static inline void mouse_wait_input(void) {
    for (int i = 0; i < 100000; ++i) {
        if ((inb(PS2_MOUSE_STATUS_PORT) & 1) != 0)
            return;
    }
}

static inline void mouse_wait_output(void) {
    for (int i = 0; i < 100000; ++i) {
        if ((inb(PS2_MOUSE_STATUS_PORT) & 2) == 0)
            return;
    }
}

static void mouse_write(uint8_t value) {
    mouse_wait_output();
    outb(PS2_MOUSE_STATUS_PORT, 0xD4);
    mouse_wait_output();
    outb(PS2_MOUSE_DATA_PORT, value);
}

static uint8_t mouse_read(void) {
    mouse_wait_input();
    return inb(PS2_MOUSE_DATA_PORT);
}

static void mouse_send_cmd(uint8_t cmd) {
    mouse_write(cmd);
    mouse_read(); // ACK
}

static void mouse_handle_packet(void) {
    static int prev_x = 0, prev_y = 0;

    int dx = (int8_t)packet[1];
    int dy = (int8_t)packet[2];
    int wheel = has_wheel ? (int8_t)packet[3] : 0;

    mouse_x += dx;
    mouse_y -= dy;

    // Clamp
    if (mouse_x < 0) mouse_x = 0;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_x > 79) mouse_x = 79;
    if (mouse_y > 24) mouse_y = 24;

    // Erase old cursor
   // Erase old
printf("\e[%d;%dH ", prev_y + 1, prev_x + 1);

// Draw new
printf("\e[%d;%dHX", mouse_y + 1, mouse_x + 1);

    prev_x = mouse_x;
    prev_y = mouse_y;

    // Dispatch mouse event (optional)
    uint8_t new_buttons = packet[0] & 0x07;
    MouseEvent event = {0};

    if (dx || dy) {
        event.type = MOUSE_EVENT_MOVE;
        event.x = mouse_x;
        event.y = mouse_y;
        event.buttons = new_buttons;
        event.wheel_delta = 0;
        if (mouse_callback)
            mouse_callback(&event);
        last_event = event;
    }

    if (mouse_buttons != new_buttons) {
        for (uint8_t mask = 1; mask <= 4; mask <<= 1) {
            if ((mouse_buttons & mask) != (new_buttons & mask)) {
                event.type = (new_buttons & mask) ? MOUSE_EVENT_BUTTON_DOWN : MOUSE_EVENT_BUTTON_UP;
                event.x = mouse_x;
                event.y = mouse_y;
                event.buttons = new_buttons;
                event.wheel_delta = 0;
                if (mouse_callback)
                    mouse_callback(&event);
                last_event = event;
            }
        }
        mouse_buttons = new_buttons;
    }

    if (has_wheel && wheel) {
        event.type = MOUSE_EVENT_WHEEL;
        event.x = mouse_x;
        event.y = mouse_y;
        event.buttons = new_buttons;
        event.wheel_delta = wheel;
        if (mouse_callback)
            mouse_callback(&event);
        last_event = event;
    }
}


static void mouse_irq_handler(interrupt_frame_t* regs) {
    (void)regs;
    uint8_t data = inb(PS2_MOUSE_DATA_PORT);

    if (mouse_cycle == 0 && !(data & 0x08))
        return; // Sync lost

    packet[mouse_cycle++] = data;

    if ((!has_wheel && mouse_cycle == 3) || (has_wheel && mouse_cycle == 4)) {
        mouse_handle_packet();
        mouse_cycle = 0;
    }
}

void mouse_init(void) {
    fb = framebuffer_request.response->framebuffers[0];
    mouse_cycle = 0;
    mouse_x = mouse_y = 0;
    mouse_buttons = 0;
    mouse_callback = 0;
    has_wheel = false;

    // Enable the auxiliary device - mouse
    mouse_wait_output();
    outb(PS2_MOUSE_STATUS_PORT, 0xA8);

    // Enable the interrupts
    mouse_wait_output();
    outb(PS2_MOUSE_STATUS_PORT, 0x20);
    mouse_wait_input();
    uint8_t status = inb(PS2_MOUSE_DATA_PORT) | 2;
    mouse_wait_output();
    outb(PS2_MOUSE_STATUS_PORT, 0x60);
    mouse_wait_output();
    outb(PS2_MOUSE_DATA_PORT, status);

    // Set default settings
    mouse_send_cmd(0xF6);

    // Enable mouse
    mouse_send_cmd(0xF4);

    // Try to enable scroll wheel (IntelliMouse)
    mouse_write(0xF3); mouse_write(200);
    mouse_write(0xF3); mouse_write(100);
    mouse_write(0xF3); mouse_write(80);
    mouse_write(0xF2);
    uint8_t id = mouse_read();
    if (id == 3)
        has_wheel = true;

    register_irq_handler(12, mouse_irq_handler);
}

bool mouse_get_event(MouseEvent* event) {
    if (!event)
        return false;
    *event = last_event;
    return true;
}

void mouse_set_callback(MouseEventCallback cb) {
    mouse_callback = cb;
}


