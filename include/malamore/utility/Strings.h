//
// Created by Giacomo Bergami on 21/09/2026.
//

#ifndef MALAMORE_STRINGS_H
#define MALAMORE_STRINGS_H

#include <iomanip>
#include <iostream>
#include <string>
#include <locale>

static inline std::string wchar_to_string(const std::wstring &wstr) {
    std::mbstate_t state{};
    const wchar_t *src = wstr.data();
    size_t len = 1 + std::wcsrtombs(nullptr, &src, 0, &state);
    if (len == static_cast<size_t>(-1)) return {};

    std::string result(len, '\0');
    std::wcsrtombs(&result[0], &src, len, &state);
    result.pop_back(); // remove null terminator
    return result;
}

static inline
std::ostream& hex_dump(std::ostream& os, const void *buffer,
                       std::size_t bufsize, bool showPrintableChars = true)
{
    // Source - https://codereview.stackexchange.com/a/165162
    // Posted by Edward, modified by community. See post 'Timeline' for change history
    // Retrieved 2026-09-18, License - CC BY-SA 3.0
    if (buffer == nullptr) {
        return os;
    }
    auto oldFormat = os.flags();
    auto oldFillChar = os.fill();
    constexpr std::size_t maxline{8};
    // create a place to store text version of string
    char renderString[maxline+1];
    char *rsptr{renderString};
    // convenience cast
    const unsigned char *buf{reinterpret_cast<const unsigned char *>(buffer)};

    for (std::size_t linecount=maxline; bufsize; --bufsize, ++buf) {
        os << std::setw(2) << std::setfill('0') << std::hex
           << static_cast<unsigned>(*buf) << ' ';
        *rsptr++ = std::isprint(*buf) ? *buf : '.';
        if (--linecount == 0) {
            *rsptr++ = '\0';  // terminate string
            if (showPrintableChars) {
                os << " | " << renderString;
            }
            os << '\n';
            rsptr = renderString;
            //linecount = std::min(maxline, bufsize);
            if (maxline > bufsize) {
                linecount = bufsize;
            } else {
                linecount = maxline;
            }
        }
    }
    // emit newline if we haven't already
    if (rsptr != renderString) {
        if (showPrintableChars) {
            for (*rsptr++ = '\0'; rsptr != &renderString[maxline+1]; ++rsptr) {
                os << "   ";
            }
            os << " | " << renderString;
        }
        os << '\n';
    }

    os.fill(oldFillChar);
    os.flags(oldFormat);
    return os;
}


#endif //MALAMORE_STRINGS_H