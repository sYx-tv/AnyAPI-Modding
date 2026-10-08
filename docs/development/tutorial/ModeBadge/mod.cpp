// ModeBadge: press F7 to show the current game mode in a corner of the screen.
#define NOMINMAX
#include <windows.h>
#include "anyapi_mod_v1.h"
#include "anyapi_services_v1.h"
#include "anyapi_gpu_draw_v1.h"
#include "anyapi_session_v1.h"
#include "anyapi_ui_state_v1.h"
#include "anyhelpers_settings_v1.h"
#include "mod_controls_v1.h"
#include <cmath>
#include <cwchar>
#include <mutex>

static constexpr const char* MOD_ID = "example.modebadge";

// Host and services. Optional ones stay null when absent.
static AnyModHostV1 host;
static const AnyGpuDrawV1* gpu;
static const AnyUiStateV1* ui;
static const AnySessionV1* session;
static const AnyHelpersSettingsV1* settings;
static const ModControlsV1* controls;

// Settings tokens, and the values the mod actually uses. These defaults apply
// when AnyHelpers is not installed.
static uint64_t enabled_token, corner_token, opacity_token, toggle_action;
static uint64_t settings_revision = UINT64_MAX;
static bool enabled = true;
static uint32_t corner = 0;     // 0 top left, 1 top right
static double opacity = 85;     // percent

// Render and input callbacks can run on different threads.
static std::mutex state_lock;
static bool visible, key_held;

template <class T> static const T* query(const AnyServicesV1* services, const char* id, uint32_t version) {
    auto table = static_cast<const T*>(services->query(id, version));
    return table && table->struct_size == sizeof(T) && table->version == version ? table : nullptr;
}

// Re-read committed settings only after AnyHelpers reports a change.
static void refresh_settings() {
    if (!settings) return;
    uint64_t revision = settings->revision();
    if (revision == settings_revision) return;
    settings_revision = revision;
    AnySettingValueV1 value;
    if (settings->get(enabled_token, &value)) enabled = value.number != 0;
    if (settings->get(corner_token, &value)) corner = value.number >= 1 ? 1 : 0;
    if (settings->get(opacity_token, &value) && std::isfinite(value.number))
        opacity = value.number < 10 ? 10 : value.number > 100 ? 100 : value.number;
}

static bool in_gameplay() {
    AnyUiSnapshotV1 state;
    return ui && ui->copy(&state) && state.kind == ANY_UI_GAMEPLAY;
}

static const wchar_t* mode_name() {
    AnySessionStateV1 state;
    if (!session || !session->copy(&state)) return L"Mode unknown";
    switch (state.mode) {
    case ANY_MODE_SURVIVAL: return L"Survival";
    case ANY_MODE_CAREER: return L"Career";
    case ANY_MODE_CREATIVE: return L"Creative";
    case ANY_MODE_SANDBOX: return L"Sandbox";
    default: return L"Mode unknown";
    }
}

static void render(const AnyFrameV1* frame, void*) {
    std::lock_guard lock(state_lock);
    refresh_settings();
    if (!frame || !frame->focused || !enabled || !visible || !in_gameplay()) return;

    const wchar_t* text = mode_name();
    const float width = 180, height = 40, margin = 16;
    const float x = corner == 1 ? float(frame->width) - width - margin : margin;

    AnyGpuCommandV1 box;
    box.kind = ANY_GPU_ROUND_RECT;
    box.rect[0] = x; box.rect[1] = margin; box.rect[2] = width; box.rect[3] = height;
    box.radius = 6;
    box.color = 0xff191c20;
    box.opacity = float(opacity / 100);
    gpu->emit(&box);

    AnyGpuCommandV1 label;
    label.kind = ANY_GPU_TEXT;
    label.flags = ANY_GPU_CENTER | ANY_GPU_VCENTER | ANY_GPU_NOWRAP;
    label.rect[0] = x; label.rect[1] = margin; label.rect[2] = width; label.rect[3] = height;
    label.font_size = 18;
    label.color = 0xfff5f7fa;
    label.text = text;
    label.text_length = uint32_t(wcslen(text));
    gpu->emit(&label);
}

