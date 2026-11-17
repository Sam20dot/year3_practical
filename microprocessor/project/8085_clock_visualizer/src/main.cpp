// 8085 simulator GUI: editor + assembler + live CPU view + 7-segment display.
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../include/emulator_core.h"

static char text[65536];

static void draw7(ImDrawList* d, ImVec2 p, float w, float h, int v) {
    static const int sg[10] = {63, 6, 91, 79, 102, 109, 125, 7, 127, 111};
    int s = (v >= 0 && v <= 9) ? sg[v] : 0;
    const float t = 7.0f, x = p.x, y = p.y;
    ImU32 on = IM_COL32(255, 60, 60, 255), off = IM_COL32(50, 14, 14, 255);
    struct { int bit; float x0, y0, x1, y1; } seg[7] = {
        {1,  x + t,     y,             x + w - t, y + t},                 // a top
        {2,  x + w - t, y + t / 2,     x + w,     y + h / 2},             // b upper right
        {4,  x + w - t, y + h / 2,     x + w,     y + h - t / 2},         // c lower right
        {8,  x + t,     y + h - t,     x + w - t, y + h},                 // d bottom
        {16, x,         y + h / 2,     x + t,     y + h - t / 2},         // e lower left
        {32, x,         y + t / 2,     x + t,     y + h / 2},             // f upper left
        {64, x + t,     y + h / 2 - t / 2, x + w - t, y + h / 2 + t / 2}  // g middle
    };
    for (auto& q : seg) d->AddRectFilled(ImVec2(q.x0, q.y0), ImVec2(q.x1, q.y1), (s & q.bit) ? on : off);
}

static void bin8(char* b, int v) { for (int k = 0; k < 8; k++) b[k] = (v >> (7 - k) & 1) ? '1' : '0'; b[8] = 0; }
static bool has_bp(int a) { for (int k = 0; k < nbp; k++) if (bp[k] == a) return true; return false; }
static void toggle_bp(int a) {
    for (int k = 0; k < nbp; k++) if (bp[k] == a) { bp[k] = bp[--nbp]; return; }
    if (nbp < 16) bp[nbp++] = a;
}

