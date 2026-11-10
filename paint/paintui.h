#pragma once

#include "common.h"

#include <UIAutomation.h>
#include <wrl/client.h>

#include <optional>
#include <utility>

namespace mcx {

struct CanvasGeometry {
    double left;
    double top;
    double scale;
    int width;
    int height;
    int viewLeft;
    int viewTop;
    int viewRight;
    int viewBottom;
    bool whole;
    int shownWidth;
    int shownHeight;

    bool operator==(const CanvasGeometry&) const = default;
};

class PaintUi {
public:
    PaintUi();
    ~PaintUi();

    bool attach(HWND window);
    std::optional<CanvasGeometry> geometry();
    bool usePencil();
    bool pickColour(const wchar_t* name);
    bool dab(POINT point);
    bool setZoom(double percent);

private:
    using Element = Microsoft::WRL::ComPtr<IUIAutomationElement>;
    using Condition = Microsoft::WRL::ComPtr<IUIAutomationCondition>;

    HWND window = nullptr;
    HSYNTHETICPOINTERDEVICE pen = nullptr;
    Microsoft::WRL::ComPtr<IUIAutomation> automation;
    Element image;
    Element sizeText;
    Element scroller;
    Element zoomSlider;
    std::optional<std::pair<int, int>> knownSize;
    bool warnedSize = false;

    Condition textIs(PROPERTYID property, const wchar_t* value);
    Condition both(Condition first, Condition second);
    Element find(Condition condition);
    Element waitFor(Condition condition);
    Element group(const wchar_t* name);
    bool rectOf(Element& element, const wchar_t* id, RECT& out);
    bool inject(POINT point, POINTER_FLAGS flags);
};

}