static uint32_t input(const AnyInputV1* event, void*) {
    if (!event) return 0;
    std::lock_guard lock(state_lock);
    refresh_settings();
    if (event->kind == ANY_FOCUS_LOST) { key_held = false; return 0; }
    uint32_t key = controls ? controls->key(toggle_action) : VK_F7;
    if (!key || event->key != key) return 0;
    if (event->kind == ANY_KEY_UP) { bool consumed = key_held; key_held = false; return consumed; }
    if (event->kind != ANY_KEY_DOWN || !enabled || !in_gameplay()) return 0;
    if (!key_held) visible = !visible;   // ignore key repeat
    key_held = true;
    return 1;                            // consume, so the game does not also see F7
}

extern "C" __declspec(dllexport) bool AnyAPI_ModInit(const AnyModHostV1* h, AnyModCallbacksV1* callbacks) {
    if (!h || !callbacks || h->abi != ANYAPI_MOD_ABI || h->struct_size != sizeof(AnyModHostV1)) return false;
    host = *h;
    *callbacks = {};
    callbacks->id = MOD_ID;
    callbacks->input = input;
    return true;
}

extern "C" __declspec(dllexport) void AnyAPI_ModReady() {
    auto services = AnyAPI_Services();
    if (!services) return;

    // Required services: without them the mod logs why and stays idle.
    gpu = query<AnyGpuDrawV1>(services, "anyapi.gpu_draw", 1);
    ui = query<AnyUiStateV1>(services, "anyapi.ui_state", 1);
    session = query<AnySessionV1>(services, "anyapi.session", 1);
    if (!gpu || !gpu->emit || !gpu->register_renderer || !ui || !ui->copy || !session || !session->copy) {
        gpu = nullptr;
        if (host.log) host.log(2, MOD_ID, "Needs AnyAPI with gpu_draw, ui_state and session.");
        return;
    }

    // Optional keybind. The F7 default still works without AnyHelpers.
    controls = query<ModControlsV1>(services, "anyhelpers.controls", 1);
    if (controls && controls->register_action && controls->key) {
        ModControlActionV1 action;
        action.mod_id = MOD_ID; action.mod_name = "ModeBadge";
        action.action_id = "toggle"; action.label = "Show game mode";
        action.default_key = VK_F7;
        toggle_action = controls->register_action(&action);
    }
    if (!toggle_action) controls = nullptr;

    // Optional settings, shown under Mod Settings -> ModeBadge.
    settings = query<AnyHelpersSettingsV1>(services, "anyhelpers.settings", 1);
    if (settings && settings->register_setting && settings->get && settings->revision) {
        static const char* corners[] = {"Top left", "Top right"};
        AnyModSettingV1 setting;
        setting.mod_id = MOD_ID; setting.mod_name = "ModeBadge";

        setting.setting_id = "enabled"; setting.label = "Enable ModeBadge"; setting.order = 0;
        setting.kind = ANY_SETTING_BOOL; setting.default_number = 1;
        enabled_token = settings->register_setting(&setting);

        setting.setting_id = "corner"; setting.label = "Screen corner"; setting.order = 1;
        setting.kind = ANY_SETTING_CHOICE; setting.default_number = 0;
        setting.choices = corners; setting.choice_count = 2;
        corner_token = settings->register_setting(&setting);

        setting.setting_id = "opacity"; setting.label = "Background opacity (%)"; setting.order = 2;
        setting.kind = ANY_SETTING_INTEGER; setting.default_number = 85;
        setting.minimum = 10; setting.maximum = 100; setting.step = 5;
        setting.choices = nullptr; setting.choice_count = 0;
        opacity_token = settings->register_setting(&setting);
    } else {
        settings = nullptr;
    }

    if (!gpu->register_renderer(render, nullptr)) {
        gpu = nullptr;
        if (host.log) host.log(2, MOD_ID, "GPU renderer registration was rejected.");
        return;
    }
    if (host.log) host.log(0, MOD_ID, "Ready. Press F7 in a world to show the game mode.");
}
