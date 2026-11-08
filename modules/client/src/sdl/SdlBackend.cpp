#include "backend/SdlBackend.hpp"
#include "Logger.hpp"
#include "sdl/SdlKeys.hpp"
#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <memory>
#include <unordered_map>
#include <vector>

namespace
{
    using namespace kuge;

    [[noreturn]] void fail(const char* what)
    {
        throw BackendError(std::format("{}: {}", what, SDL_GetError()));
    }

    // The outline of a rounded rectangle, clockwise from the left of the top edge: `segments` + 1 points on each corner
    // (the same number whatever the radius, so that two outlines of one shape can be joined point to point)
    std::vector<SDL_FPoint> roundedOutline(const Rect& rect, float radius, int segments)
    {
        constexpr float PI = 3.14159265358979f;
        const float r = std::clamp(radius, 0.0f, std::max(std::min(rect.w, rect.h) / 2.0f, 0.0f));
        const struct { float x, y, from; } corners[] = {
            {rect.x + r, rect.y + r, 180.0f}, {rect.right() - r, rect.y + r, 270.0f},
            {rect.right() - r, rect.bottom() - r, 0.0f}, {rect.x + r, rect.bottom() - r, 90.0f},
        };
        std::vector<SDL_FPoint> points;

        points.reserve(4 * static_cast<std::size_t>(segments + 1));
        for (const auto& corner : corners) {
            for (int i = 0; i <= segments; ++i) {
                const float angle = (corner.from + 90.0f * static_cast<float>(i) / static_cast<float>(segments)) * PI / 180.0f;

                points.push_back({corner.x + r * std::cos(angle), corner.y + r * std::sin(angle)});
            }
        }
        return points;
    }

    int segmentsFor(float radius) { return std::clamp(static_cast<int>(radius / 2.0f) + 3, 3, 16); }

    // The size of what the renderer draws: the logical size if there is one (see applyBase()), else its output
    Vec2 renderSize(SDL_Renderer* renderer)
    {
        int width = 0;
        int height = 0;

        SDL_RenderGetLogicalSize(renderer, &width, &height);
        if (width <= 0 || height <= 0) {
            SDL_GetRendererOutputSize(renderer, &width, &height);
        }
        return {static_cast<float>(width), static_cast<float>(height)};
    }

    // Stops a part of SDL when the last one that needs it goes
    struct Subsystem
    {
        explicit Subsystem(Uint32 flags) : m_flags(flags) {}
        ~Subsystem(void) { SDL_QuitSubSystem(m_flags); }

        Subsystem(const Subsystem&)            = delete;
        Subsystem& operator=(const Subsystem&) = delete;

        Uint32 m_flags;
    };

    struct WindowDeleter   { void operator()(SDL_Window* window) const { SDL_DestroyWindow(window); } };
    struct RendererDeleter { void operator()(SDL_Renderer* renderer) const { SDL_DestroyRenderer(renderer); } };

    ////////////////////////////////////////////////////////////////////////////
    // What the window, the inputs and the renderer share. The last one to go
    // closes it.
    ////////////////////////////////////////////////////////////////////////////
    class SdlContext
    {
        public:
            explicit SdlContext(const WindowConfig& config)
            {
                if (SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
                    fail("cannot start SDL video");
                }
                m_video = std::make_unique<Subsystem>(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
                // Gamepads are a bonus: no gamepad support is not a reason to stop
                if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) == 0) {
                    m_controllers = std::make_unique<Subsystem>(SDL_INIT_GAMECONTROLLER);
                }
                SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");   // pixels stay sharp

                Uint32 flags = SDL_WINDOW_SHOWN;
                if (config.resizable) {
                    flags |= SDL_WINDOW_RESIZABLE;
                }
                if (config.fullscreen) {
                    flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
                }
                window.reset(SDL_CreateWindow(config.title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                    config.width, config.height, flags));
                if (!window) {
                    fail("cannot create the window");
                }
                const Uint32 vsync = config.vsync ? SDL_RENDERER_PRESENTVSYNC : 0;

                renderer.reset(SDL_CreateRenderer(window.get(), -1, SDL_RENDERER_ACCELERATED | vsync));
                if (!renderer) {
                    // No GPU: draw on the CPU
                    renderer.reset(SDL_CreateRenderer(window.get(), -1, SDL_RENDERER_SOFTWARE));
                }
                if (!renderer) {
                    fail("cannot create the renderer");
                }
                SDL_SetRenderDrawBlendMode(renderer.get(), SDL_BLENDMODE_BLEND);
                baseWidth = std::max(config.baseResolution, 0);
                applyBase();
                SDL_StartTextInput();   // the OS sends what is typed (TextEvent): some platforms only do once asked
            }

