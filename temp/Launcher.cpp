#include <windows.h>
#include <shobjidl.h> 
#include <shellapi.h>
#include <commctrl.h>
#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <map> // Dil sözlüğü için eklendi

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

// STB Kütüphaneleri
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h> 

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "opengl32.lib")

// Tray Menü ID'leri
#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_EXIT 1001
#define ID_TRAY_SHOW 1002
#define IDI_ICON1 101

namespace fs = std::filesystem;

struct AppInfo {
    std::string name;
    std::wstring exePath;
    std::string iconPath;
    GLuint textureID = 0;
};

// Global Değişkenler
std::vector<AppInfo> g_Apps;
std::wstring g_PortableDir = L"";
char g_SearchFilter[128] = "";
NOTIFYICONDATAW g_NotifyIconData = { 0 };
WNDPROC original_glfw_wndproc;

// --- DİL SİSTEMİ DEĞİŞİKLİKLERİ ---
std::map<std::string, std::string> g_LangDict;
std::string g_CurrentLangFile = "turkish.lng";

void LoadLanguage(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) return;
    g_LangDict.clear();
    std::string line;
    while (std::getline(file, line)) {
        size_t sep = line.find('=');
        if (sep != std::string::npos) {
            std::string key = line.substr(0, sep);
            std::string value = line.substr(sep + 1);
            g_LangDict[key] = value;
        }
    }
    g_CurrentLangFile = filename;
    file.close();
}

const char* GetT(const std::string& key) {
    if (g_LangDict.count(key)) return g_LangDict[key].c_str();
    return "??";
}

// --- YARDIMCI FONKSİYONLAR ---

std::string WStringToString(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string str(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &str[0], size, NULL, NULL);
    return str;
}

std::wstring StringToWString(const std::string& str) {
    if (str.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, NULL, 0);
    std::wstring wstr(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], size);
    return wstr;
}

GLuint LoadTextureFromFile(const char* filename) {
    int width, height, channels;
    unsigned char* data = stbi_load(filename, &width, &height, &channels, 4);
    if (data == NULL) return 0;
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    stbi_image_free(data);
    return texture;
}

bool ExtractAndSaveIcon(const std::wstring& exePath, const std::string& savePath) {
    SHFILEINFOW sfi = { 0 };
    if (SHGetFileInfoW(exePath.c_str(), 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_LARGEICON)) {
        ICONINFO iconInfo;
        if (GetIconInfo(sfi.hIcon, &iconInfo)) {
            BITMAP bmp;
            GetObject(iconInfo.hbmColor, sizeof(BITMAP), &bmp);
            HDC hdc = GetDC(NULL);
            BITMAPINFO bmi = { 0 };
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = bmp.bmWidth;
            bmi.bmiHeader.biHeight = -bmp.bmHeight;
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;
            std::vector<unsigned char> pixels(bmp.bmWidth * bmp.bmHeight * 4);
            GetDIBits(hdc, iconInfo.hbmColor, 0, bmp.bmHeight, pixels.data(), &bmi, DIB_RGB_COLORS);
            for (size_t i = 0; i < pixels.size(); i += 4) std::swap(pixels[i], pixels[i + 2]);
            stbi_write_png(savePath.c_str(), bmp.bmWidth, bmp.bmHeight, 4, pixels.data(), bmp.bmWidth * 4);
            ReleaseDC(NULL, hdc);
            DeleteObject(iconInfo.hbmColor);
            DeleteObject(iconInfo.hbmMask);
        }
        DestroyIcon(sfi.hIcon);
        return true;
    }
    return false;
}

std::wstring SelectDirectory() {
    std::wstring chosenPath = L"";
    IFileOpenDialog* pFileOpen;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen)))) {
        DWORD dwOptions;
        pFileOpen->GetOptions(&dwOptions);
        pFileOpen->SetOptions(dwOptions | FOS_PICKFOLDERS);
        if (SUCCEEDED(pFileOpen->Show(NULL))) {
            IShellItem* pItem;
            if (SUCCEEDED(pFileOpen->GetResult(&pItem))) {
                PWSTR pszFilePath;
                if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath))) {
                    chosenPath = pszFilePath;
                    CoTaskMemFree(pszFilePath);
                }
                pItem->Release();
            }
        }
        pFileOpen->Release();
    }
    return chosenPath;
}

