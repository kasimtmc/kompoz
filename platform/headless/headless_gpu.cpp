#include "headless_gpu.hpp"

#include "../common/system.hpp"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <type_traits>
#include <utility>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif !defined(__APPLE__)
#include <dlfcn.h>
#endif

namespace kompoz::platform {

namespace {

#if !defined(__APPLE__)
#if defined(_WIN32)
// (A UTF-8 path: the program's directory in any letters.)
void* openLibrary(const std::string& name) {
    return LoadLibraryW(std::filesystem::path(std::u8string(name.begin(), name.end())).c_str());
}
void* symbol(void* library, const char* name) { return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(library), name)); }
void closeLibrary(void* library) { FreeLibrary(static_cast<HMODULE>(library)); }
#else
void* openLibrary(const std::string& name) { return dlopen(name.c_str(), RTLD_NOW | RTLD_LOCAL); }
void* symbol(void* library, const char* name) { return dlsym(library, name); }
void closeLibrary(void* library) { dlclose(library); }
#endif

#if defined(_WIN32)
const char* const kVulkanLibraries[] = {"vulkan-1.dll"};
#elif defined(__ANDROID__)
const char* const kVulkanLibraries[] = {"libvulkan.so"};
#else
const char* const kVulkanLibraries[] = {"libvulkan.so.1", "libvulkan.so"};
#endif

// EGL's, as its header has them (not needed to build: EGL is loaded at run time).
using EGLint = int32_t;
using EGLBoolean = unsigned;
constexpr EGLint kNone = 0x3038, kExtensions = 0x3055, kRenderableType = 0x3040, kOpenGLES3Bit = 0x40, kSurfaceType = 0x3033,
                 kPbufferBit = 0x1, kRedSize = 0x3024, kGreenSize = 0x3023, kBlueSize = 0x3022, kAlphaSize = 0x3021, kWidth = 0x3057,
                 kHeight = 0x3056, kContextMajorVersion = 0x3098, kContextMinorVersion = 0x30FB, kContextFlags = 0x30FC,
                 kContextDebugBit = 0x1;
constexpr unsigned kOpenGLESApi = 0x30A0, kPlatformSurfacelessMesa = 0x31DD;

struct Egl {
    void* (*getDisplay)(void*) = nullptr;
    EGLBoolean (*initialize)(void*, EGLint*, EGLint*) = nullptr;
    const char* (*queryString)(void*, EGLint) = nullptr;
    EGLBoolean (*chooseConfig)(void*, const EGLint*, void**, EGLint, EGLint*) = nullptr;
    EGLBoolean (*bindApi)(unsigned) = nullptr;
    void* (*createContext)(void*, void*, void*, const EGLint*) = nullptr;
    void* (*createPbufferSurface)(void*, void*, const EGLint*) = nullptr;
    EGLBoolean (*makeCurrent)(void*, void*, void*, void*) = nullptr;
    EGLBoolean (*destroyContext)(void*, void*) = nullptr;
    EGLBoolean (*destroySurface)(void*, void*) = nullptr;
    EGLBoolean (*terminate)(void*) = nullptr;
    void* (*getProcAddress)(const char*) = nullptr;

    bool load(void* library) {
        auto get = [library](auto& fn, const char* name) { fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(symbol(library, name)); return fn != nullptr; };
        return get(getDisplay, "eglGetDisplay") && get(initialize, "eglInitialize") && get(queryString, "eglQueryString") &&
               get(chooseConfig, "eglChooseConfig") && get(bindApi, "eglBindAPI") && get(createContext, "eglCreateContext") &&
               get(createPbufferSurface, "eglCreatePbufferSurface") && get(makeCurrent, "eglMakeCurrent") &&
               get(destroyContext, "eglDestroyContext") && get(destroySurface, "eglDestroySurface") && get(terminate, "eglTerminate") &&
               get(getProcAddress, "eglGetProcAddress");
    }
};

// The context's functions (Surface::glGetProcAddress is a plain function: one context at a time).
Egl gEgl;
void* gGles = nullptr;

bool hasWord(const char* list, const char* word) {
    if (!list) return false;
    size_t n = std::strlen(word);
    for (const char* at = std::strstr(list, word); at; at = std::strstr(at + n, word))
        if ((at == list || at[-1] == ' ') && (at[n] == ' ' || at[n] == '\0')) return true;
    return false;
}
#endif

} // namespace

