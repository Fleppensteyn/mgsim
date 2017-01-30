// -*- c++ -*-
#ifndef SDLINPUTMANAGER_H
#define SDLINPUTMANAGER_H

#include <arch/IOMessageInterface.h>
#include <sim/kernel.h>
#include <sim/storage.h>
#include <sim/config.h>
#include <sim/sampling.h>
#include <map>
#include <vector>
#include <arch/dev/sdl_wrappers.h>
#include <arch/dev/MGInputEvents.h>

namespace Simulator
{
    class Display;
    class ISDLInputClient;
    struct JoystickInfo
    {
        int id;
        unsigned char naxes;
        unsigned char nbuttons;
        unsigned char nhats;
        unsigned char nballs;
        JoystickInfo()
            : id(-1), naxes(0), nbuttons(0), nhats(0), nballs(0) {}
    };
    struct JoystickState
    {
        int instance_id;
        std::vector<int16_t> axes;
        std::vector<uint8_t> buttons;
        std::vector<uint8_t> hats;
        std::vector<int16_t> balls;
        JoystickState()
            : instance_id(-1), axes(), buttons(), hats(), balls() {}
    };
    struct SDLInputClientContext
    {
        SDL_Joystick * joystick;
        ISDLInputClient *client;
        int joyindex;
        struct JoystickInfo joyinfo;
        SDLInputClientContext()
            : joystick(0), client(0), joyindex(-1), joyinfo() {}
    };
    class SDLInputManager
    {
    public:
        // RegisterDisplay/UnregisterDisplay: register/unregister a
        // display instance that may be connected to a SDL window and
        // thus receive key or window resize events.
        void RegisterDisplay(Display *disp);
        void UnregisterDisplay(Display *disp);

        // GetMaxWindowSize: retrieve the largest possible resolution
        // for a window.
        void GetMaxWindowSize(unsigned& w, unsigned& h);

        // OnCycle(): calls CheckEvents regularly based on the defined
        // cycle counts. Events are processed every m_inputPollDelay cycles
        // and display refreshing, which also checks events, is on its own
        // delay called m_refreshDelay because it is more expensive.
        // This is called by Kernel::Step().
        void OnCycle(CycleNo cycle)
        {
            if (m_lastUpdate + m_refreshDelay > cycle)
            {
                if (m_lastInputPoll + m_inputPollDelay <= cycle){
                    m_lastInputPoll = cycle;
                    CheckEvents(false);
                }
                return;
            }
            m_lastUpdate = cycle;
            m_lastInputPoll = cycle;
            CheckEvents(true);
        }

        // CheckEvents: poll the SDL event queue and dispatch events
        // to the appropriate display or input device client.
        // Only refreshes the displays when refreshWindows is true.
        // Used by OnCycle and CommandLineReader::ReadLineHook.
        void CheckEvents(bool refreshWindows);


        // GetRefreshDelay: accessor for the refresh delay.  Used by
        // individual Displays to generate their window caption.
        unsigned GetRefreshDelay() const { return m_refreshDelay; }

        // IsSDLInitialized: used by displays to decide whether SDL can be used
        bool IsSDLInitialized() const { return m_sdl_initialized; }

        // ResetDisplays: reset all enabled displays; ie re-create
        // their SDL window and render context and redraw.  Should be
        // used after deserializing a simulation state.
        void ResetDisplays() const;

        // IsJoystickAvailable: Checks if there is an SDL joystick available with this index
        bool IsJoystickAvailable(int index) const;

        // (Un)RegisterJoystickClient: Register a client for a certain joystick
        // indicated by the index. This client will then receive all input events
        // until it unregisters itself.
        // Returns true if the registration was successful or false if the joystick wasn't available.
        bool RegisterJoystickClient(ISDLInputClient& client, int joyindex);
        bool UnregisterJoystickClient(ISDLInputClient& client);

        // (Un)RegisterMouseClient: Similar to registering Joysticks, but
        // given that SDL combines all mice into one mouse
        // there can only be one client for it.
        bool RegisterMouseClient(ISDLInputClient& client);
        bool UnregisterMouseClient(ISDLInputClient& client);

        // (Un)RegisterTouchClient: Similar to the mouse there can only be one touch client.
        bool RegisterTouchClient(ISDLInputClient& client);
        bool UnregisterTouchClient(ISDLInputClient& client);

        // Methods to request information about connected input devices.
        JoystickInfo GetJoystickInfo(int index) const;
        JoystickInfo GetMouseInfo() const;
        JoystickInfo GetTouchInfo() const;

        // Updates the state according to SDLs internal state.
        bool UpdateJoystickState(int index, JoystickState& state);
        bool UpdateMouseState(JoystickState& state);

        // ConvertSDLEvent: Converts an SDL event to a corresponding MGInputEvent.
        MGInputEvent ConvertSDLEvent(SDL_Event *event);

        // Singleton methods
        static void CreateManagerIfNotExists(Config& cfg);
        static SDLInputManager* GetManager() { return g_singleton; }

    protected:
        bool                   m_sdl_initialized;   ///< Whether SDL is available
        unsigned               m_refreshDelay_orig; ///< Initial refresh delay from config
        unsigned               m_refreshDelay;      ///< Current refresh delay as set by user
        unsigned               m_inputPollDelay;    ///< Separate delay for input event checking
        CycleNo                m_lastUpdate;        ///< Cycle number of last check
        CycleNo                m_lastInputPoll;     ///< Cycle of the last input event check
        std::vector<Display*>  m_displays;          ///< Currently registered Display instances
        std::map<int, SDLInputClientContext> m_joystickclients; ///< Maps clients to the internal SDL id of their connected joystick
        SDLInputClientContext m_mouseclient; ///< Info about a client for the mouse
        SDLInputClientContext m_touchclient; ///< Info about a client for touch input

        static SDLInputManager* g_singleton;         ///< Singleton instance


        // Constructor, used by CreateManagerIfNotExists.
        SDLInputManager(unsigned refreshDelay, unsigned inputPollDelay);
    };
    class ISDLInputClient
    {
    public:
        virtual void OnInputEvent(MGInputEvent event) = 0;
        virtual ~ISDLInputClient() {}
    };
}

#endif
