#include "JoyInput.h"
#include "sim/config.h"

using namespace std;

enum
{
    JOYINPUT_JOYSTICK = 1,
    JOYINPUT_MOUSE,
    JOYINPUT_TOUCH,
    JOYINPUT_REPLAY
};

namespace Simulator
{
    JoyInput::JoyInput(const string& name, Object& parent,
                                   IOMessageInterface& ioif, IODeviceID devid)
        : Object(name, parent),
          m_ioif(ioif),
          m_devid(devid),
          m_clock(m_ioif.RegisterClient(devid, *this)),

          InitStateVariable(enabled, false),
          InitStateVariable(events_enabled, false),

          InitStateVariable(interrupt_enabled, false),
          InitStorage(m_interrupt, m_clock, false),
          InitProcess(p_SendInterrupt, DoSendInterrupt),
          InitStateVariable(interruptChannel, 0),

/*          InitStorage(m_delayResponse, m_clock, false),
          InitProcess(p_DelayedResponse, DelayedResponse),*/

          m_sdljoyindex(-1),
          m_devicetype(0),
          m_eventqueue(),
          m_joyinfo(),
          m_joystate(),
          m_replayfile(),
          m_writereplay(false),
          m_replaydata()
    {
        string type = GetConf("JoyInputDeviceType", string);
        if (type == "MOUSE")
            m_devicetype = JOYINPUT_MOUSE;
        else if (type == "TOUCH")
            m_devicetype = JOYINPUT_TOUCH;
        else if (type == "JOYSTICK")
        {
            m_sdljoyindex = GetConf("InputJoystickIndex", int);
            m_devicetype = JOYINPUT_JOYSTICK;
        }
        else if (type == "REPLAY")
            m_devicetype = JOYINPUT_REPLAY;
        else
            throw exceptf<InvalidArgumentException>("No device type for JoyInput specified");
        if (m_devicetype != JOYINPUT_REPLAY)
        {
            SDLInputManager::CreateManagerIfNotExists(*GetKernel()->GetConfig());
            if (m_devicetype == JOYINPUT_JOYSTICK)
            {
                if (!SDLInputManager::GetManager()->IsJoystickAvailable(m_sdljoyindex))
                    throw exceptf<InvalidArgumentException>("No SDL Joystick with index detected: %d", m_sdljoyindex);
            }
        }

        string replayfilename = GetConfOpt("JoyInputReplayFile", string,"");
        if (replayfilename != "")
        {
            if (m_devicetype == JOYINPUT_REPLAY)
                m_replayfile = fopen(replayfilename.c_str(),"r");
            else
            {
                m_replayfile = fopen(replayfilename.c_str(),"w");
                m_writereplay = true;
            }
            if (m_replayfile == NULL)
                throw exceptf<InvalidArgumentException>("Unable to open file for JoyInput replay: %s (%s)", replayfilename.c_str(), strerror(errno));
            else if (m_devicetype == JOYINPUT_REPLAY)
                LoadNextReplayEvent();
        }
        p_SendInterrupt.SetStorageTraces(m_ioif.GetBroadcastTraces(m_devid));
        m_interrupt.Sensitive(p_SendInterrupt);
/*        p_DelayedResponse.SetStorageTraces(m_ioif.GetBroadcastTraces(m_devid));
        m_delayResponse.Sensitive(p_DelayedResponse);*/
        RegisterModelObject(*this, "joyinput");
    }

    JoyInput::~JoyInput(){
        if (m_replayfile != NULL)
            fclose(m_replayfile);
    }

    StorageTraceSet JoyInput::GetReadRequestTraces() const
    {
        return m_ioif.GetRequestTraces(m_devid);// ^ opt(m_delayResponse);
    }

    StorageTraceSet JoyInput::GetWriteRequestTraces() const
    {
        return opt(m_interrupt);
    }