bool HeadlessGpu::open(gpu::Backend backend, bool debug, gpu::Surface& surface, std::string* error) {
    (void)debug;
    switch (backend) {
    case gpu::Backend::Metal: return true;  // (no layer: it draws only into textures)
#if !defined(__APPLE__)
    case gpu::Backend::Vulkan:
        for (const char* name : kVulkanLibraries)
            if ((vulkan_ = openLibrary(name))) break;
        if (vulkan_) surface.vulkanGetInstanceProcAddr = reinterpret_cast<void* (*)(void*, const char*)>(symbol(vulkan_, "vkGetInstanceProcAddr"));
        if (!surface.vulkanGetInstanceProcAddr) {
            *error = "no Vulkan loader";
            close();
            return false;
        }
        return true;  // (no surface, so no instance extensions for one)
    case gpu::Backend::GLES: {
#if defined(_WIN32)
        egl_ = openLibrary(programDirectory() + "libEGL.dll");  // ANGLE's
        gGles = egl_ ? openLibrary(programDirectory() + "libGLESv2.dll") : nullptr;
#elif defined(__ANDROID__)
        egl_ = openLibrary("libEGL.so");
        gGles = egl_ ? openLibrary("libGLESv3.so") : nullptr;
#else
        egl_ = openLibrary("libEGL.so.1");
        gGles = egl_ ? openLibrary("libGLESv2.so.2") : nullptr;
#endif
        auto failed = [&](const char* why) {
            *error = why;
            close();
            return false;
        };
        if (!egl_ || !gGles || !gEgl.load(egl_)) return failed("no EGL and OpenGL ES libraries");
        // Mesa's surfaceless platform where it has it (the GPU without a display server), else the
        // default display.
        if (hasWord(gEgl.queryString(nullptr, kExtensions), "EGL_MESA_platform_surfaceless"))
            if (auto platformDisplay = reinterpret_cast<void* (*)(unsigned, void*, const EGLint*)>(gEgl.getProcAddress("eglGetPlatformDisplayEXT")))
                display_ = platformDisplay(kPlatformSurfacelessMesa, nullptr, nullptr);
        if (!display_) display_ = gEgl.getDisplay(nullptr);
        EGLint major = 0, minor = 0;
        if (!display_ || !gEgl.initialize(display_, &major, &minor)) {
            display_ = nullptr;
            return failed("no EGL display");
        }
        const EGLint attributes[] = {kRenderableType, kOpenGLES3Bit, kSurfaceType, kPbufferBit, kRedSize, 8, kGreenSize, 8, kBlueSize, 8, kAlphaSize, 8, kNone};
        void* config = nullptr;
        EGLint count = 0;
        if (!gEgl.chooseConfig(display_, attributes, &config, 1, &count) || count < 1) return failed("no OpenGL ES 3 configuration with a pbuffer");
        gEgl.bindApi(kOpenGLESApi);
        bool flags = hasWord(gEgl.queryString(display_, kExtensions), "EGL_KHR_create_context");
        EGLint contextAttributes[7] = {kContextMajorVersion, 3, kNone, kNone, kNone, kNone, kNone};
        if (flags) {
            contextAttributes[2] = kContextMinorVersion, contextAttributes[3] = 0;
            if (debug) contextAttributes[4] = kContextFlags, contextAttributes[5] = kContextDebugBit;
        }
        context_ = gEgl.createContext(display_, config, nullptr, contextAttributes);
        const EGLint pbufferAttributes[] = {kWidth, 16, kHeight, 16, kNone};
        pbuffer_ = context_ ? gEgl.createPbufferSurface(display_, config, pbufferAttributes) : nullptr;
        if (!pbuffer_ || !gEgl.makeCurrent(display_, pbuffer_, pbuffer_, context_)) return failed("no OpenGL ES 3 context through EGL");
        surface.glGetProcAddress = [](const char* name) -> void* {
            if (void* f = symbol(gGles, name)) return f;
            return gEgl.getProcAddress(name);
        };
        return true;
    }
#endif
    default: break;
    }
    *error = std::string("no ") + gpu::backendName(backend) + " without a window here";
    return false;
}

void HeadlessGpu::close() {
#if !defined(__APPLE__)
    if (display_) {
        gEgl.makeCurrent(display_, nullptr, nullptr, nullptr);
        if (pbuffer_) gEgl.destroySurface(display_, pbuffer_);
        if (context_) gEgl.destroyContext(display_, context_);
        gEgl.terminate(display_);
    }
    display_ = context_ = pbuffer_ = nullptr;
    if (gGles) closeLibrary(std::exchange(gGles, nullptr));
    if (egl_) closeLibrary(std::exchange(egl_, nullptr));
    if (vulkan_) closeLibrary(std::exchange(vulkan_, nullptr));
#endif
}

} // namespace kompoz::platform
