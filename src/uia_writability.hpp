#pragma once
#include <windows.h>
#include <UIAutomation.h>
#include <oleauto.h>

namespace heben {
enum class WritableState { Writable, ReadOnly, Unknown };
inline WritableState WritableFromReadOnlyAttribute(const VARIANT &value) {
    if(value.vt!=VT_BOOL)return WritableState::Unknown;
    if(value.boolVal==VARIANT_FALSE)return WritableState::Writable;
    if(value.boolVal==VARIANT_TRUE)return WritableState::ReadOnly;
    return WritableState::Unknown;
}

// Metadata only: does not read document text or change the selection.
// This is a writability signal, not proof that terminal input is non-sensitive.
inline WritableState TextPatternWritability(IUIAutomationElement *element,HRESULT &detail) {
    if(!element){detail=E_POINTER;return WritableState::Unknown;}
    IUnknown *unknown=nullptr;
    detail=element->GetCurrentPattern(UIA_TextPatternId,&unknown);
    if(FAILED(detail)||!unknown)return WritableState::Unknown;
    IUIAutomationTextPattern *pattern=nullptr;
    detail=unknown->QueryInterface(IID_PPV_ARGS(&pattern));unknown->Release();
    if(FAILED(detail)||!pattern)return WritableState::Unknown;
    IUIAutomationTextRange *range=nullptr;
    detail=pattern->get_DocumentRange(&range);pattern->Release();
    if(FAILED(detail)||!range)return WritableState::Unknown;
    VARIANT value{};VariantInit(&value);
    detail=range->GetAttributeValue(UIA_IsReadOnlyAttributeId,&value);range->Release();
    WritableState state=SUCCEEDED(detail)?WritableFromReadOnlyAttribute(value):WritableState::Unknown;
    VariantClear(&value);return state;
}
}
