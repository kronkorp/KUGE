extern "C" {
    #include "kronklab/kronklab.h"
}
#include "backend/dummy/DummyFont.hpp"
#include "client_fixture.hpp"
#include "ui/TextRenderer.hpp"
#include "ui_fixture.hpp"
#include <set>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    using Call = kuge::DummyRenderer::Call;
}

Test(text, the_dummy_font_measures)
{
    kuge::DummyFont font;

    Assert(font.measure("Hello") == kuge::Vec2(40.0f, 12.0f), "8 pixels a letter, one line");
    Assert(font.measure("ab\ncdef\ng") == kuge::Vec2(32.0f, 36.0f), "a line break starts a line: the longest is 4 letters");
    Assert(font.measure("") == kuge::Vec2(0.0f, 12.0f), "no text, still a line high");
    Assert(font.measure("one two three", 48) == kuge::Vec2(40.0f, 36.0f), "wrapped within 6 letters: \"one\", \"two\", \"three\", the longest has 5");
    Assert(font.measure("one two three", 0) == kuge::Vec2(104.0f, 12.0f), "not wrapped without a width");
    Assert(font.measure("unbreakable", 24) == kuge::Vec2(88.0f, 12.0f), "a word longer than the width is left whole");
    AssertEq(font.lineHeight(), 12, "line height");
    const auto image = font.rasterize("Hello");
    AssertEq(image.width, 40, "the picture has the size of the text");
    AssertEq(image.height, 12, "in both directions");
    AssertEq(image.rgba.size(), 40 * 12 * 4, "4 bytes a pixel");
    Assert(font.rasterize("").empty(), "an empty text has no picture");
}

Test(text, fonts_have_their_own_id)
{
    kuge::DummyFont a;
    kuge::DummyFont b;
    std::set<std::uint64_t> ids{a.id(), b.id()};

    AssertEq(ids.size(), 2, "two fonts, two ids");
    {
        kuge::DummyFont gone;
        ids.insert(gone.id());
    }
    kuge::DummyFont after;
    Assert(ids.count(after.id()) == 0, "an id is not used again when a font goes (an address would be)");
}

Test(text, drawing_a_text)
{
    auto dummy = kuge::makeDummyBackend();
    kuge::TextRenderer text(*dummy.renderer);
    kuge::DummyFont font;

    dummy.renderer->begin({});
    text.draw(font, "Hello", {10.0f, 20.0f}, {255, 0, 0, 255});
    const auto& calls = dummy.renderer->current();

    AssertEq(calls.size(), 1, "one picture");
    Assert(calls[0].kind == Call::Kind::Texture, "drawn as a texture");
    Assert(calls[0].destination == kuge::Rect(10.0f, 20.0f, 40.0f, 12.0f), "at the place, at the size of the text");
    Assert(calls[0].texture.tint == kuge::Color(255, 0, 0, 255), "in the color asked for");
    AssertEq(dummy.renderer->textureCount(), 1, "made once");
}

Test(text, an_empty_text_draws_nothing)
{
    auto dummy = kuge::makeDummyBackend();
    kuge::TextRenderer text(*dummy.renderer);
    kuge::DummyFont font;

    text.draw(font, "", {0.0f, 0.0f}, {});
    AssertEq(dummy.renderer->current().size(), 0, "no call");
    AssertEq(dummy.renderer->textureCount(), 0, "and no texture");
    AssertEq(text.cached(), 1, "but it is remembered as empty, so it is not worked out again");
}

Test(text, a_text_is_made_once)
{
    auto dummy = kuge::makeDummyBackend();
    kuge::TextRenderer text(*dummy.renderer);
    kuge::DummyFont font;

    for (int frame = 0; frame < 50; ++frame) {
        text.beginFrame();
        dummy.renderer->begin({});
        text.draw(font, "Score: 100", {0.0f, 0.0f}, {255, 255, 255, 255});
        text.draw(font, "Score: 100", {0.0f, 20.0f}, {255, 0, 0, 255});    // elsewhere, in another color: the same picture
    }
    AssertEq(dummy.renderer->textureCount(), 1, "one picture for a text, at any place and in any color");
    AssertEq(dummy.renderer->current().size(), 2, "and it is drawn each time");
    text.draw(font, "Score: 101", {0.0f, 0.0f}, {});
    AssertEq(dummy.renderer->textureCount(), 2, "another text, another picture");
    text.draw(font, "Score: 100", {0.0f, 0.0f}, {}, 40);
    AssertEq(dummy.renderer->textureCount(), 3, "the same text wrapped differently too");
}

