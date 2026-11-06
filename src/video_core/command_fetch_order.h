// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace VideoCommon {

// Whether a command list entry may be executed from its submit-time snapshot:
// only fresh, non-resumed entries that have a snapshot, lie on no GPU-written
// pages, and hold no newer words a safe read would flush. (zortos fae3bc6)
// NOTE: this baseline has no GPU-written page tracking yet (no
// FlushGpuWrittenCommands / Nx2MarkGpuWritten), so callers pass gpu_written
// as false; the safe-read dirty check still covers GPU-newer words.
constexpr bool CanUseSubmittedCommands(bool resumed, bool has_snapshot, bool gpu_written,
                                       bool safe_read_dirty) {
    return !resumed && has_snapshot && !gpu_written && !safe_read_dirty;
}

} // namespace VideoCommon
