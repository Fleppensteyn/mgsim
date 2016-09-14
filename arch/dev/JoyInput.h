// -*- c++ -*-
#ifndef JOYINPUT_H
#define JOYINPUT_H

#include "SDLInputManager.h"

#include "arch/IOMessageInterface.h"
#include "sim/kernel.h"
#include "sim/flag.h"


class Config;

enum
{
    JOYINPUT_JOYSTICK = 1,
    JOYINPUT_MOUSE,
    JOYINPUT_TOUCH,
    JOYINPUT_REPLAY
};


namespace Simulator
{
    struct ReplayData
    {
        CycleNo cycle;
        char type;
        unsigned char size;
        unsigned short from;
        MemAddr addr;
        unsigned char data[4];
    };
    class JoyInput :  public IIOMessageClient, public Object, public ISDLInputClient
    {
        IOMessageInterface& m_ioif;
        IODeviceID m_devid;
        Clock& m_clock;

        DefineStateVariable(bool, enabled);
        DefineStateVariable(bool, events_enabled);

        DefineStateVariable(bool, interrupt_enabled);
        Flag    m_interrupt;
        Process p_SendInterrupt;
        DefineStateVariable(IONotificationChannelID, interruptChannel);
        Result DoSendInterrupt();

        int m_sdljoyindex;
        int m_devicetype;
        std::deque<MGInputEvent> m_eventqueue;
        JoystickInfo m_joyinfo;
        JoystickState m_joystate;
        FILE* m_replayfile;
        bool m_writereplay;
        struct ReplayData m_replaydata;

        void LoadNextReplayEvent();

    public:
        JoyInput(const std::string& name, Object& parent,
                 IOMessageInterface& iobus, IODeviceID devid);
        ~JoyInput();
        JoyInput(const JoyInput&) = delete;
        JoyInput& operator=(const JoyInput&) = delete;

        // from IIOBusClient
        bool OnReadRequestReceived(IODeviceID from, MemAddr addr, MemSize size) override;
        bool OnWriteRequestReceived(IODeviceID from, MemAddr addr, const IOData& data) override;
        void GetDeviceIdentity(IODeviceIdentification& id) const override;
        StorageTraceSet GetReadRequestTraces() const override;
        StorageTraceSet GetWriteRequestTraces() const override;
        const std::string& GetIODeviceName() const override;

        void OnInputEvent(MGInputEvent event) override;

    };

}

#endif