// Generic DLL loading, Windows input and DXGI/D3D12 frame composition.
// This service contains no map filenames, keybinds, road paths or atlas policy.
#include "anyapi_mod_v1.h"
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
struct Plugin {HMODULE module{};std::filesystem::path path;std::wstring game_path,plugin_path;AnyModHostV1 host{};AnyModCallbacksV1 callbacks{};ComPtr<ID2D1Bitmap1> bitmap;uint64_t revision{};uint32_t width{},height{};std::atomic<bool> faulted{};};
static std::array<Plugin,32> plugins;static std::atomic<uint32_t> plugin_count{};
static std::atomic<int> capture_owner{-1};
static HWND game_window{};static WNDPROC window_original{};
static bool raw_mouse_only{},raw_keyboard_only{};static AnyTextDecoder text_decoder;
static std::atomic<bool> enabled{true};static std::atomic<uint64_t> frames{};
static SRWLOCK hook_lock=SRWLOCK_INIT;
struct Hook {void** slot{};void* original{};void* replacement{};};
static Hook hooks[64]{};static size_t hook_count{};
static std::recursive_mutex render_mutex;
static std::mutex queue_mutex;
static ComPtr<ID3D12CommandQueue> observed_queue;
static std::unordered_map<IDXGISwapChain3*,ComPtr<ID3D12CommandQueue>> presentation_queues;
static ComPtr<IDXGISwapChain3> swap;
static ComPtr<ID3D12CommandQueue> queue;
static ComPtr<ID3D11Device> device11;static ComPtr<ID3D11DeviceContext> context11;
static ComPtr<ID3D11On12Device> on12;static ComPtr<ID2D1Factory1> factory2d;
static ComPtr<ID2D1Device> device2d;static ComPtr<ID2D1DeviceContext> context2d;
static std::vector<ComPtr<ID3D11Resource>> wrapped;
static std::vector<ComPtr<ID2D1Bitmap1>> targets;
static ComPtr<ID3D12Fence> fence;static uint64_t fence_value{};
static HANDLE fence_event{};static uint64_t retry_at{};
static bool reported_frame{},reported_graphics{};
static decltype(&SetCursorPos) real_set_cursor_pos{};
static decltype(&ClipCursor) real_clip_cursor{};
static decltype(&SetCursor) real_set_cursor{};
static decltype(&GetKeyState) real_key_state{};
static decltype(&GetRawInputData) real_raw_input{};
}
#include "anyapi_gpu_draw.inc"
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

// Publish the original COM slot before atomically replacing it; no instruction patching.
static bool hook_slot(void* object,size_t index,void* replacement){
 auto slot=(*(void***)object)+index;AcquireSRWLockExclusive(&hook_lock);
 for(size_t i=0;i<hook_count;++i)if(hooks[i].slot==slot){bool ok=hooks[i].replacement==replacement;ReleaseSRWLockExclusive(&hook_lock);return ok;}
 if(hook_count==std::size(hooks)){ReleaseSRWLockExclusive(&hook_lock);return false;}
 DWORD old{};if(!VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&old)){ReleaseSRWLockExclusive(&hook_lock);return false;}
 void* original=*slot;hooks[hook_count++]={slot,original,replacement};
 bool ok=InterlockedCompareExchangePointer(slot,replacement,original)==original;
 DWORD ignored{};VirtualProtect(slot,sizeof(void*),old,&ignored);
 if(!ok)--hook_count;ReleaseSRWLockExclusive(&hook_lock);return ok;
}
// Find the chained method for this exact COM table, preserving other overlays' hooks.
template<class T> static T original(void* object,size_t index){auto slot=(*(void***)object)+index;void* fn{};AcquireSRWLockShared(&hook_lock);
 for(size_t i=0;i<hook_count;++i)if(hooks[i].slot==slot){fn=hooks[i].original;break;}ReleaseSRWLockShared(&hook_lock);return (T)fn;}
