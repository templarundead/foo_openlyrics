#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

// These are characters, not font attributes: the styles survive saving as plain text.
// Unicode only defines these alphabets for Latin A-Z/a-z and (for some styles) digits.
// All other UTF-16 code units, including surrogate pairs, are preserved verbatim.
namespace unicode_text_style
{
    enum class Style
    {
        Bold,
        Italic,
        BoldItalic,
        Script,
        ScriptBold,
        Fraktur,
        DoubleStruck,
        FrakturBold,
        Sans,
        SansBold,
        SansItalic,
        SansBoldItalic,
        Monospace
    };

    struct Alphabet
    {
        char32_t upper;
        char32_t lower;
        char32_t digit; // Zero means Unicode has no digits for this style.
    };

    inline constexpr std::array<Alphabet, 13> alphabets = { { { 0x1D400, 0x1D41A, 0x1D7CE },
                                                              { 0x1D434, 0x1D44E, 0 },
                                                              { 0x1D468, 0x1D482, 0 },
                                                              { 0x1D49C, 0x1D4B6, 0 },
                                                              { 0x1D4D0, 0x1D4EA, 0 },
                                                              { 0x1D504, 0x1D51E, 0 },
                                                              { 0x1D538, 0x1D552, 0x1D7D8 },
                                                              { 0x1D56C, 0x1D586, 0 },
                                                              { 0x1D5A0, 0x1D5BA, 0x1D7E2 },
                                                              { 0x1D5D4, 0x1D5EE, 0x1D7EC },
                                                              { 0x1D608, 0x1D622, 0 },
                                                              { 0x1D63C, 0x1D656, 0 },
                                                              { 0x1D670, 0x1D68A, 0x1D7F6 } } };

    inline char32_t map(char16_t c, Style style)
    {
        const auto index = static_cast<size_t>(style);
        if(index >= alphabets.size()) return c;
        const auto alphabet = alphabets[index];
        char32_t result = c;
        if(c >= u'A' && c <= u'Z')
            result = alphabet.upper + c - u'A';
        else if(c >= u'a' && c <= u'z')
            result = alphabet.lower + c - u'a';
        else if(c >= u'0' && c <= u'9' && alphabet.digit != 0)
            result = alphabet.digit + c - u'0';

        // Holes in Mathematical Alphanumeric Symbols are encoded in Letterlike Symbols.
        switch(result)
        {
            case 0x1D455: return 0x210E; // italic h
            case 0x1D49D: return 0x212C; // script B
            case 0x1D4A0: return 0x2130; // script E
            case 0x1D4A1: return 0x2131; // script F
            case 0x1D4A3: return 0x210B; // script H
            case 0x1D4A4: return 0x2110; // script I
            case 0x1D4A7: return 0x2112; // script L
            case 0x1D4A8: return 0x2133; // script M
            case 0x1D4AD: return 0x211B; // script R
            case 0x1D4BA: return 0x212F; // script e
            case 0x1D4BC: return 0x210A; // script g
            case 0x1D4C4: return 0x2134; // script o
            case 0x1D506: return 0x212D; // fraktur C
            case 0x1D50B: return 0x210C; // fraktur H
            case 0x1D50C: return 0x2111; // fraktur I
            case 0x1D515: return 0x211C; // fraktur R
            case 0x1D51D: return 0x2128; // fraktur Z
            case 0x1D53A: return 0x2102; // double-struck C
            case 0x1D53F: return 0x210D; // double-struck H
            case 0x1D545: return 0x2115; // double-struck N
            case 0x1D547: return 0x2119; // double-struck P
            case 0x1D548: return 0x211A; // double-struck Q
            case 0x1D549: return 0x211D; // double-struck R
            case 0x1D551: return 0x2124; // double-struck Z
            default: return result;
        }
    }

    inline std::u16string format(std::u16string_view text, Style style)
    {
        std::u16string output;
        output.reserve(text.size() * 2);
        for(char16_t c : text)
        {
            const char32_t mapped = map(c, style);
            if(mapped <= 0xFFFF)
                output.push_back(static_cast<char16_t>(mapped));
            else
            {
                const char32_t value = mapped - 0x10000;
                output.push_back(static_cast<char16_t>(0xD800 + (value >> 10)));
                output.push_back(static_cast<char16_t>(0xDC00 + (value & 0x3FF)));
            }
        }
        return output;
    }
}
