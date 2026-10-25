#pragma once

namespace tcs {
// Total Commander SpecialView 0 = standard view, 1 = comments, 2 = thumbnails,
// 3 = first custom column set. [CustomFields] starts at preset index 1.
constexpr int SpecialViewForCustomPreset(int index) noexcept {
    return index >= 1 && index <= 90 ? index + 2 : 0;
}
}