// Each plugin can capture input only for itself, and can only release its own capture.
static void capture(uint32_t active){if(current_plugin<0)return;if(active)capture_owner.store(current_plugin);else {int expected=current_plugin;capture_owner.compare_exchange_strong(expected,-1);}}
// Invoke a plugin input callback inside a native exception boundary; mark it disabled on a fault.
static uint32_t call_input(Plugin* p,const AnyInputV1* e){__try{return p->callbacks.input?p->callbacks.input(e,p->callbacks.user):0;}__except(EXCEPTION_EXECUTE_HANDLER){p->faulted=true;return 0;}}
// Invoke a plugin render callback inside a native exception boundary; mark it disabled on a fault.
static bool call_render(Plugin* p,const AnyFrameV1* f,AnyCanvasV1* c){__try{if(p->callbacks.render)p->callbacks.render(f,c,p->callbacks.user);return true;}__except(EXCEPTION_EXECUTE_HANDLER){p->faulted=true;return false;}}
// Invoke plugin initialization inside a native exception boundary.
static bool call_init(AnyModInitV1 init,const AnyModHostV1* host,AnyModCallbacksV1* callbacks){__try{return init(host,callbacks);}__except(EXCEPTION_EXECUTE_HANDLER){return false;}}
// Dispatch input to active DLLs without running their code under a loader lock.
static bool input(const AnyInputV1& e){if(services::input(e)&&e.kind!=ANY_FOCUS_LOST)return true;bool used=false;auto count=plugin_count.load();for(uint32_t i=0;i<count;++i){auto& p=plugins[i];if(p.faulted)continue;current_plugin=int(i);used=call_input(&p,&e)!=0||used;current_plugin=-1;if(p.faulted){capture_owner.store(-1);log_line(ANY_LOG_ERROR,p.callbacks.id,"Plugin input disabled after exception");}}return used;}
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
 if(m==WM_KILLFOCUS||m==WM_ACTIVATEAPP&&!w){capture_owner.store(-1);text_decoder.reset();e.kind=ANY_FOCUS_LOST;event=true;}
 else if(m==WM_UNICHAR&&w==UNICODE_NOCHAR)return TRUE;
 else if(m==WM_CHAR||m==WM_UNICHAR){if(GetForegroundWindow()==h){bool consumed=text_input(uint32_t(w));if(consumed||capture_owner.load()>=0)return 0;}}
 else if(m==WM_KEYDOWN||m==WM_SYSKEYDOWN||m==WM_KEYUP||m==WM_SYSKEYUP){e.kind=(m==WM_KEYDOWN||m==WM_SYSKEYDOWN)?ANY_KEY_DOWN:ANY_KEY_UP;e.key=uint32_t(w);event=true;}
 else if(m==WM_MOUSEMOVE||m==WM_LBUTTONDOWN||m==WM_LBUTTONUP||m==WM_RBUTTONDOWN||m==WM_RBUTTONUP||m==WM_MOUSEWHEEL){
  e.x=GET_X_LPARAM(l);e.y=GET_Y_LPARAM(l);event=true;
  if(m==WM_MOUSEWHEEL){POINT p{e.x,e.y};ScreenToClient(h,&p);e.x=p.x;e.y=p.y;e.kind=ANY_MOUSE_WHEEL;e.wheel=GET_WHEEL_DELTA_WPARAM(w);}
  else if(m==WM_MOUSEMOVE)e.kind=ANY_MOUSE_MOVE;
  else {e.kind=(m==WM_LBUTTONDOWN||m==WM_RBUTTONDOWN)?ANY_MOUSE_DOWN:ANY_MOUSE_UP;e.button=(m==WM_LBUTTONDOWN||m==WM_LBUTTONUP)?1:2;}
 }else if(m==WM_INPUT){UINT bytes=sizeof(RAWINPUT);RAWINPUT r{};UINT got=real_raw_input((HRAWINPUT)l,RID_INPUT,&r,&bytes,sizeof(RAWINPUTHEADER));
  if(got!=UINT(-1)&&got>=sizeof(RAWINPUTHEADER)+sizeof(RAWKEYBOARD)&&r.header.dwType==RIM_TYPEKEYBOARD){e.kind=(r.data.keyboard.Flags&RI_KEY_BREAK)?ANY_KEY_UP:ANY_KEY_DOWN;e.key=r.data.keyboard.VKey;event=true;if(GetForegroundWindow()==h)raw_used=raw_text(r.data.keyboard);}
  else if(got!=UINT(-1)&&got>=sizeof(RAWINPUTHEADER)+sizeof(RAWMOUSE)&&r.header.dwType==RIM_TYPEMOUSE&&raw_mouse_only&&GetForegroundWindow()==h){
   POINT p{};GetCursorPos(&p);ScreenToClient(h,&p);raw_used=raw_mouse(r.data.mouse,p.x,p.y);raw_release=(r.data.mouse.usButtonFlags&(RI_MOUSE_LEFT_BUTTON_UP|RI_MOUSE_RIGHT_BUTTON_UP|RI_MOUSE_MIDDLE_BUTTON_UP|RI_MOUSE_BUTTON_4_UP|RI_MOUSE_BUTTON_5_UP))!=0;
  }}
 bool used=(event&&(e.kind==ANY_FOCUS_LOST||GetForegroundWindow()==h)&&input(e))||raw_used;
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
 log_line(ANY_LOG_INFO,"framework","PLUGIN_INPUT game_window=ATTACHED overlay_windows=0");}
