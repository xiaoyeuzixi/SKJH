#include <cstdio>
#include <cstdlib>
#include <exception>
#include <csignal>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")

extern int g_lang;

inline void ShowFatalError() {
    MessageBoxW(nullptr, L"程序发生错误，已退出。",
                L"SKJH", MB_OK | MB_ICONERROR);
}

LONG WINAPI CrashHandler(EXCEPTION_POINTERS*) {
    ShowFatalError();
    return EXCEPTION_EXECUTE_HANDLER;
}

void TerminateHandler() {
    ShowFatalError();
    ExitProcess(1);
}

void SignalHandler(int) {
    ShowFatalError();
    ExitProcess(1);
}

#include "Mem.h"
#include "Overlay.h"
#include "main.h"
#include "Diagnostics.h"
#include <cctype>
#include <filesystem>
#include <string>

// Offset.h extern 兼容
uint64_t NameKey   = 0;
uint64_t BaseName  = 0;
uint64_t BaseWorld = 0;

static std::string FormatRawHidTemplate(
    const std::vector<uint8_t>& bytes) {
    std::string result;
    result.reserve(bytes.size() * 3);
    char encoded[4] = {};
    for (size_t index = 0; index < bytes.size(); ++index) {
        if (index) result.push_back(' ');
        snprintf(encoded, sizeof(encoded), "%02X", bytes[index]);
        result.append(encoded);
    }
    return result;
}

static bool ParseRawHidTemplate(
    const char* text, std::vector<uint8_t>& bytes) {
    bytes.clear();
    if (!text) return false;
    const auto hexValue = [](unsigned char value) -> int {
        if (value >= '0' && value <= '9') return value - '0';
        value = static_cast<unsigned char>(std::toupper(value));
        return value >= 'A' && value <= 'F' ? value - 'A' + 10 : -1;
    };

    size_t index = 0;
    while (text[index]) {
        while (text[index] &&
               (std::isspace(static_cast<unsigned char>(text[index])) ||
                text[index] == ',' || text[index] == ';' ||
                text[index] == ':' || text[index] == '-')) {
            ++index;
        }
        if (!text[index]) break;
        if (text[index] == '0' &&
            (text[index + 1] == 'x' || text[index + 1] == 'X')) {
            index += 2;
        }
        const int high = hexValue(static_cast<unsigned char>(text[index]));
        if (high < 0 || !text[index + 1]) return false;
        const int low =
            hexValue(static_cast<unsigned char>(text[index + 1]));
        if (low < 0 || bytes.size() >= 512) return false;
        bytes.push_back(static_cast<uint8_t>((high << 4) | low));
        index += 2;
    }
    return true;
}

