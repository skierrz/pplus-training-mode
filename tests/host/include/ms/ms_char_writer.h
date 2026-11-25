#pragma once

#include <types.h>

#include "data/font_advances.h"
#include "host/canvas.h"

// Stand-in for the game's character writer, as far as src/main/gfx/text.cpp
// uses it. The two things that decide where text lands are the game's:
//
//   Print (0x8006FE50) asks the font for the character's glyph and moves
//   the cursor by the glyph's advance times the horizontal scale (times two
//   members the constructor sets to 1 and the mod never changes).
//   GetFontHeight (0x8006FCF8) is the font's height times the vertical
//   scale.
//
// Both were read in the game's code in a memory dump; the advances and the
// height are the game's font's (tests/host/data/font_advances.h). Nothing
// is drawn: every character is reported to host/canvas.
namespace nw4r {
namespace ut {
    struct Color {
        Color() : rgba(0)
        {
        }
        Color(u32 value) : rgba(value)
        {
        }
        u32 rgba;
    };
}
}

namespace ms {
    class CharWriter {
    public:
        // Fixed width set, so text.cpp's print() hands every character to
        // Print below: its own glyph path calls into the game.
        CharWriter()
            : m_fontScaleX(1.0f), m_fontScaleY(1.0f), m_xPos(0.0f), m_yPos(0.0f), m_zPos(0.0f), m_font(NULL),
              m_isFixedWidth(true), m_104(1.0f), m_unknownFontWidthModifier(1.0f), m_edgeWidth(0.0f), m_edgeColor(0),
              m_color(0)
        {
            m_colorRect.m_topLeft = 0;
            m_colorRect.m_topRight = 0;
            m_colorRect.m_bottomLeft = 0;
            m_colorRect.m_bottomRight = 0;
        }
        void SetTextColor(nw4r::ut::Color color)
        {
            m_color = color.rgba;
            m_colorRect.m_topLeft = color;
            m_colorRect.m_topRight = color;
            m_colorRect.m_bottomLeft = color;
            m_colorRect.m_bottomRight = color;
        }
        void SetEdge(float width, nw4r::ut::Color color)
        {
            m_edgeWidth = width;
            m_edgeColor = color;
        }
        void SetScale(float x, float y)
        {
            m_fontScaleX = x;
            m_fontScaleY = y;
        }
        void SetCursor(float x, float y, float)
        {
            m_xPos = x;
            m_yPos = y;
            bool disp = m_font == reinterpret_cast<void*>(0x80497DA4);
            host::canvas::begin(x, y, m_fontScaleX, m_fontScaleY, m_color, disp);
        }
        void SetupGX()
        {
        }
        void Print(u16 character)
        {
            bool known;
            bool disp = m_font == reinterpret_cast<void*>(0x80497DA4);
            float adv = disp ? host::canvas::advanceDisplay(static_cast<unsigned char>(character), known)
                             : host::canvas::advance(static_cast<unsigned char>(character), known);
            float advance = adv * m_fontScaleX;
            m_xPos += advance;
            host::canvas::put(static_cast<unsigned char>(character), advance, known, m_xPos);
        }
        float GetCursorX() const
        {
            return m_xPos;
        }
        float GetFontHeight() const
        {
            bool disp = m_font == reinterpret_cast<void*>(0x80497DA4);
            return m_fontScaleY * static_cast<float>(disp ? host::font::kDisplayHeight : host::font::kHeight);
        }

        struct {
            nw4r::ut::Color m_topLeft;
            nw4r::ut::Color m_topRight;
            nw4r::ut::Color m_bottomLeft;
            nw4r::ut::Color m_bottomRight;
        } m_colorRect;
        float m_fontScaleX;
        float m_fontScaleY;
        float m_xPos;
        float m_yPos;
        float m_zPos;
        void* m_font;
        bool m_isFixedWidth;
        float m_104;
        float m_unknownFontWidthModifier;
        float m_edgeWidth;
        nw4r::ut::Color m_edgeColor;

    private:
        u32 m_color;
    };
}
