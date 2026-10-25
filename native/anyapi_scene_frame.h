#pragma once
// Frame core shared by AnyAPI's scene passes (AnyAPI 0.40.2). Header-only and
// self-contained: tests include it without dinput8_proxy.cpp.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

namespace scene_frame {

// ---------------------------------------------------------------- K01 reads
// In-process read: SEH-guarded memcpy, never ReadProcessMemory. Returns false
// on null/zero size, on an address outside user space, or on an access
// violation, guard-page hit or in-page error. A guard-page hit is re-armed
// (PAGE_GUARD set again on that page) so a stale pointer into another thread's
// stack guard cannot break that stack's growth. Safe on any thread. On failure
// the contents of out are unspecified.
inline std::atomic<uint32_t> rpm_calls{0};     // ++ in ::safe_read_memory (dinput8_proxy.cpp); platform samples it per present
inline std::atomic<uint32_t> read_faults{0};   // reads that faulted (all kinds)
inline std::atomic<uint32_t> guard_hits{0};    // of those, guard-page hits (re-armed)
inline void (*log_sink)(bool warning, const char* line){}; // set by dinput8_proxy.cpp (log_line); null in tests

namespace detail {
struct Fault {DWORD code; uintptr_t address;};
inline int classify(EXCEPTION_POINTERS* e, Fault& f) {
 auto r = e->ExceptionRecord; f.code = r->ExceptionCode;
 f.address = r->NumberParameters >= 2 ? uintptr_t(r->ExceptionInformation[1]) : 0;
 return f.code == EXCEPTION_ACCESS_VIOLATION || f.code == STATUS_GUARD_PAGE_VIOLATION || f.code == EXCEPTION_IN_PAGE_ERROR
  ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}
#if (defined(_MSC_VER) && !defined(__clang__)) || defined(ANYAPI_SCENE_FRAME_SEH)
// MSVC (the shipped build; clang with -fasync-exceptions may opt in with ANYAPI_SCENE_FRAME_SEH): SEH is asynchronous inside __try, so a fault in the
// copy lands in the filter. noinline keeps the size a runtime value.
__declspec(noinline) inline bool copy(void* out, const void* in, size_t n, Fault& f) {
 __try { std::memcpy(out, in, n); return true; }
 __except (classify(GetExceptionInformation(), f)) { return false; }
}
#else
// clang/mingw cross builds (tests under Wine): clang's __try does not catch
// hardware faults without -fasync-exceptions, so validate every page first.
// Guard pages are reported but never touched. Not race-free against a
// concurrent VirtualFree; only the MSVC path ships.
inline bool copy(void* out, const void* in, size_t n, Fault& f) {
 const DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
 for (uintptr_t a = uintptr_t(in), end = a + n; a < end;) {
  MEMORY_BASIC_INFORMATION m{};
  if (!VirtualQuery((void*)a, &m, sizeof(m)) || m.State != MEM_COMMIT || !(m.Protect & readable)) { f = {EXCEPTION_ACCESS_VIOLATION, a}; return false; }
  if (m.Protect & PAGE_GUARD) { f = {STATUS_GUARD_PAGE_VIOLATION, a}; return false; }
  a = uintptr_t(m.BaseAddress) + m.RegionSize;
 }
 std::memcpy(out, in, n); return true;
}
#endif
inline std::atomic<bool> guard_logged{false};
inline void rearm(uintptr_t address) {
 MEMORY_BASIC_INFORMATION m{};
 if (!address || !VirtualQuery((void*)address, &m, sizeof(m)) || m.State != MEM_COMMIT || (m.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return;
 DWORD old = 0; VirtualProtect((void*)(address & ~uintptr_t(0xfff)), 1, m.Protect | PAGE_GUARD, &old);
}
}  // namespace detail

inline bool read(uintptr_t address, void* out, size_t size) {
 if (!address || !out || !size || address < 0x10000 || size > 0x7fff0000ull || address + size > 0x7ffffffeffffull) return false;
 detail::Fault f{};
 if (detail::copy(out, (const void*)address, size, f)) return true;
 read_faults.fetch_add(1, std::memory_order_relaxed);
 if (f.code == STATUS_GUARD_PAGE_VIOLATION) {
  guard_hits.fetch_add(1, std::memory_order_relaxed); detail::rearm(f.address);
  if (!detail::guard_logged.exchange(true) && log_sink) log_sink(true, "SCENE_FRAME guard_page_read=1 rearmed=1");
 }
 return false;
}
template<class T> inline bool read_value(uintptr_t a, T& v) { return read(a, &v, sizeof(T)); }
inline auto reader = [](uintptr_t a, void* o, size_t n) { return read(a, o, n); };  // the Read functor for capture()s

// ---------------------------------------------------------------- K03 view
// scene+0x668..0x66b: m_is_underground, m_is_underwater, m_is_nightvision,
// m_is_thermal (render_scene, 0.1.27 game code). Any byte >1 or a failed read: unreadable.
enum : uint32_t { VIEW_UNDERGROUND = 1, VIEW_UNDERWATER = 2, VIEW_NIGHT_VISION = 4, VIEW_THERMAL = 8,
                  VIEW_SPECIAL_VISION = VIEW_NIGHT_VISION | VIEW_THERMAL, VIEW_UNREADABLE = 0x80000000u };
inline uint32_t view_flags(uintptr_t scene) {
 unsigned char b[4]{};
 if (!scene || !read(scene + 0x668, b, 4)) return VIEW_UNREADABLE;
 uint32_t v = 0;
 for (uint32_t i = 0; i < 4; ++i) { if (b[i] > 1) return VIEW_UNREADABLE; if (b[i]) v |= 1u << i; }
 return v;
}

// ---------------------------------------------------------------- K02 resources
// True if *resource's vtable method 10 (ID3D12Resource::GetDesc) lies in
// committed, executable MEM_IMAGE memory of a module that is not the game exe.
// D3D12Core.dll, d3d12.dll (Wine/vkd3d) and wrapper layers (ReShade, RenderDoc,
// PIX) all pass. Validated vtables are kept in an 8-entry cache, so the common
// case is one read and 8 compares. No named-module lookup on this path.
namespace detail {
inline std::atomic<uintptr_t> vtables[8]{};
inline std::atomic<uint32_t> vtable_next{0};
inline std::atomic<uintptr_t> exe_base{0};
inline std::atomic<uintptr_t> modules_logged[8]{};
inline bool executable(DWORD p) {
 return (p & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) && !(p & (PAGE_GUARD | PAGE_NOACCESS));
}
inline void note_module(uintptr_t base) {  // one log line per new module that validates, at most 8
 for (auto& m : modules_logged) {
  uintptr_t seen = m.load(std::memory_order_relaxed);
  if (seen == base) return;
  if (!seen && m.compare_exchange_strong(seen, base)) {
   if (!log_sink) return;
   wchar_t name[MAX_PATH]{}; char utf8[MAX_PATH * 3]{};
   DWORD n = GetModuleFileNameW(HMODULE(base), name, MAX_PATH);
   const wchar_t* leaf = name; for (DWORD i = 0; i < n; ++i) if (name[i] == L'\\' || name[i] == L'/') leaf = name + i + 1;
   WideCharToMultiByte(CP_UTF8, 0, leaf, -1, utf8, int(sizeof(utf8)), nullptr, nullptr);
   log_sink(false, (std::string("SCENE_FRAME resource_module=") + (utf8[0] ? utf8 : "?")).c_str());
   return;
  }
  if (m.load(std::memory_order_relaxed) == base) return;
 }
}
}  // namespace detail
inline bool resource_valid(uintptr_t resource) {
 uintptr_t vtable{};
 if (!read_value(resource, vtable) || !vtable) return false;
 for (auto& v : detail::vtables) if (v.load(std::memory_order_relaxed) == vtable) return true;
 uintptr_t method{}; MEMORY_BASIC_INFORMATION m{};
 if (!read_value(vtable + 10 * 8, method) || !method || !VirtualQuery((void*)method, &m, sizeof(m)) || m.State != MEM_COMMIT ||
     m.Type != MEM_IMAGE || !detail::executable(m.Protect)) return false;
 uintptr_t exe = detail::exe_base.load(std::memory_order_relaxed);
 if (!exe) { exe = uintptr_t(GetModuleHandleW(nullptr)); detail::exe_base.store(exe, std::memory_order_relaxed); }
 if (uintptr_t(m.AllocationBase) == exe) return false;
 detail::vtables[detail::vtable_next.fetch_add(1, std::memory_order_relaxed) % 8].store(vtable, std::memory_order_relaxed);
 detail::note_module(uintptr_t(m.AllocationBase));
 return true;
}
inline void reset_resource_cache() { for (auto& v : detail::vtables) v.store(0, std::memory_order_relaxed); }

// ---------------------------------------------------------------- K43 backoff
// Exponential retry 1,2,4,8,16,32,60,60.. s. Single-threaded use by its owner.
struct Backoff {
 uint64_t next_ms{}; uint32_t failures{};
 bool ready(uint64_t now_ms) const { return now_ms >= next_ms; }
 void failed(uint64_t now_ms) { if (failures < 1000) ++failures; next_ms = now_ms + (failures >= 7 ? 60000ull : 1000ull << (failures - 1)); }
 void succeeded() { failures = 0; next_ms = 0; }
 void reset() { failures = 0; next_ms = 0; }
};

// ---------------------------------------------------------------- call cells
// Copies of scene_controls::resolve_cell / resolve_unique_cell that read with read().
inline void** resolve_cell(uintptr_t caller, size_t end, size_t slot, const unsigned char* target, size_t bytes) {
 uintptr_t cell{}, body{}; unsigned char prefix[32]{};
 if (!target || !bytes || bytes > sizeof(prefix)) return nullptr;
 if (!read(caller + end + slot * 8, &cell, 8) || !cell || !read(cell, &body, 8) || !body || !read(body, prefix, bytes) || std::memcmp(prefix, target, bytes)) return nullptr;
 return (void**)cell;
}
// on_render's short body is shared by hundreds of wrappers: require one distinct call cell.
inline void** resolve_unique_cell(const std::vector<uintptr_t>& callers, size_t end, size_t slot, const unsigned char* target, size_t bytes) {
 void** selected = nullptr;
 for (auto caller : callers) { auto cell = resolve_cell(caller, end, slot, target, bytes); if (!cell) continue; if (selected && selected != cell) return nullptr; selected = cell; }
 return selected;
}

// ---------------------------------------------------------------- service registry
// Framework service versions added after 0.40.2's hard-coded table. Register at
// static init only (string-literal ids starting "anyapi."); the first
// find_service() call seals the registry. At most 32 entries; duplicates refused.
struct ServiceEntry {const char* id; uint32_t version; const void* table;};
namespace detail {
inline ServiceEntry services[32]{};
inline std::atomic<uint32_t> service_count{0}, service_rejects{0};
inline std::atomic<bool> services_sealed{false};
}
inline bool register_service(const char* id, uint32_t version, const void* table) {
 using namespace detail;
 uint32_t n = service_count.load(std::memory_order_relaxed);
 bool bad = services_sealed.load(std::memory_order_acquire) || !id || !table || !version || std::strncmp(id, "anyapi.", 7) || std::strlen(id) > 95 || n >= 32;
 for (uint32_t i = 0; !bad && i < n; ++i) bad = services[i].version == version && !std::strcmp(services[i].id, id);
 if (bad) { service_rejects.fetch_add(1, std::memory_order_relaxed); return false; }
 services[n] = {id, version, table}; service_count.store(n + 1, std::memory_order_release); return true;
}
inline const void* find_service(const char* id, uint32_t version) {
 using namespace detail;
 if (!services_sealed.load(std::memory_order_relaxed)) services_sealed.store(true, std::memory_order_release);
 if (!id) return nullptr;
 uint32_t n = service_count.load(std::memory_order_acquire);
 for (uint32_t i = 0; i < n; ++i) if (services[i].version == version && !std::strcmp(services[i].id, id)) return services[i].table;
 return nullptr;
}
inline uint32_t service_entries() { return detail::service_count.load(std::memory_order_acquire); }
inline const ServiceEntry* service_entry(uint32_t i) { return i < service_entries() ? &detail::services[i] : nullptr; }
inline uint32_t service_rejected() { return detail::service_rejects.load(); }
#define ANYAPI_REGISTER_SERVICE(tag, id, version, table) static const bool anyapi_service_##tag = scene_frame::register_service(id, version, table)

// ---------------------------------------------------------------- board
// Seqlock slot: one writer thread, readers anywhere. load() fails if never
// stored, or if a write stays in progress over 4 attempts.
template<class T> struct Slot {
 static_assert(std::is_trivially_copyable_v<T>, "Slot<T> needs a trivially copyable T");
 std::atomic<uint32_t> seq{0}; T value{};
 void store(const T& v) {
  uint32_t s = seq.load(std::memory_order_relaxed);
  seq.store(s + 1, std::memory_order_relaxed); std::atomic_thread_fence(std::memory_order_release);
  std::memcpy((void*)&value, &v, sizeof(T));
  uint32_t next = s + 2; seq.store(next ? next : 2, std::memory_order_release);
 }
 bool load(T& out) const {
  for (int attempt = 0; attempt < 4; ++attempt) {
   uint32_t a = seq.load(std::memory_order_acquire);
   if (!a) return false;
   if (a & 1) { YieldProcessor(); continue; }
   std::memcpy((void*)&out, (const void*)&value, sizeof(T)); std::atomic_thread_fence(std::memory_order_acquire);
   if (seq.load(std::memory_order_relaxed) == a) return true;
  }
  return false;
 }
};

struct Sample {             // controls: scene_hook, BEFORE scene_apply/wind_scale (native, unscaled values)
 uint64_t frame; uint32_t valid;             // SAMPLE_* bits
 double cycle;                               // scene+0xba8 m_time_of_day_factor, 0..1
 double native_fog;                          // scene+0xb60
 double light_main[3], light_sky[3];         // scene+0xaf0, +0xb08
 double sun_dir[3];                          // scene+0xa78 as stored (points away from the sun; negate for "toward")
 double wind[2];                             // scene+0xb78
 uint8_t weather_particles, ocean;           // scene+0xb70, +0xb71
 uint32_t view;                              // view_flags()
};
enum : uint32_t { SAMPLE_CYCLE = 1, SAMPLE_FOG = 2, SAMPLE_LIGHTS = 4, SAMPLE_SUN = 8, SAMPLE_WIND = 16, SAMPLE_FLAGS = 32, SAMPLE_VIEW = 64 };
struct Clock {              // controls: scene_hook after Sample; celestial::evaluate + cross-check + fallback
 uint64_t frame; uint32_t source;            // 0 none, 1 game clock (0xba8, calibrated), 2 fallback detector
 uint32_t parked;                            // t<0.2||t>0.8 (game's own branch)
 double t;                                   // -1 when source==2
 double sun[3], moon[3];                     // unit vectors TOWARD sun / moon, graphics space
 double sun_y, night;                        // night 0..1 = smoothstep(.05,-.12,sun_y)
 double star_angle;                          // -2*pi*t about world z
 uint32_t day_index; float phase, lit_fraction; // phase 0..1 (8 game days per lunation), lit_fraction (1+cos)/2, floor by consumer
};
struct Weather {            // controls: ramps in scene_hook from rain/snow sampled in world_time observe
 uint64_t frame; uint32_t source;            // 0 none, 1 v1 (weather_particles m_factor_rain/m_factor_snow)
 float rain, snow, rain_fx, snow_fx, wet, cover, fog_ratio; // fog_ratio = clamp(native_fog/0.01,.5,4) even when source==0
};
struct Temporal {           // aa: apply_jitter (scene_hook) for this frame
 uint64_t frame; uint32_t taa_active;        // TAA history valid last frame and method>=3
 uint32_t jitter_active, index; float jitter_ndc[2], jitter_px[2];
};
struct Flash {              // lighting: ring parse in lighting_hook; omni radius >= 4096
 uint64_t frame; uint32_t active; float colour[3], direction[3], intensity; uint32_t frames_total;
};
struct GpuIdentity {        // platform: first direct-queue execute() per device
 uint32_t known, vendor_id, device_id, uma; uint64_t luid; uint32_t dedicated_mb, shared_mb; char adapter_name[128];
};
struct SkyPolicy { uint32_t sky_model, cloud_model, weather, moon_disc_follows_light; }; // effects set/set_v2/set_v3 (V1/V2 -> all 0)
struct MoonState { uint64_t frame; uint32_t active; float added_luminance; double direction[3]; }; // controls, K49 Stage A

static_assert(std::atomic<float>::is_always_lock_free && std::atomic<int32_t>::is_always_lock_free && std::atomic<uint64_t>::is_always_lock_free);
struct Board {
 std::atomic<uint64_t> frame{0};             // controls: ++ at renderer_hook entry; fresh(x) := x.frame && x.frame==frame
 Slot<Sample> sample; Slot<Clock> clock; Slot<Weather> weather; Slot<Temporal> temporal;
 Slot<Flash> flash; Slot<GpuIdentity> gpu; Slot<SkyPolicy> sky_policy; Slot<MoonState> moon;
 std::atomic<float> pass_ms[16]{-1.f, -1.f, -1.f, -1.f, -1.f, -1.f, -1.f, -1.f, -1.f, -1.f, -1.f, -1.f, -1.f, -1.f, -1.f, -1.f};
                                             // AnyScenePassV2 index; owner pass writes after each record, -1 when off
 std::atomic<int32_t> pass_error[16]{};      // 0 ok/off, >0 last bypass code, -1 inactive by design (quiet)
 std::atomic<uint32_t> scene_width{0}, scene_height{0}; // effects: HDR target size from its capture
 std::atomic<uint64_t> cpu_ticks{0};         // QPC ticks inside AnyAPI scene hooks since the last present
 std::atomic<uint32_t> finish_since_ok{UINT32_MAX}; // K04: effects stores 0 on a rendered finish; controls ++ (saturating) per frame
};
constinit inline Board board;                // constant-initialised: pass_ms is -1 before any code runs
template<class X> inline bool fresh(const X& x) { return x.frame && x.frame == board.frame.load(std::memory_order_relaxed); }

// Adds the elapsed QPC ticks to board.cpu_ticks. Wrap AnyAPI work only, never a
// call into the game's original function. Has a destructor: MSVC refuses it in
// a function that also contains __try (C2712), so put it in the caller.
struct CpuScope {
 LARGE_INTEGER start;
 CpuScope() { QueryPerformanceCounter(&start); }
 ~CpuScope() { LARGE_INTEGER end; QueryPerformanceCounter(&end); board.cpu_ticks.fetch_add(uint64_t(end.QuadPart - start.QuadPart), std::memory_order_relaxed); }
 CpuScope(const CpuScope&) = delete; CpuScope& operator=(const CpuScope&) = delete;
};

// ---------------------------------------------------------------- seams
struct Seams {              // provider assigns at static init; consumer calls only if non-null
 void (*install_postprocess)(const std::vector<uintptr_t>& record_command_callers){}; // aa provides; controls' worker calls
 bool (*replace_ssao)(void* renderer, void* scene){};  // effects provides; true = AnyAPI AO was written, skip the native SSAO
};
constinit inline Seams seams;
}  // namespace scene_frame