    Result JoyInput::DoSendInterrupt()
    {
        if (!m_ioif.SendInterruptRequest(m_devid, m_interruptChannel))
        {
            DeadlockWrite("Unable to send data ready interrupt to I/O bus");
            return FAILED;
        }
        return SUCCESS;
    }

/*    Result JoyInput::DelayedResponse()
    {
        DebugIOWrite("Stalling for input component replay");
        if (m_replaydata.cycle > GetKernel()->GetCycleNo())
            return FAILED;

        m_delayResponse.Clear();
        DebugIOWrite("Stalling for input component replay completed");
        COMMIT { LoadNextReplayEvent(); }
        if (m_replaydata.type == 'w')
            return SUCCESS;

        IOMessage *msg = m_ioif.CreateReadResponse(m_devid, m_replaydata.addr, m_replaydata.size);
        COMMIT {
            memcpy(&msg->read_response.data.data[0], &m_replaydata.data[0],m_replaydata.size);
        }

        if (!m_ioif.SendMessage(m_devid, m_replaydata.from, msg))
        {
            DeadlockWrite("Cannot send delayed input component read response to I/O bus");
            return FAILED;
        }
        return SUCCESS;
    }*/

    bool JoyInput::OnWriteRequestReceived(IODeviceID from, MemAddr addr, const IOData& iodata)
    {
        DebugIOWrite("Write from device %u to %#016llx/%u", (unsigned)from, (unsigned long long)addr, (unsigned)iodata.size);
        if (iodata.size != 1 || addr > 4)
        {
            throw exceptf<>(*this, "Invalid write from device %u to %#016llx/%u", (unsigned)from, (unsigned long long)addr, (unsigned)iodata.size);
        }

        if (m_devicetype == JOYINPUT_REPLAY)
        {
            if (m_replaydata.type == 'w' &&
                from == m_replaydata.from && addr == m_replaydata.addr &&
                iodata.size == m_replaydata.size && iodata.data[0] == m_replaydata.data[0])
            {
                // if (m_replaydata.cycle > GetKernel()->GetCycleNo())
                // {
                //     m_delayResponse.Set();
                // }
                // else
                // {
                    COMMIT {
                        LoadNextReplayEvent();
                    }
                // }
                return true;
            }
            else
                throw exceptf<>(*this, "Write request did not match JoyInput replay data.");
        } else if (m_writereplay)
        {
            COMMIT {
                fprintf(m_replayfile,"%lu w %u %lu %lu %02x\n",GetKernel()->GetCycleNo(), from, addr,
                                                               iodata.size,(unsigned char)*iodata.data);
                fflush(m_replayfile);
            }
        }

        unsigned char data = *(unsigned char*)iodata.data;

        switch (addr)
        {
            case 0:
                if (!data && m_enabled)
                {
                    DebugIOWrite("De-activating the input component");
                    COMMIT {
                        m_enabled = false;
                        m_eventqueue.clear();
                        m_interrupt.Clear();
                        if (m_devicetype == JOYINPUT_JOYSTICK)
                            SDLInputManager::GetManager()->UnregisterJoystickClient(*this);
                        else if (m_devicetype == JOYINPUT_MOUSE)
                            SDLInputManager::GetManager()->UnregisterMouseClient(*this);
                        else if (m_devicetype == JOYINPUT_TOUCH)
                            SDLInputManager::GetManager()->UnregisterTouchClient(*this);
                    }
                }
                else if (data && !m_enabled)
                {
                    DebugIOWrite("Activating the input component");
                    if (m_devicetype == JOYINPUT_JOYSTICK)
                    {
                        COMMIT {
                            if (SDLInputManager::GetManager()->RegisterJoystickClient(*this, m_sdljoyindex))
                            {
                                m_enabled = true;
                                m_joyinfo = SDLInputManager::GetManager()->GetJoystickInfo(m_sdljoyindex);
                                DebugIOWrite("Joystickinfo instance id: %d", m_joyinfo.id);
                                if (!SDLInputManager::GetManager()->UpdateJoystickState(m_sdljoyindex, m_joystate))
                                {
                                    DebugIOWrite("Device state couldn't be updated");
                                    m_enabled = false;
                                    SDLInputManager::GetManager()->UnregisterJoystickClient(*this);
                                }
                            }
                        }
                    }
                    else if (m_devicetype == JOYINPUT_MOUSE)
                    {
                        COMMIT {
                            if (SDLInputManager::GetManager()->RegisterMouseClient(*this))
                            {
                                m_enabled = true;
                                m_joyinfo = SDLInputManager::GetManager()->GetMouseInfo();
                                if (!SDLInputManager::GetManager()->UpdateMouseState(m_joystate))
                                {
                                    DebugIOWrite("Device state couldn't be updated");
                                    m_enabled = false;
                                    SDLInputManager::GetManager()->UnregisterMouseClient(*this);
                                }
                            }
                        }
                    }
                    else if (m_devicetype == JOYINPUT_TOUCH)
                    {
                        COMMIT {
                            if (SDLInputManager::GetManager()->RegisterTouchClient(*this))
                            {
                                m_enabled = true;
                                m_joyinfo = SDLInputManager::GetManager()->GetTouchInfo();
                            }
                        }
                    }
                }
                break;
            case 1:
                COMMIT {
                    m_events_enabled = (data != 0);
                    if (!m_events_enabled){
                        m_eventqueue.clear();
                        m_interrupt.Clear();
                    }
                }
                break;
            case 2:
                if (data && m_writereplay)
                    throw exceptf<>(*this, "Enabling JoyInput interrupts is not supported when recording a replay");
                COMMIT {
                    m_interrupt_enabled = (data != 0);
                    if (data == 0 && m_interrupt.IsSet())
                        m_interrupt.Clear();
                }
                break;
            case 3:
                COMMIT { m_interruptChannel = data; }
                break;
            case 4:
                DebugIOWrite("Popping the event queue");
                if (m_eventqueue.size() == 1)
                    m_interrupt.Clear(); //Interrupt condition cleared

                COMMIT {
                    if (!m_eventqueue.empty())
                        m_eventqueue.pop_front();
                }
                break;
        }
        return true;
    }


