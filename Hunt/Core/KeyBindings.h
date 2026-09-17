#pragma once
#include "../Platform/Platform.h"

// WM_KEYDOWN uses generic modifier VKs; saved defaults may name a side.
// Keep generic bindings working for either side without rewriting user saves.
inline bool KeyDownMatches(int binding, const Platform::KeyEvent& event)
{
    if (binding <= 0 || binding > 255) return false;
    return binding == event.key || binding == event.sidedKey;
}
