//compile release x64 or somth use it however you want. almost 1:1 skeet loader remake by @jonzu00
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>
#include <windows.h>
#include <windowsx.h>
#include <string>
#include <vector>
#include <cstring>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);


enum class Page { Login, Loader };
static Page g_page = Page::Login;
static bool g_running = true;
static ImFont* g_pixelFont = nullptr; 

struct LogLine { std::string text; bool warn; };
static std::vector<LogLine> g_log;

static char g_user[64] = "";
static char g_pass[64] = "";

static const ImVec2 kLoginSize(330, 235);
static const ImVec2 kLoaderSize(508, 389);
static const float  kFontSize = 11.f; 


static ImU32 C(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }
static const ImU32 colWindowBg = C(22, 22, 22);
static const ImU32 colOuter = C(58, 58, 58);
static const ImU32 colOuterIn = C(36, 36, 36);
static const ImU32 colPanel = C(38, 38, 38);
static const ImU32 colPanelDark = C(26, 26, 26);
static const ImU32 colBorder = C(14, 14, 14);
static const ImU32 colLine = C(52, 52, 52);
static const ImU32 colText = C(190, 190, 190);
static const ImU32 colGreen = C(130, 200, 20);


static ID3D11Device* g_dev = nullptr;
static ID3D11DeviceContext* g_ctx = nullptr;
static IDXGISwapChain* g_swap = nullptr;
static ID3D11RenderTargetView* g_rtv = nullptr;

static void CreateRTV() {
    ID3D11Texture2D* bb = nullptr;
    g_swap->GetBuffer(0, IID_PPV_ARGS(&bb));
    g_dev->CreateRenderTargetView(bb, nullptr, &g_rtv);
    bb->Release();
}
static void CleanupRTV() { if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; } }

static bool CreateDevice(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    D3D_FEATURE_LEVEL fl;
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    if (D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2,
        D3D11_SDK_VERSION, &sd, &g_swap, &g_dev, &fl, &g_ctx) != S_OK) return false;
    CreateRTV();
    return true;
}

static ID3D11ShaderResourceView* g_gameIcon = nullptr;

static bool LoadTexture(const char* path, ID3D11ShaderResourceView** out) {
    int w = 0, h = 0, n = 0;
    unsigned char* data = stbi_load(path, &w, &h, &n, 4);
    if (!data) return false;
    D3D11_TEXTURE2D_DESC d{};
    d.Width = w; d.Height = h; d.MipLevels = 1; d.ArraySize = 1;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM; d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sub{ data, (UINT)(w * 4), 0 };
    ID3D11Texture2D* tex = nullptr;
    if (FAILED(g_dev->CreateTexture2D(&d, &sub, &tex))) { stbi_image_free(data); return false; }
    D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
    sv.Format = d.Format; sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; sv.Texture2D.MipLevels = 1;
    g_dev->CreateShaderResourceView(tex, &sv, out);
    tex->Release();
    stbi_image_free(data);
    return *out != nullptr;
}


static std::string ExeDirFile(const char* name) {
    char buf[MAX_PATH]; GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string p = buf; p = p.substr(0, p.find_last_of("\\/") + 1);
    return p + name;
}

static LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return true;
    switch (msg) {
    case WM_SIZE:
        if (g_dev && wp != SIZE_MINIMIZED) {
            CleanupRTV();
            g_swap->ResizeBuffers(0, LOWORD(lp), HIWORD(lp), DXGI_FORMAT_UNKNOWN, 0);
            CreateRTV();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wp & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}


static void Log(const std::string& s, bool warn = false) { g_log.push_back({ s, warn }); }


static void DrawFrame(ImDrawList* dl, ImVec2 size) {
    dl->AddRectFilled({ 0, 0 }, size, colOuter);
    dl->AddRectFilled({ 2, 2 }, { size.x - 2, size.y - 2 }, colOuterIn);
    dl->AddRectFilled({ 4, 4 }, { size.x - 4, size.y - 4 }, colWindowBg);
    dl->AddRect({ 4, 4 }, { size.x - 4, size.y - 4 }, colBorder);

    
    const float x0 = 6, x1 = size.x - 6, y = 6, h = 2, mid = x0 + (x1 - x0) * 0.5f;
    ImU32 blue = C(30, 160, 255), pink = C(235, 60, 200), lime = C(190, 225, 40);
    dl->AddRectFilledMultiColor({ x0, y }, { mid, y + h }, blue, pink, pink, blue);
    dl->AddRectFilledMultiColor({ mid, y }, { x1, y + h }, pink, lime, lime, pink);
}

static bool FlatButton(const char* label, ImVec2 size) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 mn = ImGui::GetCursorScreenPos(), mx(mn.x + size.x, mn.y + size.y);

    
    ImU32 top = IM_COL32(40, 40, 40, 255), bot = IM_COL32(26, 26, 26, 255);
    dl->AddRectFilledMultiColor(mn, mx, top, top, bot, bot);

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.04f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.08f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImColor(IM_COL32(46, 46, 46, 255)).Value);
    ImGui::PushStyleColor(ImGuiCol_Text, ImColor(IM_COL32(225, 225, 225, 255)).Value);
    bool r = ImGui::Button(label, size);
    ImGui::PopStyleColor(5);

    
    dl->AddRect({ mn.x - 1, mn.y - 1 }, { mx.x + 1, mx.y + 1 }, IM_COL32(10, 10, 10, 255));
    return r;
}


