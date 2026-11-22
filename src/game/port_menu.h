// Port-added menu content (port baseline, User approval 2026-10-20; docs/decisions/port-baseline.md):
// the Modern control types on CONTROLLER SETUP (slots 0-3), the GRAPHICS page under SCREEN SETTINGS
// (4-19), QUIT GAME in the main menu (20-21, 2026-10-22) and reworded save texts that grew (22-31,
// pc_texts.h). Their values come from common/port_hooks.h, never from settings.
//
// The game draws texts and menu items from guest addresses, so the strings and tables of these
// additions live in a port-owned guest area above the initial stack pointer (0x01FFF800-0x01FFFFFF,
// guest_stack.h): the game never reads or writes it. It is written only while one of these pages
// is open, so RAM elsewhere stays as on the PS2.
#pragma once

#include "common/types.h"

namespace ac04::game::port_menu {

constexpr u32 kAreaBase = 0x01FFF800u;
constexpr u32 kSlotSize = 0x40u;
constexpr u32 kSlots = 0x20u;

// True for a guest address inside the port menu area (a string or table the port added).
constexpr bool Contains(u32 addr) { return addr >= kAreaBase && addr < kAreaBase + kSlots * kSlotSize; }

// Writes `text` (at most 63 characters, NUL-terminated) into slot `slot` and returns its address.
u32 String(u32 slot, const char* text);
// The same at byte `offset` of the slot (at most 63 - offset characters), so short fixed strings
// can share a slot.
u32 StringAt(u32 slot, u32 offset, const char* text);
// Writes `count` words into slot `slot` (at most 16) and returns its address.
u32 Words(u32 slot, const u32* words, u32 count);

}  // namespace ac04::game::port_menu
