// SPDX-FileCopyrightText: Copyright 2026 Lichii contributors (zortos293/Lichii #215), EdGlantine-PS5
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>

#include "common/common_types.h"
#include "video_core/memory_manager.h"
#include "video_core/rasterizer_interface.h"

namespace VideoCommon::QueryDma {

/// Writes the reports in [source, source + size) to guest memory, waiting for pending host
/// results. Called where the rasterizer's caches are owned (the split's back is drained).
inline void FlushPending(const Tegra::MemoryManager& memory,
                         VideoCore::RasterizerInterface& rasterizer, GPUVAddr source, size_t size) {
    constexpr GPUVAddr page_size = 0x1000;
    const GPUVAddr end = source + size;
    for (GPUVAddr begin = source; begin < end;) {
        const GPUVAddr page_end = (std::min)((begin & ~(page_size - 1)) + page_size, end);
        if (const auto device = memory.GpuToCpuAddress(begin)) {
            rasterizer.FlushQueryResults(*device, page_end - begin);
        }
        begin = page_end;
    }
}

} // namespace VideoCommon::QueryDma
