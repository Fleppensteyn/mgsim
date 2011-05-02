#ifndef LCD_H
#define LCD_H

#include "arch/IOBus.h"
#include "sim/kernel.h"

class Config;

namespace Simulator
{

class LCD : public IIOBusClient, public Object
{
    char*     m_buffer;

    size_t    m_width;
    size_t    m_height;

    size_t    m_startrow;
    size_t    m_startcolumn;

    unsigned  m_bgcolor;
    unsigned  m_fgcolor;

    size_t    m_curx;
    size_t    m_cury;

    void Refresh(unsigned firstrow, unsigned lastrow) const;

public:
    LCD(const std::string& name, Object& parent, Config& config);
    ~LCD();

    bool OnReadRequestReceived(IODeviceID from, MemAddr address, MemSize size) { return false; }
    bool OnReadResponseReceived(IODeviceID from, const IOData& data) { return false; }

    bool OnInterruptRequestReceived(IOInterruptID which) { return true; }
    bool OnNotificationReceived(IOInterruptID which, Integer tag) { return true; }

    bool OnWriteRequestReceived(IODeviceID from, MemAddr address, const IOData& data);

    void GetDeviceIdentity(IODeviceIdentification& id) const;

    std::string GetIODeviceName() const { return GetFQN(); }
};


}

#endif