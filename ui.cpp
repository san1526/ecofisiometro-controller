#include "shorthand.hpp"

#include "imgui.h"
#include "implot.h"

#include "app.hpp"
#include "log.hpp"

static struct UIState {
    s32 selected_com_port = -1;
} gUIState;

static bool date_picker_widget(const char *id, std::chrono::year_month_day *ymd)
{
    using namespace std::chrono;
    ImGui::PushID(id);
    _defer
    {
        ImGui::PopID();
    };

    bool changed = false;

    static char date_buffer[512];
    auto r = std::format_to_n(date_buffer, 511, "{}", *ymd);
    *r.out = 0;
    if (ImGui::BeginCombo(id, date_buffer, ImGuiComboFlags_HeightLarge)) {
        s32 selected_year = static_cast<s32>(ymd->year());
        if (ImGui::InputInt("##year", &selected_year)) {
            *ymd = year(selected_year) / ymd->month() / ymd->day();
        }

        static const char *kMonths[] = {
            "January",
            "February",
            "March",
            "April",
            "May",
            "June",
            "July",
            "August",
            "September",
            "October",
            "November",
            "December",
        };
        s32 selected_month = static_cast<u32>(ymd->month()) - 1;
        if (ImGui::Combo("##month", &selected_month, kMonths, sizeof(kMonths) / sizeof(char *))) {
            *ymd = ymd->year() / month(selected_month + 1) / ymd->day();
        }

        if (ImGui::BeginTable("##Calendar", 7)) {
            static const char *kDays[] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};
            for (const char *d : kDays) {
                ImGui::TableSetupColumn(d);
            }
            ImGui::TableHeadersRow();
            ImGui::TableNextRow();

            auto first_date = year_month_weekday(ymd->year() / ymd->month() / day(1));
            auto last_date = ymd->year() / ymd->month() / std::chrono::last;

            auto start = first_date.weekday().c_encoding();
            ImGui::TableSetColumnIndex(start > 0 ? start - 1 : 0);
            for (u32 i = 0; i < static_cast<u32>(last_date.day()); ++i) {
                ImGui::TableNextColumn();
                {
                    static char n_buffer[4];
                    snprintf(n_buffer, 4, "%d", i + 1);
                    ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.5, 0.5));
                    if (ImGui::Selectable(n_buffer, i + 1 == static_cast<u32>(ymd->day()))) {
                        *ymd = ymd->year() / ymd->month() / day(i + 1);
                        changed = true;
                    }
                    ImGui::PopStyleVar();
                }
            }

            ImGui::EndTable();
        }
        ImGui::EndCombo();
    }

    return changed;
}

static void draw_plot(CCDOperation *op)
{
#if 0
    if (app->operation_selection_changed) {
        app->operation_selection_changed = false;
        ImPlot::SetNextAxesToFit();
    }
#endif

    if (ImPlot::BeginPlot("Averaged values", ImVec2(-1, -1))) {
        if (op) {
            auto date = [op] { return std::format("{:%d-%m-%Y %H:%M:%OS}", op->ts); };
            // ImPlot::SetupAxisFormat(ImAxis_Y1, "%u");
            ImPlot::PlotLine(op->name.empty() ? date().c_str() : op->name.c_str(),
                             op->accumulated_values.data(),
                             (int)op->accumulated_values.size());
        }
        ImPlot::EndPlot();
    }
}

static void draw_com_port_selector(Comms *comms)
{
    static s32 selected_com_port = 0;
    static char com_port_path[256];

    ImGui::SetNextWindowSize(ImVec2(500, -1), ImGuiCond_Appearing);
    ImGui::Begin("Device Connection");
    bool has_enumerated_ports = !comms->enumerated_ports.empty();
    if (has_enumerated_ports) {
        auto getter = [](void *d, int indx) {
            auto *p = static_cast<std::vector<ComPort> *>(d);
            return (*p)[indx].friendly_name.c_str();
        };
        if (ImGui::BeginCombo(
                "Available COM ports", comms->enumerated_ports[selected_com_port].friendly_name.c_str(), 0)) {
            for (s32 i = 0; i < comms->enumerated_ports.size(); ++i) {
                const ComPort &port = comms->enumerated_ports[i];
                if (ImGui::Selectable(port.friendly_name.c_str(), i == selected_com_port)) {
                    selected_com_port = i;
                }
            }
            ImGui::EndCombo();
        }
    } else {
        ImGui::InputText("Com port", com_port_path, sizeof(com_port_path));
    }

    ImGui::BeginDisabled(!has_enumerated_ports && strlen(com_port_path) == 0);
    if (ImGui::Button("Connect to device")) {
        queue_command({
            .type = AppCommand::ConnectToDevice,
            .data{
                  .com_path = has_enumerated_ports ? comms->enumerated_ports[selected_com_port].com_path
                                                 : std::string_view{com_port_path, strlen(com_port_path)},
                  }
        });
    }
    ImGui::EndDisabled();

    if (comms->connection_status == COMConnectionStatus::CONNECTION_ERROR) {
        ImGui::SameLine();
        ImGui::Text("Connection error");
    }
    ImGui::End();
}

