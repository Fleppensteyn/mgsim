#include <arch/dev/Display.h>
#include <arch/dev/SDLInputManager.h>
#include <arch/dev/sdl_wrappers.h>
#include <sim/sampling.h>

#include <fstream>
#include <iomanip>
#include <algorithm>
#include <cstring>
#include <cstdlib>

using namespace std;

namespace Simulator
{
    struct SDLContext
    {
        SDL_Window* window;
        uint32_t windowID;
        SDL_Renderer* renderer;
        SDL_Texture* texture;
        SDLContext()
            : window(0), windowID(0), renderer(0), texture(0) {}
    };

    static unsigned currentDelayScale(unsigned x)
    {
        for (unsigned i = 10000000; i > 0; i /= 10)
            if (x > i) return i;
        return 1;
    }

    static short convertToFixed(float f)
    {
        if (f > 0.9999999) f = 0.9999999;
        else if (f < -0.9999999) f = -0.9999999;
        return (short)(f * 32768.0);
    }

    void SDLInputManager::RegisterDisplay(Display *disp)
    {
        for (auto p : m_displays)
            if (p == disp)
                return;
        m_displays.push_back(disp);
    }

    void SDLInputManager::UnregisterDisplay(Display *disp)
    {
        remove(m_displays.begin(), m_displays.end(), disp);
    }

    void SDLInputManager::GetMaxWindowSize(unsigned& w, unsigned& h)
    {
        if (!m_sdl_initialized)
            return;

        w = h = 0;
        int numDisplays = SDL_GetNumVideoDisplays();
        for (int i = 0; i < numDisplays; ++i)
        {
            SDL_Rect r;
            if (SDL_GetDisplayBounds(i, &r) == 0)
            {
                if (r.w > (int)w || r.h > (int)h)
                {  w = r.w; h = r.h; }
            }
        }
    }

    void SDLInputManager::ResetDisplays() const
    {
        for (auto d : m_displays)
            d->ResetDisplay();
    }

    SDLInputManager* SDLInputManager::g_singleton = 0;

    void SDLInputManager::CreateManagerIfNotExists(Config& cfg)
    {
        if (g_singleton == 0)
            g_singleton = new SDLInputManager(cfg.getValue<unsigned>("SDLRefreshDelay"),
                                cfg.getValueOrDefault<unsigned>("SDLInputPollDelay",1000));
    }

    SDLInputManager::SDLInputManager(unsigned refreshDelay, unsigned inputPollDelay)
        : m_sdl_initialized(false),
          m_refreshDelay_orig(refreshDelay),
          m_refreshDelay(refreshDelay),
          m_inputPollDelay(inputPollDelay),
          m_lastUpdate(0),
          m_lastInputPoll(0),
          m_displays(),
          m_joystickclients(),
          m_mouseclient(),
          m_touchclient()
    {
        if (SDL_Init(SDL_INIT_VIDEO) != 0)
        {
            cerr << "# unable to set up SDL: " << SDL_GetError() << endl;
            return;
        }
        SDL_InitSubSystem(SDL_INIT_JOYSTICK);
        m_sdl_initialized = true;
        if (SDL_HasQuit)
            atexit(SDL_Quit);
    }

    bool SDLInputManager::IsJoystickAvailable(int index) const
    {
        if (SDL_NumJoysticks() <= index)
        {
            return false;
        }
        for (const auto& client : m_joystickclients)
        {
            if (client.second.joyindex == index)
                return false;
        }
        return true;
    }

    bool SDLInputManager::RegisterJoystickClient(ISDLInputClient& client, int joyindex)
    {
        if (!IsJoystickAvailable(joyindex))
            return false;

        SDLInputClientContext context;
        context.client = &client;
        context.joyindex = joyindex;
        context.joystick = SDL_JoystickOpen(joyindex);
        if (!context.joystick)
            return false;

        context.joyinfo.id = SDL_JoystickInstanceID(context.joystick);
        context.joyinfo.nbuttons = SDL_JoystickNumButtons(context.joystick);
        context.joyinfo.naxes = SDL_JoystickNumAxes(context.joystick);
        context.joyinfo.nhats = SDL_JoystickNumHats(context.joystick);
        context.joyinfo.nballs = SDL_JoystickNumBalls(context.joystick);
        m_joystickclients[context.joyinfo.id] = context;
        return true;
    }

