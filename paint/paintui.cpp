#include "paintui.h"

#include <cmath>
#include <cwctype>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace mcx {

namespace {

constexpr int floatingPanelWidth = 100;
constexpr double sliderTolerance = 0.05;
constexpr auto dabPause = 12;

std::wstring take(BSTR text) {
    std::wstring out = text ? text : L"";
    SysFreeString(text);
    return out;
}

std::vector<int> numbersIn(const std::wstring& text) {
    std::vector<int> found;
    int value = -1;
    for (wchar_t c : text) {
        if (std::iswdigit(c)) {
            value = (value < 0 ? 0 : value * 10) + (c - L'0');
        } else if (value >= 0 && c != L',' && c != L'.') {
            found.push_back(value);
            value = -1;
        }
    }
    if (value >= 0) found.push_back(value);
    return found;
}

std::vector<HWND> ownWindows() {
    std::vector<HWND> found;
    EnumWindows([](HWND hwnd, LPARAM list) -> BOOL {
        DWORD pid;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid == GetCurrentProcessId() && IsWindowVisible(hwnd)) reinterpret_cast<std::vector<HWND>*>(list)->push_back(hwnd);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&found));
    return found;
}

}

PaintUi::PaintUi() {
    HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(result)) throw std::runtime_error(std::format("CoInitializeEx failed {:x}", std::uint32_t(result)));
    result = CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation));
    if (FAILED(result)) throw std::runtime_error(std::format("no UI Automation {:x}", std::uint32_t(result)));
    pen = CreateSyntheticPointerDevice(PT_PEN, 1, POINTER_FEEDBACK_NONE);
    if (!pen) throw std::runtime_error(std::format("no synthetic pen {}", GetLastError()));
}

PaintUi::~PaintUi() {
    DestroySyntheticPointerDevice(pen);
    image.Reset();
    sizeText.Reset();
    scroller.Reset();
    zoomSlider.Reset();
    automation.Reset();
    CoUninitialize();
}

bool PaintUi::attach(HWND paint) {
    window = paint;
    return true;
}

PaintUi::Condition PaintUi::textIs(PROPERTYID property, const wchar_t* value) {
    VARIANT text{};
    text.vt = VT_BSTR;
    text.bstrVal = SysAllocString(value);
    Condition condition;
    automation->CreatePropertyCondition(property, text, &condition);
    VariantClear(&text);
    return condition;
}

PaintUi::Condition PaintUi::both(Condition first, Condition second) {
    Condition condition;
    automation->CreateAndCondition(first.Get(), second.Get(), &condition);
    return condition;
}

PaintUi::Element PaintUi::find(Condition condition) {
    Element root, found;
    if (SUCCEEDED(automation->ElementFromHandle(window, &root)) && root) root->FindFirst(TreeScope_Descendants, condition.Get(), &found);
    if (found) return found;
    for (HWND other : ownWindows()) {
        Element popup;
        if (other == window || FAILED(automation->ElementFromHandle(other, &popup)) || !popup) continue;
        popup->FindFirst(TreeScope_Subtree, condition.Get(), &found);
        if (found) return found;
    }
    return nullptr;
}

PaintUi::Element PaintUi::waitFor(Condition condition) {
    for (int attempt = 0; attempt < 25; attempt++) {
        if (auto found = find(condition)) return found;
        Sleep(40);
    }
    return nullptr;
}

PaintUi::Element PaintUi::group(const wchar_t* name) {
    return find(both(textIs(UIA_NamePropertyId, name), textIs(UIA_AutomationIdPropertyId, L"CollapsedButton")));
}

bool PaintUi::rectOf(Element& element, const wchar_t* id, RECT& out) {
    if (!element) element = find(textIs(UIA_AutomationIdPropertyId, id));
    if (!element) return false;
    if (FAILED(element->get_CurrentBoundingRectangle(&out))) {
        element.Reset();
        return false;
    }
    return true;
}

std::optional<CanvasGeometry> PaintUi::geometry() {
    RECT canvas{}, view{};
    if (!rectOf(image, L"image", canvas) || !rectOf(scroller, L"scrollViewer", view)) return std::nullopt;
    if (!sizeText) sizeText = find(textIs(UIA_AutomationIdPropertyId, L"CanvasSizeTextBlock"));
    BSTR raw = nullptr;
    if (sizeText && SUCCEEDED(sizeText->get_CurrentName(&raw))) {
        auto read = numbersIn(take(raw));
        if (read.size() == 2 && read[0] > 0 && read[1] > 0) knownSize = {read[0], read[1]};
    } else {
        sizeText.Reset();
    }
    if (!knownSize) {
        if (!warnedSize) log("can't see Paint's canvas size, make the window a little wider");
        warnedSize = true;
        return std::nullopt;
    }
    if (canvas.right <= canvas.left || canvas.bottom <= canvas.top) return std::nullopt;
    int size[] = {knownSize->first, knownSize->second};
    int shownWidth = canvas.right - canvas.left;
    int shownHeight = canvas.bottom - canvas.top;
    bool whole = canvas.left > view.left && canvas.top > view.top && canvas.right < view.right && canvas.bottom < view.bottom;
    double scale = whole ? double(shownWidth) / size[0] : 0;
    if (!zoomSlider) zoomSlider = find(textIs(UIA_AutomationIdPropertyId, L"ZoomSliderControl"));
    ComPtr<IUIAutomationRangeValuePattern> range;
    double percent = 0;
    if (zoomSlider && SUCCEEDED(zoomSlider->GetCurrentPatternAs(UIA_RangeValuePatternId, IID_PPV_ARGS(&range))) && range && SUCCEEDED(range->get_CurrentValue(&percent))) {
        if (whole && std::abs(percent / 100 - scale) < sliderTolerance) scale = percent / 100;
    } else {
        zoomSlider.Reset();
    }
    return CanvasGeometry{
        double(canvas.left), double(canvas.top), scale,
        size[0], size[1],
        int(view.left) + floatingPanelWidth, int(view.top), int(view.right), int(view.bottom), whole,
        shownWidth, shownHeight
    };
}

