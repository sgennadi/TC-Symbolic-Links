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

// Width allocation for the name field and command buttons. At ordinary
// widths show complete labels, including "Link Manager"; shorten only when
// the actual client width cannot hold the full native text and icon padding.
struct NameRow {
    int name=0;
    int preview=0;
    int manager=0;
    int undo=0;
    int compact=0;
    bool shortLabels=false;
};
inline NameRow ResolveNameRow(int availablePx,int dpi,int gapPx,
                               bool russian,bool hebrew) noexcept {
    const int d=dpi>0?dpi:96;
    const auto dp=[d](int n) noexcept {
        return static_cast<int>(
            (static_cast<long long>(n)*d+48)/96);
    };
    NameRow result{};
    result.preview=dp(russian||hebrew?135:124);
    result.manager=dp(russian?208:hebrew?184:178);
    result.undo=dp(russian?154:hebrew?144:145);
    result.compact=dp(russian?135:hebrew?122:114);
    const int gap=(std::max)(0,gapPx);
    const int full=result.preview+result.manager+result.undo+
        result.compact+4*gap+dp(155);
    if(availablePx<full) {
        result.shortLabels=true;
        result.preview=dp(99);
        result.manager=dp(russian?128:116);
        result.undo=dp(russian?116:108);
        result.compact=dp(russian?120:107);
    }
    int buttons=result.preview+result.manager+result.undo+result.compact;
    const int space=(std::max)(0,availablePx-4*gap);
    if(buttons>space-dp(80) && buttons>0) {
        const int budget=(std::max)(0,space-dp(80));
        result.preview=result.preview*budget/buttons;
        result.manager=result.manager*budget/buttons;
        result.undo=result.undo*budget/buttons;
        result.compact=(std::max)(0,budget-result.preview-result.manager-result.undo);
        buttons=result.preview+result.manager+result.undo+result.compact;
    }
    result.name=(std::max)(0,space-buttons);
    return result;
}
} // namespace tcs::maker::density
