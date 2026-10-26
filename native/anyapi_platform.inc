// Generic DLL loading, Windows input and DXGI/D3D12 frame composition.
// This service contains no map filenames, keybinds, road paths or atlas policy.
#include "anyapi_mod_v1.h"
#include "anyapi_bundle_v1.h"
#include "anyapi_services.inc"
#include "anyapi_text_input.h"
#include "anyapi_item_catalog.inc"
#include <d3d12.h>
#include <d3d11on12.h>
#include <dxgi1_4.h>
#include <d2d1_1.h>
#include <wrl/client.h>
#include <windowsx.h>
#include <memory>
#include <mutex>
#include <array>
#include <atomic>
namespace platform {
using Microsoft::WRL::ComPtr;
struct Plugin {HMODULE module{};std::filesystem::path path;void(*ready)(){};bool part{};std::wstring game_path,plugin_path;AnyModHostV1 host{};AnyModCallbacksV1 callbacks{};ComPtr<ID2D1Bitmap1> bitmap;uint64_t revision{};uint32_t width{},height{};std::atomic<bool> faulted{};
 std::wstring display_name;std::atomic<uint64_t> fault_tick{};uint32_t fault_code{};const char* fault_where{};}; // K42: toast/label name (DLL stem or bundle part name); GetTickCount64() of the first fault, 0 = none
static std::array<Plugin,32> plugins;static std::atomic<uint32_t> plugin_count{};
static std::atomic<int> capture_owner{-1};
static HWND game_window{};static WNDPROC window_original{};
static std::atomic<bool> window_focused{};static uint64_t focus_checked_at{};   // CPU-14: message-driven focus, 1 s self-heal
static bool raw_mouse_only{},raw_keyboard_only{};static AnyTextDecoder text_decoder;
static std::atomic<bool> enabled{true};static std::atomic<uint64_t> frames{};
static SRWLOCK hook_lock=SRWLOCK_INIT;
// pristine: the slot's function in its module file (pristine_of); image: the table is in a module image; head: its first 8 entries.
struct Hook {void** slot{};void* original{};void* replacement{};size_t index{};void* pristine{};bool image{};void* head[8]{};};
static Hook hooks[128]{};static size_t hook_count{};
static std::recursive_mutex render_mutex;
static std::mutex queue_mutex;
static ComPtr<ID3D12CommandQueue> observed_queue;
static std::unordered_map<IDXGISwapChain3*,ComPtr<ID3D12CommandQueue>> presentation_queues;
static ComPtr<IDXGISwapChain3> swap;                  // the game chain AnyAPI composes into; kept across overlay resets and resizes (ROB-2)
static ComPtr<ID3D12CommandQueue> scene_queue;        // the game's presentation queue, bound at chain accept; reset() never clears it (ROB-2)
static ComPtr<ID3D12CommandQueue>& queue=scene_queue; // the name every scene pass reads
// True when q is a queue of device d. Scene passes check it before they build GPU objects, so a new device is never paired with the previous chain's queue.
static bool queue_on(ID3D12CommandQueue* q,ID3D12Device* d){ComPtr<ID3D12Device> owner;return q&&d&&SUCCEEDED(q->GetDevice(IID_PPV_ARGS(&owner)))&&owner.Get()==d;}
static ComPtr<ID3D11Device> device11;static ComPtr<ID3D11DeviceContext> context11;
static ComPtr<ID3D11On12Device> on12;static ComPtr<ID2D1Factory1> factory2d;
static ComPtr<ID2D1Device> device2d;static ComPtr<ID2D1DeviceContext> context2d;
static std::vector<ComPtr<ID3D11Resource>> wrapped;
static std::vector<ComPtr<ID2D1Bitmap1>> targets;
static ComPtr<ID3D12Fence> fence;static uint64_t fence_value{};
static HANDLE fence_event{};
// K40/CPU-14: per-chain facts, cached until ResizeBuffers or a new chain.
struct ChainCache {IDXGISwapChain* raw{};IDXGISwapChain* foreign{};HWND window{};DXGI_SWAP_CHAIN_DESC desc{};bool desc_valid{},queue_bound{},queue_logged{};uint64_t accepted_at{};};
static ChainCache chain_cache;
static scene_frame::Backoff overlay_backoff;           // OVL-5: 1,2,4..60 s; reset on resize or a new chain
enum OverlayStage:uint32_t {STAGE_OK=0,STAGE_CHAIN=1,STAGE_QUEUE=2,STAGE_D3D11ON12=3,STAGE_D2D=4,STAGE_FENCE=5,STAGE_WRAP=6,STAGE_END_DRAW=7};
struct OverlayResult {uint32_t stage{STAGE_OK};HRESULT hr{S_OK};};
static std::atomic<bool> overlay_down{true};           // scene_timing v2 flag 8
static uint32_t overlay_fail_stage{};                  // test seam: initialize()/build_targets() fail at this stage; 0 in production
static uint32_t overlay_attempts{};                    // initialize()/build_targets() calls (tests)
static uint32_t overlay_logged_stage{};static HRESULT overlay_logged_hr{S_OK};
static bool reported_frame{},reported_graphics{};
static decltype(&SetCursorPos) real_set_cursor_pos{};
static decltype(&ClipCursor) real_clip_cursor{};
static decltype(&SetCursor) real_set_cursor{};
static decltype(&GetKeyState) real_key_state{};
static decltype(&GetRawInputData) real_raw_input{};
static std::atomic<uint32_t> faults_total{};
// K42: the single fault path for every plugin callback boundary. Marks the slot faulted, releases input capture only
// if this owner holds it, and logs once with the exception code. Services gated by anyapi_owner_active() drop the
// owner's policy on their next read; that is what the notice's "its changes were undone" refers to.
static void fault(int owner,const char* where,uint32_t code){if(owner<0||owner>=int(plugin_count.load()))return;
 auto& p=plugins[owner];int expected=owner;bool released=capture_owner.compare_exchange_strong(expected,-1);
 if(p.faulted.exchange(true))return;
 p.fault_code=code;p.fault_where=where?where:"callback";p.fault_tick.store(GetTickCount64());faults_total.fetch_add(1);
 char m[192];std::snprintf(m,sizeof(m),"PLUGIN_FAULT where=%s code=0x%08X capture_released=%d disabled_until_restart=1",p.fault_where,code,released?1:0);
 log_line(ANY_LOG_ERROR,p.callbacks.id?p.callbacks.id:"framework",m);}
// The plugin-id form: the first running slot with that id.
static void fault(const char* owner,const char* where,uint32_t code){if(!owner)return;auto n=plugin_count.load();
 for(uint32_t i=0;i<n;++i)if(plugins[i].callbacks.id&&!strcmp(plugins[i].callbacks.id,owner)&&!plugins[i].faulted){fault(int(i),where,code);return;}}
}
#include "anyapi_gpu_draw.inc"
#include "anyapi_post_process.inc"
#include "anyapi_frame_timing.h"
namespace platform {

// Suppress the game's cursor locking/recentering while a DLL owns input capture.
static BOOL WINAPI captured_cursor_pos(int x,int y){return capture_owner.load()>=0?TRUE:real_set_cursor_pos(x,y);}
// Release cursor clipping during plugin capture and restore normal clipping calls otherwise.
static BOOL WINAPI captured_clip(const RECT* rect){return real_clip_cursor(capture_owner.load()>=0?nullptr:rect);}
// Keep the arrow cursor visible during plugin capture; otherwise retain the game-selected cursor.
static HCURSOR WINAPI captured_cursor(HCURSOR cursor){return real_set_cursor(capture_owner.load()>=0?LoadCursorW(nullptr,MAKEINTRESOURCEW(32512)):cursor);}
// Report unpressed polled keys to the game while plugin capture is active.
static SHORT WINAPI captured_key_state(int key){return capture_owner.load()>=0?0:real_key_state(key);}
// Neutralize raw movement/down events during capture while retaining release events.
static UINT WINAPI captured_raw(HRAWINPUT input,UINT command,LPVOID data,PUINT size,UINT header){UINT result=real_raw_input(input,command,data,size,header);
 if(capture_owner.load()>=0&&command==RID_INPUT&&data&&result!=UINT(-1)&&result>=sizeof(RAWINPUTHEADER)){
  auto r=(RAWINPUT*)data;if(r->header.dwType==RIM_TYPEMOUSE&&result>=sizeof(RAWINPUTHEADER)+sizeof(RAWMOUSE)){
   r->data.mouse.lLastX=r->data.mouse.lLastY=0;r->data.mouse.usButtonFlags&=RI_MOUSE_LEFT_BUTTON_UP|RI_MOUSE_RIGHT_BUTTON_UP|RI_MOUSE_MIDDLE_BUTTON_UP|RI_MOUSE_BUTTON_4_UP|RI_MOUSE_BUTTON_5_UP;r->data.mouse.usButtonData=0;
  }else if(r->header.dwType==RIM_TYPEKEYBOARD&&result>=sizeof(RAWINPUTHEADER)+sizeof(RAWKEYBOARD)&&!(r->data.keyboard.Flags&RI_KEY_BREAK)){r->data.keyboard.VKey=0;r->data.keyboard.MakeCode=0;}
 }return result;
}
// Patch only named cursor/input imports in the game image, preserving real system targets.
static void capture_imports(){HMODULE user32=GetModuleHandleW(L"user32.dll");real_set_cursor_pos=(decltype(real_set_cursor_pos))GetProcAddress(user32,"SetCursorPos");real_clip_cursor=(decltype(real_clip_cursor))GetProcAddress(user32,"ClipCursor");real_set_cursor=(decltype(real_set_cursor))GetProcAddress(user32,"SetCursor");real_key_state=(decltype(real_key_state))GetProcAddress(user32,"GetKeyState");real_raw_input=(decltype(real_raw_input))GetProcAddress(user32,"GetRawInputData");
 auto base=(uint8_t*)GetModuleHandleW(nullptr);auto dos=(IMAGE_DOS_HEADER*)base;if(dos->e_magic!=IMAGE_DOS_SIGNATURE)return;auto nt=(IMAGE_NT_HEADERS64*)(base+dos->e_lfanew);if(nt->Signature!=IMAGE_NT_SIGNATURE)return;auto directory=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];if(!directory.VirtualAddress)return;
 auto entry=(IMAGE_IMPORT_DESCRIPTOR*)(base+directory.VirtualAddress);uint32_t installed=0;
 for(;entry->Name;++entry){if(_stricmp((char*)(base+entry->Name),"USER32.dll")||!entry->OriginalFirstThunk)continue;
  auto names=(IMAGE_THUNK_DATA64*)(base+entry->OriginalFirstThunk);auto table=(IMAGE_THUNK_DATA64*)(base+entry->FirstThunk);
  for(;names->u1.AddressOfData;++names,++table){if(IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal))continue;auto name=(char*)((IMAGE_IMPORT_BY_NAME*)(base+names->u1.AddressOfData))->Name;void* target{},*expected{};
   if(!strcmp(name,"SetCursorPos")){target=(void*)captured_cursor_pos;expected=(void*)real_set_cursor_pos;}
   else if(!strcmp(name,"ClipCursor")){target=(void*)captured_clip;expected=(void*)real_clip_cursor;}
   else if(!strcmp(name,"SetCursor")){target=(void*)captured_cursor;expected=(void*)real_set_cursor;}
   else if(!strcmp(name,"GetKeyState")){target=(void*)captured_key_state;expected=(void*)real_key_state;}
   else if(!strcmp(name,"GetRawInputData")){target=(void*)captured_raw;expected=(void*)real_raw_input;}
   if(!target||!expected||(void*)table->u1.Function!=expected)continue;DWORD old{};auto slot=(void**)&table->u1.Function;
   if(VirtualProtect(slot,8,PAGE_READWRITE,&old)){if(InterlockedCompareExchangePointer(slot,target,expected)==expected)++installed;DWORD ignored{};VirtualProtect(slot,8,old,&ignored);}
  }
 }char message[128]{};std::snprintf(message,sizeof(message),"PLUGIN_INPUT cursor_imports=%u capture=GAME_WINDOW_ONLY",installed);log_line(ANY_LOG_INFO,"framework",message);
}