Test(text, fonts_do_not_share)
{
    auto dummy = kuge::makeDummyBackend();
    kuge::TextRenderer text(*dummy.renderer);
    kuge::DummyFont one;
    kuge::DummyFont two;

    text.draw(one, "same", {0.0f, 0.0f}, {});
    text.draw(two, "same", {0.0f, 0.0f}, {});
    AssertEq(dummy.renderer->textureCount(), 2, "the same text in two fonts is two pictures");
}

Test(text, unused_texts_are_forgotten)
{
    auto dummy = kuge::makeDummyBackend();
    kuge::TextRenderer text(*dummy.renderer);
    kuge::DummyFont font;

    text.draw(font, "seen once", {0.0f, 0.0f}, {});
    text.draw(font, "always", {0.0f, 0.0f}, {});
    for (int frame = 0; frame < 100; ++frame) {
        text.beginFrame();
        text.draw(font, "always", {0.0f, 0.0f}, {});
    }
    AssertEq(text.cached(), 2, "kept for a while");
    for (int frame = 0; frame < 40; ++frame) {
        text.beginFrame();
        text.draw(font, "always", {0.0f, 0.0f}, {});
    }
    AssertEq(text.cached(), 1, "the one that is not drawn any more goes");
    AssertEq(dummy.renderer->textureCount(), 1, "and its picture with it");
}

Test(text, too_many_texts)
{
    auto dummy = kuge::makeDummyBackend();
    kuge::TextRenderer text(*dummy.renderer);
    kuge::DummyFont font;

    // A counter that changes every frame would fill the memory: there is a limit
    for (int i = 0; i < 1500; ++i) {
        text.draw(font, "text " + std::to_string(i), {0.0f, 0.0f}, {});
    }
    text.beginFrame();
    Assert(text.cached() <= 1024, "at most 1024 are kept, got %zu", text.cached());
    AssertEq(dummy.renderer->textureCount(), text.cached(), "and no picture is left behind");
}

Test(text, fonts_of_the_client)
{
    FontFile file;
    auto dummy = kuge::makeDummyBackend();
    kuge::Engine engine(kuge::Engine::Config{.mode = kuge::Engine::Mode::Windowed});
    auto& client = engine.addModule<kuge::ClientModule>(std::move(dummy.backend));

    auto a = client.loadFont(file.path, 16);
    auto b = client.loadFont(file.path, 16);
    auto c = client.loadFont(file.path, 24);
    bool missing = false;

    Assert(a == b, "the same font at the same size is loaded once");
    Assert(a != c, "another size is another font");
    AssertEq(dummy.fonts->loads, 2, "loaded twice, got %d", dummy.fonts->loads);
    a.reset();
    b.reset();
    auto again = client.loadFont(file.path, 16);
    AssertEq(dummy.fonts->loads, 3, "and loaded again when nobody held it any more");
    try { client.loadFont("/nonexistent/font.ttf", 16); } catch (const kuge::FontError&) { missing = true; }
    Assert(missing, "a file that is not there");
    c.reset();
    again.reset();
}

Test(text, a_backend_without_fonts)
{
    FontFile file;
    auto dummy = kuge::makeDummyBackend();
    kuge::Engine engine(kuge::Engine::Config{.mode = kuge::Engine::Mode::Windowed});
    bool refused = false;

    dummy.backend.fonts.reset();
    auto& client = engine.addModule<kuge::ClientModule>(std::move(dummy.backend));
    try { client.loadFont(file.path, 16); } catch (const kuge::FontError&) { refused = true; }
    Assert(refused, "no fonts: an error that says so, not a crash");
    AssertEq(client.text().cached(), 0, "and the text renderer is there all the same");
}
