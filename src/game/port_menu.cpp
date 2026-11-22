#include "game/port_menu.h"

#include <algorithm>
#include <cstring>

#include "common/guest_ref.h"
#include "common/guest_stack.h"

namespace ac04::game::port_menu {

static_assert(kAreaBase >= guest::kInitialStackPointer && kAreaBase + kSlots * kSlotSize <= 0x02000000u,
              "the port menu area must stay above the guest stack and inside RAM");

u32 String(u32 slot, const char* text) { return StringAt(slot, 0, text); }

u32 StringAt(u32 slot, u32 offset, const char* text) {
    offset = std::min(offset, kSlotSize - 1);
    const u32 addr = kAreaBase + (slot % kSlots) * kSlotSize + offset;
    const size_t n = std::min<size_t>(std::strlen(text), kSlotSize - 1 - offset);
    for (size_t i = 0; i < n; ++i) guest::GRef<u8>(addr + static_cast<u32>(i)) = static_cast<u8>(text[i]);
    guest::GRef<u8>(addr + static_cast<u32>(n)) = 0;
    return addr;
}

u32 Words(u32 slot, const u32* words, u32 count) {
    const u32 addr = kAreaBase + (slot % kSlots) * kSlotSize;
    for (u32 i = 0; i < count && i < kSlotSize / 4; ++i) guest::GRef<u32>(addr + i * 4) = words[i];
    return addr;
}

}  // namespace ac04::game::port_menu
