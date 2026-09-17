#include <gtest/gtest.h>
#include "Hunt.h"
#include "Renderer/UIText.h"
#include "Renderer/CPUTextWin32.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

// Minimal engine state for the real UIText.cpp box layout. That translation
// unit references only these two globals, so the sizing rule can be exercised
// without a renderer or a game loop.
int   WinH    = 600;
float UIScale = 1.0f;

namespace {

// The trophy panel geometry the renderers pass in, from trophy.tga/collect.tga
// (210x124 with the recessed panel spanning rows 22..98).
constexpr int kPadX = 16;
constexpr int kPadY = 18;
constexpr int kStep = 16;
constexpr int kMaxW = 190;
constexpr int kMaxH = 80;

// Recreates the font DrawBox sizes, so a returned size can be measured the same
// way the helper measures it.
HFONT MakeFont(int px)
{
    const int width = static_cast<int>(std::lround(static_cast<double>(px) * 7 / 16));
    return CreateFont(px, width, 0, 0, 100, 0, 0, 0, ANSI_CHARSET,
                      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                      DEFAULT_PITCH | FF_SWISS, nullptr);
}

int Measure(HDC hdc, const char* s)
{
    SIZE sz{};
    GetTextExtentPoint32(hdc, s, static_cast<int>(std::strlen(s)), &sz);
    return sz.cx;
}

// Holds the row strings alongside the Seg/Row views over them, so the pointers
// handed to DrawBox stay valid for the lifetime of the object.
class Layout {
public:
    explicit Layout(std::vector<std::vector<std::string>> rows)
        : m_text(std::move(rows))
    {
        m_segs.resize(m_text.size());
        for (size_t r = 0; r < m_text.size(); r++) {
            for (const std::string& s : m_text[r]) {
                m_segs[r].push_back({ s.c_str(), 0x00BFBFBF });
            }
        }
        m_rows.resize(m_segs.size());
        for (size_t r = 0; r < m_segs.size(); r++) {
            m_rows[r] = { m_segs[r].data(), static_cast<int>(m_segs[r].size()) };
        }
    }

    const uitxt::Row* rows() const { return m_rows.data(); }
    int count() const { return static_cast<int>(m_rows.size()); }
    const std::vector<std::vector<std::string>>& text() const { return m_text; }

private:
    std::vector<std::vector<std::string>> m_text;
    std::vector<std::vector<uitxt::Seg>>  m_segs;
    std::vector<uitxt::Row>               m_rows;
};

// The grouped five-row trophy layout.
std::vector<std::vector<std::string>> TrophyRows(const std::string& name)
{
    return {
        { "Name: ", name },
        { "Weight: ", "5.98T", "Length: ", "5.36m" },
        { "Weapon: ", "Pistol", "Score: ", "10" },
        { "Range of kill: ", "36.1m" },
        { "Date: ", "10.9.2026", "Time: ", "20:00" },
    };
}

// The eight single-stat rows the GL renderer used before, in its original order.
std::vector<std::vector<std::string>> EightRowRows(const std::string& name)
{
    return {
        { "Name: ", name },
        { "Weight: ", "5.98T" },
        { "Length: ", "5.36m" },
        { "Weapon: ", "Pistol" },
        { "Score: ", "10" },
        { "Range of kill: ", "36.1m" },
        { "Date: ", "10.9.2026" },
        { "Time: ", "20:00" },
    };
}

class UITextTest : public testing::Test {
protected:
    HDC hdc = nullptr;
    CPUText::GDICanvas canvas;

    void SetUp() override
    {
        hdc = CreateCompatibleDC(nullptr);
        ASSERT_NE(hdc, nullptr);
        canvas.dc = hdc;
        WinH = 600;
        UIScale = 1.0f;
    }

    void TearDown() override
    {
        if (hdc) DeleteDC(hdc);
    }

    int Draw(const Layout& layout, int maxH = kMaxH)
    {
        return uitxt::DrawBox(&canvas, 0, 0, kPadX, kPadY, kStep, kMaxW, maxH,
                              layout.rows(), layout.count());
    }

    // Widest rendered row, using the same gap rule DrawBox applies.
    int WidestRowAt(const Layout& layout, int px)
    {
        const int gap = (std::max)(1, px / 2);
        HFONT font = MakeFont(px);
        HGDIOBJ old = SelectObject(hdc, font);

        int widest = 0;
        for (const auto& row : layout.text()) {
            int w = 0;
            for (size_t i = 0; i < row.size(); i++) {
                w += Measure(hdc, row[i].c_str());
                if (i + 1 < row.size()) w += gap;
            }
            widest = (std::max)(widest, w);
        }

        SelectObject(hdc, old);
        DeleteObject(font);
        return widest;
    }
};

} // namespace

TEST_F(UITextTest, ScaleIsResolutionOverSixHundred)
{
    WinH = 600;
    EXPECT_FLOAT_EQ(uitxt::Scale(), 1.0f);

    WinH = 1080;
    EXPECT_FLOAT_EQ(uitxt::Scale(), 1.8f);

    WinH = 1440;
    EXPECT_FLOAT_EQ(uitxt::Scale(), 2.4f);
}

