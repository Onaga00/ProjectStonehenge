#include "platform/debug/overlay.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "build_info.h"
#include "common/debug_hooks.h"
#include "common/guest_memory.h"
#include "platform/log.h"
#include "platform/paths.h"
#include "platform/render/renderer.h"

namespace ac04::platform {
namespace {

// MainContext fields read by the scene inspector (docs/game/boot.md).
constexpr u32 kMainContextPointer = 0x004104A8;  // g_pMainContext
constexpr u32 kGameStatePointer = 0x004104A4;    // g_GameState

const char* const kSceneNames[8] = {"SceneBootLoader", "SceneMenuEntry (dead)", "SceneFlight", "SceneMovie (dead)",
                                    "SceneMenu", "SceneResults", "SceneStartup", "SceneHddError"};

// Read-only guest access: RAM and scratchpad only. Hardware registers are never read from the
// overlay (some reads have side effects, e.g. timer 0).
bool Peek(u32 address, void* dst, u32 size) {
    const u8* p = guest::Mem().Translate(address, size);
    if (p == nullptr) return false;
    std::memcpy(dst, p, size);
    return true;
}

template <typename T>
T PeekOr(u32 address, T fallback) {
    T v{};
    return Peek(address, &v, sizeof(T)) ? v : fallback;
}

void CheckVk(VkResult r) {
    if (r != VK_SUCCESS) log::WriteOnce("overlay:vk", log::Level::Error, "overlay", "ImGui Vulkan error %d", r);
}

// UI scale from the window height: 1 up to 1080 pixels, 2 at 2160 (quarter steps), so the panels
// keep the same share of the screen at higher resolutions.
float UiScale(SDL_Window* window) {
    int w = 0, h = 0;
    if (window == nullptr || !SDL_GetWindowSizeInPixels(window, &w, &h) || h <= 0) return 1.0f;
    return std::max(1.0f, std::floor(static_cast<float>(h) / 1080.0f * 4.0f) / 4.0f);
}

bool ContainsNoCase(const std::string& text, const char* needle) {
    const size_t n = std::strlen(needle);
    if (n == 0) return true;
    auto it = std::search(text.begin(), text.end(), needle, needle + n, [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    });
    return it != text.end();
}

void ApplySquareStyle(float scale) {
    ImGui::GetStyle() = ImGuiStyle();
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 0.0f;
    s.ChildRounding = 0.0f;
    s.FrameRounding = 0.0f;
    s.PopupRounding = 0.0f;
    s.ScrollbarRounding = 0.0f;
    s.GrabRounding = 0.0f;
    s.TabRounding = 0.0f;
    s.WindowBorderSize = 1.0f;
    s.Colors[ImGuiCol_WindowBg].w = 0.88f;
    s.ScaleAllSizes(scale);
    s.FontScaleDpi = scale;
}

}  // namespace

DebugOverlay::DebugOverlay() = default;
DebugOverlay::~DebugOverlay() { Shutdown(); }

bool DebugOverlay::Apply(const DebugSettings& settings, SDL_Window* window, render::Renderer& renderer,
                         std::string& error) {
    // Toggle: a typed character, or "key:<scancode name>".
    toggle_scancode_ = 0;
    toggle_text_ = settings.overlay_toggle.empty() ? "\\" : settings.overlay_toggle;
    if (toggle_text_.rfind("key:", 0) == 0) {
        toggle_scancode_ = SDL_GetScancodeFromName(toggle_text_.c_str() + 4);
        if (toggle_scancode_ == SDL_SCANCODE_UNKNOWN) {
            AC04_LOG_WARN("overlay", "unknown overlay toggle key '%s'; using '\\'", toggle_text_.c_str());
            toggle_text_ = "\\";
        }
    }
    panels_ = settings.overlay_enabled;
    fps_ = settings.fps_overlay;
    if (!panels_) visible_ = false;
    if (!panels_ && !fps_) {
        Shutdown();
        return true;
    }
    if (active_) {
        if (panels_ && symbols_source_.empty()) LoadSymbols();
        return true;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // nothing written next to the executable or the working directory
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.Fonts->AddFontDefaultVector();  // scalable, stays sharp at the UI scale
    window_ = window;
    scale_ = UiScale(window);
    ApplySquareStyle(scale_);

    if (!ImGui_ImplSDL3_InitForVulkan(window)) {
        error = "ImGui SDL3 backend failed";
        ImGui::DestroyContext();
        return false;
    }
    render::VkContext& vk = renderer.Context();
    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = VK_API_VERSION_1_2;
    info.Instance = vk.instance;
    info.PhysicalDevice = vk.physical_device;
    info.Device = vk.device;
    info.QueueFamily = vk.queue_family;
    info.Queue = vk.queue;
    info.DescriptorPoolSize = 16;
    info.MinImageCount = 2;
    info.ImageCount = std::max<uint32_t>(2, renderer.SwapchainImageCount());
    info.PipelineInfoMain.RenderPass = renderer.OutputRenderPass();
    info.PipelineInfoMain.Subpass = 0;
    info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    info.CheckVkResultFn = CheckVk;
    if (!ImGui_ImplVulkan_Init(&info)) {
        error = "ImGui Vulkan backend failed";
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        return false;
    }
    renderer_ = &renderer;
    last_render_pass_ = renderer.OutputRenderPass();
    renderer.SetOverlayHook([this](VkCommandBuffer cmd) { Draw(cmd); });
    active_ = true;
    visible_ = false;
    if (!panels_) return true;  // frame-rate overlay only
    LoadSymbols();
    AC04_LOG_INFO("overlay", "debug overlay ready (toggle '%s'); %zu symbols from %s", toggle_text_.c_str(),
                  symbols_.size(), symbols_source_.empty() ? "nowhere" : symbols_source_.c_str());
    return true;
}

void DebugOverlay::Shutdown() {
    if (!active_) return;
    if (renderer_ != nullptr) {
        vkDeviceWaitIdle(renderer_->Context().device);
        renderer_->SetOverlayHook(nullptr);
    }
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    active_ = false;
    visible_ = false;
    frame_built_ = false;
    renderer_ = nullptr;
    window_ = nullptr;
}

bool DebugOverlay::HandleEvent(const SDL_Event& event) {
    if (!active_ || !panels_) return false;
    const bool toggle =
        toggle_scancode_ != 0
            ? (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.scancode == toggle_scancode_)
            : (event.type == SDL_EVENT_TEXT_INPUT && event.text.text != nullptr && toggle_text_ == event.text.text);
    if (toggle) {
        visible_ = !visible_;
        // Opening the panels must not take the keyboard from the game: no panel is focused until
        // the user clicks one.
        if (visible_) unfocus_pending_ = true;
        return true;
    }
    if (!visible_) return false;
    ImGui_ImplSDL3_ProcessEvent(&event);
    const ImGuiIO& io = ImGui::GetIO();
    switch (event.type) {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
        case SDL_EVENT_TEXT_INPUT:
            return io.WantCaptureKeyboard;
        case SDL_EVENT_MOUSE_MOTION:
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        case SDL_EVENT_MOUSE_WHEEL:
            return io.WantCaptureMouse;
        default:
            return false;
    }
}

bool DebugOverlay::WantsKeyboard() const { return Visible() && ImGui::GetIO().WantCaptureKeyboard; }

void DebugOverlay::BuildFrame(const OverlayFrameData& data) {
    frame_built_ = false;
    if (!active_) return;
    // Frame-time history keeps running while the overlay is hidden.
    frame_ms_[frame_ms_pos_] = static_cast<float>(data.frame_seconds * 1000.0);
    frame_ms_pos_ = (frame_ms_pos_ + 1) % frame_ms_.size();
    if (!visible_ && !fps_) return;

    // The output render pass is recreated when the swapchain format changes.
    if (renderer_->OutputRenderPass() != last_render_pass_) {
        ImGui_ImplVulkan_PipelineInfo pi{};
        pi.RenderPass = renderer_->OutputRenderPass();
        pi.Subpass = 0;
        pi.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        ImGui_ImplVulkan_CreateMainPipeline(&pi);
        last_render_pass_ = renderer_->OutputRenderPass();
    }

    // Follow window size changes (fullscreen, windowed resize) with the UI scale.
    const float scale = UiScale(window_);
    if (scale != scale_) {
        scale_ = scale;
        ApplySquareStyle(scale_);
    }

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    if (fps_) DrawFps();
    if (!visible_) {
        EndFrame();
        return;
    }
    ImGui::SetNextWindowPos(Scaled(8, 8), ImGuiCond_FirstUseEver);
    ImGui::Begin("Debug", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::TextUnformatted("Debug layer panels (Graphics: presentation only)");
    ImGui::Checkbox("Performance", &show_perf_);
    ImGui::SameLine();
    ImGui::Checkbox("Scene", &show_scene_);
    ImGui::SameLine();
    ImGui::Checkbox("Memory", &show_memory_);
    ImGui::SameLine();
    ImGui::Checkbox("Log", &show_log_);
    ImGui::Checkbox("Graphics", &show_graphics_);
    ImGui::SameLine();
    ImGui::Checkbox("Input", &show_input_);
    ImGui::TextDisabled("Toggle: %s", toggle_text_.c_str());
    ImGui::End();

    if (show_perf_) DrawPerformance(data);
    if (show_scene_) DrawScene();
    if (show_memory_) DrawMemory();
    if (show_log_) DrawLog();
    if (show_graphics_) DrawGraphics(data);
    if (show_input_) DrawInput(data);
    if (unfocus_pending_) {
        ImGui::SetWindowFocus(nullptr);
        unfocus_pending_ = false;
    }
    EndFrame();
}

void DebugOverlay::EndFrame() {
    ImGui::Render();
    frame_built_ = true;
    // imgui_impl_sdl3 stops SDL text input when a text field loses focus; the toggle character
    // arrives as text input, so turn it back on or the toggle stops working.
    if (window_ != nullptr && !SDL_TextInputActive(window_)) SDL_StartTextInput(window_);
}

ImVec2 DebugOverlay::Scaled(float x, float y) const { return ImVec2(x * scale_, y * scale_); }

void DebugOverlay::Draw(VkCommandBuffer cmd) {
    if (!frame_built_) return;
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    frame_built_ = false;
}

void DebugOverlay::DrawFps() {
    // Mean over the last half second of presented frames: small text with a 1-pixel shadow, no
    // window or background, so it covers as little of the picture as possible.
    float sum = 0.0f;
    int n = 0;
    for (size_t i = 0; i < frame_ms_.size() && sum < 500.0f; ++i) {
        const float v = frame_ms_[(frame_ms_pos_ + frame_ms_.size() - 1 - i) % frame_ms_.size()];
        if (v <= 0.0f) break;
        sum += v;
        ++n;
    }
    const float ms = n > 0 ? sum / static_cast<float>(n) : 0.0f;
    char text[48];
    std::snprintf(text, sizeof(text), "%.0f FPS\n%.2f ms", ms > 0.0f ? 1000.0f / ms : 0.0f, ms);
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImVec2 size = ImGui::CalcTextSize(text);
    const ImVec2 pos(ImGui::GetIO().DisplaySize.x - size.x - 8.0f * scale_, 6.0f * scale_);
    dl->AddText(ImVec2(pos.x + 1.0f, pos.y + 1.0f), IM_COL32(0, 0, 0, 200), text);
    dl->AddText(pos, IM_COL32(255, 255, 255, 230), text);
}

void DebugOverlay::DrawPerformance(const OverlayFrameData& data) {
    // Default layout (1080-line units, times the UI scale): Performance under the main window,
    // Scene and Memory side by side at the top, Session log under Performance.
    ImGui::SetNextWindowPos(Scaled(8, 100), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(Scaled(440, 0), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Performance", &show_perf_)) {
        ImGui::End();
        return;
    }
    float sum = 0.0f, worst = 0.0f;
    for (float v : frame_ms_) {
        sum += v;
        worst = std::max(worst, v);
    }
    const float avg = sum / static_cast<float>(frame_ms_.size());
    ImGui::Text("Presented: %.1f fps (avg %.2f ms, worst %.2f ms)", avg > 0.0f ? 1000.0f / avg : 0.0f, avg, worst);
    ImGui::PlotLines("##frame", frame_ms_.data(), static_cast<int>(frame_ms_.size()), static_cast<int>(frame_ms_pos_),
                     "frame ms", 0.0f, 40.0f, ImVec2(-1, 50 * scale_));
    ImGui::Checkbox("FPS overlay (top right)", &fps_);
    if (renderer_ != nullptr) {
        const render::Renderer::Stats& rs = renderer_->LastStats();
        ImGui::Text("Renderer CPU: GS %.2f ms, record %.2f ms", rs.gs_ms, rs.record_ms);
        ImGui::Text("Textures: %u drawn, %u GPU decodes, %u CPU uploads, %u memory updates, %.0f KiB", rs.textures.textures,
                    rs.textures.decodes, rs.textures.uploads, rs.textures.memory_updates,
                    static_cast<double>(rs.textures.upload_bytes) / 1024.0);
        ImGui::Text("Texture images: %u (%.0f MiB)", rs.textures.images,
                    static_cast<double>(rs.textures.image_bytes) / (1024.0 * 1024.0));
        ImGui::Text("GPU work: %u render passes, %u barriers, %u frame-buffer copies (%.2f Mpixels)",
                    rs.passes.render_passes, rs.passes.barriers, rs.passes.copies,
                    static_cast<double>(rs.passes.copy_pixels) / 1e6);
    }
    ImGui::Separator();
    ImGui::Text("Simulation: %s, 59.94 Hz fixed step", data.game_running ? "running" : "stopped");
    ImGui::Text("Steps this frame: %u   total: %llu", data.ticks_run, static_cast<unsigned long long>(data.vblank_count));
    ImGui::Text("Dropped steps (catch-up limit 4): %llu", static_cast<unsigned long long>(data.dropped_ticks));
    ImGui::Text("Interpolation alpha: %.3f", data.alpha);
    ImGui::Separator();
    ImGui::TextUnformatted("DMA intake (counted, not drawn yet)");
    ImGui::Text("GIF  ch2: %llu qw last step, %llu sends", static_cast<unsigned long long>(data.gif_qwords),
                static_cast<unsigned long long>(data.gif_sends));
    ImGui::Text("VIF1 ch1: %llu qw last step, %llu sends", static_cast<unsigned long long>(data.vif1_qwords),
                static_cast<unsigned long long>(data.vif1_sends));
    if (data.bad_chains != 0) ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "Bad chains: %llu", static_cast<unsigned long long>(data.bad_chains));
    ImGui::Text("DISPFB %08llX  DISPLAY %016llX", static_cast<unsigned long long>(data.dispenv[2]),
                static_cast<unsigned long long>(data.dispenv[3]));
    ImGui::End();
}

void DebugOverlay::DrawScene() {
    ImGui::SetNextWindowPos(Scaled(460, 8), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(Scaled(470, 440), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Scene", &show_scene_)) {
        ImGui::End();
        return;
    }
    const u32 ctx = PeekOr<u32>(kMainContextPointer, 0);
    if (ctx == 0) {
        ImGui::TextUnformatted("MainContext not set up yet");
    } else {
        const u16 scene = PeekOr<u16>(ctx + 0x08, 0);
        ImGui::Text("Scene %u: %s", scene, scene < 8 ? kSceneNames[scene] : "?");
        ImGui::Text("Sub-state %u   prev scene %u", PeekOr<u16>(ctx + 0x0E, 0), PeekOr<u16>(ctx + 0x0A, 0));
        ImGui::Text("Next scene %u / sub %u   req %08X", PeekOr<u16>(ctx + 0x0C, 0), PeekOr<u16>(ctx + 0x10, 0),
                    PeekOr<u32>(ctx + 0x04, 0));
        ImGui::Text("Flags %08X", PeekOr<u32>(ctx + 0x00, 0));
        ImGui::Text("frame_count %u   unpaused %u", PeekOr<u32>(ctx + 0x28, 0), PeekOr<u32>(ctx + 0x2C, 0));
        ImGui::Text("scene_frame %u   unpaused %u", PeekOr<u32>(ctx + 0x30, 0), PeekOr<u32>(ctx + 0x34, 0));
        const u32 game = PeekOr<u32>(kGameStatePointer, 0);
        if (game != 0) {
            const u8 mission_id = PeekOr<u8>(game + 0x27, 0);
            ImGui::Text("Mission id (game+0x27): 0x%02X (%u)", mission_id, mission_id);
        }
    }
    ImGui::Separator();
    const debug::SceneRequestLog& log = debug::SceneRequests();
    ImGui::Text("Requests (%llu total, newest first)", static_cast<unsigned long long>(log.total));
    // The table fills the rest of the window; the request column gets the most room.
    if (ImGui::BeginTable("requests", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY,
                          ImVec2(0, std::max(ImGui::GetContentRegionAvail().y, 120.0f * scale_)))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("step", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("frame", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("from", ImGuiTableColumnFlags_WidthStretch, 0.6f);
        ImGui::TableSetupColumn("request", ImGuiTableColumnFlags_WidthStretch, 2.6f);
        ImGui::TableHeadersRow();
        const u64 n = std::min<u64>(log.total, debug::SceneRequestLog::kSize);
        for (u64 i = 0; i < n; ++i) {
            const debug::SceneRequest& e = log.entries[(log.total - 1 - i) % debug::SceneRequestLog::kSize];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%llu", static_cast<unsigned long long>(e.vblank));
            ImGui::TableNextColumn();
            ImGui::Text("%u", e.frame_count);
            ImGui::TableNextColumn();
            ImGui::Text("%u", e.from_scene);
            ImGui::TableNextColumn();
            if (e.kind == debug::SceneRequestKind::kScene) {
                ImGui::Text("RequestScene(%u)", e.value);
            } else {
                ImGui::Text("RequestSubState(%u)", e.value);
            }
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

void DebugOverlay::LoadSymbols() {
    symbols_.clear();
    symbols_source_.clear();
    std::vector<std::filesystem::path> candidates;
#if AC04_DEV_BUILD
    candidates.push_back(std::filesystem::u8path(AC04_SOURCE_DIR) / "docs" / "symbols.csv");
#endif
    candidates.push_back(ExecutableDir() / "symbols.csv");
    for (const auto& path : candidates) {
        std::ifstream f(path);
        if (!f) continue;
        std::string line;
        std::getline(f, line);  // header: address,name,kind,status
        while (std::getline(f, line)) {
            const size_t c1 = line.find(',');
            if (c1 == std::string::npos) continue;
            const size_t c2 = line.find(',', c1 + 1);
            const std::string addr = line.substr(0, c1);
            const std::string name = line.substr(c1 + 1, c2 == std::string::npos ? std::string::npos : c2 - c1 - 1);
            char* end = nullptr;
            const unsigned long a = std::strtoul(addr.c_str(), &end, 16);
            if (end == addr.c_str() || name.empty()) continue;
            symbols_.push_back({static_cast<u32>(a), name});
        }
        std::sort(symbols_.begin(), symbols_.end(), [](const Symbol& x, const Symbol& y) { return x.address < y.address; });
        symbols_source_ = PathToUtf8(path);
        return;
    }
}

void DebugOverlay::DrawMemory() {
    ImGui::SetNextWindowPos(Scaled(940, 8), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(Scaled(800, 440), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Memory", &show_memory_)) {
        ImGui::End();
        return;
    }
    ImGui::SetNextItemWidth(220 * scale_);
    const bool go = ImGui::InputTextWithHint("##goto", "address (hex) or symbol name", goto_text_, sizeof(goto_text_),
                                             ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button("Go") || go) {
        goto_error_.clear();
        const std::string text = goto_text_;
        char* end = nullptr;
        const unsigned long a = std::strtoul(text.c_str(), &end, 16);
        if (!text.empty() && end != text.c_str() && *end == 0) {
            mem_target_ = static_cast<u32>(a);
            mem_address_ = mem_target_ & ~0xFu;
        } else {
            auto it = std::find_if(symbols_.begin(), symbols_.end(), [&](const Symbol& s) { return s.name == text; });
            if (it != symbols_.end()) {
                mem_target_ = it->address;
                mem_address_ = it->address & ~0xFu;
            } else {
                goto_error_ = "not found";
            }
        }
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90 * scale_);
    ImGui::Combo("##view", &mem_view_, "bytes\0u32\0f32\0");
    ImGui::SameLine();
    if (ImGui::Button("-0x100")) mem_address_ -= 0x100;
    ImGui::SameLine();
    if (ImGui::Button("+0x100")) mem_address_ += 0x100;
    if (!goto_error_.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", goto_error_.c_str());
    if (symbols_.empty()) ImGui::TextDisabled("No symbols.csv found (development builds read docs/symbols.csv)");

    if (ImGui::BeginChild("rows", ImVec2(0, 0), ImGuiChildFlags_Borders)) {
        for (u32 row = 0; row < 32; ++row) {
            const u32 a = mem_address_ + row * 16;
            // Label: the last symbol at or before the row (within the row, or the row's owner).
            auto it = std::upper_bound(symbols_.begin(), symbols_.end(), a + 15,
                                       [](u32 v, const Symbol& s) { return v < s.address; });
            const Symbol* label = it != symbols_.begin() ? &*std::prev(it) : nullptr;
            u8 bytes[16];
            const bool ok = Peek(a, bytes, 16);
            char text[160];
            int n = std::snprintf(text, sizeof(text), "%08X  ", a);
            if (!ok) {
                std::snprintf(text + n, sizeof(text) - static_cast<size_t>(n), "-- not RAM/scratchpad --");
            } else if (mem_view_ == 0) {
                for (int i = 0; i < 16; ++i) n += std::snprintf(text + n, sizeof(text) - static_cast<size_t>(n), "%02X ", bytes[i]);
                n += std::snprintf(text + n, sizeof(text) - static_cast<size_t>(n), " ");
                for (int i = 0; i < 16; ++i) text[n++] = std::isprint(bytes[i]) ? static_cast<char>(bytes[i]) : '.';
                text[n] = 0;
            } else {
                for (int i = 0; i < 4; ++i) {
                    u32 w;
                    std::memcpy(&w, bytes + 4 * i, 4);
                    if (mem_view_ == 1) {
                        n += std::snprintf(text + n, sizeof(text) - static_cast<size_t>(n), "%08X  ", w);
                    } else {
                        float f;
                        std::memcpy(&f, &w, 4);
                        n += std::snprintf(text + n, sizeof(text) - static_cast<size_t>(n), "%14.6g ", f);
                    }
                }
            }
            // The row holding the address last looked up is highlighted.
            if (mem_target_ - a < 16) {
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1), "%s", text);
            } else {
                ImGui::TextUnformatted(text);
            }
            if (label != nullptr && label->address + 0x10000 > a) {
                ImGui::SameLine();
                if (label->address >= a) {
                    ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1), "%s @%08X", label->name.c_str(), label->address);
                } else {
                    ImGui::TextDisabled("%s+%X", label->name.c_str(), a - label->address);
                }
            }
        }
    }
    ImGui::EndChild();
    ImGui::End();
}

void DebugOverlay::DrawLog() {
    ImGui::SetNextWindowPos(Scaled(8, 480), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(Scaled(920, 300), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Session log", &show_log_)) {
        ImGui::End();
        return;
    }
    ImGui::SetNextItemWidth(240 * scale_);
    ImGui::InputTextWithHint("##filter", "filter (module or text, any case)", log_filter_, sizeof(log_filter_));
    if (ImGui::BeginChild("lines", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar)) {
        const bool at_bottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f;
        for (const std::string& line : log::RecentLines(400)) {
            if (!ContainsNoCase(line, log_filter_)) continue;
            const bool error = line.find("] ERROR") != std::string::npos;
            const bool warn = line.find("] WARN") != std::string::npos;
            if (error) {
                ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", line.c_str());
            } else if (warn) {
                ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "%s", line.c_str());
            } else {
                ImGui::TextUnformatted(line.c_str());
            }
        }
        if (at_bottom) ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
    ImGui::End();
}

std::optional<PresentationRequest> DebugOverlay::TakePresentationRequest() {
    std::optional<PresentationRequest> r = std::move(presentation_request_);
    presentation_request_.reset();
    return r;
}

// Every presentation setting that applies while the port runs (port-features.md §2: the GRAPHICS
// page's rows and the windowed size). Not here: presentation.default_screen_ratio, which the port
// applies once at boot (the game's SCREEN RATIO option itself is in its OPTIONS menu).
void DebugOverlay::DrawGraphics(const OverlayFrameData& data) {
    ImGui::SetNextWindowPos(Scaled(940, 8), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(Scaled(380, 0), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Graphics", &show_graphics_)) {
        ImGui::End();
        return;
    }
    PresentationSettings p = data.presentation;
    bool changed = false;
    const auto combo = [&](const char* label, int& index, const char* const* names, int count) {
        if (ImGui::Combo(label, &index, names, count)) changed = true;
    };

    int mode = p.window_mode == WindowMode::Fullscreen ? 0 : 1;
    const char* const kModes[] = {"Fullscreen", "Windowed"};
    combo("Window", mode, kModes, 2);
    p.window_mode = mode == 0 ? WindowMode::Fullscreen : WindowMode::Windowed;

    // Windowed client size: fixed choices (4:3 and 16:9), or the current one.
    static constexpr int kSizes[][2] = {{960, 720},   {1280, 960},  {1600, 1200}, {1920, 1440},
                                        {1280, 720},  {1600, 900},  {1920, 1080}, {2560, 1440}};
    char current[32];
    std::snprintf(current, sizeof(current), "%d x %d", p.window_width, p.window_height);
    ImGui::BeginDisabled(p.window_mode != WindowMode::Windowed);
    if (ImGui::BeginCombo("Window size", current)) {
        for (const auto& sz : kSizes) {
            char text[32];
            std::snprintf(text, sizeof(text), "%d x %d", sz[0], sz[1]);
            const bool selected = sz[0] == p.window_width && sz[1] == p.window_height;
            if (ImGui::Selectable(text, selected) && !selected) {
                p.window_width = sz[0];
                p.window_height = sz[1];
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();

    if (ImGui::Checkbox("V-sync", &p.vsync)) changed = true;

    int cap = 0;
    for (int i = 0; i < static_cast<int>(std::size(kFrameCaps)); ++i)
        if (kFrameCaps[i] == p.frame_cap) cap = i;
    const char* const kCaps[] = {"60 fps", "120 fps", "144 fps", "240 fps", "360 fps"};
    static_assert(std::size(kCaps) == std::size(kFrameCaps));
    ImGui::BeginDisabled(p.vsync);
    combo("Frame cap", cap, kCaps, static_cast<int>(std::size(kCaps)));
    ImGui::EndDisabled();
    if (p.vsync) ImGui::TextDisabled("  frame cap: not used with v-sync on");
    p.frame_cap = kFrameCaps[cap];

    if (ImGui::Checkbox("Frame interpolation", &p.frame_interpolation)) changed = true;

    int scale = std::clamp(p.internal_scale, 1, kMaxInternalScale) - 1;
    const char* const kScales[] = {"1x (native)", "2x", "3x", "4x", "5x", "6x", "7x", "8x"};
    static_assert(std::size(kScales) == kMaxInternalScale);
    combo("Internal resolution", scale, kScales, kMaxInternalScale);
    p.internal_scale = scale + 1;

    int filter = p.filter == ScaleFilter::Sharp ? 0 : 1;
    const char* const kFilters[] = {"Sharp", "Smooth"};
    combo("Scaling filter", filter, kFilters, 2);
    p.filter = filter == 0 ? ScaleFilter::Sharp : ScaleFilter::Smooth;

    int aa = static_cast<int>(p.antialiasing);
    const char* const kAa[] = {"Off", "FXAA", "SMAA"};
    combo("Anti-aliasing", aa, kAa, 3);
    p.antialiasing = static_cast<AntiAliasing>(aa);

    ImGui::Separator();
    bool save = false;
    if (ImGui::Button("Defaults for this display")) {
        // As the GRAPHICS page's DEFAULT: the windowed size and the SCREEN RATIO default stay.
        const PresentationSettings d = DefaultSettings(DefaultsDisplay()).presentation;
        p.window_mode = d.window_mode;
        p.vsync = d.vsync;
        p.frame_cap = d.frame_cap;
        p.frame_interpolation = d.frame_interpolation;
        p.internal_scale = d.internal_scale;
        p.filter = d.filter;
        p.antialiasing = d.antialiasing;
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Save to settings.json")) save = true;
    ImGui::TextDisabled("Changes apply at once for this session.");
    ImGui::TextDisabled("Applied at the next start only: SCREEN RATIO default (%s).", ToString(p.default_screen_ratio));
    ImGui::End();
    if (changed || save) presentation_request_ = PresentationRequest{p, save};
}

// Host input per port as the game will read it (scePadRead bytes, before the Modern swap), the
// device's pressure support, and the game's own pad type and the pressures it last read.
void DebugOverlay::DrawInput(const OverlayFrameData& data) {
    ImGui::SetNextWindowPos(Scaled(940, 330), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(Scaled(380, 0), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Input", &show_input_)) {
        ImGui::End();
        return;
    }
    static constexpr const char* kPressureNames[12] = {"Right", "Left", "Up",     "Down", "Triangle", "Circle",
                                                       "Cross", "Square", "L1",   "R1",   "L2",       "R2"};
    const u32 ctx = PeekOr<u32>(kMainContextPointer, 0);
    for (int port = 0; port < 2; ++port) {
        ImGui::PushID(port);
        const PadDeviceInfo& dev = data.pad_devices[static_cast<size_t>(port)];
        const PadState& st = data.pads[static_cast<size_t>(port)];
        if (dev.name.empty()) {
            ImGui::Text("Port %d: %s", port + 1, port == 0 ? "keyboard" : "no gamepad");
        } else {
            ImGui::Text("Port %d: %s", port + 1, dev.name.c_str());
            if (dev.pressure_buttons > 0) ImGui::TextColored(ImVec4(0.5f, 1, 0.5f, 1), "  pressure-sensitive buttons: %d", dev.pressure_buttons);
            else ImGui::TextDisabled("  pressure: triggers only (other buttons 0 or 255)");
        }
        ImGui::Text("  buttons %04X  L %02X %02X  R %02X %02X", st.buttons, st.lx, st.ly, st.rx, st.ry);
        if (st.connected)
            ImGui::Text("  map button: %s", st.map_cycle ? "each press steps the map (digital)" : "held, zoom by pressure");
        // Game side: pad type of ctx->pad_state[port] and the pressures of the read it converted.
        const u32 type = ctx != 0 ? PeekOr<u32>(ctx + 0x210 + 0x50u * static_cast<u32>(port), 0) : 0;
        const char* type_name = type == 5 ? "DualShock 2, pressure mode" : type == 4 ? "analog" : type == 6 ? "USB joystick"
                              : type == 0xFF ? "digital" : type == 0x80 ? "not connected" : "?";
        ImGui::Text("  game pad type %u (%s)", type, type_name);
        if (ImGui::BeginTable("pressures", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Button");
            ImGui::TableSetupColumn("Host");
            ImGui::TableSetupColumn("Game read");
            ImGui::TableHeadersRow();
            const u32 rec = 0x004121D8u + 100u * static_cast<u32>(port);  // libpad port record (pad.cpp)
            for (int i = 0; i < 12; ++i) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(kPressureNames[i]);
                ImGui::TableNextColumn();
                ImGui::Text("%3u", st.pressure[static_cast<size_t>(i)]);
                ImGui::TableNextColumn();
                ImGui::Text("%3u", PeekOr<u8>(rec + 0x40u + static_cast<u32>(i), 0));
            }
            ImGui::EndTable();
        }
        ImGui::PopID();
        if (port == 0) ImGui::Separator();
    }
    ImGui::End();
}

}  // namespace ac04::platform
