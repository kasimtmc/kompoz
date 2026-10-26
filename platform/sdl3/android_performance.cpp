#include "android_performance.hpp"

#include <dlfcn.h>
#include <unistd.h>

namespace kompoz::android_performance {

namespace {
// The NDK's APerformanceHint functions (API 33), looked up at run time: the app runs from Android 7.
struct Api {
    void* (*getManager)() = nullptr;
    void* (*createSession)(void* manager, const int32_t* tids, size_t count, int64_t targetNs) = nullptr;
    int (*updateTarget)(void* session, int64_t targetNs) = nullptr;
    int (*reportActual)(void* session, int64_t actualNs) = nullptr;
    void (*closeSession)(void* session) = nullptr;
    bool load() {
        void* lib = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
        if (!lib) return false;
        getManager = reinterpret_cast<decltype(getManager)>(dlsym(lib, "APerformanceHint_getManager"));
        createSession = reinterpret_cast<decltype(createSession)>(dlsym(lib, "APerformanceHint_createSession"));
        updateTarget = reinterpret_cast<decltype(updateTarget)>(dlsym(lib, "APerformanceHint_updateTargetWorkDuration"));
        reportActual = reinterpret_cast<decltype(reportActual)>(dlsym(lib, "APerformanceHint_reportActualWorkDuration"));
        closeSession = reinterpret_cast<decltype(closeSession)>(dlsym(lib, "APerformanceHint_closeSession"));
        return getManager && createSession && updateTarget && reportActual && closeSession;
    }
};
Api api;
void* session = nullptr;
int64_t target = 0;
} // namespace

void start(int64_t targetNs) {
    if (session || targetNs <= 0 || !api.load()) return;
    void* manager = api.getManager();
    int32_t tid = int32_t(gettid());
    session = manager ? api.createSession(manager, &tid, 1, targetNs) : nullptr;  // (null: not supported)
    target = targetNs;
}

void setTarget(int64_t targetNs) {
    if (!session || targetNs <= 0 || targetNs == target) return;
    api.updateTarget(session, targetNs);
    target = targetNs;
}

void report(int64_t workNs) {
    if (session && workNs > 0) api.reportActual(session, workNs);
}

void stop() {
    if (session) api.closeSession(session);
    session = nullptr;
}

} // namespace kompoz::android_performance
