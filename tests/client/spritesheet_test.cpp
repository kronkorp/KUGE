extern "C" {
    #include "kronklab/kronklab.h"
}
#include "backend/dummy/DummyBackend.hpp"
#include "render/Spritesheet.hpp"

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    // The renderer must outlive the texture: the caller keeps both
    kuge::Spritesheet sheetOf(kuge::DummyBackend& dummy, int width, int height, int cellWidth, int cellHeight,
        int margin = 0, int spacing = 0)
    {
        kuge::Spritesheet sheet;
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4, 255);

        sheet.texture = kuge::Texture::fromPixels(*dummy.renderer, width, height, pixels);
        sheet.frameWidth = cellWidth;
        sheet.frameHeight = cellHeight;
        sheet.margin = margin;
        sheet.spacing = spacing;
        return sheet;
    }

    bool same(kuge::Rect a, kuge::Rect b) { return a == b; }
}

Test(sheets, cells_of_a_picture)
{
    auto dummy = kuge::makeDummyBackend();
    auto sheet = sheetOf(dummy, 64, 32, 16, 16);

    AssertEq(sheet.columnsOf(), 4, "4 columns");
    AssertEq(sheet.rowsOf(), 2, "2 rows");
    AssertEq(sheet.frameCount(), 8, "8 cells");
    Assert(same(sheet.frame(0), {0.0f, 0.0f, 16.0f, 16.0f}), "the first is at the top left");
    Assert(same(sheet.frame(3), {48.0f, 0.0f, 16.0f, 16.0f}), "the end of the first row");
    Assert(same(sheet.frame(5), {16.0f, 16.0f, 16.0f, 16.0f}), "then the next row");
    Assert(same(sheet.frame(99), sheet.frame(0)), "out of range: the first");
    Assert(same(sheet.frame(-1), sheet.frame(0)), "negative too");
    sheet.texture.reset();
}

Test(sheets, margin_and_spacing)
{
    auto dummy = kuge::makeDummyBackend();
    // 1 pixel around, 1 between the cells: 1 + 16 + 1 + 16 + 1 = 35 wide
    auto sheet = sheetOf(dummy, 35, 18, 16, 16, 1, 1);

    AssertEq(sheet.columnsOf(), 2, "two columns fit");
    AssertEq(sheet.rowsOf(), 1, "one row");
    Assert(same(sheet.frame(0), {1.0f, 1.0f, 16.0f, 16.0f}), "inside the margin");
    Assert(same(sheet.frame(1), {18.0f, 1.0f, 16.0f, 16.0f}), "after the spacing");
    sheet.texture.reset();
}

Test(sheets, columns_can_be_given)
{
    auto dummy = kuge::makeDummyBackend();
    auto sheet = sheetOf(dummy, 64, 32, 16, 16);

    sheet.columns = 2;
    AssertEq(sheet.columnsOf(), 2, "the number asked for");
    Assert(same(sheet.frame(3), {16.0f, 16.0f, 16.0f, 16.0f}), "numbering follows it");
    sheet.texture.reset();
}

Test(sheets, nothing_to_cut)
{
    kuge::Spritesheet none;

    AssertEq(none.columnsOf(), 0, "no picture, no columns");
    AssertEq(none.frameCount(), 0, "no cells");
    Assert(same(none.frame(3), {0.0f, 0.0f, 16.0f, 16.0f}), "and it does not divide by zero");
}