// The function a COM slot holds in its module's file on disk: the system implementation, before any overlay
// patched the table. nullptr when the slot is not in the module's initialised data (a copy made at run time), the
// file on disk is not the loaded one, or the headers are not a PE32+ image. Read from the raw file (section table),
// so no loader or relocation state is involved.
static void* pristine_of(HMODULE m,void** slot){auto live=(uint8_t*)m;auto ldos=(const IMAGE_DOS_HEADER*)live;
 if(ldos->e_magic!=IMAGE_DOS_SIGNATURE||ldos->e_lfanew<=0||ldos->e_lfanew>LONG(4096-sizeof(IMAGE_NT_HEADERS64)))return nullptr;
 auto lnt=(const IMAGE_NT_HEADERS64*)(live+ldos->e_lfanew);if(lnt->Signature!=IMAGE_NT_SIGNATURE||lnt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC)return nullptr;
 const uint32_t size=lnt->OptionalHeader.SizeOfImage;const uint64_t rva=uint64_t((uint8_t*)slot-live);if(rva+8>size)return nullptr;
 wchar_t path[1024];DWORD n=GetModuleFileNameW(m,path,DWORD(std::size(path)));if(!n||n>=std::size(path))return nullptr;
 HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);if(file==INVALID_HANDLE_VALUE)return nullptr;
 auto read=[&](uint64_t at,void* out,DWORD bytes){LARGE_INTEGER p;p.QuadPart=LONGLONG(at);DWORD got{};return SetFilePointerEx(file,p,nullptr,FILE_BEGIN)&&ReadFile(file,out,bytes,&got,nullptr)&&got==bytes;};
 void* result{};IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS64 nt{};
 if(read(0,&dos,sizeof(dos))&&dos.e_magic==IMAGE_DOS_SIGNATURE&&read(uint64_t(dos.e_lfanew),&nt,sizeof(nt))&&nt.Signature==IMAGE_NT_SIGNATURE&&nt.OptionalHeader.Magic==IMAGE_NT_OPTIONAL_HDR64_MAGIC
    &&nt.OptionalHeader.SizeOfImage==size&&nt.FileHeader.TimeDateStamp==lnt->FileHeader.TimeDateStamp){
  const uint64_t table=uint64_t(dos.e_lfanew)+FIELD_OFFSET(IMAGE_NT_HEADERS64,OptionalHeader)+nt.FileHeader.SizeOfOptionalHeader;
  auto section=[&](uint64_t r,IMAGE_SECTION_HEADER& out){for(WORD i=0;i<nt.FileHeader.NumberOfSections&&i<96;++i){IMAGE_SECTION_HEADER h{};if(!read(table+i*sizeof(h),&h,sizeof(h)))return false;
    if(r>=h.VirtualAddress&&r+8<=uint64_t(h.VirtualAddress)+h.SizeOfRawData){out=h;return true;}}return false;};
  IMAGE_SECTION_HEADER home{},code{};uint64_t value{};
  if(section(rva,home)&&read(rva-home.VirtualAddress+home.PointerToRawData,&value,8)&&value>=nt.OptionalHeader.ImageBase){const uint64_t target=value-nt.OptionalHeader.ImageBase;
   if(target<size&&section(target,code)&&(code.Characteristics&IMAGE_SCN_MEM_EXECUTE))result=live+target;}}
 CloseHandle(file);return result;}
// Same class: entries 3..7 of two tables (the object basics after IUnknown: never hooked here and rarely by overlays,
// which do hook QueryInterface/AddRef/Release in their copies) mostly match. 0 for another class or a proxy's
// wrapper class; higher is closer.
static int likeness(void*const* a,void*const* b){int basics=0,all=0;for(int k=0;k<8;++k){const bool same=a[k]==b[k];all+=same;basics+=k>=3&&same;}return basics>=4?1+all:0;}
// The table in a module image hooked at this index (and replacement, unless null) of head's class, closest first,
// the earliest on a tie: the class's system table, which start() or the first chain hooked. Needs hook_lock.
static const Hook* table_of(void*const* head,size_t index,void* replacement){const Hook* best{};int score=0;
 for(size_t i=0;i<hook_count;++i){const Hook& h=hooks[i];if(!h.image||h.index!=index||(replacement&&h.replacement!=replacement))continue;const int s=likeness(h.head,head);if(s>score){best=&h;score=s;}}return best;}
// Publish the original COM slot before atomically replacing it; no instruction patching.
// Copies (crash 2026-10-26): an overlay may give an object its own copy of a hooked table. Slots of the copy that
// still hold our hook get no record, original() resolves them through the table they were copied from (table_of);
// recording one with our own function as its original made the hook call itself. A record of a copy that is not
// in a module image is refreshed when its slot no longer holds our hook (the block was freed and reused).
static bool hook_slot(void* object,size_t index,void* replacement){void** base=*(void***)object;auto slot=base+index;
 auto known=[&](bool& done){done=true;for(size_t i=0;i<hook_count;++i)if(hooks[i].slot==slot){done=hooks[i].replacement!=replacement||hooks[i].image||*slot==replacement;return hooks[i].replacement==replacement;}
  if(*slot==replacement)return table_of(base,index,replacement)!=nullptr;
  done=hook_count==std::size(hooks);return false;};
 bool done{};AcquireSRWLockShared(&hook_lock);bool ok=known(done);ReleaseSRWLockShared(&hook_lock);if(done)return ok;
 HMODULE m{};const bool image=GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCWSTR)slot,&m);void* pristine=image?pristine_of(m,slot):nullptr;
 AcquireSRWLockExclusive(&hook_lock);ok=known(done);if(done){ReleaseSRWLockExclusive(&hook_lock);return ok;}
 DWORD old{};if(!VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&old)){ReleaseSRWLockExclusive(&hook_lock);return false;}
 Hook* h{};for(size_t i=0;i<hook_count&&!h;++i)if(hooks[i].slot==slot)h=&hooks[i];const Hook previous=h?*h:Hook{};if(!h)h=&hooks[hook_count++];
 void* original=*slot;*h={slot,original,replacement,index,pristine,image};for(int k=0;k<8;++k)h->head[k]=base[k];
 ok=InterlockedCompareExchangePointer(slot,replacement,original)==original;
 DWORD ignored{};VirtualProtect(slot,sizeof(void*),old,&ignored);
 if(!ok){if(previous.slot)*h=previous;else --hook_count;}ReleaseSRWLockExclusive(&hook_lock);return ok;
}
// Re-entry (crash 2026-10-26, Steam overlay): an overlay that hooks a slot after us and keeps our function as its
// original sends the call straight back into our hook for the same object, so chaining again would recurse until
// the stack overflows. Enter counts how often this thread is already inside a hook for the same object and slot;
// a re-entered hook skips its own work and original() picks a target below the loop by level, always from the
// class's system table (table_of):
//  1: its original (keeps a vtable hook that sat below us), else as level 2;
//  2: its slot's function in its module file (pristine_of), else its original;
//  3+: nothing (the hook returns an error): the depth per object and slot stays bounded whatever the overlays do.
// A first call whose slot still holds the function we would chain to starts at level 1: that hook (an overlay that
// re-hooked over ours, keeping ours as its original) has already run. Known gaps, none of them a loop: such a
// re-hook in the system table seen through a later copy, or two stacked copies, can run one overlay twice or skip
// one. Only the command-list hooks of scene_antialiasing have no Enter: no known overlay vtable-hooks command lists.
struct Entered {void* object{};size_t index{};};
static thread_local Entered entered[16]{};static thread_local int entered_count{};
struct Enter {int again{};Enter(void* object,size_t index){for(int i=0;i<entered_count&&i<16;++i)again+=entered[i].object==object&&entered[i].index==index;
  if(entered_count<16)entered[entered_count]={object,index};++entered_count;}
 ~Enter(){--entered_count;}Enter(const Enter&)=delete;Enter& operator=(const Enter&)=delete;};
