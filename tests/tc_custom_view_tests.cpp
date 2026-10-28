#include "TcCustomView.h"
#include <iostream>

static_assert(tcs::SpecialViewForCustomPreset(1) == 3);
static_assert(tcs::SpecialViewForCustomPreset(2) == 4);
static_assert(tcs::SpecialViewForCustomPreset(3) == 5);
static_assert(tcs::SpecialViewForCustomPreset(90) == 92);
static_assert(tcs::SpecialViewForCustomPreset(0) == 0);
static_assert(tcs::SpecialViewForCustomPreset(-1) == 0);
static_assert(tcs::SpecialViewForCustomPreset(91) == 0);

int main() {
    for (int index = 1; index <= 90; ++index) {
        if (tcs::SpecialViewForCustomPreset(index) != index + 2) return 1;
    }
    std::cout << "PASS: all 90 Total Commander custom-column SpecialView indices\n";
}
