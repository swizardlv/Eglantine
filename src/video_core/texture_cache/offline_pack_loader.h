// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Offline pack loader (file-based pack v1, produced by
// tools/offline-pack/eden_offline_pack.py from a capture directory).
//
// Pack layout:
//   <pack>/manifest.json            {"version":1, "entries":[...], "stats":{...}}
//   <pack>/textures/<addr>_L<lv>.bc1|.bc3   one file per guest mip level
//
// A manifest entry is the capture record (gpu_addr, format, width, height,
// depth, levels, layers, type, guest_size_bytes, host_level_offset,
// astc_opaque, layout, dump) plus format_name, levels_out and status. Only
// entries with status == "ok" are usable: the builder marks an entry "ok"
// only when it transcoded every level from a "linear" dump (single layer,
// depth 1). The loader keys entries by guest address (gpu_addr) -- the same
// address the texture was captured at. Guest addresses are stable across
// runs of the same game on the same build in practice; if they ever differ,
// the lookup simply misses and the runtime falls back to transcoding (a
// wasted pack, never a wrong texture -- format/dims/levels/codec are all
// re-validated against the live image before any upload).
//
// The JSON parser here is hand-rolled for this one schema (flat objects,
// known field names, no nested objects except levels_out, no escapes in the
// fields we read). A malformed manifest is a failed load; a malformed entry
// is skipped. Loading never throws.

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "common/common_types.h"

namespace VideoCommon {

// Bytes of one BC level with the given (already mip-adjusted) level dims.
// bc1: 0.5 bytes/texel; bc3: 1 byte/texel. Rows are 4-texel aligned.
[[nodiscard]] inline u64 OfflinePackBcLevelSize(u32 width, u32 height, u32 depth, u32 layers,
                                                bool bc1) {
    const u64 aligned = static_cast<u64>(((width + 3) / 4 * 4)) * ((height + 3) / 4 * 4);
    return aligned * depth * layers / (bc1 ? 2 : 1);
}

struct OfflinePackLevel {
    u32 level = 0;
    u32 width = 0;
    u32 height = 0;
    std::string file; // relative to the pack dir, e.g. "textures/0000..._L2.bc3"
    u64 bytes = 0;
    std::string codec; // "bc1" or "bc3"
};

struct OfflinePackEntry {
    u64 gpu_addr = 0;
    u32 format = 0;
    u32 width = 0;
    u32 height = 0;
    u32 depth = 0;
    u32 levels = 0;
    u32 layers = 0;
    u32 type = 0; // VideoCore::Surface::ImageType as u32 (e2D == 1)
    u32 host_level_offset = 0;
    bool astc_opaque = false;
    std::vector<OfflinePackLevel> levels_out;
};

namespace OfflinePackJson {

// Minimal JSON scanning for the pack manifest schema. All helpers take the
// whole document and an index; they never throw.

inline void SkipWs(std::string_view s, size_t& i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) {
        ++i;
    }
}

// Index just past the matching close for the '{' or '[' at `open`.
// Returns npos on unbalanced input. String literals are respected.
inline size_t MatchClose(std::string_view s, size_t open, char close) {
    const char open_ch = s[open];
    size_t depth = 0;
    bool in_string = false;
    for (size_t i = open; i < s.size(); ++i) {
        const char c = s[i];
        if (in_string) {
            if (c == '\\') {
                ++i; // skip the escaped char
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }
        if (c == '"') {
            in_string = true;
        } else if (c == open_ch) {
            ++depth;
        } else if (c == close) {
            if (--depth == 0) {
                return i + 1;
            }
        }
    }
    return std::string_view::npos;
}

// Finds `"key"` (exact, with quotes) inside [begin, end) and returns the
// index just past the ':' that follows it, or npos.
inline size_t FindKey(std::string_view s, size_t begin, size_t end, std::string_view key) {
    std::string quoted;
    quoted.reserve(key.size() + 2);
    quoted.push_back('"');
    quoted += key;
    quoted.push_back('"');
    size_t pos = s.find(quoted, begin);
    while (pos != std::string_view::npos && pos < end) {
        size_t i = pos + quoted.size();
        SkipWs(s, i);
        if (i < end && s[i] == ':') {
            return i + 1;
        }
        pos = s.find(quoted, pos + 1);
    }
    return std::string_view::npos;
}

inline bool ParseString(std::string_view s, size_t& i, std::string& out) {
    SkipWs(s, i);
    if (i >= s.size() || s[i] != '"') {
        return false;
    }
    ++i;
    out.clear();
    while (i < s.size() && s[i] != '"') {
        if (s[i] == '\\' && i + 1 < s.size()) {
            ++i;
            out.push_back(s[i]);
        } else {
            out.push_back(s[i]);
        }
        ++i;
    }
    if (i >= s.size()) {
        return false;
    }
    ++i; // closing quote
    return true;
}

inline bool ParseUnsigned(std::string_view s, size_t& i, u64& out) {
    SkipWs(s, i);
    if (i >= s.size() || s[i] < '0' || s[i] > '9') {
        return false;
    }
    u64 v = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
        v = v * 10 + static_cast<u64>(s[i] - '0');
        ++i;
    }
    out = v;
    return true;
}

inline bool ParseBool(std::string_view s, size_t& i, bool& out) {
    SkipWs(s, i);
    if (s.substr(i, 4) == "true") {
        out = true;
        i += 4;
        return true;
    }
    if (s.substr(i, 5) == "false") {
        out = false;
        i += 5;
        return true;
    }
    return false;
}

// Reads one field of an object span. `after_colon` is the index just past
// the field's ':' (from FindKey). Returns false when the value is absent or
// the wrong JSON type.
inline bool FieldString(std::string_view s, size_t after_colon, std::string& out) {
    size_t i = after_colon;
    return ParseString(s, i, out);
}

inline bool FieldUnsigned(std::string_view s, size_t after_colon, u64& out) {
    size_t i = after_colon;
    return ParseUnsigned(s, i, out);
}

inline bool FieldBool(std::string_view s, size_t after_colon, bool& out) {
    size_t i = after_colon;
    return ParseBool(s, i, out);
}

} // namespace OfflinePackJson