int main() {
    if (!glfwInit()) return -1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* window = glfwCreateWindow(1280, 720, "8085 Simulator", NULL, NULL);
    if (!window) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) return -1;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    sim_quiet = 1; build();
    snprintf(text, sizeof text, "%s", sim_sample());
    load_text(text); bool ok = assemble() != 0;

    bool running = false, autoasm = true, dirty = false, follow = true;
    double dirtyAt = 0, acc = 0, last = glfwGetTime();
    int speedIdx = 0; static const double spd[] = {3e6, 3e5, 3e4, 3e3, 300, 30};
    static const char* spdName[] = {"3 MHz (real 8085)", "300 kHz", "30 kHz", "3 kHz", "300 Hz", "30 Hz (see every instruction)"};
    Cpu prev = c; int lastPc = -1; char abuf[8] = "0000";

    auto doasm = [&]() { load_text(text); ok = assemble() != 0; running = false; prev = c; acc = 0; };

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        double now = glfwGetTime(), dt = now - last; last = now; if (dt > 0.1) dt = 0.1;

        if (running) {
            acc += dt * spd[speedIdx];
            if (acc >= 1) {
                uint64_t budget = (uint64_t)acc; if (budget > 5000000) budget = 5000000;
                prev = c; uint64_t t0 = c.t; run(budget);
                acc -= (double)(c.t - t0);
                if (c.halt || c.t - t0 < budget) running = false;
            }
        }
        if (dirty && autoasm && now - dirtyAt > 0.4) { doasm(); dirty = false; }

        ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame();

        // ---------------- Editor ----------------
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(480, 430), ImGuiCond_FirstUseEver);
        ImGui::Begin("Editor");
        if (ImGui::Button("Assemble")) { doasm(); dirty = false; }
        ImGui::SameLine();
        if (ImGui::Button(running ? "Pause" : "Run")) { if (c.halt) reset(); running = !running; acc = 0; }
        ImGui::SameLine();
        if (ImGui::Button("Step")) { running = false; prev = c; step(); }
        ImGui::SameLine();
        if (ImGui::Button("Reset")) { reset(); running = false; prev = c; }
        ImGui::SameLine();
        if (ImGui::Button("Clock sample")) { snprintf(text, sizeof text, "%s", sim_sample()); doasm(); dirty = false; }
        ImGui::Checkbox("Auto-assemble while typing (reloads and resets the CPU)", &autoasm);
        ImGui::SetNextItemWidth(260);
        ImGui::Combo("Speed", &speedIdx, spdName, 6);
        if (ImGui::InputTextMultiline("##src", text, sizeof text, ImVec2(-1, -70), ImGuiInputTextFlags_AllowTabInput)) { dirty = true; dirtyAt = now; }
        if (ok && !dirty) ImGui::TextColored(ImVec4(0.3f, 1, 0.3f, 1), "Assembled OK");
        else if (dirty) ImGui::TextColored(ImVec4(1, 1, 0.3f, 1), "editing...");
        else ImGui::TextColored(ImVec4(1, 0.35f, 0.35f, 1), "%s", errlog);
        ImGui::End();

        // ---------------- Listing ----------------
        ImGui::SetNextWindowPos(ImVec2(10, 450), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(480, 260), ImGuiCond_FirstUseEver);
        ImGui::Begin("Listing (click a line to toggle a breakpoint)");
        int curLine = line_of[c.pc];
        for (int i = 1; i <= nsrc; i++) {
            char hb[16] = ""; for (int j = 0; j < ll[i] && j < 3; j++) sprintf(hb + strlen(hb), "%02X ", mem[(la[i] + j) & 0xFFFF]);
            char row[400];
            snprintf(row, sizeof row, "%c%c %3d  %04X  %-9s %s", has_bp(la[i]) && ll[i] ? '*' : ' ', i == curLine ? '>' : ' ', i, ll[i] ? la[i] : 0, hb, sim_src(i));
            bool cur = (i == curLine);
            if (cur) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 1, 0.3f, 1));
            if (ImGui::Selectable(row, cur) && ll[i]) toggle_bp(la[i]);
            if (cur) { ImGui::PopStyleColor(); if (follow && lastPc != c.pc) ImGui::SetScrollHereY(0.5f); }
        }
        lastPc = c.pc;
        ImGui::End();

        // ---------------- CPU ----------------
        ImGui::SetNextWindowPos(ImVec2(500, 10), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(380, 330), ImGuiCond_FirstUseEver);
        ImGui::Begin("CPU");
        if (ImGui::BeginTable("regs", 4, ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Reg"); ImGui::TableSetupColumn("Hex"); ImGui::TableSetupColumn("Dec"); ImGui::TableSetupColumn("Binary");
            ImGui::TableHeadersRow();
            const char* nm = "BCDEHL?A"; int order[7] = {7, 0, 1, 2, 3, 4, 5};
            for (int k = 0; k < 7; k++) {
                int i = order[k]; char b[9]; bin8(b, c.r[i]);
                ImVec4 col = c.r[i] != prev.r[i] ? ImVec4(0.3f, 1, 0.3f, 1) : ImVec4(1, 1, 1, 1);
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::Text("%c", nm[i]);
                ImGui::TableNextColumn(); ImGui::TextColored(col, "%02X", c.r[i]);
                ImGui::TableNextColumn(); ImGui::TextColored(col, "%3d", c.r[i]);
                ImGui::TableNextColumn(); ImGui::TextColored(col, "%s", b);
            }
            int hl = c.r[4] << 8 | c.r[5]; char b[9]; bin8(b, mem[hl]);
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("M");
            ImGui::TableNextColumn(); ImGui::Text("%02X", mem[hl]);
            ImGui::TableNextColumn(); ImGui::Text("%3d", mem[hl]);
            ImGui::TableNextColumn(); ImGui::Text("%s", b);
            ImGui::EndTable();
        }
        auto flag = [&](const char* n, int v, int o) { ImGui::TextColored(v != o ? ImVec4(0.3f, 1, 0.3f, 1) : (v ? ImVec4(1, 0.8f, 0.2f, 1) : ImVec4(0.6f, 0.6f, 0.6f, 1)), "%s:%d", n, v); ImGui::SameLine(); };
        flag("S", c.s, prev.s); flag("Z", c.z, prev.z); flag("AC", c.ac, prev.ac); flag("P", c.p, prev.p); flag("CY", c.cy, prev.cy);
        ImGui::NewLine();
        ImGui::Text("HL:%04X  SP:%04X  PC:%04X", c.r[4] << 8 | c.r[5], c.sp, c.pc);
        ImGui::Text("T-states: %llu   instructions: %llu", (unsigned long long)c.t, (unsigned long long)c.n);
        ImGui::Text("= %.6f s at 3 MHz%s", c.t / 3e6, c.halt ? "   [HALTED]" : "");
        char d[40]; dis(c.pc, d); ImGui::TextColored(ImVec4(1, 1, 0.3f, 1), "next: %04X  %s", c.pc, d);
        ImGui::Checkbox("Follow PC in listing", &follow);
        ImGui::End();

        // ---------------- Display ----------------
        ImGui::SetNextWindowPos(ImVec2(890, 10), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(380, 330), ImGuiCond_FirstUseEver);
        ImGui::Begin("Display (ports 12h:11h:10h = HH:MM:SS in BCD)");
        {
            ImDrawList* dl = ImGui::GetWindowDrawList(); ImVec2 p = ImGui::GetCursorScreenPos();
            const float w = 38, h = 70, gap = 8; float x = p.x + 6;
            dl->AddRectFilled(p, ImVec2(p.x + 360, p.y + h + 20), IM_COL32(10, 4, 4, 255));
            int dig[6] = {port[0x12] >> 4, port[0x12] & 15, port[0x11] >> 4, port[0x11] & 15, port[0x10] >> 4, port[0x10] & 15};
            for (int i = 0; i < 6; i++) {
                draw7(dl, ImVec2(x, p.y + 10), w, h, dig[i]); x += w + gap;
                if (i == 1 || i == 3) {
                    dl->AddCircleFilled(ImVec2(x + 2, p.y + 10 + h * 0.33f), 3, IM_COL32(255, 60, 60, 255));
                    dl->AddCircleFilled(ImVec2(x + 2, p.y + 10 + h * 0.66f), 3, IM_COL32(255, 60, 60, 255)); x += 12;
                }
            }
            ImGui::Dummy(ImVec2(360, h + 24));
        }
        ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "PORT 12h (hours):   %02X", port[0x12]);
        ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "PORT 11h (minutes): %02X", port[0x11]);
        ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "PORT 10h (seconds): %02X", port[0x10]);
        ImGui::End();

        // ---------------- Memory ----------------
        ImGui::SetNextWindowPos(ImVec2(500, 350), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(770, 360), ImGuiCond_FirstUseEver);
        ImGui::Begin("Memory");
        ImGui::SetNextItemWidth(80);
        ImGui::InputText("Address (hex)", abuf, sizeof abuf, ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase);
        ImGui::SameLine(); if (ImGui::Button("Go to PC")) snprintf(abuf, sizeof abuf, "%04X", c.pc);
        ImGui::SameLine(); if (ImGui::Button("Go to SP")) snprintf(abuf, sizeof abuf, "%04X", c.sp);
        int base = (int)(strtol(abuf, 0, 16) & 0xFFF0);
        ImGui::Text("      00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F   (yellow = PC, cyan = SP)");
        for (int r = 0; r < 16; r++) {
            int a = (base + r * 16) & 0xFFFF;
            ImGui::Text("%04X:", a);
            for (int k = 0; k < 16; k++) {
                int x = (a + k) & 0xFFFF; ImGui::SameLine(0, 8);
                ImVec4 col = x == c.pc ? ImVec4(1, 1, 0.2f, 1) : x == c.sp ? ImVec4(0.3f, 1, 1, 1) : mem[x] ? ImVec4(1, 1, 1, 1) : ImVec4(0.45f, 0.45f, 0.45f, 1);
                ImGui::TextColored(col, "%02X", mem[x]);
            }
        }
                ImGui::End();   // closes the "Memory" window

        // ---------------- Render ----------------
        ImGui::Render();
        int fw, fh;
        glfwGetFramebufferSize(window, &fw, &fh);
        glViewport(0, 0, fw, fh);
        glClearColor(0.08f, 0.08f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }   // end of while loop

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}   // end of main
