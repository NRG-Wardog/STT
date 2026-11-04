#pragma once
#include <windows.h>
#include <array>

namespace heben {
// This only constructs Ctrl+V; it contains no Enter, Tab, or text events.
inline std::array<INPUT,4> PasteKeyEvents() {
    std::array<INPUT,4> input{};
    for(auto &event:input)event.type=INPUT_KEYBOARD;
    input[0].ki.wVk=VK_CONTROL;
    input[1].ki.wVk='V';
    input[2].ki.wVk='V';input[2].ki.dwFlags=KEYEVENTF_KEYUP;
    input[3].ki.wVk=VK_CONTROL;input[3].ki.dwFlags=KEYEVENTF_KEYUP;
    return input;
}
}