// Wait only during resource teardown; every submitted overlay uses the game's direct queue.
static bool idle(){if(!queue||!fence)return true;if(FAILED(queue->Signal(fence.Get(),++fence_value)))return false;
 if(fence->GetCompletedValue()>=fence_value)return true;if(!fence_event)fence_event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
 return fence_event&&SUCCEEDED(fence->SetEventOnCompletion(fence_value,fence_event))&&WaitForSingleObject(fence_event,3000)==WAIT_OBJECT_0;}
// Release all references to back buffers before forwarding ResizeBuffers.
static void reset(){if(context2d)context2d->SetTarget(nullptr);if(context11){context11->ClearState();context11->Flush();}idle();gpu_draw::reset();
 for(auto& p:plugins){p.bitmap.Reset();p.width=p.height=0;p.revision=0;}targets.clear();wrapped.clear();context2d.Reset();device2d.Reset();factory2d.Reset();on12.Reset();context11.Reset();device11.Reset();swap.Reset();queue.Reset();fence.Reset();fence_value=0;}
// Build a private 11-on-12/Direct2D context around this game's swapchain and command queue.
static bool initialize(IDXGISwapChain* chain,ID3D12CommandQueue* q){
 reset();ComPtr<ID3D12Device> device;ComPtr<IDXGISwapChain3> next;DXGI_SWAP_CHAIN_DESC desc{};
 if(FAILED(chain->QueryInterface(IID_PPV_ARGS(&next)))||FAILED(chain->GetDevice(IID_PPV_ARGS(&device)))||FAILED(chain->GetDesc(&desc))||!desc.BufferCount||desc.BufferCount>16)return false;
 ComPtr<ID3D12Device> qdevice;if(FAILED(q->GetDevice(IID_PPV_ARGS(&qdevice)))||qdevice.Get()!=device.Get())return false;
 queue=q;swap=next;IUnknown* queues[]={q};
 if(FAILED(D3D11On12CreateDevice(device.Get(),D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,queues,1,0,&device11,&context11,nullptr))||FAILED(device11.As(&on12)))return false;
 D2D1_FACTORY_OPTIONS options{};ComPtr<IDXGIDevice> dxgi;
 if(FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED,__uuidof(ID2D1Factory1),&options,(void**)factory2d.GetAddressOf()))||FAILED(device11.As(&dxgi))||FAILED(factory2d->CreateDevice(dxgi.Get(),&device2d))||FAILED(device2d->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,&context2d)))return false;
 auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,D2D1::PixelFormat(desc.BufferDesc.Format,D2D1_ALPHA_MODE_IGNORE),96,96);
 wrapped.resize(desc.BufferCount);targets.resize(desc.BufferCount);D3D11_RESOURCE_FLAGS flags{D3D11_BIND_RENDER_TARGET};
 for(UINT i=0;i<desc.BufferCount;++i){ComPtr<ID3D12Resource> buffer;ComPtr<IDXGISurface> surface;
  if(FAILED(chain->GetBuffer(i,IID_PPV_ARGS(&buffer)))||FAILED(on12->CreateWrappedResource(buffer.Get(),&flags,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_PRESENT,IID_PPV_ARGS(&wrapped[i])))||FAILED(wrapped[i].As(&surface))||FAILED(context2d->CreateBitmapFromDxgiSurface(surface.Get(),&props,&targets[i])))return false;
 }
 if(FAILED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))))return false;attach_window(desc.OutputWindow);
 if(!reported_graphics){log_line(ANY_LOG_INFO,"framework","PLUGIN_RENDER backend=D3D12_11ON12_D2D state=PRESENT_TO_PRESENT game_backbuffer=READY");reported_graphics=true;}return true;
}
// Validate a plugin bitmap before passing it to the graphics driver.
static bool valid_canvas(const AnyCanvasV1& c){return c.struct_size==sizeof(c)&&c.pixels&&c.width&&c.height&&c.width<=8192&&c.height<=8192&&c.pitch>=c.width*4&&c.pitch<=131072&&uint64_t(c.pitch)*c.height<=256ull*1024*1024;}
// Compose each DLL's canvas into the real back buffer immediately before presentation.
static void draw(IDXGISwapChain* chain){if(!enabled||!plugin_count.load())return;std::lock_guard<std::recursive_mutex> lock(render_mutex);
 DXGI_SWAP_CHAIN_DESC desc{};if(FAILED(chain->GetDesc(&desc)))return;DWORD pid{};GetWindowThreadProcessId(desc.OutputWindow,&pid);if(pid!=GetCurrentProcessId())return;
 ++frames; // Native-only mods still need a presentation clock when no canvas is drawn.
 if(auto state=anyapi_ui_state_service()){AnyUiSnapshotV1 snapshot;state->copy(&snapshot);}
 if(!swap||swap.Get()!=chain||!context2d||targets.empty()){
  uint64_t now=GetTickCount64();if(now<retry_at)return;retry_at=now+1000;ComPtr<ID3D12CommandQueue> q;{std::lock_guard<std::mutex> qlock(queue_mutex);ComPtr<IDXGISwapChain3> identity;chain->QueryInterface(IID_PPV_ARGS(&identity));auto found=presentation_queues.find(identity.Get());q=found!=presentation_queues.end()?found->second:observed_queue;}
  if(!q||!initialize(chain,q.Get())){reset();return;}
 }
 auto index=swap->GetCurrentBackBufferIndex();if(index>=targets.size())return;
 AnyFrameV1 frame{};frame.width=desc.BufferDesc.Width;frame.height=desc.BufferDesc.Height;frame.tick=GetTickCount64();frame.focused=GetForegroundWindow()==game_window;
 if(!frame.focused)capture_owner.store(-1);
 // All callbacks execute before acquiring a wrapped resource, so plugin faults cannot strand it.
 std::array<AnyCanvasV1,32> canvases{};std::array<bool,32> drawable{};bool any=false;uint32_t count=plugin_count.load();
 for(uint32_t i=0;i<count;++i){auto& p=plugins[i];if(p.faulted)continue;current_plugin=int(i);bool ok=call_render(&p,&frame,&canvases[i]);current_plugin=-1;
  if(!ok){capture_owner.store(-1);log_line(ANY_LOG_ERROR,p.callbacks.id,"Plugin render disabled after exception");continue;}
  current_plugin=int(i);bool commands=gpu_draw::collect(i,frame);current_plugin=-1;if(commands)any=true;
  auto& c=canvases[i];if(!valid_canvas(c))continue;
  if(!p.bitmap||p.width!=c.width||p.height!=c.height){p.bitmap.Reset();p.revision=~c.revision;auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
   if(FAILED(context2d->CreateBitmap(D2D1::SizeU(c.width,c.height),nullptr,0,&props,&p.bitmap)))continue;p.width=c.width;p.height=c.height;}
  if(p.revision!=c.revision){if(FAILED(p.bitmap->CopyFromMemory(nullptr,c.pixels,c.pitch)))continue;p.revision=c.revision;}drawable[i]=true;any=true;
 }
 if(!any)return;ID3D11Resource* resource=wrapped[index].Get();on12->AcquireWrappedResources(&resource,1);
 context2d->SetTarget(targets[index].Get());context2d->BeginDraw();context2d->SetTransform(D2D1::Matrix3x2F::Identity());
 for(uint32_t i=0;i<count;++i){auto& c=canvases[i];auto& p=plugins[i];if(p.faulted||!drawable[i])continue;
  auto rect=D2D1::RectF(float(c.x),float(c.y),float(c.x)+c.width,float(c.y)+c.height);context2d->DrawBitmap(p.bitmap.Get(),&rect,1,D2D1_INTERPOLATION_MODE_LINEAR,nullptr);}
 for(uint32_t i=0;i<count;++i)if(!plugins[i].faulted&&!gpu_draw::renderers[i].commands.empty())gpu_draw::render(i);
 HRESULT hr=context2d->EndDraw();context2d->SetTarget(nullptr);on12->ReleaseWrappedResources(&resource,1);context11->Flush();
 if(FAILED(hr)){retry_at=GetTickCount64()+5000;reset();log_line(ANY_LOG_ERROR,"framework","PLUGIN_RENDER frame=FAILED retry_seconds=5");return;}
 if(!reported_frame){reported_frame=true;log_line(ANY_LOG_INFO,"framework","PLUGIN_RENDER game_backbuffer=DRAWN helper_processes=0");}
 if(capture_owner.load()>=0){ClipCursor(nullptr);SetCursor(LoadCursorW(nullptr,MAKEINTRESOURCEW(32512)));}
}
using PresentFn=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*,UINT,UINT);
using Present1Fn=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*,UINT,UINT,const DXGI_PRESENT_PARAMETERS*);
using ResizeFn=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*,UINT,UINT,UINT,DXGI_FORMAT,UINT);
using Resize1Fn=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3*,UINT,UINT,UINT,DXGI_FORMAT,UINT,const UINT*,IUnknown*const*);
using ExecuteFn=void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*,UINT,ID3D12CommandList*const*);
static thread_local bool presenting{};
// Chain DXGI Present and draw plugin canvases before normal presentation; skip test-only presents.
static HRESULT STDMETHODCALLTYPE present(IDXGISwapChain* s,UINT interval,UINT flags){auto fn=original<PresentFn>(s,8);if(!fn)return E_FAIL;
 if(!presenting&&!(flags&DXGI_PRESENT_TEST)){presenting=true;try{draw(s);}catch(...){capture_owner.store(-1);log_line(ANY_LOG_ERROR,"framework","PLUGIN_RENDER exception=CPP frame_skipped=1");}presenting=false;}return fn(s,interval,flags);}
