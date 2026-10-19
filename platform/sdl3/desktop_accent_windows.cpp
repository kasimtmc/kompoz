// The accent color of Windows 10 and 11 (Settings > Personalization > Colors), as DWM keeps it:
// HKCU\Software\Microsoft\Windows\DWM\AccentColor, 0xAABBGGRR. Read, and followed as it changes
// (RegNotifyChangeKeyValue), on a thread of its own; each value comes to the main loop as an SDL
// event, as the Linux portal's does (desktop_accent.cpp).

#include "desktop_accent.hpp"

#include <SDL3/SDL.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <mutex>
#include <thread>

namespace kompoz::sdl {

namespace {

std::optional<Color> readAccent(HKEY key) {
    DWORD value = 0, size = sizeof value, type = 0;
    if (RegQueryValueExW(key, L"AccentColor", nullptr, &type, reinterpret_cast<BYTE*>(&value), &size) != ERROR_SUCCESS ||
        type != REG_DWORD)
        return std::nullopt;
    return Color{float(value & 0xFF) / 255.f, float((value >> 8) & 0xFF) / 255.f, float((value >> 16) & 0xFF) / 255.f, 1};
}

} // namespace

struct DesktopAccent::State {
    uint32_t eventType = 0;
    std::atomic<bool> running{true};
    mutable std::mutex mutex;
    std::optional<Color> accent;
    HKEY key = nullptr;
    HANDLE changed = nullptr, stop = nullptr;
    std::thread thread;

    void tell(std::optional<Color> value) {
        {
            std::lock_guard lock(mutex);
            accent = value;
        }
        SDL_Event e{};
        e.type = eventType;
        SDL_PushEvent(&e);
    }

    void run() {
        tell(readAccent(key));
        while (running) {
            if (RegNotifyChangeKeyValue(key, FALSE, REG_NOTIFY_CHANGE_LAST_SET, changed, TRUE) != ERROR_SUCCESS) return;
            HANDLE handles[] = {changed, stop};
            if (WaitForMultipleObjects(2, handles, FALSE, INFINITE) != WAIT_OBJECT_0) return;  // stopped
            tell(readAccent(key));
        }
    }
};

DesktopAccent::DesktopAccent() : state_(std::make_shared<State>()) {
    State& s = *state_;
    if (SDL_getenv("KOMPOZ_NO_DESKTOP_ACCENT")) return;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\DWM", 0, KEY_READ | KEY_NOTIFY, &s.key) != ERROR_SUCCESS) return;
    s.changed = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    s.stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!s.changed || !s.stop) return;
    s.eventType = SDL_RegisterEvents(1);
    if (s.eventType == 0) return;
    s.thread = std::thread([&s] {
        s.run();
        SDL_CleanupTLS();  // what SDL keeps for a thread it didn't start
    });
}

DesktopAccent::~DesktopAccent() {
    State& s = *state_;
    s.running = false;
    if (s.stop) SetEvent(s.stop);
    if (s.thread.joinable()) s.thread.join();
    if (s.changed) CloseHandle(s.changed);
    if (s.stop) CloseHandle(s.stop);
    if (s.key) RegCloseKey(s.key);
}

uint32_t DesktopAccent::eventType() const { return state_->eventType; }

std::optional<Color> DesktopAccent::accent() const {
    std::lock_guard lock(state_->mutex);
    return state_->accent;
}

} // namespace kompoz::sdl