    bool JoyInput::OnReadRequestReceived(IODeviceID from, MemAddr addr, MemSize size)
    {
        DebugIOWrite("Read from device %u to %#016llx/%u", (unsigned)from, (unsigned long long)addr, (unsigned)size);

        IOMessage *msg = NULL;
        bool valid = false;

        if (m_devicetype == JOYINPUT_REPLAY)
        {
            if (m_replaydata.type == 'r' &&
                from == m_replaydata.from && addr == m_replaydata.addr &&
                size == m_replaydata.size)
            {
                // if (m_replaydata.cycle > GetKernel()->GetCycleNo())
                // {
                //     m_delayResponse.Set();
                //     return true;
                // }
                msg = m_ioif.CreateReadResponse(m_devid, addr, size);
                COMMIT{
                    memcpy(&msg->read_response.data.data[0], &m_replaydata.data[0],size);
                    LoadNextReplayEvent();
                }
                valid = true;

            }
            else
                throw exceptf<>(*this, "Read request did not match JoyInput replay data.");
        }
        else
        {
            switch ((addr & ~0x3ffL) >> 10)
            {
                case 0:
                    switch (addr & 0x300L)
                    {
                        case 0: //control section
                            if (size == 1){
                                valid = true;
                                unsigned char data = 0;
                                switch (addr){
                                    case 0: // Is the component enabled
                                        data = m_enabled? m_devicetype : 0;
                                        break;
                                    case 1: // Events enabled?
                                        data = m_events_enabled;
                                        break;
                                    case 2: // Interrupts enabled?
                                        data = m_interrupt_enabled;
                                        break;
                                    case 3: // Interrupt channel
                                        data = m_interruptChannel;
                                        break;
                                    case 4: // Event queue size limited to byte size
                                        data = (m_eventqueue.size() > 255) ? 255 : m_eventqueue.size();
                                        break;
                                    default:
                                        valid = false;
                                }
                                if (valid)
                                {
                                    msg = m_ioif.CreateReadResponse(m_devid, addr, 1);
                                    COMMIT {
                                        msg->read_response.data.data[0] = data;
                                    }
                                }
                            }
                            break;
                        case 0x100://info on latter sections
                            if (size == 4 && (addr % 4) == 0 && m_enabled) {
                                unsigned int data = 0;
                                valid = true;
                                int index = (addr & 0x7f) >> 2;
                                switch (index)
                                {
                                    //Number of items
                                    //Access width
                                    //bits per entry
                                    //entries per item
                                    case 0: //axes
                                        data = (m_joyinfo.naxes << 24) | (2 << 16) | (16 << 8) | 1;
                                        break;
                                    case 1://buttons
                                        data = (m_joyinfo.nbuttons << 24) | (1 << 16) | (1 << 8) | 1;
                                        break;
                                    case 2://hats
                                        data = (m_joyinfo.nhats << 24) | (1 << 16) | (8 << 8) | 1;
                                        break;
                                    case 3://balls
                                        data = (m_joyinfo.nballs << 24) | (2 << 16) | (16 << 8) | 2;
                                        break;
                                    case 4://Make this accessible but read 0 to indicate no further sections
                                        valid = true;
                                        break;
                                    default:
                                        valid = false;
                                }
                                if (valid)
                                {
                                    msg = m_ioif.CreateReadResponse(m_devid, addr, 4);
                                    COMMIT {
                                        SerializeRegister(RT_INTEGER, data, msg->read_response.data.data, 4);
                                    }
                                }
                            }
                            break;
                        default: //event access
                            if (size == 4 && (addr % 4) == 0 && m_enabled && m_events_enabled)
                            {
                                int index = (addr & 0xff) >> 2;
                                if (index < 6 && m_eventqueue.size() > 0)
                                {
                                    unsigned int *ev = (unsigned int *)(void *)&m_eventqueue.front();
                                    valid = true;
                                    msg = m_ioif.CreateReadResponse(m_devid, addr, 4);
                                    COMMIT {
                                        SerializeRegister(RT_INTEGER, ev[index], msg->read_response.data.data, 4);
                                    }
                                }
                            }
                            break;
                    }
                    break;
                case 1: //axes
                    if (size == 2 && addr % 2 == 0 && m_enabled){
                        int index = (addr & 0x3ffL) >> 1;
                        if (index < m_joyinfo.naxes){
                            valid = true;
                            msg = m_ioif.CreateReadResponse(m_devid, addr, 2);
                            COMMIT {
                                SerializeRegister(RT_INTEGER, m_joystate.axes[index], msg->read_response.data.data, 2);
                            }
                        }
                    }
                    break;
                case 2: //buttons
                    if (size == 1 && m_enabled){
                        int index = (addr & 0x3ffL);
                        if (index < m_joyinfo.nbuttons){
                            valid = true;
                            msg = m_ioif.CreateReadResponse(m_devid, addr, 1);
                            COMMIT {
                                msg->read_response.data.data[0] = m_joystate.buttons[index];
                            }
                        }
                    }
                    break;
                case 3: //hats
                    if (size == 1 && m_enabled){
                        int index = (addr & 0x3ffL);
                        if (index < m_joyinfo.nhats){
                            valid = true;
                            msg = m_ioif.CreateReadResponse(m_devid, addr, 1);
                            COMMIT {
                                msg->read_response.data.data[0] = m_joystate.hats[index];
                            }
                        }
                    }
                    break;
                case 4: //balls
                    if (size == 2 && addr % 2 == 0 && m_enabled){
                        int index = (addr & 0x3ffL) >> 1;
                        if (index < (m_joyinfo.nballs * 2)){
                            valid = true;
                            msg = m_ioif.CreateReadResponse(m_devid, addr, 2);
                            COMMIT {
                                SerializeRegister(RT_INTEGER, m_joystate.balls[index], msg->read_response.data.data, 2);
                            }
                        }
                    }
                    break;
            }
        }

        if (!valid)
        {
            throw exceptf<>(*this, "Invalid read from device %u to %#016llx/%u", (unsigned)from, (unsigned long long)addr, (unsigned)size);
        }

        if (m_writereplay)
        {
            COMMIT{
                fprintf(m_replayfile, "%lu r %u %lu %lu",GetKernel()->GetCycleNo(),from,addr,size);
                for (unsigned i = 0; i < size; i++)
                    fprintf(m_replayfile, " %02x",(unsigned char)msg->read_response.data.data[i]);
                fprintf(m_replayfile,"\n");
            }
        }

        if (!m_ioif.SendMessage(m_devid, from, msg))
        {
            DeadlockWrite("Cannot send input component read response to I/O bus");
            return false;
        }
        return true;
    }

