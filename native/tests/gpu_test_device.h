#pragma once
// Shared WARP device for the GPU tests (K39). Debug layer plus GPU-based
// validation when the D3D12 SDK layers are installed; an InfoQueue turns
// ERROR/CORRUPTION messages into a test failure. Without the layers the test
// still runs, prints one note and reports validated=false.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>

namespace gpu_test {
struct Device {
 Microsoft::WRL::ComPtr<ID3D12Device> device;
 Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
 Microsoft::WRL::ComPtr<ID3D12InfoQueue> info;
 bool validated{};
};
inline bool create(Device& out, bool gpu_based_validation = true) {
 out = {};
 Microsoft::WRL::ComPtr<ID3D12Debug> debug; bool layer = false;
 if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
  debug->EnableDebugLayer(); layer = true;
  Microsoft::WRL::ComPtr<ID3D12Debug1> debug1;
  if (gpu_based_validation && SUCCEEDED(debug.As(&debug1))) debug1->SetEnableGPUBasedValidation(TRUE);
 }
 Microsoft::WRL::ComPtr<IDXGIFactory4> factory; Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
 if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) || FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)))) return false;
 if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&out.device)))) return false;
 D3D12_COMMAND_QUEUE_DESC qd{}; qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
 if (FAILED(out.device->CreateCommandQueue(&qd, IID_PPV_ARGS(&out.queue)))) return false;
 if (layer && SUCCEEDED(out.device.As(&out.info))) {
  out.info->SetMessageCountLimit(4096); out.info->ClearStoredMessages(); out.validated = true;
 } else {
  std::printf("NOTE: D3D12 debug layer unavailable (install Graphics Tools); this GPU test runs without validation\n");
 }
 return true;
}
// Blocks until the queue is idle (one fence per call; tests only).
inline bool wait(const Device& d) {
 Microsoft::WRL::ComPtr<ID3D12Fence> fence;
 if (!d.device || FAILED(d.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))) || FAILED(d.queue->Signal(fence.Get(), 1))) return false;
 if (fence->GetCompletedValue() >= 1) return true;
 HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr); if (!event) return false;
 bool ok = SUCCEEDED(fence->SetEventOnCompletion(1, event)) && WaitForSingleObject(event, 30000) == WAIT_OBJECT_0;
 CloseHandle(event); return ok;
}
// Prints and counts the ERROR/CORRUPTION messages stored since the last call, then clears them.
inline unsigned errors(const Device& d, const char* where) {
 if (!d.validated || !d.info) return 0;
 UINT64 n = d.info->GetNumStoredMessages(); unsigned bad = 0;
 for (UINT64 i = 0; i < n; ++i) {
  SIZE_T bytes = 0; if (FAILED(d.info->GetMessage(i, nullptr, &bytes)) || !bytes) continue;
  std::vector<unsigned char> storage(bytes); auto m = (D3D12_MESSAGE*)storage.data();
  if (FAILED(d.info->GetMessage(i, m, &bytes))) continue;
  if (m->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION || m->Severity == D3D12_MESSAGE_SEVERITY_ERROR) {
   ++bad; std::printf("D3D12 %s at %s: %.*s\n", m->Severity == D3D12_MESSAGE_SEVERITY_ERROR ? "ERROR" : "CORRUPTION", where, int(m->DescriptionByteLength), m->pDescription);
  }
 }
 d.info->ClearStoredMessages();
 return bad;
}
// Call after the GPU work has finished (wait()): GPU-based validation reports at
// completion. Fails the process on any ERROR or CORRUPTION message stored since
// the previous call, after printing every one of them.
inline void expect_clean(const Device& d, const char* where) {
 if (unsigned bad = errors(d, where)) { std::printf("FAIL: %u D3D12 validation message(s) at %s\n", bad, where); std::fflush(stdout); std::exit(3); }
}
// Fills a freshly created render target (already in RENDER_TARGET state) with
// NaN on float formats, or magenta on UNORM formats, so a pass that reads
// uninitialised texels fails a finite/range check instead of seeing WARP's zeros.
inline void poison(ID3D12GraphicsCommandList* list, ID3D12Resource* rt, D3D12_CPU_DESCRIPTOR_HANDLE rtv) {
 if (!list || !rt) return;
 auto format = rt->GetDesc().Format;
 bool floating = format == DXGI_FORMAT_R16G16B16A16_FLOAT || format == DXGI_FORMAT_R32G32B32A32_FLOAT || format == DXGI_FORMAT_R11G11B10_FLOAT ||
                 format == DXGI_FORMAT_R16G16_FLOAT || format == DXGI_FORMAT_R32G32_FLOAT || format == DXGI_FORMAT_R16_FLOAT || format == DXGI_FORMAT_R32_FLOAT;
 const float n = std::numeric_limits<float>::quiet_NaN();
 const float nan4[4]{n, n, n, n}, magenta[4]{1, 0, 1, 1};
 list->ClearRenderTargetView(rtv, floating ? nan4 : magenta, 0, nullptr);
}
}  // namespace gpu_test
