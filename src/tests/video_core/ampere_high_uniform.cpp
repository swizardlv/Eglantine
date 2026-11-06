// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>

#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"

TEST_CASE("Ampere LDG.E with a shadowed destination lowers", "[video_core][ampere]") {
    using Shader::Ampere::Instruction;
    using Shader::Ampere::LowerToMaxwell;
    const Instruction exit{{0x0000794d, 0, 0x03800000, 0}};

    // Real 0x981 instances rejected in build53 (game uses R228, which collides with the
    // uniform bank). Each should now lower through a shadow copy.
    for (const Instruction load : {
             Instruction{{0xffe47981, 0x00002004, 0x0c1e8b00, 0x000f6800}},
             Instruction{{0xffe47981, 0x00002004, 0x0c1e8b00, 0x000f6200}},
             Instruction{{0xffec7981, 0x00002004, 0x0c1e8b00, 0x000f2200}},
             Instruction{{0xffe07981, 0x00002004, 0x0c1e8b00, 0x000f6800}},
         }) {
        const auto lowered = LowerToMaxwell(std::array{load, exit});
        REQUIRE(static_cast<bool>(lowered));
    }
}

TEST_CASE("Ampere R2UR to UR28/UR29 lowers through the high-uniform mirror", "[video_core][ampere]") {
    using Shader::Ampere::Instruction;
    using Shader::Ampere::LowerToMaxwell;
    const Instruction exit{{0x0000794d, 0, 0x03800000, 0}};

    // Real 0x3c2 instances rejected in build53 (UR28/UR29, above the UR27 bank end).
    for (const Instruction move : {
             Instruction{{0x071d73c2, 0x00000000, 0x000e0000, 0x020ee200}},
             Instruction{{0x071c73c2, 0x00000000, 0x000e0000, 0x020ee200}},
         }) {
        const auto lowered = LowerToMaxwell(std::array{move, exit});
        REQUIRE(static_cast<bool>(lowered));
    }
}
