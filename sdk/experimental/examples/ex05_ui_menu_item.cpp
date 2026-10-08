// Example 5: create and clean up a UI element.
//
// The game's UI toolkits are immediate-mode: widgets are emitted every frame from inside update calls,
// so "create" means emitting during the frame and "clean up" means no longer emitting (plus freeing any
// game strings you built). This example appends a button to the mm_ui menu bar by hooking
// client.update_ui_menu_bar and calling mm_ui.button after the original has emitted its own items.
// Uncertain (static inference from names): that update_ui_menu_bar runs inside an open menu bar each
// frame and that the menu bar is visible in retail builds (it may be a debug menu). Validate in game.
// Thread: main. Cleanup: example_ui_stop() removes the hook; the label string is freed every frame.
// Label: compile-tested, statically reviewed.
#include "example_common.hpp"

namespace {
using namespace example;
cell_hook h_menu;
std::atomic<int> g_clicks{0};
using menu_bar_t = void (*)(void* client, void* frontend_ui);
using button_t = void (*)(bool* ret, void* mm_ui, const gc_string_view* label);

void* g_ui_obj;           // g_ui (mm_ui) is a global object, not a ref
button_t g_button;

void on_menu_bar(void* client, void* fui) {
    ((menu_bar_t)h_menu.original)(client, fui);
    if (!g_ui_obj || !g_button) return;
    game_string label("SDK example");
    bool pressed = false;
    g_button(&pressed, g_ui_obj, &label.s);
    if (pressed) log("[ex05] clicked %d times", ++g_clicks);
}
}  // namespace

extern "C" bool example_ui_start() {
    if (!build_matches()) return false;
    g_ui_obj = resolve_global<void>(sym::g_ui);
    g_button = (button_t)resolve(sym::mm_ui_button);
    if (!g_ui_obj || !g_button) return false;
    return h_menu.install(cell_of(sym::client_update_ui_menu_bar), (void*)&on_menu_bar);
}
extern "C" void example_ui_stop() { h_menu.remove((void*)&on_menu_bar); }