// Chain DXGI Present1 with the same plugin composition behavior.
static HRESULT STDMETHODCALLTYPE present1(IDXGISwapChain1* s,UINT interval,UINT flags,const DXGI_PRESENT_PARAMETERS* params){auto fn=original<Present1Fn>(s,22);if(!fn)return E_FAIL;
 if(!presenting&&!(flags&DXGI_PRESENT_TEST)){presenting=true;try{draw(s);}catch(...){capture_owner.store(-1);}presenting=false;}return fn(s,interval,flags,params);}
// Release overlay back-buffer references, forward ResizeBuffers and rebuild on a subsequent frame.
static HRESULT STDMETHODCALLTYPE resize(IDXGISwapChain* s,UINT count,UINT w,UINT h,DXGI_FORMAT format,UINT flags){auto fn=original<ResizeFn>(s,13);if(!fn)return E_FAIL;std::lock_guard<std::recursive_mutex> lock(render_mutex);if(swap.Get()==s)reset();retry_at=0;return fn(s,count,w,h,format,flags);}
// Handle extended ResizeBuffers1 with the same resource teardown behavior.
static HRESULT STDMETHODCALLTYPE resize1(IDXGISwapChain3* s,UINT count,UINT w,UINT h,DXGI_FORMAT format,UINT flags,const UINT* masks,IUnknown*const* queues){auto fn=original<Resize1Fn>(s,39);if(!fn)return E_FAIL;std::lock_guard<std::recursive_mutex> lock(render_mutex);if(swap.Get()==s)reset();retry_at=0;return fn(s,count,w,h,format,flags,masks,queues);}
// Observe direct command-queue submissions for graphics discovery, then forward the original submission.
static void STDMETHODCALLTYPE execute(ID3D12CommandQueue* q,UINT count,ID3D12CommandList*const* lists){auto fn=original<ExecuteFn>(q,10);if(!fn)return;
 if(q->GetDesc().Type==D3D12_COMMAND_LIST_TYPE_DIRECT){std::lock_guard<std::mutex> lock(queue_mutex);if(observed_queue.Get()!=q)observed_queue=q;}fn(q,count,lists);}