static std::atomic<uint64_t> reentry_logged{};
// Find the chained method for this exact COM table, preserving other overlays' hooks; a copy made after we hooked
// its table resolves through that table.
template<class T> static T original(void* object,size_t index,int again=0){void** base=*(void***)object;auto slot=base+index;const Hook* self{};void* fn{};int level=again;AcquireSRWLockShared(&hook_lock);
 for(size_t i=0;i<hook_count&&!self;++i)if(hooks[i].slot==slot)self=&hooks[i];
 if(!self)self=table_of(base,index,nullptr);   // a copy of a hooked table that still holds our hook
 if(self){fn=self->original;void* mine=self->replacement;const Hook* system{};bool found=false;auto usable=[&](void* p){return p&&p!=fn&&p!=mine;};
  auto table=[&]{if(!found){system=table_of(self->head,index,mine);found=true;}return system;};
  if(!level&&fn!=mine&&*slot==fn&&!(table()&&fn==system->pristine))level=1;
  if(!level&&fn==mine)level=1;
  if(level){void* direct{};
   if(table()&&level==1&&usable(system->original))direct=system->original;
   if(table()&&level<=2&&!direct)direct=usable(system->pristine)?system->pristine:usable(system->original)?system->original:nullptr;
   fn=direct;}}
 ReleaseSRWLockShared(&hook_lock);
 if(level){const uint64_t bit=1ull<<(index&63);if(!(reentry_logged.load(std::memory_order_relaxed)&bit)&&!(reentry_logged.fetch_or(bit)&bit)){char m[96];
   std::snprintf(m,sizeof(m),"PLUGIN_PLATFORM hook_reentry slot=%zu level=%d system_call=%d",index,level,fn!=nullptr);log_line(ANY_LOG_WARN,"framework",m);}}
 return (T)fn;}
// Each plugin can capture input only for itself, and can only release its own capture.
static void capture(uint32_t active){if(current_plugin<0)return;if(active)capture_owner.store(current_plugin);else {int expected=current_plugin;capture_owner.compare_exchange_strong(expected,-1);}}
// Invoke a plugin input callback inside a native exception boundary; code receives the exception code (callers call fault()).
static uint32_t call_input(Plugin* p,const AnyInputV1* e,uint32_t& code){__try{return p->callbacks.input?p->callbacks.input(e,p->callbacks.user):0;}__except(code=GetExceptionCode(),EXCEPTION_EXECUTE_HANDLER){return 0;}}
// Invoke a plugin render callback inside a native exception boundary; false (and code) on a fault.
static bool call_render(Plugin* p,const AnyFrameV1* f,AnyCanvasV1* c,uint32_t& code){__try{if(p->callbacks.render)p->callbacks.render(f,c,p->callbacks.user);return true;}__except(code=GetExceptionCode(),EXCEPTION_EXECUTE_HANDLER){return false;}}
// Invoke plugin initialization inside a native exception boundary.
static bool call_init(AnyModInitV1 init,const AnyModHostV1* host,AnyModCallbacksV1* callbacks){__try{return init(host,callbacks);}__except(EXCEPTION_EXECUTE_HANDLER){return false;}}
// Dispatch input to active DLLs without running their code under a loader lock.
static bool input(const AnyInputV1& e){if(services::input(e)&&e.kind!=ANY_FOCUS_LOST)return true;bool used=false;auto count=plugin_count.load();for(uint32_t i=0;i<count;++i){auto& p=plugins[i];if(p.faulted)continue;uint32_t code=0;current_plugin=int(i);used=call_input(&p,&e,code)!=0||used;current_plugin=-1;if(code)fault(int(i),"input",code);}return used;}
// Translate raw-only mouse packets into move, button and signed wheel events without duplication.
static bool raw_mouse(const RAWMOUSE& mouse,int x,int y){AnyInputV1 e{};e.x=x;e.y=y;bool used=false;
 if(mouse.lLastX||mouse.lLastY){e.kind=ANY_MOUSE_MOVE;used=input(e)||used;}
 for(auto pair:{std::pair<USHORT,uint32_t>{RI_MOUSE_LEFT_BUTTON_DOWN,1},{RI_MOUSE_LEFT_BUTTON_UP,1},{RI_MOUSE_RIGHT_BUTTON_DOWN,2},{RI_MOUSE_RIGHT_BUTTON_UP,2}}){
  if(mouse.usButtonFlags&pair.first){e.kind=(pair.first==RI_MOUSE_LEFT_BUTTON_DOWN||pair.first==RI_MOUSE_RIGHT_BUTTON_DOWN)?ANY_MOUSE_DOWN:ANY_MOUSE_UP;e.button=pair.second;used=input(e)||used;}}
 if(mouse.usButtonFlags&RI_MOUSE_WHEEL){e.kind=ANY_MOUSE_WHEEL;e.wheel=SHORT(mouse.usButtonData);used=input(e)||used;}return used;
}
// WM_CHAR is the normal text source. Raw-only keyboards use the active Windows
// layout once per raw press while a mod captures input, avoiding duplicate sources.
static bool text_input(uint32_t unit){uint32_t scalar{};if(!text_decoder.decode(unit,scalar))return false;AnyInputV1 e;e.kind=ANY_TEXT_INPUT;e.key=scalar;return input(e);}
static bool raw_text(const RAWKEYBOARD& key){if(!raw_keyboard_only||capture_owner.load()<0||key.Flags&RI_KEY_BREAK||key.VKey>=256)return false;
 BYTE state[256]{};GetKeyboardState(state);state[key.VKey]|=0x80;if((state[VK_CONTROL]&0x80)&&!(state[VK_MENU]&0x80))return false;wchar_t text[16]{};int count=ToUnicodeEx(key.VKey,key.MakeCode,state,text,16,0,GetKeyboardLayout(0));bool used=false;for(int i=0;i<count;++i)used=text_input(uint16_t(text[i]))||used;return used;}