    bool SDLInputManager::UnregisterJoystickClient(ISDLInputClient& client)
    {
        for (auto& item : m_joystickclients)
        {
            if (item.second.client == &client)
            {
                if (SDL_JoystickGetAttached(item.second.joystick))
                    SDL_JoystickClose(item.second.joystick);

                m_joystickclients.erase(item.first);
                return true;
            }
        }
        return false;
    }

    bool SDLInputManager::RegisterMouseClient(ISDLInputClient& client)
    {
        if (m_mouseclient.joyindex == 1)
            return false;

        SDLInputClientContext mousecontext;
        mousecontext.joyindex = 1;
        mousecontext.client = &client;
        mousecontext.joyinfo.id = -1;
        mousecontext.joyinfo.naxes = 2;
        mousecontext.joyinfo.nbuttons = 5;
        mousecontext.joyinfo.nhats = 0;
        mousecontext.joyinfo.nballs = 2;
        m_mouseclient = mousecontext;
        return true;
    }

    bool SDLInputManager::UnregisterMouseClient(ISDLInputClient& client)
    {
        if (m_mouseclient.client != &client)
            return false;

        m_mouseclient = SDLInputClientContext();
        return true;
    }

    bool SDLInputManager::RegisterTouchClient(ISDLInputClient& client)
    {
        if (m_touchclient.joyindex == 1)
            return false;

        SDLInputClientContext touchcontext;
        touchcontext.joyindex = 1;
        touchcontext.client = &client;
        touchcontext.joyinfo.id = -1;
        touchcontext.joyinfo.naxes = 0;
        touchcontext.joyinfo.nbuttons = 0;
        touchcontext.joyinfo.nhats = 0;
        touchcontext.joyinfo.nballs = 0;
        m_touchclient = touchcontext;
        return true;
    }

    bool SDLInputManager::UnregisterTouchClient(ISDLInputClient& client)
    {
        if (m_touchclient.client != &client)
            return false;

        m_touchclient = SDLInputClientContext();
        return true;
    }

    JoystickInfo SDLInputManager::GetJoystickInfo(int index) const
    {
        if (SDL_NumJoysticks() <= index)
            return JoystickInfo();

        for (const auto& client : m_joystickclients)
        {
            if (client.second.joyindex == index)
                return client.second.joyinfo;
        }
        return JoystickInfo();
    }

    JoystickInfo SDLInputManager::GetMouseInfo() const
    {
        return m_mouseclient.joyinfo;
    }

    JoystickInfo SDLInputManager::GetTouchInfo() const
    {
        return m_touchclient.joyinfo;
    }

