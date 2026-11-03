// Cross-platform geometry: explicit Compact differs from automatic
// narrow-screen adaptation, even at 200% DPI.
#include "TcMakerDensity.h"
#include <iostream>
#include <stdexcept>
#include <initializer_list>
using tcs::maker::density::Resolve;
void require(bool value,const char* message) {
    if(!value)throw std::runtime_error(message);
}
int main() {
    try {
        for(int dpi:{96,120,144,192}) {
            const auto wide=Resolve(false,dpi*1440/96,dpi*900/96,dpi);
            const auto narrow=Resolve(false,dpi*720/96,dpi*480/96,dpi);
            const auto compact=Resolve(true,dpi*720/96,dpi*480/96,dpi);
            require(!wide.compact&&!wide.adaptive,"Wide must stay normal");
            require(!narrow.compact&&narrow.adaptive,
                    "Small window must use adaptive geometry");
            require(compact.compact,"Explicit Compact not selected");
            require(compact.bannerBottom<wide.bannerBottom,
                    "Compact must shrink the banner");
            require(compact.pathY<narrow.pathY-20,
                    "Compact must reposition fields on narrow screens");
            require(compact.paneBottomInset<narrow.paneBottomInset,
                    "Compact must allow taller list view");
            const int regularListTop=narrow.pathY+narrow.pathH+9;
            const int denseListTop=compact.pathY+compact.pathH+7;
            const int gained=(regularListTop-denseListTop)+
                (narrow.paneBottomInset-compact.paneBottomInset);
            require(gained>=80,"Compact must gain >= 80 logical pixels");
        }
        std::cout<<"PASS: Normal, Adaptive, Compact distinct at 100-200% DPI\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"FAIL: "<<e.what()<<"\n";
        return 1;
    }
}