// Game WndProc integration: keys/mouse stay in the real game window, with optional capture.
static LRESULT CALLBACK wndproc(HWND h,UINT m,WPARAM w,LPARAM l){
 AnyInputV1 e{};bool event=false,raw_used=false,raw_release=false;
 if(m==WM_ACTIVATEAPP)window_focused=w!=0;else if(m==WM_ACTIVATE)window_focused=LOWORD(w)!=WA_INACTIVE;else if(m==WM_SETFOCUS)window_focused=true;else if(m==WM_KILLFOCUS)window_focused=false;else if(m==WM_DISPLAYCHANGE)frame_timing::refresh_due=true;
 if(m==WM_KILLFOCUS||m==WM_ACTIVATEAPP&&!w){capture_owner.store(-1);text_decoder.reset();e.kind=ANY_FOCUS_LOST;event=true;}
 else if(m==WM_UNICHAR&&w==UNICODE_NOCHAR)return TRUE;
 else if(m==WM_CHAR||m==WM_UNICHAR){if(window_focused.load(std::memory_order_relaxed)){bool consumed=text_input(uint32_t(w));if(consumed||capture_owner.load()>=0)return 0;}}
 else if(m==WM_KEYDOWN||m==WM_SYSKEYDOWN||m==WM_KEYUP||m==WM_SYSKEYUP){e.kind=(m==WM_KEYDOWN||m==WM_SYSKEYDOWN)?ANY_KEY_DOWN:ANY_KEY_UP;e.key=uint32_t(w);event=true;}
 else if(m==WM_MOUSEMOVE||m==WM_LBUTTONDOWN||m==WM_LBUTTONUP||m==WM_RBUTTONDOWN||m==WM_RBUTTONUP||m==WM_MOUSEWHEEL){
  e.x=GET_X_LPARAM(l);e.y=GET_Y_LPARAM(l);event=true;
  if(m==WM_MOUSEWHEEL){POINT p{e.x,e.y};ScreenToClient(h,&p);e.x=p.x;e.y=p.y;e.kind=ANY_MOUSE_WHEEL;e.wheel=GET_WHEEL_DELTA_WPARAM(w);}
  else if(m==WM_MOUSEMOVE)e.kind=ANY_MOUSE_MOVE;
  else {e.kind=(m==WM_LBUTTONDOWN||m==WM_RBUTTONDOWN)?ANY_MOUSE_DOWN:ANY_MOUSE_UP;e.button=(m==WM_LBUTTONDOWN||m==WM_LBUTTONUP)?1:2;}
 }else if(m==WM_INPUT){UINT bytes=sizeof(RAWINPUT);RAWINPUT r{};UINT got=real_raw_input((HRAWINPUT)l,RID_INPUT,&r,&bytes,sizeof(RAWINPUTHEADER));
  if(got!=UINT(-1)&&got>=sizeof(RAWINPUTHEADER)+sizeof(RAWKEYBOARD)&&r.header.dwType==RIM_TYPEKEYBOARD){e.kind=(r.data.keyboard.Flags&RI_KEY_BREAK)?ANY_KEY_UP:ANY_KEY_DOWN;e.key=r.data.keyboard.VKey;event=true;if(window_focused.load(std::memory_order_relaxed))raw_used=raw_text(r.data.keyboard);}
  else if(got!=UINT(-1)&&got>=sizeof(RAWINPUTHEADER)+sizeof(RAWMOUSE)&&r.header.dwType==RIM_TYPEMOUSE&&raw_mouse_only&&window_focused.load(std::memory_order_relaxed)){
   POINT p{};GetCursorPos(&p);ScreenToClient(h,&p);raw_used=raw_mouse(r.data.mouse,p.x,p.y);raw_release=(r.data.mouse.usButtonFlags&(RI_MOUSE_LEFT_BUTTON_UP|RI_MOUSE_RIGHT_BUTTON_UP|RI_MOUSE_MIDDLE_BUTTON_UP|RI_MOUSE_BUTTON_4_UP|RI_MOUSE_BUTTON_5_UP))!=0;
  }}
 bool used=(event&&(e.kind==ANY_FOCUS_LOST||window_focused.load(std::memory_order_relaxed))&&input(e))||raw_used;
 bool release=raw_release||e.kind==ANY_KEY_UP||e.kind==ANY_MOUSE_UP||m==WM_LBUTTONUP||m==WM_RBUTTONUP||m==WM_MBUTTONUP||m==WM_XBUTTONUP;
 if(m==WM_INPUT&&used&&!release)return DefWindowProcW(h,m,w,l);
 // Captured raw mouse packets reach the game only through the sanitized import above.
 if((used||capture_owner.load()>=0)&&!release&&m!=WM_INPUT&&(m==WM_KEYDOWN||m==WM_SYSKEYDOWN||m==WM_KEYUP||m==WM_SYSKEYUP||m>=WM_MOUSEFIRST&&m<=WM_MOUSELAST)){
  return 0;
 }
 if(m==WM_SETCURSOR&&capture_owner.load()>=0){SetCursor(LoadCursorW(nullptr,MAKEINTRESOURCEW(32512)));return TRUE;}
 return CallWindowProcW(window_original,h,m,w,l);
}
// Attach to the game's existing window; no overlay HWND or helper process is created.
static void attach_window(HWND h){if(!h||game_window==h)return;DWORD pid{};GetWindowThreadProcessId(h,&pid);if(pid!=GetCurrentProcessId())return;
 if(game_window&&window_original&&IsWindow(game_window)&&WNDPROC(GetWindowLongPtrW(game_window,GWLP_WNDPROC))==wndproc)SetWindowLongPtrW(game_window,GWLP_WNDPROC,LONG_PTR(window_original));
 game_window=h;window_original=WNDPROC(GetWindowLongPtrW(h,GWLP_WNDPROC));SetLastError(0);
 if(!SetWindowLongPtrW(h,GWLP_WNDPROC,LONG_PTR(wndproc))&&GetLastError()){game_window=nullptr;window_original=nullptr;return;}
 UINT count=0;if(GetRegisteredRawInputDevices(nullptr,&count,sizeof(RAWINPUTDEVICE))!=UINT(-1)&&count&&count<=64){std::vector<RAWINPUTDEVICE> devices(count);
  if(GetRegisteredRawInputDevices(devices.data(),&count,sizeof(RAWINPUTDEVICE))!=UINT(-1))for(auto& d:devices)if(d.usUsagePage==1&&d.usUsage==2&&(d.dwFlags&RIDEV_NOLEGACY)==RIDEV_NOLEGACY)raw_mouse_only=true;else if(d.usUsagePage==1&&d.usUsage==6&&(d.dwFlags&RIDEV_NOLEGACY)==RIDEV_NOLEGACY)raw_keyboard_only=true;}
 window_focused=GetForegroundWindow()==h;focus_checked_at=GetTickCount64()+1000;
 log_line(ANY_LOG_INFO,"framework","PLUGIN_INPUT game_window=ATTACHED overlay_windows=0");}
// Wait only during resource teardown; every submitted overlay uses the game's direct queue.
static bool idle(){if(!queue||!fence)return true;if(FAILED(queue->Signal(fence.Get(),++fence_value)))return false;
 if(fence->GetCompletedValue()>=fence_value)return true;if(!fence_event)fence_event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
 return fence_event&&SUCCEEDED(fence->SetEventOnCompletion(fence_value,fence_event))&&WaitForSingleObject(fence_event,3000)==WAIT_OBJECT_0;}
// OVL-6: back-buffer references only; devices, plugin bitmaps, gpu_draw textures and post-process shaders stay.
// D3D11On12 resize order: drop the D2D target and the wrapped buffers, then ClearState + Flush, then wait.
// 11on12 drops a released wrapped buffer's D3D12 reference at a Flush only once the GPU is done with it, and the kept devices are never torn
// down here: Flush again after the wait so a frame still in flight at the first Flush cannot keep a back buffer alive into ResizeBuffers.
static void release_targets(){if(context2d)context2d->SetTarget(nullptr);post_process::release_targets();targets.clear();wrapped.clear();
 if(context11){context11->ClearState();context11->Flush();}idle();if(context11)context11->Flush();}
// Full overlay teardown (start failure, D2D device loss, new chain). Never touches swap or scene_queue (ROB-2).
static void reset(){release_targets();gpu_draw::reset();post_process::reset();for(auto& p:plugins){p.bitmap.Reset();p.width=p.height=0;p.revision=0;}
 context2d.Reset();device2d.Reset();factory2d.Reset();on12.Reset();context11.Reset();device11.Reset();fence.Reset();fence_value=0;overlay_down=true;}
// The game is replacing its chain for this window: drop every reference to the old one. Scene passes keep scene_queue until the new chain binds.
static void forget_chain(){reset();swap.Reset();chain_cache.raw=nullptr;chain_cache.window=nullptr;chain_cache.desc_valid=chain_cache.queue_bound=false;}
// Wrap the chain's buffers as Direct2D targets: the only step a resize repeats.
static OverlayResult build_targets(IDXGISwapChain* chain){auto& desc=chain_cache.desc;HRESULT hr=S_OK;if(!desc.BufferCount||desc.BufferCount>16)return {STAGE_CHAIN,E_FAIL};
 auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,D2D1::PixelFormat(desc.BufferDesc.Format,D2D1_ALPHA_MODE_IGNORE),96,96);
 wrapped.resize(desc.BufferCount);targets.resize(desc.BufferCount);D3D11_RESOURCE_FLAGS flags{D3D11_BIND_RENDER_TARGET};
 for(UINT i=0;i<desc.BufferCount;++i){ComPtr<ID3D12Resource> buffer;ComPtr<IDXGISurface> surface;
  if(overlay_fail_stage==STAGE_WRAP||FAILED(hr=chain->GetBuffer(i,IID_PPV_ARGS(&buffer)))||FAILED(hr=on12->CreateWrappedResource(buffer.Get(),&flags,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_PRESENT,IID_PPV_ARGS(&wrapped[i])))
   ||FAILED(hr=wrapped[i].As(&surface))||FAILED(hr=context2d->CreateBitmapFromDxgiSurface(surface.Get(),&props,&targets[i]))){release_targets();return {STAGE_WRAP,hr==S_OK?E_FAIL:hr};}}
 return {};}