    MGInputEvent SDLInputManager::ConvertSDLEvent(SDL_Event *event)
    {
        MGInputEvent mgev;
        memset(&mgev, 0, sizeof(MGInputEvent));
        mgev.common.timestamp = event->common.timestamp;
        switch (event->type)
        {
            case SDL_JOYAXISMOTION:
                mgev.joy.type = MG_JOYAXISMOTION;
                mgev.joy.num = event->jaxis.axis;
                mgev.joy.value = event->jaxis.value;
                break;
            case SDL_JOYBALLMOTION:
                mgev.joy.type = MG_JOYBALLMOTION;
                mgev.joy.num = event->jball.ball;
                mgev.joy.value = event->jball.xrel;
                mgev.joy.value2 = event->jball.yrel;
                break;
            case SDL_JOYHATMOTION:
                mgev.joy.type = MG_JOYHATMOTION;
                mgev.joy.num = event->jhat.hat;
                mgev.joy.value = event->jhat.value;
                break;
            case SDL_JOYBUTTONDOWN:
            case SDL_JOYBUTTONUP:
                mgev.joy.type = MG_JOYBUTTON;
                mgev.joy.num = event->jbutton.button;
                mgev.joy.value = event->jbutton.state;
                break;
            case SDL_JOYDEVICEADDED:
                mgev.joy.type = MG_DEVICEATTACHED;
                mgev.joy.num = event->jdevice.which;
                break;
            case SDL_JOYDEVICEREMOVED:
                mgev.joy.type = MG_DEVICEREMOVED;
                mgev.joy.num = event->jdevice.which;
                break;
            case SDL_MOUSEWHEEL:
                mgev.mouse.type = MG_MOUSEWHEEL;
                mgev.mouse.xrel = event->wheel.x;
                mgev.mouse.yrel = event->wheel.y;
                break;
            case SDL_MOUSEBUTTONUP:
            case SDL_MOUSEBUTTONDOWN:
                mgev.mouse.type = MG_MOUSEBUTTON;
                mgev.mouse.num = event->button.button - 1;//correct SDLs offset
                mgev.mouse.state = (event->button.state == SDL_PRESSED)?1:0;
                mgev.mouse.clicks = event->button.clicks;
                mgev.mouse.xpos = (short)event->button.x;
                mgev.mouse.ypos = (short)event->button.y;
                break;
            case SDL_MOUSEMOTION:
                mgev.mouse.type = MG_MOUSEMOTION;
                mgev.mouse.state = event->motion.state;
                mgev.mouse.xpos = (short)event->motion.x;
                mgev.mouse.ypos = (short)event->motion.y;
                mgev.mouse.xrel = (short)event->motion.xrel;
                mgev.mouse.yrel = (short)event->motion.yrel;
                break;
            case SDL_FINGERDOWN:
            case SDL_FINGERUP:
            case SDL_FINGERMOTION:
                mgev.touch.type = (event->type == SDL_FINGERDOWN)? MG_TOUCHDOWN :
                                    ((event->type == SDL_FINGERUP)? MG_TOUCHUP: MG_TOUCHMOTION);
                mgev.touch.num = event->tfinger.fingerId;
                mgev.touch.device = event->tfinger.touchId;
                mgev.touch.xpos = convertToFixed(event->tfinger.x);
                mgev.touch.ypos = convertToFixed(event->tfinger.y);
                mgev.touch.xrel = convertToFixed(event->tfinger.dx);
                mgev.touch.yrel = convertToFixed(event->tfinger.dy);
                mgev.touch.pressure = convertToFixed(event->tfinger.pressure);
                break;
        }
        return mgev;
    }

    bool SDLInputManager::UpdateJoystickState(int index, JoystickState& state)
    {
        if (SDL_NumJoysticks() <= index)
            return false;

        for (const auto& client : m_joystickclients)
        {
            if (client.second.joyindex == index)
            {
                state.instance_id = client.second.joyinfo.id;
                SDL_Joystick* joystick = client.second.joystick;
                state.axes.resize(client.second.joyinfo.naxes);
                state.buttons.resize(1 + (client.second.joyinfo.nbuttons/8));
                state.hats.resize(client.second.joyinfo.nhats);
                state.balls.resize(client.second.joyinfo.nballs * 2);
                for (int i = 0; i < client.second.joyinfo.naxes; i++)
                    state.axes[i] = SDL_JoystickGetAxis(joystick, i);
                for (int i = 0; i < client.second.joyinfo.nbuttons; i++){
                    if (SDL_JoystickGetButton(joystick, i))
                        state.buttons[i/8] |= 1 << (i % 8);
                    else
                        state.buttons[i/8] &= ~(1 << (i % 8));
                }
                for (int i = 0; i < client.second.joyinfo.nhats; i++)
                    state.hats[i] = SDL_JoystickGetHat(joystick, i);
                for (int i = 0; i < client.second.joyinfo.nballs; i++)
                {
                    int x,y;
                    SDL_JoystickGetBall(joystick, i, &x, &y);
                    state.balls[i * 2] = x;
                    state.balls[i * 2 + 1] = y;
                }
                return true;
            }
        }
        return false;
    }

    bool SDLInputManager::UpdateMouseState(JoystickState& state)
    {
        if (m_mouseclient.joyindex != 1)
            return false;

        state.axes.resize(2);
        state.buttons.resize(1);
        state.balls.resize(4);
        state.balls[0] = 0;
        state.balls[1] = 0;
        state.balls[2] = 0;
        state.balls[3] = 0;
        int x, y;
        state.buttons[0] = (unsigned char)SDL_GetMouseState(&x, &y);
        state.axes[0] = x;
        state.axes[1] = y;
        return true;
    }

