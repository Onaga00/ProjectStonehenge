// Port menu area (src/game/port_menu.*) and the GRAPHICS page's view of the ultrawide settings.
#include <doctest/doctest.h>

#include <string>

#include "common/guest_memory.h"
#include "common/guest_ref.h"
#include "common/port_hooks.h"
#include "game/port_menu.h"

using ac04::guest::GRef;
using ac04::guest::Mem;
namespace port_menu = ac04::game::port_menu;

namespace {

std::string Text(u32 addr) {
    std::string out;
    while (GRef<u8>(addr) != 0) out += static_cast<char>(GRef<u8>(addr++));
    return out;
}

}  // namespace

TEST_CASE("port menu: strings at an offset share a slot without overwriting each other") {
    Mem().Reset();
    const u32 a = port_menu::StringAt(5, 0, "ANTI-ALIASING");
    const u32 b = port_menu::StringAt(5, 0x20, "ULTRAWIDE");
    CHECK(a == port_menu::kAreaBase + 5 * port_menu::kSlotSize);
    CHECK(b == a + 0x20);
    CHECK(Text(a) == "ANTI-ALIASING");
    CHECK(Text(b) == "ULTRAWIDE");
    CHECK(port_menu::String(5, "FULLSCREEN") == a);
    CHECK(Text(a) == "FULLSCREEN");
    CHECK(Text(b) == "ULTRAWIDE");
}

TEST_CASE("port menu: a string at an offset stays inside its slot") {
    Mem().Reset();
    GRef<u8>(port_menu::kAreaBase + 6 * port_menu::kSlotSize) = 0x5A;  // the next slot's first byte
    const std::string long_text(80, 'X');
    const u32 p = port_menu::StringAt(5, 0x30, long_text.c_str());
    CHECK(Text(p).size() == port_menu::kSlotSize - 1 - 0x30);
    CHECK(GRef<u8>(port_menu::kAreaBase + 6 * port_menu::kSlotSize) == 0x5A);
}

TEST_CASE("GRAPHICS options: the ultrawide values take part in the comparison") {
    ac04::port::GraphicsOptions a, b;
    CHECK(a == b);
    CHECK(a.ultrawide);
    CHECK(a.ultrawide_4x3_fov);
    b.ultrawide = false;
    CHECK(a != b);
    b = a;
    b.ultrawide_4x3_fov = false;
    CHECK(a != b);
}