static void BeginGroup(const char* id, const char* label, ImVec2 pos, ImVec2 size) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 wp = ImGui::GetWindowPos();
    ImVec2 a(wp.x + pos.x, wp.y + pos.y), b(a.x + size.x, a.y + size.y);
    dl->AddRect(a, b, colBorder);
    dl->AddRect({ a.x + 1, a.y + 1 }, { b.x - 1, b.y - 1 }, colLine);
    dl->AddRectFilled({ a.x + 1, a.y + 1 }, { b.x - 1, b.y - 1 }, C(20, 20, 20));
    dl->AddRect(a, b, C(44, 44, 44));
    ImVec2 ts = ImGui::CalcTextSize(label);
    dl->AddRectFilled({ a.x + 8, a.y - 1 }, { a.x + 16 + ts.x, a.y + 2 }, colWindowBg);
    dl->AddText({ a.x + 12, a.y - ts.y * 0.5f }, C(150, 150, 150), label);
    ImGui::SetCursorPos(pos);
    ImGui::BeginChild(id, size, false, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
}


static void LoginField(const char* id, const char* hint, char* buf, size_t n, bool password,
    ImVec2 pos, float w) {
    ImGui::SetCursorPos(pos);
    ImGui::SetNextItemWidth(w);
    ImGui::InputText(id, buf, n, password ? ImGuiInputTextFlags_Password : 0);

    ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float padX = ImGui::GetStyle().FramePadding.x;
    const float hintSize = 13.f;

    if (!buf[0] && g_pixelFont)
        dl->AddText(g_pixelFont, hintSize, { mn.x + padX, mn.y + (mx.y - mn.y - hintSize) * 0.5f },
            C(85, 85, 85), hint);

    if (ImGui::IsItemActive()) {
        
        std::string shown = password ? std::string(strlen(buf), '*') : std::string(buf);
        float tx = mn.x + padX + ImGui::CalcTextSize(shown.c_str()).x;
        if (tx > mx.x - padX - 1) tx = mx.x - padX - 1;
        dl->AddRectFilled({ tx - 1, mn.y + 3 }, { tx + 1, mx.y - 3 }, colGreen);
    }
}

static void DrawLogin() {
    const float fw = 130.f;                 
    const float x = (kLoginSize.x - fw) * 0.5f;

    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImColor(C(26, 26, 26)).Value);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImColor(C(26, 26, 26)).Value);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImColor(C(28, 28, 28)).Value);
    ImGui::PushStyleColor(ImGuiCol_Border, ImColor(C(52, 52, 52)).Value);
    LoginField("##user", "Username", g_user, sizeof(g_user), false, { x, 56 }, fw);
    LoginField("##pass", "Password", g_pass, sizeof(g_pass), true, { x, 86 }, fw);
    ImGui::PopStyleColor(4);

    ImGui::SetCursorPos({ x, 130 });
    if (FlatButton("Login", { fw, 22 })) {
        
        g_log.clear();
        Log("Connected");
        Log("Welcome back,");
        Log("Added Counter-Strike 2 (   days remaining)");
        Log("Your session expires in 3 minutes");
        Log("Warning: Steam opened before client", true);
        g_page = Page::Loader;
        SetWindowPos(GetActiveWindow(), nullptr, 0, 0, (int)kLoaderSize.x, (int)kLoaderSize.y,
            SWP_NOMOVE | SWP_NOZORDER);
    }
    ImGui::SetCursorPos({ x, 158 });
    if (FlatButton("Exit##login", { fw, 22 })) g_running = false;
}

