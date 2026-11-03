// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstddef>
#include <deque>
#include "common/range_sets.inc"
#include "common/common_types.h"

namespace VideoCommon {
// Adapted from zortos293/Eglantine 5bef7354 fence_download_ranges.h.
template<class Copies, class Counts>
void DropPendingFenceCopies(u64 address, u64 size, Copies& fences, Counts& counts) {
    if (size == 0) return;
    bool pending = false;
    counts.ForEachInRange(address, size, [&](auto, auto, auto) { pending = true; });
    if (!pending) return;
    const u64 end = address + size;
    for (auto& copies : fences) {
        for (size_t i = 0; i < copies.size();) {
            const auto copy = copies[i];
            const u64 begin = copy.src_offset, stop = begin + copy.size;
            const u64 first = (std::max)(begin, address), last = (std::min)(stop, end);
            if (first >= last) { ++i; continue; }
            counts.Subtract(first, last - first);
            auto left = copy, right = copy;
            left.size = first - begin;
            right.src_offset = last;
            right.dst_offset += last - begin;
            right.size = stop - last;
            if (left.size && right.size) {
                copies[i] = left;
                copies.insert(copies.begin() + static_cast<std::ptrdiff_t>(i + 1), right);
                i += 2;
            } else if (left.size) { copies[i++] = left;
            } else if (right.size) { copies[i++] = right;
            } else { copies.erase(copies.begin() + static_cast<std::ptrdiff_t>(i)); }
        }
    }
}

// Selection mode may change when a different channel becomes current. Preserve
// older fences' publication, but never use their old rule to retire newer writes.
class DownloadSelectionEpochs {
public:
    void NoteSelection(bool download_all, bool nothing_selected) noexcept {
        if (known && download_all != last) ++current;
        known = true;
        last = download_all;
        if (nothing_selected) oldest = current;
    }
    u64 OldestSelection() const noexcept { return oldest; }
    bool IsCurrent(u64 epoch) const noexcept { return epoch == current; }
private:
    bool known = false, last = false;
    u64 current = 0, oldest = 0;
};

// Publish an earlier fence even if a later GPU binding has been announced.
// Only CPU/inline overwrites clip its copy list. Newer selected GPU writes keep
// their exact bytes dirty; after a selection-policy change retain dirtiness
// conservatively until a fresh synchronous download or current fence.
template<class Counts, class Modified, class Write>
void PublishFenceCopy(DAddr address, u64 size, const u8* bytes, Counts& counts,
                      const Common::RangeSet<DAddr>& uncommitted,
                      const std::deque<Common::RangeSet<DAddr>>& committed,
                      Modified& modified, bool selection_current, Write&& write) {
    counts.ForEachInRange(address, size, [&](DAddr begin, DAddr end, s32) {
        write(begin, bytes + (begin - address), end - begin);
    });
    counts.Subtract(address, size, [&](DAddr begin, DAddr end) {
        if (!selection_current) return;
        Common::RangeSet<DAddr> completed;
        completed.Add(begin, end - begin);
        const auto exclude = [&](const auto& ranges) {
            ranges.ForEachInRange(begin, end - begin, [&](DAddr a, DAddr b) {
                completed.Subtract(a, b - a);
            });
        };
        exclude(uncommitted);
        for (const auto& ranges : committed) exclude(ranges);
        completed.ForEach([&](DAddr a, DAddr b) { modified.Subtract(a, b - a); });
    });
}
}
