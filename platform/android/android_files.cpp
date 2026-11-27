#include "android_files.hpp"

#include "android_ime.hpp"

#include <kompoz/filekit.hpp>

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace kompoz::android_files {

namespace {

using kompoz::detail::FileDialogRequest;
using kompoz::detail::FileDialogResult;

jclass gClass = nullptr;
jmethodID gShow, gOpen, gQuery, gChildren, gCreate, gRemove, gPersist;

std::mutex gLock;
std::map<int64_t, std::function<void(FileDialogResult)>> gPending;
int64_t gNext = 1;

JNIEnv* env() { return static_cast<JNIEnv*>(kompoz::android_ime::jniEnv()); }

bool failed(JNIEnv* e) {
    if (!e->ExceptionCheck()) return false;
    e->ExceptionDescribe();
    e->ExceptionClear();
    return true;
}

std::string str(JNIEnv* e, jstring s) {
    if (!s) return {};
    const char* chars = e->GetStringUTFChars(s, nullptr);
    std::string out = chars ? chars : "";
    if (chars) e->ReleaseStringUTFChars(s, chars);
    return out;
}

std::vector<std::string> strings(JNIEnv* e, jobjectArray array) {
    std::vector<std::string> out;
    if (!array) return out;
    for (jsize i = 0, n = e->GetArrayLength(array); i < n; ++i) {
        auto s = static_cast<jstring>(e->GetObjectArrayElement(array, i));
        out.push_back(str(e, s));
        e->DeleteLocalRef(s);
    }
    return out;
}

// A Java string of UTF-8 text (NewStringUTF takes modified UTF-8: fine for what has no NUL and no
// characters past the BMP, which a path or URI may have; so through a byte[] and String(bytes, UTF-8)).
jstring jstr(JNIEnv* e, const std::string& s) {
    jbyteArray bytes = e->NewByteArray(jsize(s.size()));
    e->SetByteArrayRegion(bytes, 0, jsize(s.size()), reinterpret_cast<const jbyte*>(s.data()));
    jclass stringClass = e->FindClass("java/lang/String");
    jmethodID make = e->GetMethodID(stringClass, "<init>", "([BLjava/lang/String;)V");
    jstring utf8 = e->NewStringUTF("UTF-8");
    auto out = static_cast<jstring>(e->NewObject(stringClass, make, bytes, utf8));
    e->DeleteLocalRef(utf8);
    e->DeleteLocalRef(bytes);
    e->DeleteLocalRef(stringClass);
    return out;
}

} // namespace

void init(JNIEnv* e) {
    if (gClass) return;
    jclass local = e->FindClass("dev/kompoz/KompozFiles");
    if (!local || failed(e)) return;
    gClass = static_cast<jclass>(e->NewGlobalRef(local));
    e->DeleteLocalRef(local);
    gShow = e->GetStaticMethodID(gClass, "show", "(Landroid/app/Activity;JII[Ljava/lang/String;ZILjava/lang/String;Ljava/lang/String;)V");
    gOpen = e->GetStaticMethodID(gClass, "open", "(Ljava/lang/String;Ljava/lang/String;)I");
    gQuery = e->GetStaticMethodID(gClass, "query", "(Ljava/lang/String;)[Ljava/lang/String;");
    gChildren = e->GetStaticMethodID(gClass, "children", "(Ljava/lang/String;)[Ljava/lang/String;");
    gCreate = e->GetStaticMethodID(gClass, "create", "(Ljava/lang/String;Ljava/lang/String;Z)Ljava/lang/String;");
    gRemove = e->GetStaticMethodID(gClass, "remove", "(Ljava/lang/String;)Z");
    gPersist = e->GetStaticMethodID(gClass, "persist", "(Ljava/lang/String;)V");
    if (failed(e)) {
        e->DeleteGlobalRef(gClass);
        gClass = nullptr;
        return;
    }
    // (any thread: worker threads are attached to the VM as they ask)
    kompoz::detail::setContentFiles({
        .open = [](const std::string& uri, const char* mode) {
            JNIEnv* e = env();
            jstring u = jstr(e, uri), m = e->NewStringUTF(mode);
            int fd = e->CallStaticIntMethod(gClass, gOpen, u, m);
            e->DeleteLocalRef(u), e->DeleteLocalRef(m);
            return failed(e) ? -1 : fd;
        },
        .query = [](const std::string& uri) -> std::optional<kompoz::detail::ContentFileInfo> {
            JNIEnv* e = env();
            jstring u = jstr(e, uri);
            auto row = static_cast<jobjectArray>(e->CallStaticObjectMethod(gClass, gQuery, u));
            e->DeleteLocalRef(u);
            if (failed(e) || !row) return std::nullopt;
            std::vector<std::string> v = strings(e, row);
            e->DeleteLocalRef(row);
            if (v.size() < 5) return std::nullopt;
            return kompoz::detail::ContentFileInfo{.name = v[0], .size = std::stoll(v[1]), .mimeType = v[2], .lastModified = std::stoll(v[3]),
                                                   .directory = v[4] == "d"};
        },
        .children = [](const std::string& uri) {
            JNIEnv* e = env();
            jstring u = jstr(e, uri);
            auto list = static_cast<jobjectArray>(e->CallStaticObjectMethod(gClass, gChildren, u));
            e->DeleteLocalRef(u);
            std::vector<std::pair<std::string, std::string>> out;
            if (failed(e) || !list) return out;
            std::vector<std::string> v = strings(e, list);
            e->DeleteLocalRef(list);
            for (size_t i = 0; i + 1 < v.size(); i += 2) out.emplace_back(std::move(v[i]), std::move(v[i + 1]));
            return out;
        },
        .create = [](const std::string& parent, const std::string& name, bool directory) -> std::optional<std::string> {
            JNIEnv* e = env();
            jstring p = jstr(e, parent), n = jstr(e, name);
            auto made = static_cast<jstring>(e->CallStaticObjectMethod(gClass, gCreate, p, n, jboolean(directory)));
            e->DeleteLocalRef(p), e->DeleteLocalRef(n);
            if (failed(e) || !made) return std::nullopt;
            std::string out = str(e, made);
            e->DeleteLocalRef(made);
            return out;
        },
        .remove = [](const std::string& uri) {
            JNIEnv* e = env();
            jstring u = jstr(e, uri);
            bool removed = e->CallStaticBooleanMethod(gClass, gRemove, u);
            e->DeleteLocalRef(u);
            return !failed(e) && removed;
        },
        .persist = [](const std::string& uri) {
            JNIEnv* e = env();
            jstring u = jstr(e, uri);
            e->CallStaticVoidMethod(gClass, gPersist, u);
            e->DeleteLocalRef(u);
            failed(e);
        },
    });
}

void show(JNIEnv* e, jobject activity, const FileDialogRequest& r, std::function<void(FileDialogResult)> done) {
    if (!gClass) return done({.error = "kompoz: dev.kompoz.KompozFiles isn't in the app"});
    int64_t id;
    {
        std::lock_guard lock(gLock);
        id = gNext++;
        gPending.emplace(id, std::move(done));
    }
    jclass stringClass = e->FindClass("java/lang/String");
    jobjectArray extensions = e->NewObjectArray(jsize(r.type.extensions.size()), stringClass, nullptr);
    for (size_t i = 0; i < r.type.extensions.size(); ++i) {
        jstring s = jstr(e, r.type.extensions[i]);
        e->SetObjectArrayElement(extensions, jsize(i), s);
        e->DeleteLocalRef(s);
    }
    std::string name = r.suggestedName;
    if (r.defaultExtension && !r.defaultExtension->empty()) name += "." + *r.defaultExtension;
    jstring suggested = jstr(e, name);
    jstring initial = r.directory.empty() ? nullptr : jstr(e, r.directory);
    e->CallStaticVoidMethod(gClass, gShow, activity, jlong(id), jint(r.kind), jint(r.type.kind), extensions, jboolean(r.multiple),
                            jint(r.maxItems.value_or(0)), suggested, initial);
    e->DeleteLocalRef(extensions), e->DeleteLocalRef(stringClass), e->DeleteLocalRef(suggested);
    if (initial) e->DeleteLocalRef(initial);
    if (failed(e)) {
        std::function<void(FileDialogResult)> f;
        {
            std::lock_guard lock(gLock);
            auto it = gPending.find(id);
            if (it == gPending.end()) return;
            f = std::move(it->second);
            gPending.erase(it);
        }
        f({.error = "kompoz: the file dialog couldn't be shown"});
    }
}

} // namespace kompoz::android_files

extern "C" JNIEXPORT void JNICALL Java_dev_kompoz_KompozFiles_nativeOnResult(JNIEnv* e, jclass, jlong request, jobjectArray uris, jstring error) {
    using namespace kompoz::android_files;
    std::function<void(kompoz::detail::FileDialogResult)> done;
    {
        std::lock_guard lock(gLock);
        auto it = gPending.find(request);
        if (it == gPending.end()) return;
        done = std::move(it->second);
        gPending.erase(it);
    }
    kompoz::detail::FileDialogResult result;
    if (error) result.error = str(e, error);
    else result.files = strings(e, uris);
    done(std::move(result));
}