static void draw_controls(App *app)
{
    static uint32_t exposure_time = 0;
    static uint32_t iterations = 0;
    ImGui::InputScalar("Exposure Time (us)", ImGuiDataType_U32, &exposure_time, NULL, NULL, "%u");
    ImGui::InputScalar("Iterations", ImGuiDataType_U32, &iterations, NULL, NULL, "%u");

    ImGui::BeginDisabled(iterations == 0 || exposure_time == 0);
    if (ImGui::Button("Send Command")) {
        queue_command({
            .type = AppCommand::StartCCDOperation,
            .data{.ccd_op = {exposure_time, iterations}},
        });
    }
    ImGui::EndDisabled();
}

namespace ImGui {
int TableGetHoveredRow();
} // namespace ImGui

static u32 draw_results(App *app)
{
    auto resize_cb = [](ImGuiInputTextCallbackData *data) {
        if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
            std::string *str = (std::string *)data->UserData;
            IM_ASSERT(data->Buf == str->c_str());
            str->resize(data->BufTextLen);
            data->Buf = (char *)str->c_str();
        }
        return 0;
    };

    static ImGuiTextFilter filter;
    if (ImGui::CollapsingHeader("Filters")) {
        filter.Draw();

        using namespace std::chrono;
        static year_month_day end_date{
            time_point_cast<days>(current_zone()->to_local(system_clock::now()))}; // Current date
        static year_month_day start_date{sys_days()};                              // Start of epoch
        auto w = ImGui::GetContentRegionAvail().x * 0.25f;
        ImGui::SetNextItemWidth(w);
        bool changed = date_picker_widget("Start Date", &start_date);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(w);
        changed |= date_picker_widget("End Date", &end_date);
        if (changed) {
            auto start = time_point_cast<seconds>(local_days(start_date)).time_since_epoch();
            auto end = time_point_cast<seconds>(local_days(end_date)).time_since_epoch();
            queue_command({
                .type = AppCommand::CCDOperationLoad, .data{.start_date = start, .end_date = end}
            });
        }
    }

    static u32 selected_operation;

    constexpr ImGuiTableFlags table_flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg
                                            | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV
                                            | ImGuiTableFlags_Resizable | ImGuiTableFlags_Hideable;

    if (ImGui::BeginTable("completed-reads", 5, table_flags)) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Timestamp", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Exposure time (us)", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Iterations", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Notes", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        // NOTE - Is an error to change this to be an unsigned type because on 0 size it will underflow
        for (s32 i = (s32)app->ccd_operations.size() - 1; i >= 0; --i) {
            ImGui::PushID((int)i);
            _defer
            {
                ImGui::PopID();
            };
            CCDOperation const &op = app->ccd_operations[i];

            static char id_buffer[1_KB];
            auto temp_name = [&op] {
                auto s = std::format_to_n(id_buffer, 1_KB - 1, "ccd_result({})", op.id);
                *s.out = 0;
                return id_buffer;
            };

            const char *name = op.name.empty() ? temp_name() : op.name.c_str();
            if (!filter.PassFilter(name)) {
                continue;
            }

            ImGui::TableNextRow();

            if (ImGui::TableNextColumn()) {

                if (ImGui::Selectable(name,
                                      i == selected_operation,
                                      ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
                    selected_operation = i;
                    // This is so the graph resizes and re-center when we are re-selecting a new result
                    // TODO remove this from here as its super obscure to what's happening
                    ImPlot::SetNextAxesToFit();
                }

                if (ImGui::TableGetHoveredRow() == app->ccd_operations.size() - i
                    && ImGui::TableGetColumnFlags(-1) & ImGuiTableColumnFlags_IsHovered && ImGui::IsMouseReleased(1)) {
                    ImGui::OpenPopup("Edit Name");
                }
                if (ImGui::BeginPopup("Edit Name")) {
                    static constexpr size_t kMaxNameLen = 256;
                    if (ImGui::IsWindowAppearing()) {
                        ImGui::SetKeyboardFocusHere();
                    }
                    ImGui::InputText("##name",
                                     (char *)op.name.c_str(),
                                     std::min(op.name.capacity(), kMaxNameLen),
                                     ImGuiInputTextFlags_CallbackResize,
                                     resize_cb,
                                     (void *)&op.name);
                    if (ImGui::IsItemDeactivated() || ImGui::Button("Done")) {
                        // TODO this should be done by App and not directly here
                        if (!op.name.empty()) {
                            queue_command(
                                {.type = AppCommand::CCDOperationUpdateNote, .data{.operation_to_update = op.id}});
                        }
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }
            } else {
                break;
            }
            ImGui::TableNextColumn();
            {
                static char date_buffer[1_KB];
                std::format_to_n(date_buffer, 1_KB, "{:%d-%m-%Y %H:%M:%OS}", op.ts);
                ImGui::Text(date_buffer);
            }
            ImGui::TableNextColumn();
            {
                ImGui::Text("%u", op.exposure_time_in_us);
            }
            ImGui::TableNextColumn();
            {
                ImGui::Text("%u", op.iterations);
            }
            ImGui::TableNextColumn();
            {
                ImGui::Text(op.note.empty() ? "(empty)" : op.note.c_str());

                if (ImGui::TableGetHoveredRow() == app->ccd_operations.size() - i
                    && ImGui::TableGetColumnFlags(-1) & ImGuiTableColumnFlags_IsHovered && ImGui::IsMouseReleased(1)) {
                    ImGui::OpenPopup("Edit Note");
                }
                if (ImGui::BeginPopup("Edit Note")) {
                    static constexpr size_t kMaxNoteLen = 16_KB;
                    if (ImGui::IsWindowAppearing()) {
                        ImGui::SetKeyboardFocusHere();
                    }
                    ImGui::InputTextMultiline("##notes",
                                              (char *)op.note.c_str(),
                                              std::min(op.note.capacity(), kMaxNoteLen),
                                              ImVec2(0, 0),
                                              ImGuiInputTextFlags_CallbackResize,
                                              resize_cb,
                                              (void *)&op.note);
                    if (ImGui::IsItemDeactivatedAfterEdit() || ImGui::Button("Done")) {
                        // TODO this should be done by App and not directly here
                        if (!op.note.empty()) {
                            queue_command(
                                {.type = AppCommand::CCDOperationUpdateNote, .data{.operation_to_update = op.id}});
                        }
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }
            }
        }

        ImGui::EndTable();
    }

    return selected_operation;
}

static void draw_log()
{
    static bool show_device = true;
    static bool show_app = true;
    static s32 level;

    static int log_c;

    const char *log_level_label[] = {"DEBUG", "INFO", "ERROR"};
    ImGui::Checkbox("Show Device Logs", &show_device);
    ImGui::SameLine();
    ImGui::Checkbox("Show App Logs", &show_app);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.3f);
    ImGui::SliderInt("Log Level", &level, 0, (s32)LogSeverity::MAX, log_level_label[level]);
    ImGui::Separator();
    ImGui::Spacing();

    constexpr ImGuiTableFlags table_flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg
                                            | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV
                                            | ImGuiTableFlags_Hideable;

    if (ImGui::BeginTable("completed-reads", 5, table_flags)) {
        ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Timesamp", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Context", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Severity", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Message", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        const auto &logs = get_log_lines();
        for (const LogEntry &log : logs) {
            if ((s32)log.severity < level) {
                continue;
            }

            bool is_device = log.context == LogContext::DEVICE;
            if (!show_device && is_device) {
                continue;
            }

            bool is_app = log.context == LogContext::APP;
            if (!show_app && is_app) {
                continue;
            }

            ImGui::TableNextRow();

            if (ImGui::TableNextColumn()) {
                static char fn_buffer[1_KB];
                auto out = std::format_to_n(fn_buffer, 1_KB, "{}::{}({})", log.file, log.function, log.line);
                *out.out = '\0';
                ImGui::Text(fn_buffer);
            } else {
                break;
            }
            ImGui::TableNextColumn();
            {
                static char ts_buffer[1_KB];
                auto local_time = std::chrono::current_zone()->to_local(log.ts);
                std::format_to_n(ts_buffer, 1_KB, "{:%d-%m-%Y %H:%M:%OS}", local_time);
                ImGui::Text(ts_buffer);
            }
            ImGui::TableNextColumn();
            {
                ImVec4 c[] = {ImVec4(0, 1, 1, 1), ImVec4(1, 1, 1, 1)};
                ImGui::TextColored(c[is_app], is_app ? "APP" : "DEVICE");
            }

            ImGui::TableNextColumn();
            {
                ImVec4 c[] = {ImVec4(1, 1, 1, 1), ImVec4(1, 1, 0, 1), ImVec4(1, 0, 0, 1)};
                ImGui::TextColored(c[(s32)log.severity], log_level_label[(s32)log.severity]);
            }
            ImGui::TableNextColumn();
            {
                ImGui::Text(log.msg.c_str());
            }
        }

        ImGui::EndTable();
    }
}

void draw_ui(App *app, Comms *comms)
{
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;

    ImGui::Begin("Main Window", NULL, flags);

    if (comms->com_connection == nullptr) {
        draw_com_port_selector(comms);
    } else {
        if (ImGui::BeginTabBar("##tabs")) {
            if (ImGui::BeginTabItem("CCD")) {
                ImGui::BeginChild(
                    "Controls", ImVec2(ImGui::GetContentRegionAvail().x * 0.3f, -FLT_MIN), ImGuiChildFlags_ResizeX);

                draw_controls(app);

                ImGui::Spacing();
                ImGui::SeparatorText("Results");
                ImGui::Spacing();

                u32 selected = draw_results(app);

                ImGui::EndChild();

                ImGui::SameLine();
                CCDOperation *op = nullptr;
                if (!app->ccd_operations.empty() && selected < app->ccd_operations.size()) {
                    op = &app->ccd_operations[selected];
                }
                draw_plot(op);
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Log")) {
                draw_log();
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }

    ImGui::End();
}