// ═══════════════════════════════════════
//  UI 菜单 — 重新设计, 贴合失控进化生存射击玩法
// ═══════════════════════════════════════
static void DrawUI() {
    if (g_HideUI) return;

    static bool first = true;
    static float uiW = 1280.0f, uiH = 920.0f;
    static int lastMonitorW = 0, lastMonitorH = 0;
    static bool s_Dragging = false;
    static ImVec2 s_DragOffset(0, 0);
    static double s_ConfigStatusUntil = 0.0;
    static bool s_ConfigStatusOk = true;
    static std::string s_ConfigStatus;
    const auto setConfigStatus = [&](bool ok, const char* text) {
        s_ConfigStatusOk = ok;
        s_ConfigStatus = text ? text : "";
        s_ConfigStatusUntil = ImGui::GetTime() + 2.5;
    };
    const auto loadConfigAndApply = [&]() {
        const bool loaded = SKJH_LoadGlobalConfigAndApply();
        if (!loaded) {
            setConfigStatus(false, u8"没有已保存配置");
            return;
        }
        if (!SetOverlayDisplayMode(g_DisplayMode)) {
            SetOverlayDisplayMode(1);
            setConfigStatus(true, u8"配置已加载，未检测到副屏，已使用复制模式");
            return;
        }
        setConfigStatus(true, u8"配置已加载");
    };
    {
        const RECT r = GetMonitorRect(g_CurMonitor >= 0 ? g_CurMonitor : 0);
        const int monitorWidth = r.right - r.left;
        const int monitorHeight = r.bottom - r.top;
        if (first || monitorWidth != lastMonitorW ||
            monitorHeight != lastMonitorH) {
            uiW = (std::min)(1280.0f, monitorWidth * 0.92f);
            uiH = (std::min)(920.0f, monitorHeight * 0.92f);
            const float centerX = (monitorWidth - uiW) * 0.5f;
            const float centerY = (monitorHeight - uiH) * 0.5f;
            ImGui::SetNextWindowPos(
                ImVec2(centerX, centerY), ImGuiCond_Always);
            ImGui::SetNextWindowSize(
                ImVec2(uiW, uiH), ImGuiCond_Always);
            lastMonitorW = monitorWidth;
            lastMonitorH = monitorHeight;
        }
        first = false;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::Begin(L(u8"失控进化 · DMA透视", "SKJH DMA ESP"), nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse);

    const ImVec2 winPos = ImGui::GetWindowPos();
    const ImVec2 winSize = ImGui::GetWindowSize();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    HoloWindowBackground(dl, winPos, winSize, 40.0f);
    const ImU32 frameCol = Holo::ToU32(Holo::VIOLET, 0.60f);
    HoloGlowRect(dl, winPos, ImVec2(winPos.x + winSize.x, winPos.y + winSize.y),
        frameCol, 4.0f, 8.0f);
    HoloCornerBrackets(dl, winPos, ImVec2(winPos.x + winSize.x, winPos.y + winSize.y),
        18.0f, Holo::ToU32(Holo::VIOLET, 0.90f), 3.0f);

    const float titleBarH = 40.0f;
    HoloTitleBar(L(u8"SKJH DMA 全息控制台", "SKJH DMA HOLOGRAPHIC CONSOLE"),
        winPos, ImVec2(winSize.x, titleBarH));

    // The custom title bar is the only drag handle for this borderless window.
    {
        const ImVec2 titleP1(winPos.x, winPos.y);
        const ImVec2 titleP2(winPos.x + winSize.x, winPos.y + titleBarH);
        const bool hoverTitle = ImGui::IsMouseHoveringRect(titleP1, titleP2, false);
        if (hoverTitle && ImGui::IsMouseClicked(0) && !ImGui::IsAnyItemHovered()) {
            s_Dragging = true;
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            s_DragOffset = ImVec2(mouse.x - winPos.x, mouse.y - winPos.y);
        }
        if (s_Dragging) {
            if (ImGui::IsMouseDown(0)) {
                const ImVec2 mouse = ImGui::GetIO().MousePos;
                ImVec2 newPos(mouse.x - s_DragOffset.x, mouse.y - s_DragOffset.y);
                const RECT monitor = GetMonitorRect(g_CurMonitor >= 0 ? g_CurMonitor : 0);
                const float monitorWidth =
                    static_cast<float>(monitor.right - monitor.left);
                const float monitorHeight =
                    static_cast<float>(monitor.bottom - monitor.top);
                if (newPos.x < -winSize.x + 120.0f)
                    newPos.x = -winSize.x + 120.0f;
                if (newPos.x > monitorWidth - 120.0f)
                    newPos.x = monitorWidth - 120.0f;
                if (newPos.y < 0.0f)
                    newPos.y = 0.0f;
                if (newPos.y > monitorHeight - titleBarH)
                    newPos.y = monitorHeight - titleBarH;
                ImGui::SetWindowPos(newPos);
            } else {
                s_Dragging = false;
            }
        }
        if (hoverTitle && !s_Dragging)
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }

    const float contentY = titleBarH + 8.0f;
    const float contentH = winSize.y - contentY - 10.0f;
    HoloPanel(dl, ImVec2(winPos.x + 10.0f, winPos.y + contentY),
        ImVec2(winPos.x + winSize.x - 10.0f, winPos.y + contentY + contentH),
        nullptr, 6.0f, Holo::ToU32(Holo::VIOLET, 0.40f));

    ImGui::SetCursorPos(ImVec2(20.0f, contentY + 10.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
    ImGui::BeginChild("MainContent", ImVec2(winSize.x - 40.0f, contentH - 20.0f), false);

    HoloPushTabStyle();
    if (ImGui::BeginTabBar("MainTabs", ImGuiTabBarFlags_None)) {

        // ═══════════════════════════════════════
        //  标签1: 透视设置
        // ═══════════════════════════════════════
        if (ImGui::BeginTabItem(L(u8"◆ 透视设置", "◆ ESP Settings"))) {
            ImGui::Spacing();

            HoloHeader(L(u8"通用显示", "General Display"));
            if (ImGui::BeginTable("VisualToggles", 3, ImGuiTableFlags_SizingStretchSame)) {
                ImGui::TableNextColumn(); HoloCheckbox(L(u8"方框", "Box"), &g_ShowBox);
                ImGui::TableNextColumn(); HoloCheckbox(L(u8"动态血量", "Dynamic Health"), &g_ShowHealth);
                ImGui::TableNextColumn(); HoloCheckbox(L(u8"手持武器", "Held Weapon"), &g_ShowWeapon);
                ImGui::TableNextColumn(); HoloCheckbox(L(u8"距离", "Distance"), &g_ShowDistance);
                ImGui::TableNextColumn(); HoloCheckbox(L(u8"名称", "Name"), &g_ShowName);
                ImGui::TableNextColumn(); HoloCheckbox(L(u8"射线", "Rays"), &g_ShowRays);
                ImGui::TableNextColumn(); HoloCheckbox(L(u8"实体ID", "Entity ID"), &g_ShowEntityId);
                ImGui::TableNextColumn(); HoloCheckbox(L(u8"威胁警告", "Threat Warning"), &g_ShowThreatWarn);
                ImGui::TableNextColumn(); HoloCheckbox(L(u8"绘制自己", "Draw Self"), &g_DrawSelf);
                ImGui::TableNextColumn(); HoloCheckbox(L(u8"物资总开关", "All Loot"), &g_ShowItems);
                ImGui::EndTable();
            }

            HoloSeparator();

            HoloHeader(L(u8"全局设置", "Global Settings"));
            ImGui::SetNextItemWidth(420.0f);
            HoloSliderInt(L(u8"全局最大距离(米)", "Global Max Distance"), &g_MaxDist, 50, 3000);
            HoloCheckbox(L(u8"帧率限制", "FPS Limit"), &g_FpsLimitEnabled);
            if (g_FpsLimitEnabled)
                HoloSliderInt(L(u8"帧率上限", "Max FPS"), &g_FpsLimit, 30, 200);

            HoloSeparator();

            HoloHeader(L(u8"实体类型过滤", "Entity Type Filters"));

            // 表头
            ImGui::Columns(4, "typefilter", false);
            ImGui::Text(u8"类型");      ImGui::NextColumn();
            ImGui::Text(u8"显示");      ImGui::NextColumn();
            ImGui::Text(u8"距离(米)");  ImGui::NextColumn();
            ImGui::Text(u8"颜色");      ImGui::NextColumn();
            ImGui::Separator();

            // 每种类型一行
            int displayTypes[] = { SKJH_PLAYER, SKJH_MONSTER, SKJH_BOX, SKJH_CORPSE, SKJH_LOOT,
                                   SKJH_COLLECT, SKJH_ORE,
                                   SKJH_PART, SKJH_VEHICLE, SKJH_NPC,
                                   SKJH_TERRITORY, SKJH_TREE, SKJH_SYSTEM, SKJH_UNKNOWN };
            for (int t : displayTypes) {
                // 类型名 + 颜色色块
                float c[4]; SKJH_GetEntityColor(t, c);
                ImU32 col = IM_COL32((int)(c[0]*255),(int)(c[1]*255),(int)(c[2]*255),255);
                ImGui::TextColored(ImVec4(c[0], c[1], c[2], 1.0f), u8"●");
                ImGui::SameLine();
                ImGui::Text(u8"%s", SKJH_GetEntityDisplayName(t));
                ImGui::NextColumn();

                // 显示开关
                char cbLabel[32]; snprintf(cbLabel, sizeof(cbLabel), "##show_%d", t);
                HoloCheckbox(cbLabel, &g_TypeEnabled[t], col);
                ImGui::NextColumn();

                // 距离滑块
                char slLabel[32]; snprintf(slLabel, sizeof(slLabel), "##dist_%d", t);
                ImGui::PushItemWidth(120);
                HoloSliderInt(slLabel, &g_TypeMaxDist[t], 10, 2000, col);
                ImGui::PopItemWidth();
                ImGui::NextColumn();

                // 颜色色块 (只读)
                char colLabel[32]; snprintf(colLabel, sizeof(colLabel), "##col_%d", t);
                ImGui::ColorButton(colLabel, ImGui::ColorConvertU32ToFloat4(col),
                    ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoPicker,
                    ImVec2(14, 14));
                ImGui::NextColumn();
            }
            ImGui::Columns(1);

            HoloSeparator();

            HoloHeader(L(u8"快捷键与操作", "Hotkeys & Actions"));
            {
                char label[64];
                snprintf(label, sizeof(label), "%s: [%s]%s",
                    L(u8"隐藏菜单", "Hide Menu"), VKName(g_HotkeyVK),
                    g_BindingHotkey == 1 ? L(u8" <- 请按键...", " <- press key...") : "");
                if (HoloButton(label, ImVec2(280, 30))) g_BindingHotkey = 1;
            }
            ImGui::SameLine();
            ImGui::TextColored(Holo::ToVec4(Holo::TEXT_DIM),
                L(u8"F6: 显示/隐藏叠加层", "F6: Show/Hide Overlay"));

            HoloSeparator();

            if (HoloButton(L(u8"保存配置", "Save Config"), ImVec2(130, 32),
                    Holo::ToU32(Holo::GREEN_NEON, 0.80f))) {
                const bool saved = SaveGlobalConfig();
                setConfigStatus(saved,
                    saved ? u8"配置已保存" : u8"配置保存失败");
            }
            ImGui::SameLine();
            if (HoloButton(L(u8"加载配置", "Load Config"), ImVec2(130, 32),
                    Holo::ToU32(Holo::VIOLET_PALE, 0.80f))) {
                loadConfigAndApply();
            }
            ImGui::SameLine();
            if (HoloButton(L(u8"安全退出", "Safe Exit"), ImVec2(130, 32),
                    Holo::ToU32(Holo::RED_ALERT, 0.80f))) RequestOverlayExit();
            if (!s_ConfigStatus.empty() &&
                ImGui::GetTime() < s_ConfigStatusUntil) {
                ImGui::SameLine();
                ImGui::TextColored(
                    s_ConfigStatusOk
                        ? Holo::ToVec4(Holo::GREEN_NEON)
                        : Holo::ToVec4(Holo::RED_ALERT),
                    "%s", s_ConfigStatus.c_str());
            }

            ImGui::EndTabItem();
        }

        // ═══════════════════════════════════════
        //  标签2: 物资逐项筛选
        // ═══════════════════════════════════════
        if (ImGui::BeginTabItem(u8"◆ 物资筛选")) {
            ImGui::Spacing();
            static int selectedType = SKJH_LOOT;
            static char searchText[128] = {};
            static std::array<std::vector<SKJH_TemplateChoice>,
                              SKJH_TYPE_COUNT> choiceCache;
            static std::array<double, SKJH_TYPE_COUNT> refreshAt{};

            const int categories[] = {
                SKJH_LOOT, SKJH_BOX, SKJH_CORPSE, SKJH_ORE,
                SKJH_COLLECT, SKJH_TREE, SKJH_VEHICLE,
                SKJH_MONSTER, SKJH_PART, SKJH_NPC};

            ImGui::BeginChild(
                "LootFilterCategories", ImVec2(215.0f, 0.0f), true);
            HoloHeader(u8"分类");
            for (const int type : categories) {
                ImGui::PushID(type);
                const bool selected = selectedType == type;
                float color[4];
                SKJH_GetEntityColor(type, color);
                if (selected) {
                    ImGui::PushStyleColor(ImGuiCol_Header,
                        ImVec4(color[0], color[1], color[2], 0.36f));
                }
                if (ImGui::Selectable(
                        SKJH_GetEntityDisplayName(type), selected,
                        ImGuiSelectableFlags_None, ImVec2(0.0f, 32.0f))) {
                    selectedType = type;
                    searchText[0] = 0;
                }
                if (selected) ImGui::PopStyleColor();
                ImGui::PopID();
            }
            ImGui::EndChild();
            ImGui::SameLine();

            ImGui::BeginChild(
                "LootFilterItems", ImVec2(0.0f, 0.0f), true);
            HoloHeader(SKJH_GetEntityDisplayName(selectedType));

            const double now = ImGui::GetTime();
            if (choiceCache[selectedType].empty() ||
                now >= refreshAt[selectedType]) {
                choiceCache[selectedType] =
                    SKJH_GetTemplateChoicesForType(selectedType);
                refreshAt[selectedType] = now + 1.5;
            }
            auto& choices = choiceCache[selectedType];
            auto& selectedIds = g_EnabledTemplateIds[selectedType];

            HoloCheckbox(
                u8"启用该分类逐项白名单",
                &g_TemplateFilterActive[selectedType]);
            ImGui::SameLine();
            ImGui::TextColored(Holo::ToVec4(Holo::TEXT_DIM),
                u8"已选 %zu / %zu", selectedIds.size(), choices.size());

            const bool compactFilterToolbar =
                ImGui::GetContentRegionAvail().x < 620.0f;
            if (HoloButton(u8"全选", ImVec2(86.0f, 28.0f),
                    Holo::ToU32(Holo::GREEN_NEON, 0.75f))) {
                selectedIds.clear();
                for (const auto& choice : choices)
                    selectedIds.insert(choice.id);
                g_TemplateFilterActive[selectedType] = true;
            }
            ImGui::SameLine();
            if (HoloButton(u8"全不选", ImVec2(86.0f, 28.0f),
                    Holo::ToU32(Holo::RED_ALERT, 0.72f))) {
                selectedIds.clear();
                g_TemplateFilterActive[selectedType] = true;
            }
            if (compactFilterToolbar)
                ImGui::Spacing();
            else
                ImGui::SameLine();
            ImGui::SetNextItemWidth(
                compactFilterToolbar ? -FLT_MIN : 360.0f);
            ImGui::InputTextWithHint(
                "##LootSearch", u8"搜索中文名称或模板 ID",
                searchText, IM_ARRAYSIZE(searchText));

            std::string query(searchText);
            std::string queryLower = query;
            std::transform(queryLower.begin(), queryLower.end(),
                queryLower.begin(), [](unsigned char value) {
                    return static_cast<char>(std::tolower(value));
                });
            std::vector<const SKJH_TemplateChoice*> filtered;
            filtered.reserve(choices.size());
            for (const auto& choice : choices) {
                bool matches = query.empty() ||
                    choice.name.find(query) != std::string::npos;
                if (!matches) {
                    std::string nameLower = choice.name;
                    std::transform(nameLower.begin(), nameLower.end(),
                        nameLower.begin(), [](unsigned char value) {
                            return static_cast<char>(std::tolower(value));
                        });
                    matches = nameLower.find(queryLower) != std::string::npos ||
                        std::to_string(choice.id).find(query) !=
                            std::string::npos;
                }
                if (matches) filtered.push_back(&choice);
            }

            const ImGuiTableFlags flags =
                ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
            const float templateTableHeight =
                (std::max)(220.0f, ImGui::GetContentRegionAvail().y - 8.0f);
            if (ImGui::BeginTable(
                    "LootTemplateTable", 3, flags,
                    ImVec2(0.0f, templateTableHeight))) {
                ImGui::TableSetupColumn(
                    u8"显示", ImGuiTableColumnFlags_WidthFixed, 66.0f);
                ImGui::TableSetupColumn(
                    u8"中文名称", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn(
                    u8"操作", ImGuiTableColumnFlags_WidthFixed, 170.0f);
                ImGui::TableHeadersRow();

                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(filtered.size()));
                while (clipper.Step()) {
                    for (int index = clipper.DisplayStart;
                         index < clipper.DisplayEnd; ++index) {
                        const auto& choice = *filtered[index];
                        ImGui::PushID(static_cast<int>(
                            choice.id ^ (choice.id >> 32)));
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        bool enabled =
                            selectedIds.find(choice.id) != selectedIds.end();
                        if (HoloCheckbox("##enabled", &enabled)) {
                            if (enabled) selectedIds.insert(choice.id);
                            else selectedIds.erase(choice.id);
                            g_TemplateFilterActive[selectedType] = true;
                        }
                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextUnformatted(choice.name.c_str());
                        ImGui::TableSetColumnIndex(2);
                        ImGui::TextDisabled("%lld",
                            static_cast<long long>(choice.id));
                        ImGui::SameLine();
                        if (HoloSmallButton(
                                u8"仅此项",
                                Holo::ToU32(Holo::VIOLET_LIGHT, 0.85f))) {
                            selectedIds.clear();
                            selectedIds.insert(choice.id);
                            g_TemplateFilterActive[selectedType] = true;
                        }
                        ImGui::PopID();
                    }
                }
                ImGui::EndTable();
            }

            if (choices.empty()) {
                ImGui::TextColored(Holo::ToVec4(Holo::AMBER),
                    u8"中文模板目录正在从国服 LanguageManager 加载");
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        // ═══════════════════════════════════════
        if (ImGui::BeginTabItem(u8"◆ 系统设置")) {
            ImGui::Spacing();
            HoloHeader(u8"显示模式");
            if (HoloButton(
                    u8"复制模式", ImVec2(150.0f, 34.0f),
                    g_DisplayMode == 1
                        ? Holo::ToU32(Holo::VIOLET_LIGHT, 0.94f)
                        : Holo::ToU32(Holo::VIOLET_DIM, 0.56f))) {
                if (SetOverlayDisplayMode(1)) {
                    const RECT rect = GetMonitorRect(g_CurMonitor);
                    ImGui::SetWindowPos(ImVec2(
                        ((rect.right - rect.left) - winSize.x) * 0.5f,
                        ((rect.bottom - rect.top) - winSize.y) * 0.5f));
                }
            }
            ImGui::SameLine();
            if (HoloButton(
                    u8"扩展模式", ImVec2(150.0f, 34.0f),
                    g_DisplayMode == 0
                        ? Holo::ToU32(Holo::VIOLET_LIGHT, 0.94f)
                        : Holo::ToU32(Holo::VIOLET_DIM, 0.56f))) {
                if (SetOverlayDisplayMode(0)) {
                    const RECT rect = GetMonitorRect(g_CurMonitor);
                    ImGui::SetWindowPos(ImVec2(
                        ((rect.right - rect.left) - winSize.x) * 0.5f,
                        ((rect.bottom - rect.top) - winSize.y) * 0.5f));
                } else {
                    setConfigStatus(false, u8"未检测到可用副屏");
                }
            }

            HoloSeparator();
            HoloHeader(u8"热键");
            {
                char menuLabel[96];
                snprintf(menuLabel, sizeof(menuLabel),
                    u8"菜单 [%s]%s", VKName(g_HotkeyVK),
                    g_BindingHotkey == 1 ? u8" <- 请按键" : "");
                if (HoloButton(menuLabel, ImVec2(220.0f, 30.0f)))
                    g_BindingHotkey = 1;
                ImGui::SameLine();
                char itemLabel[96];
                snprintf(itemLabel, sizeof(itemLabel),
                    u8"物资总开关 [%s]%s", VKName(g_ItemsHotkeyVK),
                    g_BindingHotkey == 3 ? u8" <- 请按键" : "");
                if (HoloButton(itemLabel, ImVec2(250.0f, 30.0f)))
                    g_BindingHotkey = 3;
            }

            HoloSeparator();
            HoloHeader(u8"配置");
            if (HoloButton(u8"保存配置", ImVec2(130.0f, 32.0f),
                    Holo::ToU32(Holo::GREEN_NEON, 0.82f))) {
                const bool saved = SaveGlobalConfig();
                setConfigStatus(saved,
                    saved ? u8"配置已保存" : u8"配置保存失败");
            }
            ImGui::SameLine();
            if (HoloButton(u8"加载配置", ImVec2(130.0f, 32.0f),
                    Holo::ToU32(Holo::VIOLET_PALE, 0.82f))) {
                loadConfigAndApply();
            }
            ImGui::SameLine();
            if (HoloButton(u8"安全退出", ImVec2(130.0f, 32.0f),
                    Holo::ToU32(Holo::RED_ALERT, 0.82f))) {
                RequestOverlayExit();
            }
            if (!s_ConfigStatus.empty() &&
                ImGui::GetTime() < s_ConfigStatusUntil) {
                ImGui::Spacing();
                ImGui::TextColored(
                    s_ConfigStatusOk
                        ? Holo::ToVec4(Holo::GREEN_NEON)
                        : Holo::ToVec4(Holo::RED_ALERT),
                    "%s", s_ConfigStatus.c_str());
            }

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
    HoloPopTabStyle();

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
}

// ═══════════════════════════════════════
//  渲染回调
// ═══════════════════════════════════════
static void Render() {
    if (g_OverlayVisible) {
        RECT client{};
        if (g_OverlayHwnd && GetClientRect(g_OverlayHwnd, &client)) {
            g_ScreenW = client.right - client.left;
            g_ScreenH = client.bottom - client.top;
        }
    }

    g_DMAKey.Update();

    // F6: 复制模式显示/隐藏；扩展模式在主屏和副屏间切换。
    {
        static bool lastF6 = false;
        bool curF6 = g_DMAKey.Down(VK_F6) || (GetAsyncKeyState(VK_F6) & 0x8000);
        if (curF6 && !lastF6) {
            if (g_DisplayMode == 1) {
                g_OverlayVisible = !g_OverlayVisible;
                ShowWindow(g_OverlayHwnd,
                    g_OverlayVisible ? SW_SHOWNOACTIVATE : SW_HIDE);
            } else {
                const int nextMonitor = g_CurMonitor == 0 ? 1 : 0;
                MoveToMonitor(nextMonitor);
            }
        }
        lastF6 = curF6;
    }

    // 热键绑定
    if (g_BindingHotkey != 0) {
        for (int vk = 1; vk < 256; vk++) {
            if (vk == VK_F6) continue;
            if (g_DMAKey.Down(vk) || (GetAsyncKeyState(vk) & 0x8000)) {
                if (g_BindingHotkey == 1) g_HotkeyVK = vk;
                else if (g_BindingHotkey == 2) {
                    std::lock_guard<std::mutex> lock(g_AimConfigMutex);
                    g_AimConfig.activationKey = vk;
                }
                else if (g_BindingHotkey == 3) g_ItemsHotkeyVK = vk;
                g_BindingHotkey = 0;
                break;
            }
        }
    }

    // 隐藏/显示 UI
    if (g_BindingHotkey == 0) {
        static bool last = false;
        bool cur = g_DMAKey.Down(g_HotkeyVK) || (GetAsyncKeyState(g_HotkeyVK) & 0x8000);
        if (cur && !last) { g_HideUI = !g_HideUI; ClickThrough(g_HideUI); }
        last = cur;
    }

    // 自瞄激活键由 DMA 键盘状态和本机状态共同驱动。
    {
        const SKJH_AimConfig aim = SKJH_GetAimConfigSnapshot();
        const bool current = aim.enabled && g_BindingHotkey != 2 &&
            (g_DMAKey.Down(aim.activationKey) ||
             (GetAsyncKeyState(aim.activationKey) & 0x8000));
        g_AimActivationDown.store(current);
        if (!aim.enabled ||
            aim.activation != SKJH_AimActivation::Toggle) {
            g_AimToggleActive.store(false);
        }
        static bool previous = false;
        if (aim.activation == SKJH_AimActivation::Toggle &&
            current && !previous) {
            g_AimToggleActive.store(!g_AimToggleActive.load());
        }
        previous = current;
    }

    // 物资总开关
    {
        static bool lastItems = false;
        const bool current = g_DMAKey.Down(g_ItemsHotkeyVK) ||
            (GetAsyncKeyState(g_ItemsHotkeyVK) & 0x8000);
        if (current && !lastItems) g_ShowItems = !g_ShowItems;
        lastItems = current;
    }

    DrawUI();
    DrawESP();
}

// ═══════════════════════════════════════
//  主入口
// ═══════════════════════════════════════
int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    SetUnhandledExceptionFilter(CrashHandler);
    std::set_terminate(TerminateHandler);
    signal(SIGABRT, SignalHandler);
    signal(SIGINT,  SignalHandler);
    signal(SIGILL,  SignalHandler);
    signal(SIGFPE,  SignalHandler);

    bool sdkCheck = false;
    bool dmaProbe = false;
    bool playerIntelProbe = false;
    bool configCheck = false;
    bool uiPreview = false;
    DWORD probeTimeoutMs = 30000;
    std::filesystem::path probeOutput = "dma_probe.json";
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index] ? argv[index] : "";
        if (argument == "--sdk-check") sdkCheck = true;
        else if (argument == "--probe") dmaProbe = true;
        else if (argument == "--player-probe") playerIntelProbe = true;
        else if (argument == "--config-check") configCheck = true;
        else if (argument == "--ui-preview") uiPreview = true;
        else if (argument.rfind("--timeout=", 0) == 0)
            probeTimeoutMs = static_cast<DWORD>(std::strtoul(argument.c_str() + 10, nullptr, 10));
        else if (argument.rfind("--probe-out=", 0) == 0)
            probeOutput = argument.substr(12);
    }

    // Production startup uses only the offsets compiled into this binary.
    // External SDK files are loaded solely by explicit developer diagnostics.
    const bool sdkLoaded = (sdkCheck || dmaProbe || playerIntelProbe)
        ? LoadSdkManifest()
        : SdkManifestIsSane();
    if (sdkCheck) {
        printf("%s\n", SdkManifestSummary().c_str());
        printf("script.json: %s\n", g_RuntimeOffsets.scriptPath.string().c_str());
        printf("dump.cs: %s\n", g_RuntimeOffsets.dumpPath.string().c_str());
        return sdkLoaded && SdkManifestIsSane() ? 0 : 2;
    }

    if (configCheck) {
        g_lang = 0;
        const bool loaded = LoadGlobalConfig();
        const SKJH_AimConfig originalAim =
            SKJH_GetAimConfigSnapshot();
        SKJH_AimConfig probeAim = originalAim;
        probeAim.device.kind =
            SKJH::Aim::AimDeviceKind::RawHid;
        probeAim.device.rawHidVid = 0x1234;
        probeAim.device.rawHidPid = 0xABCD;
        probeAim.device.rawHidUsagePage = 0x0001;
        probeAim.device.rawHidUsage = 0x0002;
        probeAim.device.rawHidInterfaceIndex = 3;
        probeAim.device.rawHidExpectedReportBytes = 8;
        probeAim.device.rawHidReportId = 7;
        probeAim.device.rawHidButtonsOffset = 1;
        probeAim.device.rawHidXOffset = 2;
        probeAim.device.rawHidYOffset = 4;
        probeAim.device.rawHidAxisBytes = 2;
        probeAim.device.rawHidLittleEndian = false;
        probeAim.device.rawHidTransferMode =
            SKJH::Aim::RawHidTransferMode::FeatureReport;
        probeAim.device.rawHidReportTemplate =
            {7, 0, 0, 0, 0, 0, 0, 0};
        {
            std::lock_guard<std::mutex> lock(g_AimConfigMutex);
            g_AimConfig = probeAim;
        }
        const bool saved = SaveGlobalConfig();
        {
            std::lock_guard<std::mutex> lock(g_AimConfigMutex);
            g_AimConfig = {};
        }
        const bool reloaded = LoadGlobalConfig();
        const SKJH_AimConfig roundTripAim =
            SKJH_GetAimConfigSnapshot();
        const bool rawHidRoundTrip =
            roundTripAim.device.kind ==
                SKJH::Aim::AimDeviceKind::RawHid &&
            roundTripAim.device.rawHidVid == 0x1234 &&
            roundTripAim.device.rawHidPid == 0xABCD &&
            roundTripAim.device.rawHidUsagePage == 0x0001 &&
            roundTripAim.device.rawHidUsage == 0x0002 &&
            roundTripAim.device.rawHidInterfaceIndex == 3 &&
            roundTripAim.device.rawHidExpectedReportBytes == 8 &&
            roundTripAim.device.rawHidReportId == 7 &&
            roundTripAim.device.rawHidButtonsOffset == 1 &&
            roundTripAim.device.rawHidXOffset == 2 &&
            roundTripAim.device.rawHidYOffset == 4 &&
            roundTripAim.device.rawHidAxisBytes == 2 &&
            !roundTripAim.device.rawHidLittleEndian &&
            roundTripAim.device.rawHidTransferMode ==
                SKJH::Aim::RawHidTransferMode::FeatureReport &&
            roundTripAim.device.rawHidReportTemplate ==
                probeAim.device.rawHidReportTemplate;
        {
            std::lock_guard<std::mutex> lock(g_AimConfigMutex);
            g_AimConfig = originalAim;
        }
        const bool restored = SaveGlobalConfig();
        printf("Config check: loaded=%s saved=%s reloaded=%s rawhid=%s restored=%s mode=%d\n",
            loaded ? "true" : "false",
            saved ? "true" : "false",
            reloaded ? "true" : "false",
            rawHidRoundTrip ? "true" : "false",
            restored ? "true" : "false", g_DisplayMode);
        return saved && reloaded && rawHidRoundTrip && restored
            ? 0 : 5;
    }

    if (uiPreview) {
        g_lang = 0;
        LoadGlobalConfig();
        ShowWindow(GetConsoleWindow(), SW_HIDE);
        return OverlayRun(Render, g_DisplayMode);
    }

    if (dmaProbe || playerIntelProbe) {
        if (!mem.Init("SKJH.exe", probeTimeoutMs)) {
            printf("DMA probe: target process was not found within %lu ms.\n", probeTimeoutMs);
            return 3;
        }
        if (!SKJH_WaitForRuntimeSdk(probeTimeoutMs)) {
            printf("DMA probe: SDK/runtime validation timed out.\n");
            mem.Close();
            return 4;
        }
        const bool valid = playerIntelProbe
            ? SKJH_RunPlayerIntelProbe(probeOutput)
            : SKJH_RunDmaProbe(probeOutput);
        printf("%s: %s (%s)\n",
               playerIntelProbe ? "Player intel probe" : "DMA probe",
               valid ? "valid" : "invalid", probeOutput.string().c_str());
        mem.Close();
        return valid ? 0 : 4;
    }

    // 国服固定中文；显示模式从注册表恢复，无配置时默认为复制模式。
    g_lang = 0;
    LoadGlobalConfig();
    ShowWindow(GetConsoleWindow(), SW_HIDE);

    if (!mem.Init("SKJH.exe", 15000)) {
        MessageBoxW(nullptr, L"未找到目标进程或 DMA 设备。",
                    L"SKJH", MB_OK | MB_ICONERROR);
        return 1;
    }
    // Scene data may not exist while the remote process is still loading.
    // Reader threads tolerate that state and start drawing when it is ready.
    g_DMAKey.Init();

    ReserveContainers();

    std::thread t0, t1, t2, t3, t4, t5, t6;
    GameStart(t0, t1, t2, t3, t4, t5, t6);

    ShowWindow(GetConsoleWindow(), SW_HIDE);

    OverlayRun(Render, g_DisplayMode);

    GameStop(t0, t1, t2, t3, t4, t5, t6);
    mem.Close();
    return 0;
}