            ~SdlContext(void)
            {
                for (auto& [id, controller] : controllers) {
                    SDL_GameControllerClose(controller);
                }
            }

            SdlContext(const SdlContext&)            = delete;
            SdlContext& operator=(const SdlContext&) = delete;

            // Fullscreen with a base resolution: SDL draws in `baseWidth` pixels wide (the height from the ratio of the screen) and enlarges
            // it, and brings the mouse back to the same pixels. In a window, or with none, the logical size is lifted.
            void applyBase(void)
            {
                SDL_RenderSetLogicalSize(renderer.get(), 0, 0);   // (so that the real size can be read)
                if (baseWidth <= 0 || !(SDL_GetWindowFlags(window.get()) & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP))) {
                    return;
                }
                int width = 0;
                int height = 0;

                SDL_GetRendererOutputSize(renderer.get(), &width, &height);
                if (width > 0 && height > 0) {
                    SDL_RenderSetLogicalSize(renderer.get(), baseWidth, std::max(1, static_cast<int>(std::lround(static_cast<double>(baseWidth) * height / width))));
                }
            }

            int                                                baseWidth = 0;
            std::unique_ptr<Subsystem>                         m_video;
            std::unique_ptr<Subsystem>                         m_controllers;
            std::unique_ptr<SDL_Window, WindowDeleter>         window;
            std::unique_ptr<SDL_Renderer, RendererDeleter>     renderer;
            std::map<SDL_JoystickID, SDL_GameController*>      controllers;
    };

    ////////////////////////////////////////////////////////////////////////////
    // An OpenGL 3.3 core context on a window of its own that is never shown. (Not the window of the 2D renderer: SDL's
    // renderer has its own context there, and the two would fight over it.)
    class SdlOpenGL : public IOpenGL
    {
        public:
            SdlOpenGL(std::shared_ptr<SdlContext> context, int width, int height) : m_context(std::move(context))
            {
                SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
                SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
                SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
                m_window.reset(SDL_CreateWindow("KUGE OpenGL", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                    std::max(width, 1), std::max(height, 1), SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN));
                if (m_window) {
                    m_gl = SDL_GL_CreateContext(m_window.get());
                }
                SDL_GL_ResetAttributes();     // (what SDL's own renderer makes later is not ours to change)
                if (!m_gl) {
                    fail("no OpenGL 3.3 context");
                }
            }

            ~SdlOpenGL(void) override
            {
                if (m_gl) {
                    SDL_GL_DeleteContext(m_gl);
                }
            }

            SdlOpenGL(const SdlOpenGL&)            = delete;
            SdlOpenGL& operator=(const SdlOpenGL&) = delete;

            void makeCurrent(void) override { SDL_GL_MakeCurrent(m_window.get(), m_gl); }

            void (*procAddress(const char* name))(void) override
            {
                return reinterpret_cast<void (*)(void)>(SDL_GL_GetProcAddress(name));
            }

            void resize(int width, int height) override { SDL_SetWindowSize(m_window.get(), std::max(width, 1), std::max(height, 1)); }

            std::string description(void) override
            {
                using GetString = const unsigned char* (*)(unsigned);
                const auto getString = reinterpret_cast<GetString>(SDL_GL_GetProcAddress("glGetString"));

                if (!getString) {
                    return "OpenGL";
                }
                constexpr unsigned RENDERER = 0x1F01;
                constexpr unsigned VERSION = 0x1F02;
                const auto text = [&](unsigned what) {
                    const unsigned char* value = getString(what);

                    return value ? std::string(reinterpret_cast<const char*>(value)) : std::string("?");
                };

                return text(RENDERER) + ", OpenGL " + text(VERSION);
            }

        private:
            std::shared_ptr<SdlContext>                m_context;   // (the video subsystem lives as long as this)
            std::unique_ptr<SDL_Window, WindowDeleter> m_window;
            SDL_GLContext                              m_gl = nullptr;
    };

    ////////////////////////////////////////////////////////////////////////////
    class SdlWindow : public IWindow
    {
        public:
            explicit SdlWindow(std::shared_ptr<SdlContext> context) : m_context(std::move(context)) {}

            Vec2 size(void) const override
            {
                return renderSize(m_context->renderer.get());
            }

            void setTitle(const std::string& title) override
            {
                SDL_SetWindowTitle(m_context->window.get(), title.c_str());
            }

            void setSize(int width, int height) override
            {
                SDL_SetWindowSize(m_context->window.get(), std::max(width, 1), std::max(height, 1));
            }

            void setFullscreen(bool on) override
            {
                if (on == fullscreen()) {
                    return;
                }
                SDL_SetWindowFullscreen(m_context->window.get(), on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                if (!on) {
                    SDL_SetWindowPosition(m_context->window.get(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
                }
                m_context->applyBase();
            }

            void setBaseResolution(int width) override
            {
                m_context->baseWidth = std::max(width, 0);
                m_context->applyBase();
                SDL_Event event{};   // told as a resize, like any other change of the size
                const Vec2 now = size();

                event.type = SDL_WINDOWEVENT;
                event.window.event = SDL_WINDOWEVENT_SIZE_CHANGED;
                event.window.data1 = static_cast<Sint32>(now.x);
                event.window.data2 = static_cast<Sint32>(now.y);
                SDL_PushEvent(&event);
            }

            bool fullscreen(void) const override
            {
                return (SDL_GetWindowFlags(m_context->window.get()) & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP)) != 0;
            }

            void setVsync(bool on) override
            {
                SDL_RenderSetVSync(m_context->renderer.get(), on ? 1 : 0);
            }

            std::string clipboardText(void) const override
            {
                std::string text;

                if (char* clipboard = SDL_GetClipboardText()) {
                    text = clipboard;
                    SDL_free(clipboard);
                }
                return text;
            }

            void setClipboardText(const std::string& text) override
            {
                SDL_SetClipboardText(text.c_str());
            }

        private:
            std::shared_ptr<SdlContext> m_context;
    };

    ////////////////////////////////////////////////////////////////////////////
    float axisValue(Sint16 raw) { return std::clamp(static_cast<float>(raw) / 32767.0f, -1.0f, 1.0f); }

    MouseButton mouseButtonFrom(Uint8 button)
    {
        switch (button) {
            case SDL_BUTTON_LEFT:   return MouseButton::Left;
            case SDL_BUTTON_MIDDLE: return MouseButton::Middle;
            case SDL_BUTTON_RIGHT:  return MouseButton::Right;
            case SDL_BUTTON_X1:     return MouseButton::X1;
            case SDL_BUTTON_X2:     return MouseButton::X2;
            default:                return MouseButton::Count;
        }
    }

    GamepadButton gamepadButtonFrom(Uint8 button)
    {
        switch (button) {
            case SDL_CONTROLLER_BUTTON_A:             return GamepadButton::A;
            case SDL_CONTROLLER_BUTTON_B:             return GamepadButton::B;
            case SDL_CONTROLLER_BUTTON_X:             return GamepadButton::X;
            case SDL_CONTROLLER_BUTTON_Y:             return GamepadButton::Y;
            case SDL_CONTROLLER_BUTTON_BACK:          return GamepadButton::Back;
            case SDL_CONTROLLER_BUTTON_GUIDE:         return GamepadButton::Guide;
            case SDL_CONTROLLER_BUTTON_START:         return GamepadButton::Start;
            case SDL_CONTROLLER_BUTTON_LEFTSTICK:     return GamepadButton::LeftStick;
            case SDL_CONTROLLER_BUTTON_RIGHTSTICK:    return GamepadButton::RightStick;
            case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  return GamepadButton::LeftShoulder;
            case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return GamepadButton::RightShoulder;
            case SDL_CONTROLLER_BUTTON_DPAD_UP:       return GamepadButton::DPadUp;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:     return GamepadButton::DPadDown;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT:     return GamepadButton::DPadLeft;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:    return GamepadButton::DPadRight;
            default:                                  return GamepadButton::Count;
        }
    }

    GamepadAxis gamepadAxisFrom(Uint8 axis)
    {
        switch (axis) {
            case SDL_CONTROLLER_AXIS_LEFTX:        return GamepadAxis::LeftX;
            case SDL_CONTROLLER_AXIS_LEFTY:        return GamepadAxis::LeftY;
            case SDL_CONTROLLER_AXIS_RIGHTX:       return GamepadAxis::RightX;
            case SDL_CONTROLLER_AXIS_RIGHTY:       return GamepadAxis::RightY;
            case SDL_CONTROLLER_AXIS_TRIGGERLEFT:  return GamepadAxis::LeftTrigger;
            case SDL_CONTROLLER_AXIS_TRIGGERRIGHT: return GamepadAxis::RightTrigger;
            default:                               return GamepadAxis::Count;
        }
    }

    class SdlInput : public IInputSource
    {
        public:
            explicit SdlInput(std::shared_ptr<SdlContext> context) : m_context(std::move(context)) {}

            void setRelativeMouse(bool on) override
            {
                m_relative = on;
                SDL_SetRelativeMouseMode(on ? SDL_TRUE : SDL_FALSE);
                m_skipMove = on;
            }

            void poll(std::vector<Event>& out) override
            {
                SDL_Event event;

                while (SDL_PollEvent(&event)) {
                    translate(event, out);
                }
            }

        private:
            void translate(const SDL_Event& event, std::vector<Event>& out)
            {
                switch (event.type) {
                    case SDL_QUIT:
                        out.push_back(QuitEvent{});
                        break;
                    case SDL_WINDOWEVENT:
                        if (event.window.event == SDL_WINDOWEVENT_CLOSE) {
                            out.push_back(QuitEvent{});
                        } else if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                            m_context->applyBase();   // (the screen has another size: the height of the base resolution follows)
                            int logicalWidth = 0;
                            int logicalHeight = 0;

                            SDL_RenderGetLogicalSize(m_context->renderer.get(), &logicalWidth, &logicalHeight);
                            if (logicalWidth > 0 && logicalHeight > 0) {
                                out.push_back(ResizeEvent{logicalWidth, logicalHeight});   // (what the game draws in, not the pixels of the screen)
                            } else {
                                out.push_back(ResizeEvent{event.window.data1, event.window.data2});
                            }
                        } else if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST && m_relative) {
                            SDL_SetRelativeMouseMode(SDL_FALSE);    // (the cursor is given back to whoever has the focus now)
                        } else if (event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED && m_relative) {
                            SDL_SetRelativeMouseMode(SDL_TRUE);     // (and taken again when the window gets it back)
                            m_skipMove = true;
                        }
                        break;
                    case SDL_KEYDOWN:
                    case SDL_KEYUP:
                        out.push_back(KeyEvent{keyFromScancode(event.key.keysym.scancode),
                            event.type == SDL_KEYDOWN, event.key.repeat != 0});
                        break;
                    case SDL_TEXTINPUT:
                        // What was typed, in UTF-8 (the keys above are places on the keyboard, not letters)
                        if (event.text.text[0] != '\0') {
                            out.push_back(TextEvent{event.text.text});
                        }
                        break;
                    case SDL_MOUSEMOTION:
                        // The first move after the mouse is taken is a jump (the cursor is brought to the middle of
                        // the window): its delta is not a movement of the hand
                        out.push_back(MouseMoveEvent{
                            {static_cast<float>(event.motion.x), static_cast<float>(event.motion.y)},
                            m_skipMove ? Vec2{} : Vec2{static_cast<float>(event.motion.xrel), static_cast<float>(event.motion.yrel)}});
                        m_skipMove = false;
                        break;
                    case SDL_MOUSEBUTTONDOWN:
                    case SDL_MOUSEBUTTONUP:
                        if (mouseButtonFrom(event.button.button) != MouseButton::Count) {
                            out.push_back(MouseButtonEvent{mouseButtonFrom(event.button.button),
                                event.type == SDL_MOUSEBUTTONDOWN,
                                {static_cast<float>(event.button.x), static_cast<float>(event.button.y)}});
                        }
                        break;
                    case SDL_MOUSEWHEEL:
                        out.push_back(MouseWheelEvent{{event.wheel.preciseX, event.wheel.preciseY}});
                        break;
                    case SDL_CONTROLLERDEVICEADDED:
                        open(event.cdevice.which, out);
                        break;
                    case SDL_CONTROLLERDEVICEREMOVED:
                        close(event.cdevice.which, out);
                        break;
                    case SDL_CONTROLLERBUTTONDOWN:
                    case SDL_CONTROLLERBUTTONUP:
                        if (gamepadButtonFrom(event.cbutton.button) != GamepadButton::Count) {
                            out.push_back(GamepadButtonEvent{static_cast<int>(event.cbutton.which),
                                gamepadButtonFrom(event.cbutton.button), event.type == SDL_CONTROLLERBUTTONDOWN});
                        }
                        break;
                    case SDL_CONTROLLERAXISMOTION:
                        if (gamepadAxisFrom(event.caxis.axis) != GamepadAxis::Count) {
                            out.push_back(GamepadAxisEvent{static_cast<int>(event.caxis.which),
                                gamepadAxisFrom(event.caxis.axis), axisValue(event.caxis.value)});
                        }
                        break;
                    default:
                        break;
                }
            }

            // Gamepads are known to the events by their instance id, not by the
            // index they had when they were plugged
            void open(int deviceIndex, std::vector<Event>& out)
            {
                SDL_GameController* controller = SDL_GameControllerOpen(deviceIndex);

                if (!controller) {
                    return;
                }
                const SDL_JoystickID id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller));

                m_context->controllers[id] = controller;
                out.push_back(GamepadConnectionEvent{static_cast<int>(id), true});
            }

            void close(SDL_JoystickID id, std::vector<Event>& out)
            {
                const auto found = m_context->controllers.find(id);

                if (found == m_context->controllers.end()) {
                    return;
                }
                SDL_GameControllerClose(found->second);
                m_context->controllers.erase(found);
                out.push_back(GamepadConnectionEvent{static_cast<int>(id), false});
            }

            std::shared_ptr<SdlContext> m_context;
            bool                        m_relative = false;   //!< The game wants the mouse for a mouse look
            bool                        m_skipMove = false;   //!< The next move is the mouse being taken: no delta
    };

    ////////////////////////////////////////////////////////////////////////////
    class SdlRenderer : public IRenderer2D
    {
        public:
            explicit SdlRenderer(std::shared_ptr<SdlContext> context) : m_context(std::move(context)) {}

            ~SdlRenderer(void) override
            {
                for (auto& [id, texture] : m_textures) {
                    SDL_DestroyTexture(texture);
                }
            }

            Vec2 outputSize(void) const override { return renderSize(renderer()); }

            void begin(Color clear) override
            {
                SDL_RenderSetClipRect(renderer(), nullptr);   // (a clip left from the last frame would cut the clear)
                SDL_SetRenderDrawColor(renderer(), clear.r, clear.g, clear.b, clear.a);
                SDL_RenderClear(renderer());
            }

            void present(void) override { SDL_RenderPresent(renderer()); }

            TextureId createTexture(int width, int height, std::span<const std::uint8_t> rgba) override
            {
                if (width <= 0 || height <= 0
                    || rgba.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4) {
                    throw std::runtime_error("SdlRenderer: the pixels do not match the size");
                }
                SDL_Texture* texture = SDL_CreateTexture(renderer(), SDL_PIXELFORMAT_RGBA32,
                    SDL_TEXTUREACCESS_STATIC, width, height);

                if (!texture) {
                    fail("cannot create a texture");
                }
                if (SDL_UpdateTexture(texture, nullptr, rgba.data(), width * 4) != 0) {
                    SDL_DestroyTexture(texture);
                    fail("cannot fill a texture");
                }
                SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
                m_textures[++m_lastId] = texture;
                return m_lastId;
            }

            void destroyTexture(TextureId id) noexcept override
            {
                const auto found = m_textures.find(id);

                if (found != m_textures.end()) {
                    SDL_DestroyTexture(found->second);
                    m_textures.erase(found);
                }
            }

            void updateTexture(TextureId id, std::span<const std::uint8_t> rgba) override
            {
                const auto found = m_textures.find(id);
                int width = 0;
                int height = 0;

                if (found == m_textures.end() || SDL_QueryTexture(found->second, nullptr, nullptr, &width, &height) != 0
                    || rgba.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4) {
                    throw std::runtime_error("SdlRenderer: unknown texture, or the pixels do not match its size");
                }
                if (SDL_UpdateTexture(found->second, nullptr, rgba.data(), width * 4) != 0) {
                    fail("cannot fill a texture");
                }
            }

            void setClip(const std::optional<Rect>& area) override
            {
                if (!area) {
                    SDL_RenderSetClipRect(renderer(), nullptr);
                    return;
                }
                // Whole pixels, rounded outwards: what the clip lets through is never less than what was asked
                const int left = static_cast<int>(std::floor(area->x));
                const int top = static_cast<int>(std::floor(area->y));
                const SDL_Rect clip{left, top, static_cast<int>(std::ceil(area->right())) - left, static_cast<int>(std::ceil(area->bottom())) - top};

                SDL_RenderSetClipRect(renderer(), &clip);
            }

            void fillRect(const Rect& destination, Color color) override
            {
                const SDL_FRect rect = toSdl(destination);

                SDL_SetRenderDrawColor(renderer(), color.r, color.g, color.b, color.a);
                SDL_RenderFillRectF(renderer(), &rect);
            }

            void strokeRect(const Rect& destination, Color color) override
            {
                const SDL_FRect rect = toSdl(destination);

                SDL_SetRenderDrawColor(renderer(), color.r, color.g, color.b, color.a);
                SDL_RenderDrawRectF(renderer(), &rect);
            }

            void fillRoundedRect(const Rect& destination, float radius, Color color) override
            {
                if (radius <= 0.0f) {
                    fillRect(destination, color);
                    return;
                }
                const auto outline = roundedOutline(destination, radius, segmentsFor(radius));
                std::vector<SDL_Vertex> vertices;
                std::vector<int> indices;
                const SDL_Color tint{color.r, color.g, color.b, color.a};

                vertices.push_back({{destination.x + destination.w / 2.0f, destination.y + destination.h / 2.0f}, tint, {}});
                for (const SDL_FPoint& point : outline) {
                    vertices.push_back({point, tint, {}});
                }
                for (std::size_t i = 0; i < outline.size(); ++i) {   // a fan from the middle
                    indices.insert(indices.end(), {0, static_cast<int>(i) + 1, static_cast<int>((i + 1) % outline.size()) + 1});
                }
                SDL_RenderGeometry(renderer(), nullptr, vertices.data(), static_cast<int>(vertices.size()), indices.data(), static_cast<int>(indices.size()));
            }

            void strokeRoundedRect(const Rect& destination, float radius, float width, Color color) override
            {
                if (width <= 0.0f) {
                    return;
                }
                width = std::min(width, std::min(destination.w, destination.h) / 2.0f);
                const int segments = segmentsFor(radius);
                const Rect inner{destination.x + width, destination.y + width, destination.w - 2.0f * width, destination.h - 2.0f * width};
                const auto outside = roundedOutline(destination, radius, segments);
                const auto inside = roundedOutline(inner, std::max(radius - width, 0.0f), segments);
                std::vector<SDL_Vertex> vertices;
                std::vector<int> indices;
                const SDL_Color tint{color.r, color.g, color.b, color.a};

                for (std::size_t i = 0; i < outside.size(); ++i) {   // a strip between the two outlines
                    vertices.push_back({outside[i], tint, {}});
                    vertices.push_back({inside[i], tint, {}});
                }
                const int count = static_cast<int>(outside.size());

                for (int i = 0; i < count; ++i) {
                    const int next = (i + 1) % count;

                    indices.insert(indices.end(), {2 * i, 2 * i + 1, 2 * next, 2 * i + 1, 2 * next + 1, 2 * next});
                }
                SDL_RenderGeometry(renderer(), nullptr, vertices.data(), static_cast<int>(vertices.size()), indices.data(), static_cast<int>(indices.size()));
            }

            // A picture with round corners, and turned: triangles with the coordinates of the picture, since SDL cannot cut it
            void drawRoundedTexture(SDL_Texture* texture, const TextureDraw& draw)
            {
                int textureWidth = 0;
                int textureHeight = 0;

                SDL_QueryTexture(texture, nullptr, nullptr, &textureWidth, &textureHeight);
                const Rect& to = draw.destination;
                const Rect from = draw.source.w > 0.0f ? draw.source : Rect{0.0f, 0.0f, static_cast<float>(textureWidth), static_cast<float>(textureHeight)};
                const auto outline = roundedOutline(to, draw.radius, segmentsFor(draw.radius));
                const SDL_Color tint{draw.tint.r, draw.tint.g, draw.tint.b, draw.tint.a};
                const float angle = draw.rotation * 3.14159265358979f / 180.0f;
                const float pivotX = to.x + draw.pivot.x * to.w;
                const float pivotY = to.y + draw.pivot.y * to.h;
                const auto place = [&](SDL_FPoint point) {
                    float u = to.w > 0.0f ? (point.x - to.x) / to.w : 0.0f;
                    float v = to.h > 0.0f ? (point.y - to.y) / to.h : 0.0f;
                    const float dx = point.x - pivotX;
                    const float dy = point.y - pivotY;

                    u = draw.flipX ? 1.0f - u : u;
                    v = draw.flipY ? 1.0f - v : v;
                    return SDL_Vertex{{pivotX + dx * std::cos(angle) - dy * std::sin(angle), pivotY + dx * std::sin(angle) + dy * std::cos(angle)}, tint,
                        {(from.x + u * from.w) / static_cast<float>(textureWidth), (from.y + v * from.h) / static_cast<float>(textureHeight)}};
                };
                std::vector<SDL_Vertex> vertices;
                std::vector<int> indices;

                vertices.push_back(place({to.x + to.w / 2.0f, to.y + to.h / 2.0f}));
                for (const SDL_FPoint& point : outline) {
                    vertices.push_back(place(point));
                }
                for (std::size_t i = 0; i < outline.size(); ++i) {
                    indices.insert(indices.end(), {0, static_cast<int>(i) + 1, static_cast<int>((i + 1) % outline.size()) + 1});
                }
                SDL_SetTextureColorMod(texture, 255, 255, 255);   // (the tint is in the vertices; a draw before this one left its own)
                SDL_SetTextureAlphaMod(texture, 255);
                SDL_RenderGeometry(renderer(), texture, vertices.data(), static_cast<int>(vertices.size()), indices.data(), static_cast<int>(indices.size()));
            }

            void drawTexture(const TextureDraw& draw) override
            {
                const auto found = m_textures.find(draw.texture);

                if (found == m_textures.end()) {
                    return;
                }
                SDL_Texture* texture = found->second;

                SDL_SetTextureScaleMode(texture, draw.filter == Filter::Linear ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
                if (draw.radius > 0.0f) {
                    drawRoundedTexture(texture, draw);
                    return;
                }
                const SDL_FRect destination = toSdl(draw.destination);
                const SDL_Rect source = {static_cast<int>(draw.source.x), static_cast<int>(draw.source.y),
                    static_cast<int>(draw.source.w), static_cast<int>(draw.source.h)};
                const SDL_FPoint pivot = {draw.pivot.x * destination.w, draw.pivot.y * destination.h};
                const int flip = (draw.flipX ? SDL_FLIP_HORIZONTAL : 0) | (draw.flipY ? SDL_FLIP_VERTICAL : 0);

                SDL_SetTextureColorMod(texture, draw.tint.r, draw.tint.g, draw.tint.b);
                SDL_SetTextureAlphaMod(texture, draw.tint.a);
                SDL_RenderCopyExF(renderer(), texture, draw.source.w > 0.0f ? &source : nullptr, &destination,
                    draw.rotation, &pivot, static_cast<SDL_RendererFlip>(flip));
            }

            Image readPixels(void) override
            {
                Image image;
                int width = 0;
                int height = 0;

                SDL_GetRendererOutputSize(renderer(), &width, &height);
                image.width = width;
                image.height = height;
                image.rgba.resize(static_cast<std::size_t>(width) * height * 4);
                if (SDL_RenderReadPixels(renderer(), nullptr, SDL_PIXELFORMAT_RGBA32, image.rgba.data(), width * 4) != 0) {
                    return {};
                }
                return image;
            }

        private:
            SDL_Renderer* renderer(void) const { return m_context->renderer.get(); }

            static SDL_FRect toSdl(const Rect& rect) { return {rect.x, rect.y, rect.w, rect.h}; }

            std::shared_ptr<SdlContext>                m_context;
            std::unordered_map<TextureId, SDL_Texture*> m_textures;
            TextureId                                  m_lastId = 0;
    };
}

kuge::Backend kuge::makeSdlBackend(const WindowConfig& config)
{
    auto context = std::make_shared<SdlContext>(config);
    Backend backend;

    backend.window = std::make_unique<SdlWindow>(context);
    backend.input = std::make_unique<SdlInput>(context);
    backend.renderer = std::make_unique<SdlRenderer>(context);
    backend.fonts = makeSdlFontLoader();
    if (config.openGL) {
        try {
            backend.opengl = std::make_unique<SdlOpenGL>(context, config.width, config.height);
        } catch (const BackendError& error) {
            Logger::logger().category("client").warn("no OpenGL for the 3D: {}", error.what());
        }
    }
    try {
        backend.audio = makeSdlAudio();
    } catch (const AudioError& error) {
        Logger::logger().category("client").warn("no sound: {}", error.what());
    }
    return backend;
}