// Build a private 11-on-12/Direct2D context on the game's device and presentation queue (bound by bind_queue()).
static OverlayResult initialize(IDXGISwapChain* chain){reset();HRESULT hr=S_OK;ComPtr<ID3D12Device> device;auto& desc=chain_cache.desc;
 if(overlay_fail_stage==STAGE_CHAIN||FAILED(hr=chain->GetDevice(IID_PPV_ARGS(&device)))||!desc.BufferCount||desc.BufferCount>16)return {STAGE_CHAIN,hr==S_OK?E_FAIL:hr};
 IUnknown* queues[]={scene_queue.Get()};
 if(overlay_fail_stage==STAGE_D3D11ON12||FAILED(hr=D3D11On12CreateDevice(device.Get(),D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,queues,1,0,&device11,&context11,nullptr))||FAILED(hr=device11.As(&on12)))return {STAGE_D3D11ON12,hr==S_OK?E_FAIL:hr};
 D2D1_FACTORY_OPTIONS options{};ComPtr<IDXGIDevice> dxgi;
 if(overlay_fail_stage==STAGE_D2D||FAILED(hr=D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED,__uuidof(ID2D1Factory1),&options,(void**)factory2d.GetAddressOf()))||FAILED(hr=device11.As(&dxgi))
  ||FAILED(hr=factory2d->CreateDevice(dxgi.Get(),&device2d))||FAILED(hr=device2d->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,&context2d)))return {STAGE_D2D,hr==S_OK?E_FAIL:hr};
 if(overlay_fail_stage==STAGE_FENCE||FAILED(hr=device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))))return {STAGE_FENCE,hr==S_OK?E_FAIL:hr};
 auto r=build_targets(chain);if(r.stage)return r;
 if(!reported_graphics){log_line(ANY_LOG_INFO,"framework","PLUGIN_RENDER backend=D3D12_11ON12_D2D state=PRESENT_TO_PRESENT game_backbuffer=READY");reported_graphics=true;}return {};}
// OVL-5: back off 1,2,4..60 s and log each distinct failure once. Scene passes and mod callbacks keep running.
static void overlay_failed(OverlayResult r,uint64_t now){overlay_backoff.failed(now);
 if(r.stage==STAGE_WRAP&&overlay_backoff.failures<3)release_targets();else reset();   // 3 wrap failures in a row: rebuild the devices too
 overlay_down=true;scene_frame::board.pass_error[ANY_PASS_OVERLAY].store(int32_t(r.stage));
 if(r.stage==overlay_logged_stage&&r.hr==overlay_logged_hr)return;overlay_logged_stage=r.stage;overlay_logged_hr=r.hr;
 static const char* names[]={"ok","chain","queue","d3d11on12","d2d","fence","wrap","end_draw"};char m[224];
 std::snprintf(m,sizeof(m),"PLUGIN_RENDER overlay=DOWN stage=%s hr=0x%08X failures=%u retry_ms=%llu scene_passes=1 mod_callbacks=1",r.stage<8?names[r.stage]:"unknown",uint32_t(r.hr),overlay_backoff.failures,
  (unsigned long long)(overlay_backoff.next_ms>now?overlay_backoff.next_ms-now:0));log_line(ANY_LOG_WARN,"framework",m);}
// True when the overlay can draw this frame; (re)builds it when the backoff allows.
static bool ensure_overlay(IDXGISwapChain* chain){if(context2d&&!targets.empty()){overlay_down=false;return true;}uint64_t now=GetTickCount64();
 if(!chain_cache.queue_bound){overlay_down=true;scene_frame::board.pass_error[ANY_PASS_OVERLAY].store(int32_t(STAGE_QUEUE));   // retried each present, no backoff
  if(!chain_cache.queue_logged&&now-chain_cache.accepted_at>5000){chain_cache.queue_logged=true;log_line(ANY_LOG_WARN,"framework","PLUGIN_RENDER overlay=WAITING stage=queue seconds=5");}return false;}
 if(!overlay_backoff.ready(now)){overlay_down=true;return false;}
 ++overlay_attempts;OverlayResult r=context2d&&on12?build_targets(chain):initialize(chain);
 if(r.stage){overlay_failed(r,now);return false;}
 if(overlay_logged_stage){log_line(ANY_LOG_INFO,"framework",("PLUGIN_RENDER overlay=UP after_failures="+std::to_string(overlay_backoff.failures)).c_str());overlay_logged_stage=0;overlay_logged_hr=S_OK;}
 overlay_backoff.succeeded();overlay_down=false;scene_frame::board.pass_error[ANY_PASS_OVERLAY].store(0);return true;}
// CPU-14: message-driven focus with a 1 s self-heal (one GetForegroundWindow per second instead of per present and per input message).
static bool focused(){uint64_t now=GetTickCount64();if(now>=focus_checked_at){focus_checked_at=now+1000;window_focused=game_window&&GetForegroundWindow()==game_window;}return window_focused.load(std::memory_order_relaxed);}
// Once per chain: pid check (CPU-14), window attach (input works while the overlay is down), new-chain reset (OVL-5).
// Two live in-process chains presenting in turn switch at most once a second (as the old 1 s retry did), not every frame.
static bool accept(IDXGISwapChain* chain){if(chain_cache.raw&&GetTickCount64()-chain_cache.accepted_at<1000)return false;
 DXGI_SWAP_CHAIN_DESC d{};ComPtr<IDXGISwapChain3> next;if(FAILED(chain->GetDesc(&d)))return false;
 DWORD pid{};GetWindowThreadProcessId(d.OutputWindow,&pid);if(pid!=GetCurrentProcessId()||FAILED(chain->QueryInterface(IID_PPV_ARGS(&next)))){chain_cache.foreign=chain;return false;}
 if(chain_cache.raw)reset();   // the overlay belonged to the previous chain
 swap=next;chain_cache.raw=chain;chain_cache.window=d.OutputWindow;chain_cache.desc=d;chain_cache.desc_valid=true;chain_cache.queue_bound=chain_cache.queue_logged=false;chain_cache.accepted_at=GetTickCount64();
 overlay_backoff.reset();frame_timing::settle();attach_window(d.OutputWindow);return true;}
// ROB-2: the scene queue is bound to the accepted chain (same device), independent of the overlay. Retried each present until bound.
static void bind_queue(IDXGISwapChain* chain){ComPtr<ID3D12CommandQueue> q;
 {std::lock_guard<std::mutex> qlock(queue_mutex);auto found=presentation_queues.find(swap.Get());q=found!=presentation_queues.end()?found->second:observed_queue;}
 ComPtr<ID3D12Device> device,qdevice;if(!q||FAILED(chain->GetDevice(IID_PPV_ARGS(&device)))||FAILED(q->GetDevice(IID_PPV_ARGS(&qdevice)))||qdevice.Get()!=device.Get())return;
 scene_queue=q;chain_cache.queue_bound=true;frame_timing::identify(device.Get(),true);}   // K24: the presenting device wins
// K42: an 8 s toast after a mod faults, and a "Stopped mods" label while the game's Settings screen is open.
static uint64_t settings_polled_at{};static bool settings_visible{};
static uint64_t fault_age(uint64_t now,uint64_t at){return now>at?now-at:0;}
static bool fault_notice_due(uint64_t now){if(!faults_total.load())return false;
 if(now>=settings_polled_at){settings_polled_at=now+250;auto menu=anyapi_menu_service();settings_visible=menu&&menu->settings_open&&menu->settings_open()!=0;}   // <=4 native reads/s, only after a fault
 uint32_t n=plugin_count.load();for(uint32_t i=0;i<n;++i){auto t=plugins[i].fault_tick.load();if(t&&fault_age(now,t)<8000)return true;}
 return settings_visible;}
static void draw_fault_notice(const AnyFrameV1& frame){auto ctx=context2d.Get();if(frame.width<96||frame.height<64)return;
 if(!gpu_draw::brush&&FAILED(ctx->CreateSolidColorBrush(D2D1::ColorF(1,1,1,1),&gpu_draw::brush)))return;
 std::wstring recent,all;uint32_t recent_count=0;float fade=0;uint32_t n=plugin_count.load();
 for(uint32_t i=0;i<n;++i){auto& p=plugins[i];auto t=p.fault_tick.load();if(!t)continue;std::wstring name=p.display_name.empty()?std::wstring(L"A mod"):p.display_name;auto age=fault_age(frame.tick,t);
  all+=(all.empty()?L"":L", ")+name;if(age<8000){recent+=(recent.empty()?L"":L", ")+name;++recent_count;fade=std::max(fade,std::min(1.f,float(8000-age)/1000.f));}}
 float size=std::max(14.f,float(frame.height)/54.f);ctx->SetTransform(D2D1::Matrix3x2F::Identity());
 if(recent_count){auto text=recent+(recent_count==1?L" stopped after an error; its changes were undone.":L" stopped after an error; their changes were undone.");
  float w=std::min(float(frame.width)-32.f,760.f),h=size*2.2f,x=(float(frame.width)-w)/2,y=std::round(float(frame.height)*.06f);
  gpu_draw::brush->SetColor(D2D1::ColorF(0,0,0,.78f*fade));ctx->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x,y,x+w,y+h),6,6),gpu_draw::brush.Get());
  if(auto l=gpu_draw::layout(text,size,w-24,h,ANY_GPU_CENTER|ANY_GPU_VCENTER|ANY_GPU_NOWRAP)){gpu_draw::brush->SetColor(D2D1::ColorF(1,1,1,fade));ctx->DrawTextLayout(D2D1::Point2F(x+12,y),l,gpu_draw::brush.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP);}}
 if(settings_visible&&!all.empty()){auto text=L"Stopped mods: "+all;float s=size*.8f,w=std::min(float(frame.width)-32.f,640.f),h=s*1.8f,x=16,y=float(frame.height)-16-h;
  gpu_draw::brush->SetColor(D2D1::ColorF(0,0,0,.6f));ctx->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x,y,x+w,y+h),4,4),gpu_draw::brush.Get());
  if(auto l=gpu_draw::layout(text,s,w-16,h,ANY_GPU_VCENTER|ANY_GPU_NOWRAP)){gpu_draw::brush->SetColor(D2D1::ColorF(1,1,1,.92f));ctx->DrawTextLayout(D2D1::Point2F(x+8,y),l,gpu_draw::brush.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP);}}}