std::wstring SelectFile(const std::wstring& defaultFolder = L"") {
    std::wstring chosenPath = L"";
    IFileOpenDialog* pFileOpen;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen)))) {
        if (!defaultFolder.empty()) {
            IShellItem* pItemFolder;
            if (SUCCEEDED(SHCreateItemFromParsingName(defaultFolder.c_str(), NULL, IID_PPV_ARGS(&pItemFolder)))) {
                pFileOpen->SetDefaultFolder(pItemFolder);
                pFileOpen->SetFolder(pItemFolder);
                pItemFolder->Release();
            }
        }
        std::wstring filterLabel = StringToWString(GetT("SELECT_EXE"));
        COMDLG_FILTERSPEC rgSpec[] = { { filterLabel.c_str(), L"*.exe" } };
        pFileOpen->SetFileTypes(1, rgSpec);
        if (SUCCEEDED(pFileOpen->Show(NULL))) {
            IShellItem* pItem;
            if (SUCCEEDED(pFileOpen->GetResult(&pItem))) {
                PWSTR pszFilePath;
                if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath))) {
                    chosenPath = pszFilePath;
                    CoTaskMemFree(pszFilePath);
                }
                pItem->Release();
            }
        }
        pFileOpen->Release();
    }
    return chosenPath;
}

void SaveSettings() {
    std::ofstream file("settings.ini");
    if (file.is_open()) {
        file << "[Settings]\nPath=" << WStringToString(g_PortableDir) << "\n";
        file << "LangFile=" << g_CurrentLangFile << "\n\n";
        file << "[Apps]\n";
        for (const auto& app : g_Apps) {
            file << app.name << "=" << WStringToString(app.exePath) << "|" << app.iconPath << "\n";
        }
        file.close();
    }
}

void LoadSettings() {
    std::ifstream file("settings.ini");
    if (!file.is_open()) {
        LoadLanguage(g_CurrentLangFile);
        return;
    }
    std::string line, section;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        if (line[0] == '[') { section = line; continue; }
        if (section == "[Settings]") {
            if (line.find("Path=") == 0) g_PortableDir = StringToWString(line.substr(5));
            if (line.find("LangFile=") == 0) g_CurrentLangFile = line.substr(9);
        }
        else if (section == "[Apps]") {
            size_t eqPos = line.find('=');
            size_t pipePos = line.find('|');
            if (eqPos != std::string::npos && pipePos != std::string::npos) {
                AppInfo info;
                info.name = line.substr(0, eqPos);
                info.exePath = StringToWString(line.substr(eqPos + 1, pipePos - (eqPos + 1)));
                info.iconPath = line.substr(pipePos + 1);
                if (fs::exists(info.iconPath)) {
                    info.textureID = LoadTextureFromFile(info.iconPath.c_str());
                }
                g_Apps.push_back(info);
            }
        }
    }
    file.close();
    LoadLanguage(g_CurrentLangFile);
}

void ScanApps() {
    for (auto& app : g_Apps) {
        if (app.textureID != 0) glDeleteTextures(1, &app.textureID);
    }
    g_Apps.clear();

    if (g_PortableDir.empty() || !fs::exists(g_PortableDir)) return;
    if (!fs::exists("icons")) fs::create_directory("icons");

    try {
        for (const auto& entry : fs::directory_iterator(g_PortableDir)) {
            if (entry.is_directory()) {
                if (fs::exists(entry.path() / ".hidden")) continue;

                for (const auto& file : fs::directory_iterator(entry.path())) {
                    if (file.path().extension() == ".exe") {
                        AppInfo info;
                        info.name = entry.path().filename().string();
                        info.exePath = file.path().wstring();
                        info.iconPath = "icons/" + info.name + ".png";
                        if (!fs::exists(info.iconPath)) ExtractAndSaveIcon(info.exePath, info.iconPath);
                        if (fs::exists(info.iconPath)) info.textureID = LoadTextureFromFile(info.iconPath.c_str());
                        g_Apps.push_back(info);
                        break;
                    }
                }
            }
        }
    }
    catch (...) {}
    SaveSettings();
}

