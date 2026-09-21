#include "backend/SdlBackend.hpp"
#include <SDL.h>
#include <SDL_ttf.h>
#include <algorithm>
#include <format>

namespace
{
    using namespace kuge;

    // TTF_Init / TTF_Quit, once for as many fonts as there are: the last one to
    // go closes the library
    class TtfLibrary
    {
        public:
            TtfLibrary(void)
            {
                if (TTF_WasInit() == 0 && TTF_Init() != 0) {
                    throw FontError(std::format("cannot start SDL_ttf: {}", TTF_GetError()));
                }
                m_started = true;
            }

            ~TtfLibrary(void)
            {
                if (m_started) {
                    TTF_Quit();
                }
            }

            TtfLibrary(const TtfLibrary&)            = delete;
            TtfLibrary& operator=(const TtfLibrary&) = delete;

        private:
            bool m_started = false;
    };

    struct SurfaceDeleter { void operator()(SDL_Surface* surface) const { SDL_FreeSurface(surface); } };
    using Surface = std::unique_ptr<SDL_Surface, SurfaceDeleter>;

    class SdlFont : public IFont
    {
        public:
            SdlFont(std::shared_ptr<TtfLibrary> library, TTF_Font* font) : m_library(std::move(library)), m_font(font) {}

            ~SdlFont(void) override { TTF_CloseFont(m_font); }

            SdlFont(const SdlFont&)            = delete;
            SdlFont& operator=(const SdlFont&) = delete;

            Vec2 measure(std::string_view text, int wrapWidth) const override
            {
                const std::string owned(text);
                int width = 0;
                int height = 0;

                if (owned.empty()) {
                    return {0.0f, static_cast<float>(lineHeight())};
                }
                if (wrapWidth > 0 || owned.find('\n') != std::string::npos) {
                    // Only the renderer knows how the text is cut: ask it
                    const Image image = rasterize(text, wrapWidth);

                    return {static_cast<float>(image.width), static_cast<float>(image.height)};
                }
                TTF_SizeUTF8(m_font, owned.c_str(), &width, &height);
                return {static_cast<float>(width), static_cast<float>(height)};
            }

            Image rasterize(std::string_view text, int wrapWidth) const override
            {
                const std::string owned(text);
                const SDL_Color white = {255, 255, 255, 255};
                Image image;

                if (owned.empty()) {
                    return image;
                }
                Surface rendered(TTF_RenderUTF8_Blended_Wrapped(m_font, owned.c_str(), white, wrapWidth > 0 ? static_cast<Uint32>(wrapWidth) : 0));

                if (!rendered) {
                    return image;
                }
                // Whatever the font's format is, the picture is RGBA
                Surface converted(SDL_ConvertSurfaceFormat(rendered.get(), SDL_PIXELFORMAT_RGBA32, 0));

                if (!converted) {
                    return image;
                }
                image.width = converted->w;
                image.height = converted->h;
                image.rgba.resize(static_cast<std::size_t>(image.width) * image.height * 4);
                for (int y = 0; y < image.height; ++y) {
                    const auto* row = static_cast<const std::uint8_t*>(converted->pixels) + static_cast<std::size_t>(y) * converted->pitch;

                    std::copy(row, row + static_cast<std::size_t>(image.width) * 4, image.rgba.begin() + static_cast<std::size_t>(y) * image.width * 4);
                }
                return image;
            }

            int lineHeight(void) const override { return TTF_FontLineSkip(m_font); }

        private:
            std::shared_ptr<TtfLibrary> m_library;   // last: closed after the font
            TTF_Font*                   m_font;
    };

    class SdlFontLoader : public IFontLoader
    {
        public:
            std::shared_ptr<IFont> load(const std::filesystem::path& file, int pointSize) override
            {
                auto library = m_library.lock();

                if (!library) {
                    library = std::make_shared<TtfLibrary>();
                    m_library = library;
                }
                TTF_Font* font = TTF_OpenFont(file.c_str(), pointSize);

                if (!font) {
                    throw FontError(std::format("cannot load the font '{}': {}", file.string(), TTF_GetError()));
                }
                return std::make_shared<SdlFont>(std::move(library), font);
            }

        private:
            std::weak_ptr<TtfLibrary> m_library;
    };
}

std::unique_ptr<kuge::IFontLoader> kuge::makeSdlFontLoader(void)
{
    return std::make_unique<SdlFontLoader>();
}