    void SDLInputManager::CheckEvents(bool refreshWindows)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            uint32_t et = SDL_GetEventType(event);
            Display *selected = 0;
            switch (et)
            {
                case SDL_WINDOWEVENT:
                    for (auto d : m_displays)
                        if (d->m_sdl_context && d->m_sdl_context->windowID == SDL_GetEventWinWinID(event))
                        { selected = d; break; }
                    break;
                case SDL_KEYUP:
                    for (auto d : m_displays)
                        if (d->m_sdl_context && d->m_sdl_context->windowID == SDL_GetEventKeyWinID(event))
                        { selected = d; break; }
                    break;
                case SDL_JOYAXISMOTION:
                case SDL_JOYBALLMOTION:
                case SDL_JOYHATMOTION:
                case SDL_JOYBUTTONDOWN:
                case SDL_JOYBUTTONUP:
                case SDL_JOYDEVICEADDED:
                case SDL_JOYDEVICEREMOVED:
                {
                    auto mapitem = m_joystickclients.find(event.jaxis.which);
                    if (mapitem != m_joystickclients.end())
                    {
                        mapitem->second.client->OnInputEvent(ConvertSDLEvent(&event));
                    }
                    break;
                }
                case SDL_MOUSEWHEEL:
                case SDL_MOUSEMOTION:
                case SDL_MOUSEBUTTONUP:
                case SDL_MOUSEBUTTONDOWN:
                {
                    if (m_mouseclient.joyindex == 1 &&
                      !(m_touchclient.joyindex == 1 && event.wheel.which == SDL_TOUCH_MOUSEID))
                    {
                        m_mouseclient.client->OnInputEvent(ConvertSDLEvent(&event));
                    }
                    break;
                }

                case SDL_FINGERDOWN:
                case SDL_FINGERUP:
                case SDL_FINGERMOTION:
                {
                    if (m_touchclient.joyindex == 1)
                    {
                        m_touchclient.client->OnInputEvent(ConvertSDLEvent(&event));
                    }
                    break;
                }
            }
            if (selected == 0)
                // stray event, ignore
                continue;
            switch(et)
            {
            case SDL_KEYUP:
                switch (SDL_GetEventKeySym(event))
                {
                case SDLK_ESCAPE:
                    selected->CloseWindow();
                    break;
                case SDLK_PAGEDOWN:
                    selected->SetWindowScale(0.5, 0.5, false);
                    break;
                case SDLK_PAGEUP:
                    selected->SetWindowScale(2.0, 2.0, false);
                    break;
                case SDLK_END:
                    selected->SetWindowScale(0.9, 0.9, false);
                    break;
                case SDLK_HOME:
                    selected->SetWindowScale(1.1, 1.1, false);
                    break;
                case SDLK_TAB:
                    selected->EqualizeWindowScale();
                    break;
                case SDLK_SPACE:
                    selected->SetWindowScale(1.0, 1.0, true);
                    break;
                case SDLK_DOWN:
                    m_refreshDelay += currentDelayScale(m_refreshDelay);
                    for (auto d : m_displays)
                        d->ResetWindowCaption();
                    break;
                case SDLK_UP:
                    if (m_refreshDelay)
                        m_refreshDelay -= currentDelayScale(m_refreshDelay);
                    for (auto d : m_displays)
                        d->ResetWindowCaption();
                    break;
                case SDLK_r:
                    m_refreshDelay = m_refreshDelay_orig;
                    for (auto d : m_displays)
                        d->SetWindowScale(1.0, 1.0, true);
                    break;
                default:
                    // do nothing (yet)
                    break;
                }
                break;

            case SDL_WINDOWEVENT:
                switch (SDL_GetEventWinType(event))
                {
                case SDL_WINDOWEVENT_CLOSE:
                    selected->CloseWindow();
                    break;
                case SDL_WINDOWEVENT_RESIZED:
                    selected->SetWindowSize(SDL_GetEventWinSizeW(event),
                                            SDL_GetEventWinSizeH(event));
                    break;
                }
                break;
            }
        }
        if (refreshWindows)
            for (auto d : m_displays)
                d->Show();
    }
}
