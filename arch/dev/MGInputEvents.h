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


typedef struct MGJoyInputEvent
{
    uint32_t timestamp;
    uint8_t type;
    uint8_t num;
    int16_t value;
    int16_t value2;
    int16_t padding;
} MGJoyInputEvent;

#define MG_HAT_UP       0x01
#define MG_HAT_RIGHT    0x02
#define MG_HAT_DOWN     0x04
#define MG_HAT_LEFT     0x08


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

#define MG_BUTTON_LEFT      1
#define MG_BUTTON_MIDDLE    2
#define MG_BUTTON_RIGHT     3
#define MG_BUTTON_X1        4
#define MG_BUTTON_X2        5

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