#pragma once
#if defined(XENON_TEST_FIXTURE_CERT_SHA256)
#include <windows.h>
#include <objbase.h>
#include <oleauto.h>
#include <UIAutomation.h>
#include <wrl/client.h>
#include <string>
#include <vector>

namespace xenon::extension_install_fixture {
// Fixed native UI driver for the opt-in, disposable-profile Store test. No
// coordinates, button text, extension IDs or native handles come from requests.
inline bool confirm_signed_fixture() {
  using Microsoft::WRL::ComPtr;
  const auto initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
  struct Apartment {bool release;~Apartment(){if(release)CoUninitialize();}} apartment{SUCCEEDED(initialized)};
  if(FAILED(initialized)&&initialized!=RPC_E_CHANGED_MODE)return false;
  ComPtr<IUIAutomation> automation;
  if(FAILED(CoCreateInstance(CLSID_CUIAutomation,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&automation))))return false;
  ComPtr<IUIAutomation2> timeout;if(SUCCEEDED(automation.As(&timeout))){timeout->put_ConnectionTimeout(2000);timeout->put_TransactionTimeout(2000);}
  VARIANT name{};name.vt=VT_BSTR;name.bstrVal=SysAllocString(L"Add extension");
  ComPtr<IUIAutomationCondition> button;const auto result=automation->CreatePropertyCondition(UIA_NamePropertyId,name,&button);VariantClear(&name);
  if(FAILED(result))return false;
  std::vector<HWND> windows;
  EnumWindows([](HWND window,LPARAM data)->BOOL{DWORD process{};GetWindowThreadProcessId(window,&process);if(process==GetCurrentProcessId()&&IsWindowVisible(window))reinterpret_cast<std::vector<HWND>*>(data)->push_back(window);return TRUE;},reinterpret_cast<LPARAM>(&windows));
  for(const auto window:windows){
    ComPtr<IUIAutomationElement> root;if(FAILED(automation->ElementFromHandle(window,&root)))continue;
    ComPtr<IUIAutomationElement> target;if(FAILED(root->FindFirst(TreeScope_Subtree,button.Get(),&target))||!target)continue;
    int process{},type{};BOOL offscreen{},enabled{};
    if(FAILED(target->get_CurrentProcessId(&process))||process!=static_cast<int>(GetCurrentProcessId())||
      FAILED(target->get_CurrentControlType(&type))||type!=UIA_ButtonControlTypeId||
      FAILED(target->get_CurrentIsOffscreen(&offscreen))||offscreen||FAILED(target->get_CurrentIsEnabled(&enabled))||!enabled)continue;
    // Require the fixed package's name in this same native window before the
    // permission button can be invoked. Only this test process is inspected.
    VARIANT package{};package.vt=VT_BSTR;package.bstrVal=SysAllocString(L"uBlock Origin Lite");
    ComPtr<IUIAutomationCondition> expected;automation->CreatePropertyConditionEx(UIA_NamePropertyId,package,PropertyConditionFlags_MatchSubstring,&expected);VariantClear(&package);
    ComPtr<IUIAutomationElement> package_name;if(!expected||FAILED(root->FindFirst(TreeScope_Subtree,expected.Get(),&package_name))||!package_name)continue;
    ComPtr<IUIAutomationInvokePattern> invoke;if(SUCCEEDED(target->GetCurrentPatternAs(UIA_InvokePatternId,IID_PPV_ARGS(&invoke)))){
      // Once dispatched, do not retry an uncertain Invoke outcome. The test
      // separately verifies installation or rejection through the Store API.
      invoke->Invoke();return true;
    }
  }
  return false;
}
}
#endif
