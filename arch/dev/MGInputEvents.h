// -*- c++ -*-
#ifndef MGINPUTEVENTS_H
#define MGINPUTEVENTS_H

enum eventtypes
{
    MG_JOYAXISMOTION = 0x01,
    MG_JOYBUTTON,
    MG_JOYHATMOTION,
    MG_JOYBALLMOTION,
    MG_DEVICEATTACHED,
    MG_DEVICEREMOVED,
    MG_MOUSEMOTION = 0x11,
    MG_MOUSEBUTTON,
    MG_MOUSEWHEEL,
    MG_TOUCHDOWN = 0x21,
    MG_TOUCHMOTION,
    MG_TOUCHUP
};

// MGJoyInputEvent
// timestamp - The timestamp SDL passes in its events
// type      - A value from eventtypes above
// num       - The index of the event source, e.g. the 6th axis
// value     - The first of the value slots, depends on the event type:
//              AXISMOTION: a 16-bit signed value containing the absolute axis position
//              BUTTON:     0 or 1 depending on button state
//              HATMOTION:  The hat state encoded in the least significant 4 bits
//              BALLMOTION: The relative x movement of the ball
// value2    - Only used for BALLMOTION events and contains the relative y movement.

typedef struct MGJoyInputEvent
{
    uint32_t timestamp;
    uint8_t type;
    uint8_t num;
    int16_t value;
    int16_t value2;
    int16_t padding;
} MGJoyInputEvent;

// Masks used to determine what state a hat is in.
#define MG_HAT_UP       0x01
#define MG_HAT_RIGHT    0x02
#define MG_HAT_DOWN     0x04
#define MG_HAT_LEFT     0x08

// MGMouseInputEvent
// timestamp - The timestamp from the SDL event
// type      - A value from eventtypes above
// num       - For MOUSEBUTTON event this is the button number using the values below (e.g. 3 = BUTTON_RIGHT)
// state     - MOUSEBUTTON: The state of the button 0 = Released, 1 = Pressed.
//             MOUSEMOTION: this contains the state of the mouse buttons accessible through bitmasks.
//               The order is the same as the values below and they start at the least significant bit.
// clicks    - For MOUSEBUTTON events, contains the amount of consecutive clicks done at this position.
// xpos      - Current absolute x position of the mouse relative to the SDL window.
// ypos      - Current absolute y position of the mouse relative to the SDL window.
// xrel      - MOUSEMOTION: relative x movement of the mouse pointer since the last event
//             MOUSEWHEEL:  relative x movement of the mouse wheel since last event
// yrel      - MOUSEMOTION: relative y movement of the mouse pointer since the last event
//             MOUSEWHEEL:  relative y movement of the mouse wheel since last event
typedef struct MGMouseInputEvent
{
    uint32_t timestamp;
    uint8_t type;
    uint8_t num;
    uint8_t state;
    uint8_t clicks;
    int16_t xpos;
    int16_t ypos;
    int16_t xrel;
    int16_t yrel;
} MGMouseInputEvent;

// Numbering to identify mousebuttons. Use buttonmask to generate a mask for a certain button
#define MG_BUTTON_LEFT      1
#define MG_BUTTON_MIDDLE    2
#define MG_BUTTON_RIGHT     3
#define MG_BUTTON_X1        4
#define MG_BUTTON_X2        5
#define MG_BUTTONMASK(X)    (1 << ((X)-1))


// MGTouchInputEvent
// timestamp - The timestamp from the SDL event
// type      - A value from eventtypes above
// num       - The finger index for this event
// device    - A device identifier copied straight from SDL
// Note: all values below are signed fixed point with all non-sign bits being the fractional part so [-1,1].
// xpos      - The x position relative to the screen.
// ypos      - The y position relative to the screen.
// xrel      - The relative x movement.
// yrel      - The relative y movement.
// pressure  - The amount of pressure applied.
typedef struct MGTouchInputEvent
{
    uint32_t timestamp;
    uint8_t type;
    uint8_t num;
    uint8_t device;
    uint8_t pad;
    int16_t xpos;
    int16_t ypos;
    int16_t xrel;
    int16_t yrel;
    int16_t pressure;
    int16_t pad2;
} MGTouchInputEvent;

struct MGCommonInputEvent
{
    uint32_t timestamp;
    uint8_t type;
};

typedef union MGInputEvent
{
    struct MGCommonInputEvent common;
    struct MGJoyInputEvent joy;
    struct MGMouseInputEvent mouse;
    struct MGTouchInputEvent touch;
    uint8_t padding[20];
} MGInputEvent;

#endif