    void JoyInput::OnInputEvent(MGInputEvent event)
    {
        DebugIOWrite("Received Event with type %02x",event.common.type);
        if (m_events_enabled)
        {
            m_eventqueue.push_back(event);
            if (m_interrupt_enabled)
                m_interrupt.Set();
        }
        switch (event.common.type)
        {
            case MG_JOYAXISMOTION:
                m_joystate.axes[event.joy.num] = event.joy.value;
                break;
            case MG_JOYBUTTON:
                if (event.joy.value)
                    m_joystate.buttons[event.joy.num/8] |= 1 << (event.joy.num % 8);
                else
                    m_joystate.buttons[event.joy.num/8] &= ~(1 << (event.joy.num % 8));
                break;
            case MG_JOYHATMOTION:
                m_joystate.hats[event.joy.num] = event.joy.value;
                break;
            case MG_JOYBALLMOTION:
                m_joystate.balls[event.joy.num * 2] = event.joy.value;
                m_joystate.balls[event.joy.num * 2 + 1] = event.joy.value2;
                break;
            case MG_MOUSEMOTION:
                m_joystate.axes[0] = event.mouse.xpos;
                m_joystate.axes[1] = event.mouse.ypos;
                m_joystate.buttons[0] = event.mouse.state;
                m_joystate.balls[0] = event.mouse.xrel;
                m_joystate.balls[1] = event.mouse.yrel;
                break;
            case MG_MOUSEWHEEL:
                m_joystate.balls[2] = event.mouse.xrel;
                m_joystate.balls[3] = event.mouse.yrel;
                break;
            case MG_MOUSEBUTTON:
                if (event.mouse.state)
                    m_joystate.buttons[0] |= 1 << event.mouse.num;
                else
                    m_joystate.buttons[0] &= ~(1 << event.mouse.num);
                m_joystate.axes[0] = event.mouse.xpos;
                m_joystate.axes[1] = event.mouse.ypos;
                break;
            case MG_TOUCHUP:
            case MG_TOUCHDOWN:
            case MG_TOUCHMOTION:
                break;
        }
    }