static void DrawLoader() {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 wp = ImGui::GetWindowPos();

    
    {
        ImVec2 a(wp.x + 20, wp.y + 21), b(a.x + 265, a.y + 135);
        dl->AddRectFilled(a, b, colPanel);
        dl->AddRect(a, b, colBorder);
        dl->AddRectFilled({ a.x + 1, a.y + 1 }, { b.x - 1, a.y + 40 }, C(28, 28, 28));

        
        ImVec2 ia(a.x + 10, a.y + 6), ib(ia.x + 27, ia.y + 28);
        if (g_gameIcon) {
            dl->AddImage((ImTextureID)(intptr_t)g_gameIcon, ia, ib);
        }
        else { 
            dl->AddRectFilled(ia, { ia.x + 13, ib.y }, C(40, 52, 110));
            dl->AddRectFilled({ ia.x + 13, ia.y }, ib, C(235, 170, 20));
        }

        dl->AddText({ a.x + 48, a.y + 5 }, colGreen, "Counter-Strike 2");
        dl->AddText({ a.x + 48, a.y + 19 }, C(215, 215, 215), "Updated 2027/01/10 1:43");
    }

    
    BeginGroup("##opt", "Options", { 297, 21 }, { 186, 135 });
    {
        ImGui::SetCursorPos({ 14, 24 });
        if (FlatButton("Load", { 158, 38 })) {
            
        }
        ImGui::SetCursorPos({ 14, 73 });
        if (FlatButton("Exit##loader", { 158, 38 })) g_running = false;
    }
    ImGui::EndChild();

    
    BeginGroup("##status", "Status", { 20, 171 }, { 464, 196 });
    {
        ImVec2 cp = ImGui::GetWindowPos();
        ImDrawList* cdl = ImGui::GetWindowDrawList();
        ImVec2 a(cp.x + 14, cp.y + 13), b(cp.x + 464 - 14, cp.y + 196 - 16);
        cdl->AddRectFilled(a, b, colPanel);
        cdl->AddRect(a, b, colBorder);

        float y = a.y + 3;
        for (auto& l : g_log) {
            if (l.warn) cdl->AddRectFilled({ a.x + 1, y - 1 }, { b.x - 1, y + 14 }, C(30, 30, 30));
            cdl->AddText({ a.x + 14, y }, l.warn ? colGreen : colText, l.text.c_str());
            y += 14.5f;
        }
    }
    ImGui::EndChild();
}


int WINAPI WinMain(HINSTANCE hi, HINSTANCE, LPSTR, int) {
    WNDCLASSEX wc{ sizeof(wc), CS_CLASSDC, WndProc, 0, 0, hi, nullptr, LoadCursor(nullptr, IDC_ARROW),
                   nullptr, nullptr, L"LoaderUI", nullptr };
    RegisterClassEx(&wc);
    HWND hwnd = CreateWindowEx(WS_EX_APPWINDOW, wc.lpszClassName, L"Loader", WS_POPUP,
        200, 200, (int)kLoginSize.x, (int)kLoginSize.y, nullptr, nullptr, hi, nullptr);
    if (!CreateDevice(hwnd)) return 1;
    LoadTexture(ExeDirFile("cs2.png").c_str(), &g_gameIcon);
    ShowWindow(hwnd, SW_SHOWDEFAULT);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    if (!io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\tahomabd.ttf", kFontSize) &&
        !io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\verdanab.ttf", kFontSize))
        io.Fonts->AddFontDefault();
    { 
        ImFontConfig pc;
        pc.SizePixels = 13.f; pc.OversampleH = pc.OversampleV = 1; pc.PixelSnapH = true;
        g_pixelFont = io.Fonts->AddFontDefault(&pc);
    }

    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowRounding = st.FrameRounding = st.ChildRounding = 0.f;
    st.WindowBorderSize = 0.f;
    st.FrameBorderSize = 1.f;
    st.WindowPadding = { 0, 0 };
    st.FramePadding = { 4, 4 };
    st.Colors[ImGuiCol_Text] = ImColor(colText).Value;

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_dev, g_ctx);

    const float clear[4] = { 0.11f, 0.11f, 0.11f, 1.f };
    MSG msg{};
    while (g_running) {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg); DispatchMessage(&msg);
            if (msg.message == WM_QUIT) g_running = false;
        }
        if (!g_running) break;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImVec2 size = g_page == Page::Login ? kLoginSize : kLoaderSize;
        ImGui::SetNextWindowPos({ 0, 0 });
        ImGui::SetNextWindowSize(size);
        ImGui::Begin("##main", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground);

        DrawFrame(ImGui::GetWindowDrawList(), size);
        if (g_page == Page::Login) DrawLogin(); else DrawLoader();

        
        if (ImGui::IsMouseClicked(0) && !ImGui::IsAnyItemHovered() && !ImGui::IsAnyItemActive()) {
            ReleaseCapture();
            SendMessage(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        }
        ImGui::End();

        ImGui::Render();
        g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_ctx->ClearRenderTargetView(g_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_swap->Present(1, 0);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupRTV();
    if (g_swap) g_swap->Release();
    if (g_ctx) g_ctx->Release();
    if (g_dev) g_dev->Release();
    DestroyWindow(hwnd);
    UnregisterClass(wc.lpszClassName, hi);
    return 0;
}