// Install presentation and resize callbacks on the interfaces supported by a swapchain.
static bool hook_chain(IDXGISwapChain* chain){bool ok=hook_slot(chain,8,(void*)present)&&hook_slot(chain,13,(void*)resize);ComPtr<IDXGISwapChain1> one;ComPtr<IDXGISwapChain3> three;
 if(SUCCEEDED(chain->QueryInterface(IID_PPV_ARGS(&one))))ok=hook_slot(one.Get(),22,(void*)present1)&&ok;
 if(SUCCEEDED(chain->QueryInterface(IID_PPV_ARGS(&three))))ok=hook_slot(three.Get(),39,(void*)resize1)&&ok;return ok;}
using CreateFn=HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory*,IUnknown*,DXGI_SWAP_CHAIN_DESC*,IDXGISwapChain**);
using CreateHwndFn=HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*,IUnknown*,HWND,const DXGI_SWAP_CHAIN_DESC1*,const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*,IDXGIOutput*,IDXGISwapChain1**);
// The factory's D3D12 pDevice parameter is the actual presentation queue, not a device.
static void created(IUnknown* d,IDXGISwapChain* s){ComPtr<ID3D12CommandQueue> q;ComPtr<IDXGISwapChain3> identity;if(SUCCEEDED(d->QueryInterface(IID_PPV_ARGS(&q)))){std::lock_guard<std::mutex> lock(queue_mutex);observed_queue=q;if(SUCCEEDED(s->QueryInterface(IID_PPV_ARGS(&identity))))presentation_queues[identity.Get()]=q;}hook_chain(s);}
// Chain legacy DXGI swapchain creation and record successful results.
static HRESULT STDMETHODCALLTYPE create(IDXGIFactory* f,IUnknown* d,DXGI_SWAP_CHAIN_DESC* desc,IDXGISwapChain** out){auto fn=original<CreateFn>(f,10);if(!fn)return E_FAIL;HRESULT hr=fn(f,d,desc,out);if(SUCCEEDED(hr)&&out&&*out)created(d,*out);return hr;}
// Chain window-based DXGI swapchain creation and record successful results.
static HRESULT STDMETHODCALLTYPE create_hwnd(IDXGIFactory2* f,IUnknown* d,HWND h,const DXGI_SWAP_CHAIN_DESC1* desc,const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* full,IDXGIOutput* restrict,IDXGISwapChain1** out){auto fn=original<CreateHwndFn>(f,15);if(!fn)return E_FAIL;HRESULT hr=fn(f,d,h,desc,full,restrict,out);if(SUCCEEDED(hr)&&out&&*out)created(d,*out);return hr;}
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
// Discover ordinary mod DLLs. Reject unsupported exports/ABI; retain accepted modules for process lifetime.
static void load(){catalog_runtime::initialize();auto directory=g_game_dir/L"AnyAPI and Modding"/L"mods";if(!std::filesystem::exists(directory))return;
 std::vector<std::filesystem::path> paths;for(auto& entry:std::filesystem::directory_iterator(directory))if(entry.is_regular_file()&&entry.path().extension()==L".dll")paths.push_back(entry.path());std::sort(paths.begin(),paths.end());
 for(auto& path:paths){auto index=plugin_count.load();if(index==plugins.size())break;HMODULE module=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);if(!module){log_line(ANY_LOG_ERROR,"framework",("Plugin DLL load failed: "+utf8(path.filename())).c_str());continue;}
  auto init=(AnyModInitV1)GetProcAddress(module,"AnyAPI_ModInit");if(!init){FreeLibrary(module);log_line(ANY_LOG_WARN,"framework",("Unsupported plugin export: "+utf8(path.filename())).c_str());continue;}
  auto& p=plugins[index];p.module=module;p.path=path;p.callbacks={};p.faulted=false;p.game_path=g_game_dir.wstring();p.plugin_path=path.parent_path().wstring();p.host.game_directory=p.game_path.c_str();p.host.plugin_directory=p.plugin_path.c_str();p.host.log=[](uint32_t level,const char* id,const char* msg){log_line(AnymakerLogLevel(level),id,msg);};p.host.copy_players=p34_copy_snapshot;p.host.capture_input=capture;
  current_plugin=int(index);bool ok=false;try{ok=call_init(init,&p.host,&p.callbacks);}catch(...){ok=false;}current_plugin=-1;
  if(!ok||p.callbacks.struct_size!=sizeof(p.callbacks)||p.callbacks.abi!=ANYAPI_MOD_ABI||!p.callbacks.id||!services::identifier(p.callbacks.id)){log_line(ANY_LOG_ERROR,"framework",("Plugin initialization rejected: "+utf8(path.filename())).c_str());p.faulted=true;plugin_count.store(index+1);continue;}
  plugin_count.store(index+1);log_line(ANY_LOG_INFO,p.callbacks.id,"DLL plugin initialized; lifecycle=PROCESS_LIFETIME");
 }
}
// Resolve optional dependencies after every provider has initialized, independent of filename order.
static void safe_ready(Plugin* p){__try{auto ready=(void(*)())GetProcAddress(p->module,"AnyAPI_ModReady");if(ready)ready();}__except(EXCEPTION_EXECUTE_HANDLER){p->faulted=true;log_line(ANY_LOG_ERROR,p->callbacks.id,"MOD_READY disabled_after_exception=1");}}
static void ready(){for(uint32_t i=0;i<plugin_count.load();++i){auto& p=plugins[i];if(p.faulted)continue;current_plugin=int(i);safe_ready(&p);current_plugin=-1;}}
} // namespace platform

static bool anyapi_owner_active(int owner){return owner>=0&&owner<int(platform::plugin_count.load())&&!platform::plugins[owner].faulted;}
static void anyapi_owner_fault(int owner){if(owner<0||owner>=int(platform::plugin_count.load()))return;platform::plugins[owner].faulted=true;int expected=owner;platform::capture_owner.compare_exchange_strong(expected,-1);}