class OfflinePackLoader {
public:
    // Loads <pack_dir>/manifest.json. Returns false (and leaves the loader
    // empty) when the file is missing, the version is not 1, or the top-level
    // structure is broken. Individual bad entries are skipped, not fatal.
    bool Load(const std::filesystem::path& pack_dir) {
        entries_.clear();
        index_.clear();
        dir_.clear();

        std::ifstream in(pack_dir / "manifest.json", std::ios::binary);
        if (!in) {
            return false;
        }
        const std::string doc{std::istreambuf_iterator<char>{in},
                              std::istreambuf_iterator<char>{}};
        if (doc.empty()) {
            return false;
        }
        using namespace OfflinePackJson;

        size_t vpos = FindKey(doc, 0, doc.size(), "version");
        u64 version = 0;
        if (vpos == std::string_view::npos || !FieldUnsigned(doc, vpos, version) || version != 1) {
            return false;
        }

        size_t epos = FindKey(doc, 0, doc.size(), "entries");
        if (epos == std::string_view::npos) {
            return false;
        }
        size_t i = epos;
        SkipWs(doc, i);
        if (i >= doc.size() || doc[i] != '[') {
            return false;
        }
        const size_t arr_end = MatchClose(doc, i, ']');
        if (arr_end == std::string_view::npos) {
            return false;
        }
        ++i; // past '['
        while (i < arr_end) {
            SkipWs(doc, i);
            if (i >= arr_end || doc[i] == ']') {
                break;
            }
            if (doc[i] != '{') {
                return false;
            }
            const size_t obj_end = MatchClose(doc, i, '}');
            if (obj_end == std::string_view::npos || obj_end > arr_end) {
                return false;
            }
            ParseEntry(doc.substr(i, obj_end - i));
            i = obj_end;
            SkipWs(doc, i);
            if (i < arr_end && doc[i] == ',') {
                ++i;
            }
        }
        if (entries_.empty()) {
            return false;
        }
        dir_ = pack_dir;
        return true;
    }

    bool Empty() const noexcept {
        return entries_.empty();
    }

    size_t EntryCount() const noexcept {
        return entries_.size();
    }

    const OfflinePackEntry* Lookup(u64 gpu_addr) const {
        const auto it = index_.find(gpu_addr);
        return it == index_.end() ? nullptr : &entries_[it->second];
    }

