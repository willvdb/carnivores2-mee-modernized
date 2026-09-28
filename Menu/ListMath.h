#pragma once

#include <algorithm>
#include <cstddef>

inline constexpr std::size_t HUNT_LIST_VISIBLE_ROWS = 10;

inline std::size_t HuntListVisibleEnd(std::size_t offset,
                                      std::size_t itemCount)
{
    return std::min(itemCount, offset + HUNT_LIST_VISIBLE_ROWS);
}

inline std::size_t HuntListDataIndex(std::size_t visibleRow,
                                     std::size_t offset)
{
    return visibleRow + offset;
}

// Menu presentation assets (pics/dinoN.tga, txt/dinoN.txm) are numbered by
// the huntable's 1-based position in the menu list, not by its AI slot. The
// stock roster gives Iguanodon and Carnotaurus the same AI (17), so an
// AI-derived number would show each of the last species its predecessor's
// asset (T-Rex displaying the Carnotaurus picture).
inline std::size_t HuntableMenuSlot(std::size_t huntableIndex)
{
    return huntableIndex + 1;
}

inline std::size_t ScrolledHuntListOffset(std::size_t currentOffset,
                                          std::size_t itemCount,
                                          int wheelSteps)
{
    const std::size_t maxOffset = itemCount > HUNT_LIST_VISIBLE_ROWS
        ? itemCount - HUNT_LIST_VISIBLE_ROWS
        : 0;
    const long long nextOffset = static_cast<long long>(currentOffset)
        - static_cast<long long>(wheelSteps);
    return static_cast<std::size_t>(std::clamp(
        nextOffset, 0LL, static_cast<long long>(maxOffset)));
}
