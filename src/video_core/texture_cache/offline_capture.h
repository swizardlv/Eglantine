// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// Offline texture capture (Eden offline-pack prototype, jarvis/offline-pack).
//
// Purpose: let a capture run on the PS5 (or any Eden build) record every ASTC
// texture the game uploads, so an offline tool can transcode ASTC -> BC1/BC3
// ahead of time and ship the results in a package the loader can use instead
// of transcoding at runtime.
//
// Design constraints:
//  * Zero new third-party dependencies: the manifest is hand-written JSON
//    (every value is numeric, hex or a fixed ASCII token, so no escaping is
//    needed) and raw bytes go through std::ofstream. This header intentionally
//    depends on NOTHING from Eden (only <std> headers), so it can be compiled
//    and unit-tested standalone.
//  * Default off: without EDEN_OFFLINE_CAPTURE=1 every call returns after a
//    single static bool check. With capture on but EDEN_OFFLINE_CAPTURE_DUMP
//    unset, only manifest metadata is recorded (no raw bytes, no disk writes
//    per texture beyond one append).
//  * Best effort, never fatal: all filesystem work is wrapped in try/catch
//    and failures are swallowed. A capture failure must never break the game.
//  * First upload wins: one manifest entry per guest address per process run.
//    A texture re-uploaded later with different contents is NOT re-recorded
//    (see docs/offline-pack-design.md, "known limits").
//
// Environment variables (read once, on first use):
//  * EDEN_OFFLINE_CAPTURE=1      master switch.
//  * EDEN_OFFLINE_CAPTURE_DUMP=1 also dump the raw (still compressed) bytes
//                                of each captured texture.
//  * EDEN_OFFLINE_CAPTURE_DIR=<path> output directory (default:
//                                "offline-capture", relative to the process
//                                working directory; on the PS5 use an absolute
//                                path, e.g. /mnt/usb1/offline-capture).
//
// Layout recorded per entry: "linear" means the dumped bytes are the
// unswizzled ASTC block stream (mip levels back to back, as produced by
// UnswizzleImage); "swizzled" / "swizzled-tail" means the bytes are still in
// Tegra block-linear (swizzled) guest layout and need an offline unswizzle
// before they can be transcoded (not implemented in the v1 offline tool).

#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <span>
#include <string>
#include <unordered_set>

namespace VideoCommon::OfflineCapture {

inline bool Enabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("EDEN_OFFLINE_CAPTURE");
        return value != nullptr && value[0] == '1';
    }();
    return enabled;
}

inline bool DumpEnabled() {
    static const bool dump = [] {
        const char* value = std::getenv("EDEN_OFFLINE_CAPTURE_DUMP");
        return value != nullptr && value[0] == '1';
    }();
    return dump;
}

inline const std::filesystem::path& OutputDir() {
    static const std::filesystem::path dir = [] {
        const char* value = std::getenv("EDEN_OFFLINE_CAPTURE_DIR");
        if (value != nullptr && value[0] != '\0') {
            return std::filesystem::path{value};
        }
        return std::filesystem::path{"offline-capture"};
    }();
    return dir;
}

/// Record one ASTC upload. `layout` is one of "linear", "swizzled",
/// "swizzled-tail". `raw_bytes` is dumped verbatim iff DumpEnabled().
/// No-op (after one bool check) when capture is disabled, and records each
/// guest address at most once per process run.
inline void RecordAstcUpload(std::uint64_t gpu_addr, std::uint32_t format,
                             std::uint32_t width, std::uint32_t height,
                             std::uint32_t depth, std::uint32_t levels,
                             std::uint32_t layers, std::uint32_t type,
                             std::uint64_t guest_size_bytes,
                             std::uint32_t host_level_offset, bool astc_opaque,
                             const char* layout,
                             std::span<const std::uint8_t> raw_bytes) {
    if (!Enabled()) {
        return;
    }
    try {
        static std::mutex mutex;
        static std::unordered_set<std::uint64_t> seen;
        const std::lock_guard lock{mutex};
        if (!seen.insert(gpu_addr).second) {
            return; // First upload wins (see header comment).
        }

        const std::filesystem::path dir = OutputDir();
        std::error_code ec;
        std::filesystem::create_directories(dir / "textures", ec);
        if (ec) {
            return;
        }

        // Dump file name is derived from the guest address, so the manifest
        // entry and the blob can be joined without any extra bookkeeping.
        char name[32];
        std::snprintf(name, sizeof(name), "%016llx.astc",
                      static_cast<unsigned long long>(gpu_addr));

        bool dumped = false;
        std::uint64_t dump_bytes = 0;
        if (DumpEnabled() && !raw_bytes.empty()) {
            std::ofstream out{dir / "textures" / name, std::ios::binary | std::ios::trunc};
            if (out) {
                out.write(reinterpret_cast<const char*>(raw_bytes.data()),
                          static_cast<std::streamsize>(raw_bytes.size()));
                dumped = static_cast<bool>(out);
                dump_bytes = dumped ? raw_bytes.size() : 0;
            }
        }

        std::ofstream manifest{dir / "manifest.jsonl", std::ios::app};
        if (!manifest) {
            return;
        }
        // Hand-rolled JSON: every field is numeric, hex or a fixed token.
        manifest << "{\"gpu_addr\":\"0x" << std::hex << gpu_addr << std::dec
                 << "\",\"format\":" << format << ",\"width\":" << width
                 << ",\"height\":" << height << ",\"depth\":" << depth
                 << ",\"levels\":" << levels << ",\"layers\":" << layers
                 << ",\"type\":" << type
                 << ",\"guest_size_bytes\":" << guest_size_bytes
                 << ",\"host_level_offset\":" << host_level_offset
                 << ",\"astc_opaque\":" << (astc_opaque ? "true" : "false")
                 << ",\"layout\":\"" << layout << "\",\"dump\":";
        if (dumped) {
            manifest << "\"textures/" << name << "\",\"dump_bytes\":" << dump_bytes;
        } else {
            manifest << "null,\"dump_bytes\":0";
        }
        manifest << "}\n";
    } catch (...) {
        // Best effort only: capture must never throw into the upload path.
    }
}

} // namespace VideoCommon::OfflineCapture