    void JoyInput::LoadNextReplayEvent(){
        int res = fscanf(m_replayfile,"%lu %c %hu %lu %hhu",&m_replaydata.cycle,
                         &m_replaydata.type, &m_replaydata.from, &m_replaydata.addr,
                         &m_replaydata.size);
        if (res == 5)
        {
            for (unsigned i = 0; i < m_replaydata.size; i++)
            {
                if (fscanf(m_replayfile," %02hhx",&m_replaydata.data[i]) != 1)
                    throw exceptf<>(*this,"Format of JoyInput replaydata doesn't match expectation");
            }
            DebugIOWrite("Loaded the next line of the replay file");
        }
        else if (res == EOF)
        {
            DebugIOWrite("End of replay file reached");
            memset(&m_replaydata,0,sizeof(struct ReplayData));
            m_replaydata.type = -1;
            return;
        }
        else
            throw exceptf<>(*this,"Format of JoyInput replaydata doesn't match expectation");
    }

    const string& JoyInput::GetIODeviceName() const
    {
        return GetName();
    }

    void JoyInput::GetDeviceIdentity(IODeviceIdentification& id) const
    {
        if (!DeviceDatabase::GetDatabase().FindDeviceByName("MGSim", "JoyInput", id))
        {
            throw InvalidArgumentException(*this, "Device identity not registered");
        }
    }
}