TEST_F(UITextTest, ScaleHonoursTheUiScaleMultiplier)
{
    WinH = 1440;
    UIScale = 0.5f;
    EXPECT_FLOAT_EQ(uitxt::Scale(), 1.2f);
}

TEST_F(UITextTest, PxRoundsWithResolution)
{
    WinH = 600;
    EXPECT_EQ(uitxt::Px(16), 16);

    WinH = 1440;
    EXPECT_EQ(uitxt::Px(16), 38);
}

// The bug being fixed: the text used to be pinned at 16px no matter how large
// the box was drawn. It must now grow with the resolution.
TEST_F(UITextTest, FontGrowsWithTheResolution)
{
    Layout layout(TrophyRows("Stegosaurus"));

    WinH = 600;
    const int pxLow = Draw(layout);
    WinH = 1080;
    const int pxMid = Draw(layout);
    WinH = 1440;
    const int pxHigh = Draw(layout);

    EXPECT_GT(pxLow, 0);
    EXPECT_GT(pxMid, pxLow);
    EXPECT_GT(pxHigh, pxMid);
    EXPECT_GT(pxHigh, 16) << "text must scale with the box, not stay at the old fixed 16px";
}

// A short name only needs a little trimming: the widest row in the grouped
// layout is the paired Date/Time line, not the name.
TEST_F(UITextTest, ShortContentStaysNearTheNominalSize)
{
    WinH = 1440;
    Layout layout(TrophyRows("Stegosaurus"));
    const int nominal = uitxt::Px(16);

    const int px = Draw(layout);

    EXPECT_GE(px, (nominal * 4) / 5);
    EXPECT_LE(WidestRowAt(layout, px), uitxt::Px(kMaxW));
}

// The regression this whole change exists for: a long name used to run past the
// right edge of the panel at every resolution.
TEST_F(UITextTest, LongNameShrinksTheFontToFitThePanel)
{
    WinH = 1440;
    Layout layout(TrophyRows("Pachycephalosaurus"));
    const int nominal = uitxt::Px(16);

    const int px = Draw(layout);

    EXPECT_GT(px, 0);
    EXPECT_LT(px, nominal) << "this name cannot fit at the nominal size";
    EXPECT_LE(WidestRowAt(layout, px), uitxt::Px(kMaxW));
}

TEST_F(UITextTest, LongModNameAlsoFits)
{
    WinH = 1440;
    Layout layout(TrophyRows("Velociraptor underling"));

    const int px = Draw(layout);

    EXPECT_GT(px, 0);
    EXPECT_LE(WidestRowAt(layout, px), uitxt::Px(kMaxW));
}

TEST_F(UITextTest, SingleLongRowIsClampedEvenWithoutPairing)
{
    WinH = 1440;
    Layout layout({ { "Name: Pachycephalosaurus" } });

    const int px = Draw(layout);

    EXPECT_GT(px, 0);
    EXPECT_LT(px, uitxt::Px(16));
    EXPECT_LE(WidestRowAt(layout, px), uitxt::Px(kMaxW));
}

// The five-row block must stay inside the panel at every supported height.
TEST_F(UITextTest, FiveRowBlockFitsThePanelHeight)
{
    for (int h : { 720, 900, 1080, 1440, 2160 }) {
        WinH = h;
        Layout layout(TrophyRows("Stegosaurus"));

        const int px = Draw(layout);
        ASSERT_GT(px, 0) << "at " << h << "p";

        const int stepPx = uitxt::Px(kStep);
        const int nominal = uitxt::Px(16);
        const int lineStep = (std::max)(px, MulDiv(stepPx, px, nominal));
        const int blockH = (layout.count() - 1) * lineStep + px;

        EXPECT_LE(blockH, uitxt::Px(kMaxH)) << "at " << h << "p the text block is too tall";
    }
}

// Why the layout was regrouped rather than merely rescaled: eight single-stat
// rows have to be squeezed far harder than five paired ones to fit the panel.
TEST_F(UITextTest, FivePairedRowsBeatEightSingleRows)
{
    WinH = 1440;
    Layout paired(TrophyRows("Stegosaurus"));
    Layout single(EightRowRows("Stegosaurus"));

    const int pxPaired = Draw(paired);
    const int pxSingle = Draw(single);

    EXPECT_GT(pxPaired, pxSingle)
        << "eight rows force a smaller font than five for the same panel";
}

TEST_F(UITextTest, DegenerateInputIsRejectedWithoutCrashing)
{
    Layout layout(TrophyRows("Stegosaurus"));

    EXPECT_EQ(uitxt::DrawBox(nullptr, 0, 0, kPadX, kPadY, kStep, kMaxW, kMaxH,
                             layout.rows(), layout.count()), 0);
    EXPECT_EQ(uitxt::DrawBox(&canvas, 0, 0, kPadX, kPadY, kStep, kMaxW, kMaxH,
                             nullptr, 0), 0);
    EXPECT_EQ(uitxt::DrawBox(&canvas, 0, 0, kPadX, kPadY, kStep, 0, kMaxH,
                             layout.rows(), layout.count()), 0);
}