bool PaintUi::usePencil() {
    if (!image) return false;
    BSTR raw = nullptr;
    if (FAILED(image->get_CurrentName(&raw))) return false;
    if (take(raw).find(L"Pencil") != std::wstring::npos) return true;

    auto pencilId = textIs(UIA_AutomationIdPropertyId, L"PencilTool");
    auto pencil = find(pencilId);
    Element tools;
    if (!pencil) {
        tools = group(L"Tools");
        ComPtr<IUIAutomationExpandCollapsePattern> expand;
        if (!tools || FAILED(tools->GetCurrentPatternAs(UIA_ExpandCollapsePatternId, IID_PPV_ARGS(&expand))) || !expand) return false;
        expand->Expand();
        pencil = waitFor(pencilId);
    }
    if (!pencil) return false;
    ComPtr<IUIAutomationTogglePattern> toggle;
    if (FAILED(pencil->GetCurrentPatternAs(UIA_TogglePatternId, IID_PPV_ARGS(&toggle))) || !toggle) return false;
    toggle->Toggle();
    if (tools) {
        ComPtr<IUIAutomationExpandCollapsePattern> collapse;
        if (SUCCEEDED(tools->GetCurrentPatternAs(UIA_ExpandCollapsePatternId, IID_PPV_ARGS(&collapse))) && collapse) collapse->Collapse();
    }
    Sleep(150);

    VARIANT sliderType{};
    sliderType.vt = VT_I4;
    sliderType.lVal = UIA_SliderControlTypeId;
    Condition isSlider;
    automation->CreatePropertyCondition(UIA_ControlTypePropertyId, sliderType, &isSlider);
    auto slider = find(both(textIs(UIA_NamePropertyId, L"Size"), isSlider));
    ComPtr<IUIAutomationRangeValuePattern> range;
    double minimum = 0;
    if (slider && SUCCEEDED(slider->GetCurrentPatternAs(UIA_RangeValuePatternId, IID_PPV_ARGS(&range))) && range && SUCCEEDED(range->get_CurrentMinimum(&minimum))) range->SetValue(minimum);
    return true;
}

bool PaintUi::pickColour(const wchar_t* name) {
    auto swatchName = textIs(UIA_NamePropertyId, name);
    VARIANT itemType{};
    itemType.vt = VT_I4;
    itemType.lVal = UIA_ListItemControlTypeId;
    Condition isItem;
    automation->CreatePropertyCondition(UIA_ControlTypePropertyId, itemType, &isItem);
    auto condition = both(swatchName, isItem);

    auto swatch = find(condition);
    Element colours;
    if (!swatch) {
        colours = group(L"Colours");
        ComPtr<IUIAutomationExpandCollapsePattern> expand;
        if (!colours || FAILED(colours->GetCurrentPatternAs(UIA_ExpandCollapsePatternId, IID_PPV_ARGS(&expand))) || !expand) return false;
        expand->Expand();
        swatch = waitFor(condition);
    }
    if (!swatch) return false;
    ComPtr<IUIAutomationInvokePattern> invoke;
    if (FAILED(swatch->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&invoke))) || !invoke) return false;
    invoke->Invoke();
    if (colours) {
        ComPtr<IUIAutomationExpandCollapsePattern> collapse;
        if (SUCCEEDED(colours->GetCurrentPatternAs(UIA_ExpandCollapsePatternId, IID_PPV_ARGS(&collapse))) && collapse) collapse->Collapse();
    }
    Sleep(100);
    return true;
}

bool PaintUi::inject(POINT point, POINTER_FLAGS flags) {
    POINTER_TYPE_INFO info{};
    info.type = PT_PEN;
    info.penInfo.pointerInfo.pointerType = PT_PEN;
    info.penInfo.pointerInfo.pointerId = 1;
    info.penInfo.pointerInfo.ptPixelLocation = point;
    info.penInfo.pointerInfo.pointerFlags = flags;
    info.penInfo.penMask = PEN_MASK_PRESSURE;
    info.penInfo.pressure = 512;
    return InjectSyntheticPointerInput(pen, &info, 1);
}

bool PaintUi::setZoom(double percent) {
    auto slider = find(textIs(UIA_AutomationIdPropertyId, L"ZoomSliderControl"));
    ComPtr<IUIAutomationRangeValuePattern> range;
    return slider && SUCCEEDED(slider->GetCurrentPatternAs(UIA_RangeValuePatternId, IID_PPV_ARGS(&range))) && range && SUCCEEDED(range->SetValue(percent));
}

bool PaintUi::dab(POINT point) {
    if (!inject(point, POINTER_FLAG_INRANGE | POINTER_FLAG_UPDATE)) return false;
    Sleep(dabPause);
    if (!inject(point, POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT | POINTER_FLAG_DOWN)) return false;
    Sleep(dabPause);
    if (!inject(point, POINTER_FLAG_INRANGE | POINTER_FLAG_UP)) return false;
    Sleep(dabPause);
    return inject(point, POINTER_FLAG_UPDATE);
}

}
