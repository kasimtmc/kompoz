#include "file_dialog.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <shobjidl.h>

#include <string>
#include <thread>
#include <vector>

namespace kompoz::platform {

namespace {

using kompoz::detail::FileDialogRequest;
using kompoz::detail::FileDialogResult;
using Kind = FileDialogRequest::Kind;

std::wstring wideOf(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}
std::string utf8Of(const wchar_t* w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n > 0 ? n - 1 : 0), '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

template <class T>
struct Com {
    T* p = nullptr;
    ~Com() {
        if (p) p->Release();
    }
    T** operator&() { return &p; }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

std::string pathOf(IShellItem* item) {
    PWSTR path = nullptr;
    std::string out;
    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
        out = utf8Of(path);
        CoTaskMemFree(path);
    }
    return out;
}

// What the dialog's type box offers: the patterns of the extensions (or the common images' and
// videos'), each "*.ext".
struct Filter {
    std::wstring name, patterns;
};
std::vector<Filter> filtersOf(const FileDialogRequest& r) {
    auto join = [](const std::vector<std::string>& extensions) {
        Filter f;
        for (const std::string& e : extensions) {
            f.name += (f.name.empty() ? L"" : L", ") + wideOf(e);
            f.patterns += (f.patterns.empty() ? L"" : L";") + (L"*." + wideOf(e));
        }
        return f;
    };
    const std::vector<std::string> images = {"png", "jpg", "jpeg", "gif", "webp", "bmp", "heic", "avif", "svg"};
    const std::vector<std::string> videos = {"mp4", "webm", "mov", "mkv", "avi", "m4v", "wmv"};
    std::vector<Filter> out;
    if (r.kind == Kind::SaveFile) {
        if (!r.allowedExtensions.empty()) out.push_back(join(r.allowedExtensions));
    } else if (r.kind == Kind::OpenFiles) {
        switch (r.type.kind) {
        case FileKitType::Kind::File:
            if (!r.type.extensions.empty()) out.push_back(join(r.type.extensions));
            break;
        case FileKitType::Kind::Image: out.push_back(join(images)); break;
        case FileKitType::Kind::Video: out.push_back(join(videos)); break;
        case FileKitType::Kind::ImageAndVideo: {
            std::vector<std::string> both = images;
            both.insert(both.end(), videos.begin(), videos.end());
            out.push_back(join(both));
            break;
        }
        }
    }
    if (!out.empty()) out.push_back({L"*.*", L"*.*"});  // (and any file, as Windows' dialogs offer)
    return out;
}

FileDialogResult show(HWND owner, const FileDialogRequest& r) {
    bool save = r.kind == Kind::SaveFile;
    Com<IFileDialog> dialog;
    // (IFileSaveDialog and IFileOpenDialog are IFileDialogs: kept as one)
    HRESULT hr = save ? CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_IFileSaveDialog, reinterpret_cast<void**>(&dialog.p))
                      : CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_IFileOpenDialog, reinterpret_cast<void**>(&dialog.p));
    if (FAILED(hr) || !dialog) return {.error = "kompoz: Windows' file dialog couldn't be made"};
    FILEOPENDIALOGOPTIONS options = 0;
    dialog->GetOptions(&options);
    options |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST;
    if (r.kind == Kind::OpenFiles) options |= FOS_FILEMUSTEXIST | (r.multiple ? FILEOPENDIALOGOPTIONS(FOS_ALLOWMULTISELECT) : FILEOPENDIALOGOPTIONS(0));
    if (r.kind == Kind::OpenDirectory) options |= FOS_PICKFOLDERS;
    if (save) options |= FOS_OVERWRITEPROMPT;
    dialog->SetOptions(options);
    if (r.title) dialog->SetTitle(wideOf(*r.title).c_str());
    std::vector<Filter> filters = filtersOf(r);
    std::vector<COMDLG_FILTERSPEC> specs;
    for (const Filter& f : filters) specs.push_back({f.name.c_str(), f.patterns.c_str()});
    if (!specs.empty()) dialog->SetFileTypes(UINT(specs.size()), specs.data());
    if (!r.directory.empty()) {
        Com<IShellItem> folder;
        if (SUCCEEDED(SHCreateItemFromParsingName(wideOf(r.directory).c_str(), nullptr, IID_PPV_ARGS(&folder.p)))) dialog->SetFolder(folder.p);
    }
    if (save) {
        std::string name = r.suggestedName;
        if (r.defaultExtension && !r.defaultExtension->empty()) {
            name += "." + *r.defaultExtension;
            dialog->SetDefaultExtension(wideOf(*r.defaultExtension).c_str());
        }
        dialog->SetFileName(wideOf(name).c_str());
    }
    hr = dialog->Show(owner);
    if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return {};
    if (FAILED(hr)) return {.error = "kompoz: Windows' file dialog failed"};
    FileDialogResult result;
    if (r.kind == Kind::OpenFiles) {
        Com<IFileOpenDialog> open;
        Com<IShellItemArray> items;
        if (SUCCEEDED(dialog->QueryInterface(IID_PPV_ARGS(&open.p))) && SUCCEEDED(open->GetResults(&items.p))) {
            DWORD count = 0;
            items->GetCount(&count);
            for (DWORD i = 0; i < count; ++i) {
                Com<IShellItem> item;
                if (SUCCEEDED(items->GetItemAt(i, &item.p)))
                    if (std::string path = pathOf(item.p); !path.empty()) result.files.push_back(std::move(path));
            }
        }
    } else {
        Com<IShellItem> item;
        if (SUCCEEDED(dialog->GetResult(&item.p)))
            if (std::string path = pathOf(item.p); !path.empty()) result.files.push_back(std::move(path));
    }
    return result;
}

} // namespace

void showWin32FileDialog(void* owner, const FileDialogRequest& request, std::function<void(FileDialogResult)> done) {
    // On a thread of its own (an apartment of its own, as the dialogs want), owned by the window: modal
    // to it while the app goes on drawing.
    std::thread([owner, request, done = std::move(done)] {
        HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        FileDialogResult result = show(static_cast<HWND>(owner), request);
        if (SUCCEEDED(com)) CoUninitialize();
        done(std::move(result));
    }).detach();
}

} // namespace kompoz::platform
