#include "backend/SdlBackend.hpp"
#include "sdl/SdlKeys.hpp"
#include <SDL.h>
#include <algorithm>
#include <format>
#include <map>
#include <memory>
#include <unordered_map>

namespace
{
    using namespace kuge;

    [[noreturn]] void fail(const char* what)
    {
        throw BackendError(std::format("{}: {}", what, SDL_GetError()));
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
            }

            ~SdlContext(void)
            {
                for (auto& [id, controller] : controllers) {
                    SDL_GameControllerClose(controller);
                }
            }

            SdlContext(const SdlContext&)            = delete;
            SdlContext& operator=(const SdlContext&) = delete;

            std::unique_ptr<Subsystem>                         m_video;
            std::unique_ptr<Subsystem>                         m_controllers;
            std::unique_ptr<SDL_Window, WindowDeleter>         window;
            std::unique_ptr<SDL_Renderer, RendererDeleter>     renderer;
            std::map<SDL_JoystickID, SDL_GameController*>      controllers;
    };

    ////////////////////////////////////////////////////////////////////////////
    class SdlWindow : public IWindow
    {
        public:
            explicit SdlWindow(std::shared_ptr<SdlContext> context) : m_context(std::move(context)) {}

            Vec2 size(void) const override
            {
                int width = 0;
                int height = 0;

                SDL_GetRendererOutputSize(m_context->renderer.get(), &width, &height);
                return {static_cast<float>(width), static_cast<float>(height)};
            }

            void setTitle(const std::string& title) override
            {
                SDL_SetWindowTitle(m_context->window.get(), title.c_str());
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
                            out.push_back(ResizeEvent{event.window.data1, event.window.data2});
                        }
                        break;
                    case SDL_KEYDOWN:
                    case SDL_KEYUP:
                        out.push_back(KeyEvent{keyFromScancode(event.key.keysym.scancode),
                            event.type == SDL_KEYDOWN, event.key.repeat != 0});
                        break;
                    case SDL_MOUSEMOTION:
                        out.push_back(MouseMoveEvent{
                            {static_cast<float>(event.motion.x), static_cast<float>(event.motion.y)},
                            {static_cast<float>(event.motion.xrel), static_cast<float>(event.motion.yrel)}});
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

            Vec2 outputSize(void) const override
            {
                int width = 0;
                int height = 0;

                SDL_GetRendererOutputSize(renderer(), &width, &height);
                return {static_cast<float>(width), static_cast<float>(height)};
            }

            void begin(Color clear) override
            {
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

            void drawTexture(const TextureDraw& draw) override
            {
                const auto found = m_textures.find(draw.texture);

                if (found == m_textures.end()) {
                    return;
                }
                SDL_Texture* texture = found->second;
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
    return backend;
}