    // Reads one level's BC file (path relative to the pack dir). Validates
    // the file size against the manifest's `bytes` field.
    bool ReadLevelFile(const OfflinePackLevel& level, std::vector<u8>& out) const {
        if (dir_.empty() || level.file.empty() || level.bytes == 0 ||
            level.bytes > (u64{1} << 31)) {
            return false;
        }
        std::ifstream in(dir_ / level.file, std::ios::binary);
        if (!in) {
            return false;
        }
        in.seekg(0, std::ios::end);
        const std::streamoff size = in.tellg();
        if (size < 0 || static_cast<u64>(size) != level.bytes) {
            return false;
        }
        out.resize(static_cast<size_t>(size));
        in.seekg(0, std::ios::beg);
        in.read(reinterpret_cast<char*>(out.data()), size);
        return static_cast<bool>(in);
    }

private:
    void ParseEntry(std::string_view obj) {
        using namespace OfflinePackJson;
        OfflinePackEntry e{};
        std::string str;
        u64 num = 0;
        bool flag = false;

        size_t p = FindKey(obj, 0, obj.size(), "status");
        if (p == std::string_view::npos || !FieldString(obj, p, str) || str != "ok") {
            return; // only usable entries
        }
        p = FindKey(obj, 0, obj.size(), "gpu_addr");
        if (p == std::string_view::npos || !FieldString(obj, p, str) || str.empty()) {
            return;
        }
        // "0x..." hex.
        const char* hex = str.c_str();
        if (str.size() > 2 && str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
            hex += 2;
        }
        char* end = nullptr;
        e.gpu_addr = std::strtoull(hex, &end, 16);
        if (end == hex || e.gpu_addr == 0) {
            return;
        }
        const auto num_field = [&](std::string_view span, std::string_view key, u32& dst) {
            size_t q = FindKey(span, 0, span.size(), key);
            u64 v = 0;
            if (q == std::string_view::npos || !FieldUnsigned(span, q, v)) {
                return false;
            }
            dst = static_cast<u32>(v);
            return true;
        };
        if (!num_field(obj, "format", e.format) || !num_field(obj, "width", e.width) ||
            !num_field(obj, "height", e.height) || !num_field(obj, "depth", e.depth) ||
            !num_field(obj, "levels", e.levels) || !num_field(obj, "layers", e.layers) ||
            !num_field(obj, "type", e.type) ||
            !num_field(obj, "host_level_offset", e.host_level_offset)) {
            return;
        }
        p = FindKey(obj, 0, obj.size(), "astc_opaque");
        if (p == std::string_view::npos || !FieldBool(obj, p, flag)) {
            return;
        }
        e.astc_opaque = flag;

        // levels_out: array of {level,width,height,file,bytes,codec}.
        p = FindKey(obj, 0, obj.size(), "levels_out");
        if (p == std::string_view::npos) {
            return;
        }
        size_t j = p;
        SkipWs(obj, j);
        if (j >= obj.size() || obj[j] != '[') {
            return;
        }
        const size_t arr_end = MatchClose(obj, j, ']');
        if (arr_end == std::string_view::npos) {
            return;
        }
        ++j;
        while (j < arr_end) {
            SkipWs(obj, j);
            if (j >= arr_end || obj[j] == ']') {
                break;
            }
            if (obj[j] != '{') {
                return;
            }
            const size_t oend = MatchClose(obj, j, '}');
            if (oend == std::string_view::npos || oend > arr_end) {
                return;
            }
            const std::string_view lv = obj.substr(j, oend - j);
            OfflinePackLevel level{};
            if (!num_field(lv, "level", level.level) || !num_field(lv, "width", level.width) ||
                !num_field(lv, "height", level.height)) {
                return;
            }
            size_t q = FindKey(lv, 0, lv.size(), "file");
            if (q == std::string_view::npos || !FieldString(lv, q, level.file) ||
                level.file.empty()) {
                return;
            }
            q = FindKey(lv, 0, lv.size(), "bytes");
            if (q == std::string_view::npos || !FieldUnsigned(lv, q, num) || num == 0) {
                return;
            }
            level.bytes = num;
            q = FindKey(lv, 0, lv.size(), "codec");
            if (q == std::string_view::npos || !FieldString(lv, q, level.codec) ||
                (level.codec != "bc1" && level.codec != "bc3")) {
                return;
            }
            e.levels_out.push_back(std::move(level));
            j = oend;
            SkipWs(obj, j);
            if (j < arr_end && obj[j] == ',') {
                ++j;
            }
        }
        if (e.levels_out.empty()) {
            return;
        }
        // First entry wins on a duplicate address.
        if (index_.emplace(e.gpu_addr, entries_.size()).second) {
            entries_.push_back(std::move(e));
        }
    }

    std::filesystem::path dir_;
    std::vector<OfflinePackEntry> entries_;
    std::unordered_map<u64, size_t> index_;
};

} // namespace VideoCommon