// Validate a plugin bitmap before passing it to the graphics driver.
static bool valid_canvas(const AnyCanvasV1& c){return c.struct_size==sizeof(c)&&c.pixels&&c.width&&c.height&&c.width<=8192&&c.height<=8192&&c.pitch>=c.width*4&&c.pitch<=131072&&uint64_t(c.pitch)*c.height<=256ull*1024*1024;}
// Compose each DLL's canvas into the real back buffer immediately before presentation. Mod render callbacks run every
// presented frame; while the overlay is down (ROB-2) their canvases are discarded and gpu_draw renderers are skipped.
static void draw(IDXGISwapChain* chain){if(!enabled||!plugin_count.load())return;std::lock_guard<std::recursive_mutex> lock(render_mutex);
 if(chain==chain_cache.foreign)return;                                   // CPU-14: pid checked once per chain
 if(chain!=chain_cache.raw&&!accept(chain))return;
 if(!chain_cache.desc_valid){if(FAILED(chain->GetDesc(&chain_cache.desc)))return;chain_cache.desc_valid=true;}   // CPU-14: BufferDesc until resize
 if(!chain_cache.queue_bound)bind_queue(chain);
 ++frames; // Native-only mods still need a presentation clock when no canvas is drawn.
 if(auto state=anyapi_ui_state_service()){AnyUiSnapshotV1 snapshot;state->copy(&snapshot);}
 frame_timing::OverlayMarks marks;marks.start=frame_timing::now();
 bool up=ensure_overlay(chain);auto& desc=chain_cache.desc;
 AnyFrameV1 frame{};frame.width=desc.BufferDesc.Width;frame.height=desc.BufferDesc.Height;frame.tick=GetTickCount64();frame.focused=focused();
 if(!frame.focused)capture_owner.store(-1);
 if(up)gpu_draw::begin_frame();
 // All callbacks execute before acquiring a wrapped resource, so plugin faults cannot strand it.
 std::array<AnyCanvasV1,32> canvases{};std::array<bool,32> drawable{};bool any=false;uint32_t count=plugin_count.load();
 for(uint32_t i=0;i<count;++i){auto& p=plugins[i];if(p.faulted)continue;uint32_t code=0;current_plugin=int(i);bool ok=call_render(&p,&frame,&canvases[i],code);current_plugin=-1;
  if(!ok){fault(int(i),"render",code);continue;}
  if(!up)continue;   // ROB-2: the callback ran (services stay driven); its canvas is discarded
  current_plugin=int(i);bool commands=gpu_draw::collect(i,frame);current_plugin=-1;if(commands)any=true;
  auto& c=canvases[i];if(!valid_canvas(c))continue;
  if(!p.bitmap||p.width!=c.width||p.height!=c.height){p.bitmap.Reset();p.revision=~c.revision;auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
   if(FAILED(context2d->CreateBitmap(D2D1::SizeU(c.width,c.height),nullptr,0,&props,&p.bitmap)))continue;p.width=c.width;p.height=c.height;}
  if(p.revision!=c.revision){if(FAILED(p.bitmap->CopyFromMemory(nullptr,c.pixels,c.pitch)))continue;p.revision=c.revision;}drawable[i]=true;any=true;
 }
 marks.collected=frame_timing::now();
 if(capture_owner.load()>=0){ClipCursor(nullptr);SetCursor(LoadCursorW(nullptr,MAKEINTRESOURCEW(32512)));}
 if(!up){frame_timing::overlay_frame(marks);return;}
 bool notice=fault_notice_due(frame.tick),effects=post_process::active();
 if(!any&&!effects&&!notice){frame_timing::overlay_frame(marks);return;}
 auto index=swap->GetCurrentBackBufferIndex();if(index>=targets.size()){frame_timing::overlay_frame(marks);return;}
 ID3D11Resource* resource=wrapped[index].Get();on12->AcquireWrappedResources(&resource,1);
 if(effects)try{post_process::render(resource);}catch(const std::exception&){post_process::problem("Post-process allocation failed; effects bypassed");}
 if(!any&&!notice){marks.recorded=marks.ended=frame_timing::now();on12->ReleaseWrappedResources(&resource,1);context11->Flush();marks.flushed=frame_timing::now();frame_timing::overlay_frame(marks);return;}
 context2d->SetTarget(targets[index].Get());context2d->BeginDraw();context2d->SetTransform(D2D1::Matrix3x2F::Identity());
 for(uint32_t i=0;i<count;++i){auto& c=canvases[i];auto& p=plugins[i];if(p.faulted||!drawable[i])continue;
  auto rect=D2D1::RectF(float(c.x),float(c.y),float(c.x)+c.width,float(c.y)+c.height);context2d->DrawBitmap(p.bitmap.Get(),&rect,1,D2D1_INTERPOLATION_MODE_LINEAR,nullptr);}
 for(uint32_t i=0;i<count;++i)if(!plugins[i].faulted&&!gpu_draw::renderers[i].commands.empty())gpu_draw::render(i);
 if(notice)draw_fault_notice(frame);   // after every mod command (K42)
 marks.recorded=frame_timing::now();HRESULT hr=context2d->EndDraw();marks.ended=frame_timing::now();
 context2d->SetTarget(nullptr);on12->ReleaseWrappedResources(&resource,1);context11->Flush();marks.flushed=frame_timing::now();frame_timing::overlay_frame(marks);
 if(FAILED(hr)){overlay_failed({STAGE_END_DRAW,hr},GetTickCount64());return;}
 if(!reported_frame){reported_frame=true;log_line(ANY_LOG_INFO,"framework","PLUGIN_RENDER game_backbuffer=DRAWN helper_processes=0");}
}
using PresentFn=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*,UINT,UINT);
using Present1Fn=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*,UINT,UINT,const DXGI_PRESENT_PARAMETERS*);
using ResizeFn=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*,UINT,UINT,UINT,DXGI_FORMAT,UINT);
using Resize1Fn=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3*,UINT,UINT,UINT,DXGI_FORMAT,UINT,const UINT*,IUnknown*const*);
using ExecuteFn=void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*,UINT,ID3D12CommandList*const*);
static thread_local bool presenting{};
static std::atomic<void(*)(ID3D12CommandQueue*,UINT,ID3D12CommandList*const*)> after_execute{};
// Chain DXGI Present and draw plugin canvases before normal presentation; skip test-only presents.
// WI-1: times the game's own Present (block) and present-to-present (entry) for anyapi.scene_timing v2.
static HRESULT STDMETHODCALLTYPE present(IDXGISwapChain* s,UINT interval,UINT flags){Enter enter(s,8);auto fn=original<PresentFn>(s,8,enter.again);if(!fn)return E_FAIL;
 if(enter.again||presenting||(flags&DXGI_PRESENT_TEST))return fn(s,interval,flags);
 auto entry=frame_timing::now();presenting=true;try{draw(s);}catch(...){capture_owner.store(-1);log_line(ANY_LOG_ERROR,"framework","PLUGIN_RENDER exception=CPP frame_skipped=1");}presenting=false;
 bool game=s==chain_cache.raw;auto before=frame_timing::now();HRESULT hr=fn(s,interval,flags);if(game)frame_timing::presented(s,entry,before,frame_timing::now(),interval);return hr;}
// Chain DXGI Present1 with the same plugin composition behavior.
static HRESULT STDMETHODCALLTYPE present1(IDXGISwapChain1* s,UINT interval,UINT flags,const DXGI_PRESENT_PARAMETERS* params){Enter enter(s,22);auto fn=original<Present1Fn>(s,22,enter.again);if(!fn)return E_FAIL;
 if(enter.again||presenting||(flags&DXGI_PRESENT_TEST))return fn(s,interval,flags,params);
 auto entry=frame_timing::now();presenting=true;try{draw(s);}catch(...){capture_owner.store(-1);log_line(ANY_LOG_ERROR,"framework","PLUGIN_RENDER exception=CPP frame_skipped=1");}presenting=false;
 bool game=chain_cache.raw==s;auto before=frame_timing::now();HRESULT hr=fn(s,interval,flags,params);if(game)frame_timing::presented(s,entry,before,frame_timing::now(),interval);return hr;}
// Release overlay back-buffer references (devices stay, OVL-6), forward ResizeBuffers and rewrap on the next frame.
static HRESULT STDMETHODCALLTYPE resize(IDXGISwapChain* s,UINT count,UINT w,UINT h,DXGI_FORMAT format,UINT flags){Enter enter(s,13);auto fn=original<ResizeFn>(s,13,enter.again);if(!fn)return E_FAIL;if(enter.again)return fn(s,count,w,h,format,flags);
 std::lock_guard<std::recursive_mutex> lock(render_mutex);if(chain_cache.raw==s){release_targets();chain_cache.desc_valid=false;overlay_backoff.reset();frame_timing::settle();}return fn(s,count,w,h,format,flags);}