// --- PENCERE VE TRAY MANTIĞI ---

void window_close_callback(GLFWwindow* window) {
    glfwHideWindow(window);
    glfwSetWindowShouldClose(window, GLFW_FALSE);
}

LRESULT CALLBACK CustomWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_TRAYICON) {
        if (LOWORD(lParam) == WM_RBUTTONUP) {
            POINT curPoint;
            GetCursorPos(&curPoint);
            HMENU hMenu = CreatePopupMenu();
            InsertMenuW(hMenu, 0, MF_BYPOSITION | MF_STRING, ID_TRAY_SHOW, StringToWString(GetT("TRAY_SHOW")).c_str());
            InsertMenuW(hMenu, 1, MF_BYPOSITION | MF_STRING, ID_TRAY_EXIT, StringToWString(GetT("TRAY_EXIT")).c_str());
            SetForegroundWindow(hWnd);
            int clicked = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, curPoint.x, curPoint.y, 0, hWnd, NULL);
            if (clicked == ID_TRAY_SHOW) glfwShowWindow(glfwGetCurrentContext());
            else if (clicked == ID_TRAY_EXIT) glfwSetWindowShouldClose(glfwGetCurrentContext(), GLFW_TRUE);
            DestroyMenu(hMenu);
        }
        else if (LOWORD(lParam) == WM_LBUTTONDBLCLK) glfwShowWindow(glfwGetCurrentContext());
    }
    return CallWindowProc(original_glfw_wndproc, hWnd, message, wParam, lParam);
}

void ApplyModernTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.FrameRounding = 4.0f;
    style.FramePadding = ImVec2(10, 8);
    style.ItemSpacing = ImVec2(10, 12);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
    style.Colors[ImGuiCol_Text] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.15f, 0.68f, 0.37f, 1.00f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.18f, 0.80f, 0.44f, 1.00f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    style.Colors[ImGuiCol_PopupBg] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.85f, 0.85f, 0.85f, 1.00f);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.80f, 0.80f, 0.80f, 1.00f);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd) {
    wchar_t exeBuffer[MAX_PATH];
    GetModuleFileNameW(NULL, exeBuffer, MAX_PATH);
    std::wstring exePath = exeBuffer;
    fs::current_path(fs::path(exePath).parent_path());
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    LoadSettings(); // Önce ayarlar ve dil yüklenir

    GLFWwindow* window = glfwCreateWindow(500, 700, GetT("TITLE"), NULL, NULL);
    if (!window) return 1;
    glfwMakeContextCurrent(window);
    HWND hWnd = glfwGetWin32Window(window);
    original_glfw_wndproc = (WNDPROC)SetWindowLongPtr(hWnd, GWLP_WNDPROC, (LONG_PTR)CustomWndProc);
    HICON hMainIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON1));
    if (hMainIcon == NULL) hMainIcon = LoadIcon(NULL, IDI_APPLICATION);
    SendMessage(hWnd, WM_SETICON, ICON_BIG, (LPARAM)hMainIcon);
    SendMessage(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)hMainIcon);
    g_NotifyIconData.cbSize = sizeof(NOTIFYICONDATAW);
    g_NotifyIconData.hWnd = hWnd;
    g_NotifyIconData.uID = 1;
    g_NotifyIconData.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_NotifyIconData.uCallbackMessage = WM_TRAYICON;
    g_NotifyIconData.hIcon = hMainIcon;
    wcscpy_s(g_NotifyIconData.szTip, StringToWString(GetT("TRAY_TIP")).c_str());
    Shell_NotifyIconW(NIM_ADD, &g_NotifyIconData);
    glfwSetWindowCloseCallback(window, window_close_callback);
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = NULL;
    if (fs::exists("C:\\Windows\\Fonts\\segoeui.ttf")) {
        static const ImWchar ranges[] = { 0x0020, 0x00FF, 0x011E, 0x011F, 0x0130, 0x0131, 0x015E, 0x015F, 0x00C7, 0x00E7, 0x00D6, 0x00F6, 0x00DC, 0x00FC, 0 };
        io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 18.0f, NULL, ranges);
    }
    else io.Fonts->AddFontDefault();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");
    ApplyModernTheme();

    if (g_PortableDir.empty() || !fs::exists(g_PortableDir)) {
        g_PortableDir = SelectDirectory();
        if (!g_PortableDir.empty()) ScanApps();
    }
    else if (g_Apps.empty()) {
        ScanApps();
    }
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        if (!glfwGetWindowAttrib(window, GLFW_VISIBLE)) {
            Sleep(10);
            continue;
        }
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        int w, h;
        glfwGetWindowSize(window, &w, &h);
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2((float)w, (float)h));
        ImGui::Begin("Main", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoBringToFrontOnFocus);

        // Dil Seçici
        static const char* langFiles[] = { "turkish.lng", "english.lng" };
        static int currentLangIdx = (g_CurrentLangFile == "english.lng") ? 1 : 0;
        ImGui::PushItemWidth(140);
        if (ImGui::Combo("##Lang", &currentLangIdx, langFiles, IM_ARRAYSIZE(langFiles))) {
            LoadLanguage(langFiles[currentLangIdx]);
            SaveSettings();
        }
        ImGui::PopItemWidth();
        ImGui::SameLine();

        if (ImGui::Button(GetT("CHANGE_DIR"))) {
            std::wstring newDir = SelectDirectory();
            if (!newDir.empty()) { g_PortableDir = newDir; ScanApps(); }
        }
        ImGui::SameLine();
        if (ImGui::Button(GetT("REFRESH"))) {
            ScanApps();
        }
        ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1.0f), GetT("DIR_LABEL"), g_PortableDir.c_str());
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##Search", GetT("SEARCH_HINT"), g_SearchFilter, 128);
        ImGui::Separator();
        ImGui::BeginChild("List", ImVec2(0, 0), false);
        std::string filterLower = g_SearchFilter;
        std::transform(filterLower.begin(), filterLower.end(), filterLower.begin(), ::tolower);
        for (auto& app : g_Apps) {
            std::string nameLower = app.name;
            std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
            if (!filterLower.empty() && nameLower.find(filterLower) == std::string::npos) continue;
            ImGui::PushID(app.name.c_str());
            if (app.textureID != 0) { ImGui::Image((void*)(intptr_t)app.textureID, ImVec2(32, 32)); ImGui::SameLine(); }
            else { ImGui::Dummy(ImVec2(32, 32)); ImGui::SameLine(); }
            if (ImGui::Selectable(app.name.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, 32))) {
                if (ImGui::IsMouseDoubleClicked(0)) ShellExecuteW(NULL, L"open", app.exePath.c_str(), NULL, NULL, SW_SHOWNORMAL);
            }
            if (ImGui::BeginPopupContextItem("ItemContextMenu")) {
                if (ImGui::MenuItem(GetT("EDIT_PATH"))) {
                    std::wstring appFolder = fs::path(app.exePath).parent_path().wstring();
                    std::wstring newExe = SelectFile(appFolder);
                    if (!newExe.empty()) {
                        app.exePath = newExe;
                        if (ExtractAndSaveIcon(app.exePath, app.iconPath)) {
                            if (app.textureID != 0) glDeleteTextures(1, &app.textureID);
                            app.textureID = LoadTextureFromFile(app.iconPath.c_str());
                        }
                        SaveSettings();
                    }
                }
                ImGui::Separator();
                if (ImGui::MenuItem(GetT("OPEN_FOLDER"))) {
                    std::wstring folder = fs::path(app.exePath).parent_path().wstring();
                    ShellExecuteW(NULL, L"explore", folder.c_str(), NULL, NULL, SW_SHOWNORMAL);
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
            ImGui::Separator();
        }
        ImGui::EndChild();
        ImGui::End();
        ImGui::Render();
        glClearColor(0.95f, 0.95f, 0.95f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }
    Shell_NotifyIconW(NIM_DELETE, &g_NotifyIconData);
    for (auto& app : g_Apps) if (app.textureID != 0) glDeleteTextures(1, &app.textureID);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}