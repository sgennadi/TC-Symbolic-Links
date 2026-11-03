#pragma once
// Pure DPI-independent geometry for Normal, Adaptive and explicit Compact.
// On small desktops the automatic Adaptive mode must NEVER make the
// "Compact" checkbox a no-op.
namespace tcs::maker::density {
struct Metrics {
    bool compact=false;
    bool adaptive=false;
    int margin=15;
    int gap=14;
    int bannerTop=14;
    int bannerBottom=104;
    int toolsTop=113;
    int toolsHeight=50;
    int toolsY=122;
    int patternY=159;
    int patternH=29;
    int headingY=199;
    int pathY=228;
    int pathH=40;
    int cardTop=185;
    int cardBottomInset=161;
    int paneBottomInset=174;
    int previewBottomInset=94;
};
inline Metrics Resolve(bool explicitCompact,int widthPx,int heightPx,
                       int dpi) noexcept {
    const int actualDpi=dpi>0?dpi:96;
    const auto dp=[actualDpi](int n) noexcept {
        return static_cast<int>(
            (static_cast<long long>(n)*actualDpi+48)/96);
    };
    const bool small=widthPx<dp(840) || heightPx<dp(555);
    if(explicitCompact) {
        // Single-line header, no secondary hint rows, 89 logical pixels
        // more table height at a fixed 720x480 client size.
        return {true,small,8,6,8,62,66,72,72,105,25,
                151,179,32,141,98,107,91};
    }
    if(small) {
        return {false,true,11,9,14,104,113,50,110,144,
                27,178,207,40,168,161,158,94};
    }
    return {};
}
} // namespace tcs::maker::density