// Handle extended ResizeBuffers1 with the same resource teardown behavior.
static HRESULT STDMETHODCALLTYPE resize1(IDXGISwapChain3* s,UINT count,UINT w,UINT h,DXGI_FORMAT format,UINT flags,const UINT* masks,IUnknown*const* queues){Enter enter(s,39);auto fn=original<Resize1Fn>(s,39,enter.again);if(!fn)return E_FAIL;
 if(enter.again)return fn(s,count,w,h,format,flags,masks,queues);std::lock_guard<std::recursive_mutex> lock(render_mutex);if(chain_cache.raw==s){release_targets();chain_cache.desc_valid=false;overlay_backoff.reset();frame_timing::settle();}return fn(s,count,w,h,format,flags,masks,queues);}
// Observe direct command-queue submissions for graphics discovery, then forward the original submission.
// K24: a newly observed queue's device is identified once (CreateDXGIFactory1 only for a device not seen before).
// identified_queue is compared only (no reference): created() may set observed_queue before the queue's first submission.
static ID3D12CommandQueue* identified_queue{};
static void STDMETHODCALLTYPE execute(ID3D12CommandQueue* q,UINT count,ID3D12CommandList*const* lists){Enter enter(q,10);auto fn=original<ExecuteFn>(q,10,enter.again);if(!fn)return;if(enter.again){fn(q,count,lists);return;}bool changed=false;
 if(q->GetDesc().Type==D3D12_COMMAND_LIST_TYPE_DIRECT){std::lock_guard<std::mutex> lock(queue_mutex);if(observed_queue.Get()!=q)observed_queue=q;if(identified_queue!=q){identified_queue=q;changed=true;}}fn(q,count,lists);
 if(changed){ComPtr<ID3D12Device> device;if(SUCCEEDED(q->GetDevice(IID_PPV_ARGS(&device))))frame_timing::identify(device.Get(),false);}
 if(auto callback=after_execute.load())callback(q,count,lists);}
// Install presentation and resize callbacks on the interfaces supported by a swapchain.
static bool hook_chain(IDXGISwapChain* chain){bool ok=hook_slot(chain,8,(void*)present)&&hook_slot(chain,13,(void*)resize);ComPtr<IDXGISwapChain1> one;ComPtr<IDXGISwapChain3> three;
 if(SUCCEEDED(chain->QueryInterface(IID_PPV_ARGS(&one))))ok=hook_slot(one.Get(),22,(void*)present1)&&ok;
 if(SUCCEEDED(chain->QueryInterface(IID_PPV_ARGS(&three))))ok=hook_slot(three.Get(),39,(void*)resize1)&&ok;return ok;}
using CreateFn=HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory*,IUnknown*,DXGI_SWAP_CHAIN_DESC*,IDXGISwapChain**);
using CreateHwndFn=HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*,IUnknown*,HWND,const DXGI_SWAP_CHAIN_DESC1*,const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*,IDXGIOutput*,IDXGISwapChain1**);
// The factory's D3D12 pDevice parameter is the actual presentation queue, not a device.
// The game chain was let go (forget_chain) and the new chain is on another device: scene_queue is dropped, so the passes bypass
// until bind_queue() binds the new queue at the first present, instead of pairing the new device with the old queue.
static void created(IUnknown* d,IDXGISwapChain* s){ComPtr<ID3D12CommandQueue> q;ComPtr<IDXGISwapChain3> identity;bool is_queue=SUCCEEDED(d->QueryInterface(IID_PPV_ARGS(&q)));
 {std::lock_guard<std::recursive_mutex> lock(render_mutex);chain_cache.foreign=nullptr;
  if(is_queue&&scene_queue&&!chain_cache.queue_bound){ComPtr<ID3D12Device> device;if(FAILED(q->GetDevice(IID_PPV_ARGS(&device)))||!queue_on(scene_queue.Get(),device.Get()))scene_queue.Reset();}}
 if(is_queue){std::lock_guard<std::mutex> lock(queue_mutex);observed_queue=q;if(SUCCEEDED(s->QueryInterface(IID_PPV_ARGS(&identity))))presentation_queues[identity.Get()]=q;}hook_chain(s);}
