//
// Created by Giacomo Bergami on 21/09/2026.
//

#ifndef MALAMORE_STRINGS_H
#define MALAMORE_STRINGS_H

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


#endif //MALAMORE_STRINGS_H