// A new chain for the game's window: let go of the old one first, so a flip-model re-creation on the same HWND can succeed.
static void release_window_chain(HWND h){std::lock_guard<std::recursive_mutex> lock(render_mutex);if(h&&chain_cache.raw&&chain_cache.window==h)forget_chain();}
// Chain legacy DXGI swapchain creation and record successful results.
static HRESULT STDMETHODCALLTYPE create(IDXGIFactory* f,IUnknown* d,DXGI_SWAP_CHAIN_DESC* desc,IDXGISwapChain** out){Enter enter(f,10);auto fn=original<CreateFn>(f,10,enter.again);if(!fn)return E_FAIL;if(enter.again)return fn(f,d,desc,out);release_window_chain(desc?desc->OutputWindow:nullptr);HRESULT hr=fn(f,d,desc,out);if(SUCCEEDED(hr)&&out&&*out)created(d,*out);return hr;}
// Chain window-based DXGI swapchain creation and record successful results.
static HRESULT STDMETHODCALLTYPE create_hwnd(IDXGIFactory2* f,IUnknown* d,HWND h,const DXGI_SWAP_CHAIN_DESC1* desc,const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* full,IDXGIOutput* restrict,IDXGISwapChain1** out){Enter enter(f,15);auto fn=original<CreateHwndFn>(f,15,enter.again);if(!fn)return E_FAIL;if(enter.again)return fn(f,d,h,desc,full,restrict,out);release_window_chain(h);HRESULT hr=fn(f,d,h,desc,full,restrict,out);if(SUCCEEDED(hr)&&out&&*out)created(d,*out);return hr;}
// Discover system COM tables with a temporary hidden swapchain, then release the probe.
static bool start(){capture_imports();ComPtr<IDXGIFactory4> f;ComPtr<ID3D12Device> d;ComPtr<ID3D12CommandQueue> q;
 if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&f)))||FAILED(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&d))))return false;
 D3D12_COMMAND_QUEUE_DESC qdesc{};if(FAILED(d->CreateCommandQueue(&qdesc,IID_PPV_ARGS(&q))))return false;
 WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"AnyAPI.GraphicsProbe";RegisterClassW(&wc);
 HWND h=CreateWindowW(wc.lpszClassName,L"",WS_POPUP,0,0,16,16,nullptr,nullptr,wc.hInstance,nullptr);if(!h)return false;
 DXGI_SWAP_CHAIN_DESC1 desc{};desc.Width=16;desc.Height=16;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=2;desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
 ComPtr<IDXGISwapChain1> sc;bool ok=SUCCEEDED(f->CreateSwapChainForHwnd(q.Get(),h,&desc,nullptr,nullptr,&sc));
 if(ok)ok=hook_chain(sc.Get())&&hook_slot(q.Get(),10,(void*)execute)&&hook_slot(f.Get(),10,(void*)create)&&hook_slot(f.Get(),15,(void*)create_hwnd);
 sc.Reset();DestroyWindow(h);enabled.store(ok);if(ok)log_line(ANY_LOG_INFO,"framework","PLUGIN_PLATFORM graphics_hooks=READY mechanism=COM_TABLE_ATOMIC");return ok;
}
static std::wstring widen(const std::string& s){int n=MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),nullptr,0);std::wstring w(size_t(std::max(n,0)),L'\0');if(n>0)MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),w.data(),n);return w;}
// Initialize one mod (a DLL, or one part of a bundle) in the next plugin slot. label names it in the log; display names it to the player (K42 notice).
static bool start_mod(HMODULE module,const std::filesystem::path& path,AnyModInitV1 init,void(*ready)(),bool part,const std::string& label,const std::string& display={}){
 auto index=plugin_count.load();if(index==plugins.size()){log_line(ANY_LOG_ERROR,"framework",("Plugin limit reached; not loaded: "+label).c_str());return false;}
 auto& p=plugins[index];p.module=module;p.path=path;p.ready=ready;p.part=part;p.callbacks={};p.faulted=false;p.display_name=display.empty()?path.stem().wstring():widen(display);p.fault_tick=0;p.fault_code=0;p.fault_where=nullptr;p.game_path=g_game_dir.wstring();p.plugin_path=path.parent_path().wstring();p.host.game_directory=p.game_path.c_str();p.host.plugin_directory=p.plugin_path.c_str();p.host.log=[](uint32_t level,const char* id,const char* msg){log_line(AnymakerLogLevel(level),id,msg);};p.host.copy_players=p34_copy_snapshot;p.host.capture_input=capture;
 current_plugin=int(index);bool ok=false;try{ok=call_init(init,&p.host,&p.callbacks);}catch(...){ok=false;}current_plugin=-1;
 if(!ok||p.callbacks.struct_size!=sizeof(p.callbacks)||p.callbacks.abi!=ANYAPI_MOD_ABI||!p.callbacks.id||!services::identifier(p.callbacks.id)){log_line(ANY_LOG_ERROR,"framework",("Plugin initialization rejected: "+label).c_str());p.faulted=true;plugin_count.store(index+1);return false;}
 plugin_count.store(index+1);log_line(ANY_LOG_INFO,p.callbacks.id,part?("Bundle part initialized; bundle="+label+" lifecycle=PROCESS_LIFETIME").c_str():"DLL plugin initialized; lifecycle=PROCESS_LIFETIME");return true;
}
// Bundles (anyapi_bundle_v1.h): one DLL carrying several mods, each started in its own plugin slot so it keeps its
// own callbacks, renderer, input, services and fault isolation. Players switch parts off for the next launch.
namespace bundles {
struct Part {std::string id,name,bundle;uint32_t state{},flags{};bool enabled_next{true};};
static std::mutex mutex;static std::vector<Part> parts;static std::vector<std::string> off;static std::filesystem::path file;
static bool is_off(const std::string& id){return std::find(off.begin(),off.end(),id)!=off.end();}
// One "<id>\t0" line per switched-off part. Unknown ids are kept, so a bundle that is removed for a while keeps them.
static void read(){file=g_game_dir/L"AnyAPI and Modding"/L"bundle-parts.tsv";std::ifstream in(file);std::string line;
 while(std::getline(in,line)&&off.size()<256){if(!line.empty()&&line.back()=='\r')line.pop_back();auto tab=line.find('\t');if(tab==std::string::npos)continue;auto id=line.substr(0,tab);if(line.substr(tab+1)=="0"&&services::identifier(id.c_str())&&!is_off(id))off.push_back(id);}}
static bool save(){std::string data;for(auto& id:off)data+=id+"\t0\n";auto temporary=file;temporary+=L".tmp";
 {std::ofstream out(temporary,std::ios::binary|std::ios::trunc);if(!out)return false;out<<data;out.flush();if(!out.good())return false;}
 return MoveFileExW(temporary.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;}
static void text(char* out,size_t capacity,const std::string& value){auto n=std::min(value.size(),capacity-1);memcpy(out,value.data(),n);out[n]=0;}
static uint32_t count(){std::lock_guard<std::mutex> lock(mutex);return uint32_t(parts.size());}
static bool copy(uint32_t index,AnyBundlePartInfoV1* out){if(!out||out->struct_size!=sizeof(*out))return false;std::lock_guard<std::mutex> lock(mutex);if(index>=parts.size())return false;auto& p=parts[index];
 *out={};out->state=p.state;out->flags=p.flags;out->enabled_next=p.enabled_next;text(out->id,sizeof(out->id),p.id);text(out->name,sizeof(out->name),p.name);text(out->bundle,sizeof(out->bundle),p.bundle);return true;}
static bool set_enabled(const char* id,uint32_t enabled){if(!id||!services::identifier(id))return false;std::lock_guard<std::mutex> lock(mutex);
 bool known=false;for(auto& p:parts)if(p.id==id){if(p.flags&ANY_BUNDLE_PART_REQUIRED)return false;known=true;}if(!known)return false;
 auto previous=off;off.erase(std::remove(off.begin(),off.end(),std::string(id)),off.end());if(!enabled)off.push_back(id);
 if(off!=previous&&!save()){off=previous;log_line(ANY_LOG_ERROR,"framework","BUNDLE_PART_SWITCH saved=0");return false;}
 for(auto& p:parts)if(p.id==id)p.enabled_next=enabled!=0;if(off!=previous)log_line(ANY_LOG_INFO,"framework",("BUNDLE_PART_SWITCH id="+std::string(id)+" enabled_next="+std::to_string(enabled!=0)).c_str());return true;}
static const AnyBundlePartsV1 api{sizeof(AnyBundlePartsV1),1,count,copy,set_enabled};
static uint32_t call_list(AnyBundlePartsExportV1 list,const AnyBundlePartV1** table){__try{return list(table);}__except(EXCEPTION_EXECUTE_HANDLER){*table=nullptr;return 0;}}
static bool running(const char* id){for(uint32_t i=0;i<plugin_count.load();++i)if(!plugins[i].faulted&&plugins[i].callbacks.id&&!strcmp(plugins[i].callbacks.id,id))return true;return false;}
static void load(HMODULE module,const std::filesystem::path& path,AnyBundlePartsExportV1 list){
 const AnyBundlePartV1* table=nullptr;uint32_t n=call_list(list,&table);auto bundle=utf8(path.stem());
 if(!table||!n||n>16){log_line(ANY_LOG_ERROR,"framework",("Bundle rejected (no parts): "+utf8(path.filename())).c_str());return;}
 for(uint32_t k=0;k<n;++k){auto& d=table[k];
  if(d.struct_size!=sizeof(AnyBundlePartV1)||d.abi!=ANYAPI_MOD_ABI||!d.id||!services::identifier(d.id)||!d.init){log_line(ANY_LOG_ERROR,"framework",("Bundle part rejected: "+bundle+" #"+std::to_string(k)).c_str());continue;}
  Part part{d.id,d.name&&*d.name?std::string(d.name).substr(0,63):std::string(d.id),bundle,0,d.flags,true};
  {std::lock_guard<std::mutex> lock(mutex);part.enabled_next=(d.flags&ANY_BUNDLE_PART_REQUIRED)||!is_off(part.id);}
  auto legacy=d.legacy_file&&*d.legacy_file?path.parent_path()/d.legacy_file:std::filesystem::path();std::error_code error;
  // The old standalone DLL wins while it is still installed (it may sort after the bundle), so nothing runs twice.
  if(running(d.id)||(!legacy.empty()&&std::filesystem::is_regular_file(legacy,error))){part.state=ANY_PART_LEGACY;log_line(ANY_LOG_WARN,d.id,("BUNDLE_PART_SKIPPED bundle="+bundle+" reason=standalone_dll_installed").c_str());}
  else if(!part.enabled_next){part.state=ANY_PART_OFF;log_line(ANY_LOG_INFO,d.id,("BUNDLE_PART_SKIPPED bundle="+bundle+" reason=switched_off").c_str());}
  // A part keeps its old DLL's name as its path, so menus keep their order.
  else part.state=start_mod(module,legacy.empty()?path:legacy,d.init,d.ready,true,bundle,part.name)?ANY_PART_LOADED:ANY_PART_FAILED;
  std::lock_guard<std::mutex> lock(mutex);parts.push_back(std::move(part));
 }
}
} // namespace bundles
// Discover mod DLLs. Reject unsupported exports/ABI; retain accepted modules for process lifetime.
static void load(){catalog_runtime::initialize();bundles::read();auto directory=g_game_dir/L"AnyAPI and Modding"/L"mods";std::error_code error;if(!std::filesystem::exists(directory,error))return;
 // error_code overloads: an unreadable mods folder or entry skips mods instead of throwing on the loader thread.
 std::vector<std::filesystem::path> paths;for(std::filesystem::directory_iterator it(directory,error),end;!error&&it!=end;it.increment(error)){std::error_code entry;if(it->is_regular_file(entry)&&it->path().extension()==L".dll")paths.push_back(it->path());}
 if(error)log_line(ANY_LOG_WARN,"framework",("MODS_FOLDER unreadable=1 error="+std::to_string(error.value())).c_str());std::sort(paths.begin(),paths.end());
 for(auto& path:paths){if(plugin_count.load()==plugins.size())break;HMODULE module=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);if(!module){log_line(ANY_LOG_ERROR,"framework",("Plugin DLL load failed: "+utf8(path.filename())).c_str());continue;}
  if(auto list=(AnyBundlePartsExportV1)GetProcAddress(module,"AnyAPI_BundleParts")){bundles::load(module,path,list);continue;}
  auto init=(AnyModInitV1)GetProcAddress(module,"AnyAPI_ModInit");if(!init){FreeLibrary(module);log_line(ANY_LOG_WARN,"framework",("Unsupported plugin export: "+utf8(path.filename())).c_str());continue;}
  start_mod(module,path,init,nullptr,false,utf8(path.filename()));
 }
}
// Resolve optional dependencies after every provider has initialized, independent of filename order.
static void safe_ready(Plugin* p,uint32_t& code){__try{auto ready=p->part?p->ready:(void(*)())GetProcAddress(p->module,"AnyAPI_ModReady");if(ready)ready();}__except(code=GetExceptionCode(),EXCEPTION_EXECUTE_HANDLER){}}
static void ready(){for(uint32_t i=0;i<plugin_count.load();++i){auto& p=plugins[i];if(p.faulted)continue;uint32_t code=0;current_plugin=int(i);safe_ready(&p,code);current_plugin=-1;if(code)fault(int(i),"ready",code);}}
} // namespace platform

static bool anyapi_owner_active(int owner){return owner>=0&&owner<int(platform::plugin_count.load())&&!platform::plugins[owner].faulted;}
static void anyapi_owner_fault(int owner,const char* where,uint32_t code){platform::fault(owner,where,code);}
static void anyapi_owner_fault(int owner){platform::fault(owner,"service_callback",0);}   // services.inc input filters, settings tabs, hud hints
static const void* anyapi_bundle_parts_service(){return &platform::bundles::api;}
