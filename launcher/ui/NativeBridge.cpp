#include "NativeBridge.h"
#include "NativeViewRegistry.h"
#include "LayoutInflater.h"
#include "ActivityManager.h"
#include "../../runtime/bytecode_interpreter/BytecodeInterpreter.h"
#include "../../runtime/bytecode_interpreter/Frame.h"
#include "../../runtime/dex_loader/DexFile.h"
#include "../../runtime/object_model/StringObject.h"
#include "../../runtime/object_model/ClassObject.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <mutex>
#include <set>
#include <cstring>

namespace fs = std::filesystem;

// Persistent log of every native-method miss. The launcher reads this
// file after a run to determine which Android framework classes still
// need to be implemented. The set is deduplicated so an APK that calls
// the same missing method thousands of times only shows up once.
namespace {
std::mutex g_missMutex;
std::set<std::string> g_seenMisses;
void logMiss(const std::string& line) {
    std::lock_guard<std::mutex> lock(g_missMutex);
    if (g_seenMisses.insert(line).second) {
        std::ofstream f("logs\\native_bridge_missing.log", std::ios::app);
        if (f) {
            f << line << "\n";
        }
    }
}
void logHit(const std::string& line) {
    static std::mutex m;
    static std::set<std::string> seen;
    std::lock_guard<std::mutex> lock(m);
    if (seen.insert(line).second) {
        std::ofstream f("logs\\native_bridge_registered.log", std::ios::app);
        if (f) f << line << "\n";
    }
}
}

using namespace NativeDex::Runtime;

namespace {
inline uint32_t u32f(float f) { uint32_t v; std::memcpy(&v, &f, 4); return v; }
inline uint64_t u64(double d) { uint64_t v; std::memcpy(&v, &d, 8); return v; }
inline int32_t i32(uint32_t v) { return static_cast<int32_t>(v); }
}
using namespace NativeDex::Runtime::BytecodeInterpreter;
using NativeDex::Runtime::ObjectModel::StringObject;
using NativeDex::Runtime::ObjectModel::ClassObject;

namespace {
class OpaqueHandleObject : public NativeDex::Runtime::ObjectModel::Object {
public:
    explicit OpaqueHandleObject(uint64_t value) : value_(value) {}
    size_t objectSize() const override { return sizeof(*this); }
    void visitReferences(void (*visitor)(NativeDex::Runtime::ObjectModel::Object**)) override {
        (void)visitor;
    }
    uint64_t value() const { return value_; }
private:
    uint64_t value_;
};

class BoxedIntObject : public NativeDex::Runtime::ObjectModel::Object {
public:
    explicit BoxedIntObject(int32_t value) : value_(value) {}
    size_t objectSize() const override { return sizeof(*this); }
    void visitReferences(void (*visitor)(NativeDex::Runtime::ObjectModel::Object**)) override {
        (void)visitor;
    }
    int32_t value() const { return value_; }
private:
    int32_t value_;
};

static NativeDex::Runtime::ObjectModel::Object* makeOpaqueHandle(uint64_t id) {
    return new OpaqueHandleObject(id);
}
}

namespace NativeDex::Launcher {

size_t NativeBridge::KeyHash::operator()(const Key& k) const noexcept {
    auto h1 = std::hash<std::string>{}(k.cls);
    auto h2 = std::hash<std::string>{}(k.name);
    auto h3 = std::hash<std::string>{}(k.proto);
    return h1 ^ (h2 * 0x9E3779B97F4A7C15ULL) ^ (h3 * 0xBF58476D1CE4E5B9ULL);
}

NativeBridge& NativeBridge::instance() {
    static NativeBridge inst;
    return inst;
}

void NativeBridge::registerMethod(const std::string& cls,
                                  const std::string& name,
                                  const std::string& proto,
                                  NativeBridge::NativeHandler handler) {
    table_[Key{cls, name, proto}] = std::move(handler);
}

NativeBridge::NativeHandler NativeBridge::find(const std::string& classDescriptor,
                                               const std::string& methodName,
                                               const std::string& methodProto) const {
    // Direct lookup first.
    auto it = table_.find(Key{classDescriptor, methodName, methodProto});
    if (it != table_.end()) {
        logHit(classDescriptor + "->" + methodName + " " + methodProto);
        return it->second;
    }

    // Walk known framework base classes so calls on AppCompatActivity /
    // MainActivity / etc. resolve to our Android.* handlers.
    static const char* const kFallbackClasses[] = {
        "Landroid/app/Activity;",
        "Landroidx/appcompat/app/AppCompatActivity;",
        "Landroidx/activity/ComponentActivity;",
        "Landroidx/core/app/ComponentActivity;",
        "Landroid/content/Context;",
        "Landroid/content/ContextWrapper;",
        "Ljava/lang/Object;",
    };
    for (const char* base : kFallbackClasses) {
        auto jt = table_.find(Key{base, methodName, methodProto});
        if (jt != table_.end()) {
            logHit(std::string(base) + "==>" + classDescriptor + "->" + methodName + " " + methodProto);
            return jt->second;
        }
    }

    // Persistently log the miss so we know what to implement next.
    logMiss(classDescriptor + "->" + methodName + " " + methodProto);
    return nullptr;
}

bool NativeBridge::lookupByMethodIndex(const void* dexFileVoid, uint32_t methodIdx,
                                       NativeBridge::NativeHandler& outHandler) const {
    auto* dexFile = reinterpret_cast<const DexLoader::DexFile*>(dexFileVoid);
    if (!dexFile || methodIdx >= dexFile->methods.size()) return false;
    const auto& m = dexFile->methods.getMethod(methodIdx);
    std::string cls(dexFile->types.getTypeDescriptor(m.classIdx, dexFile->strings));
    std::string name(dexFile->strings.getString(m.nameIdx));
    std::string proto = DexLoader::DexFile::buildProtoString(dexFile, m.protoIdx);
    outHandler = find(cls, name, proto);
    return (bool)outHandler;
}

bool NativeBridge::lookupByMethodIndex(
    const std::vector<const DexLoader::DexFile*>& dexFiles,
    uint32_t methodIdx, NativeBridge::NativeHandler& outHandler) const {
    for (auto* dexFile : dexFiles) {
        if (!dexFile || methodIdx >= dexFile->methods.size()) continue;
        const auto& m = dexFile->methods.getMethod(methodIdx);
        std::string cls(dexFile->types.getTypeDescriptor(m.classIdx,
                                                         dexFile->strings));
        std::string name(dexFile->strings.getString(m.nameIdx));
        std::string proto = DexLoader::DexFile::buildProtoString(dexFile,
                                                                  m.protoIdx);
        outHandler = find(cls, name, proto);
        if (outHandler) return true;
    }
    return false;
}
static uint32_t readU32(Frame* f, uint16_t r)        { return f->getRegister(r); }
static int32_t  readI32(Frame* f, uint16_t r)        { return static_cast<int32_t>(readU32(f, r)); }
static Object*  readObj(Frame* f, uint16_t r)        { return f->getObjectRegister(r); }
static void     writeObj(Frame* f, uint16_t r, Object* o) { f->setObjectRegister(r, o); }
static void     writeU32(Frame* f, uint16_t r, uint32_t v) { f->setRegister(r, v); }

// Read the i-th method argument (not the receiver) from the current
// invoke-kind instruction. For invoke-virtual, args are {v1, v2, ...};
// for invoke-static, args are {v0, v1, ...}. The Interpreter decodes
// the invoke encoding into Frame::argRegisters so handlers can read
// args uniformly.
static uint16_t argReg(Frame* f, size_t i) {
    if (i >= f->argRegisters.size()) return 0;
    return f->argRegisters[i];
}
static uint32_t readArgU32(Frame* f, size_t i) { return readU32(f, argReg(f, i)); }
static int32_t  readArgI32(Frame* f, size_t i) { return readI32(f, argReg(f, i)); }
static Object*  readArgObj(Frame* f, size_t i) { return readObj(f, argReg(f, i)); }

// ---------------------------------------------------------------------------
// Android framework native implementations
// ---------------------------------------------------------------------------

// android.app.Activity.setContentView(int layoutResId)
static void native_Activity_setContentView_int(Frame* f) {
    int32_t layoutId = readArgI32(f, 0);
    auto& reg = NativeViewRegistry::instance();

    // Resolve the layout through the APK's resource table.
    uint64_t rootId = LayoutInflater::instance().inflateById(static_cast<uint32_t>(layoutId));
    if (rootId != 0) {
        reg.setContentView(0, rootId);
    }

    if (auto* am = ActivityManager::global()) {
        am->requestInvalidate();
    }
}

// android.app.Activity.setContentView(android.view.View)
static void native_Activity_setContentView_View(Frame* f) {
    Object* thisObj = readObj(f, 0);
    Object* viewObj = readArgObj(f, 0);
    uint64_t viewId = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(viewObj));
    uint64_t thisId = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(thisObj));
    NativeViewRegistry::instance().setContentView(thisId, viewId);
    if (auto* am = ActivityManager::global()) am->requestInvalidate();
}

// android.app.Activity.findViewById(int)
static void native_Activity_findViewById(Frame* f) {
    int32_t id = readArgI32(f, 0);
    uint64_t viewId = LayoutInflater::instance().resolveByIntId(id);
    if (viewId == 0) viewId = NativeViewRegistry::instance().findViewById(id, "");
    Object* obj = reinterpret_cast<Object*>(static_cast<uintptr_t>(viewId));
    writeObj(f, 0, obj);
}

// android.view.View.<init>(android.content.Context)
static void native_View_init(Frame* f) {
    (void)f; // The view object itself is in v0; nothing to do for the native side.
}

// android.widget.TextView.<init>(android.content.Context)
static void native_TextView_init(Frame* f) {
    (void)f; // Real init happens via setText below
}

// android.widget.TextView.setText(java.lang.CharSequence)
static void native_TextView_setText(Frame* f) {
    Object* viewObj = readObj(f, 0);
    Object* arg = readArgObj(f, 0);
    std::string text;
    if (arg) text = static_cast<StringObject*>(arg)->toUtf8();
    uint64_t viewId = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(viewObj));
    NativeViewRegistry::instance().setText(viewId, text);
}

// android.widget.TextView.setTextSize(float)
static void native_TextView_setTextSize(Frame* f) {
    (void)f; // Text size left at default.
}

// android.widget.LinearLayout.<init>(android.content.Context)
static void native_LinearLayout_init(Frame* f) {
    (void)f;
}

// android.view.View.setLayoutParams(android.view.ViewGroup$LayoutParams)
static void native_View_setLayoutParams(Frame* f) {
    // The LayoutParams opaque id encodes (width, height) directly: low 16
    // bits = width, next 16 bits = height. Real Android uses a typed object
    // for LayoutParams; the bytecode usually builds one inline, so we
    // re-derive the values from the LinearLayout's width/height properties.
    // For simplicity, just accept the call. Layout is handled by the parent.
    (void)f;
}

// android.view.View.setBackgroundColor(int)
static void native_View_setBackgroundColor(Frame* f) {
    Object* viewObj = readObj(f, 0);
    int32_t color = readArgI32(f, 0);
    uint64_t viewId = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(viewObj));
    NativeViewRegistry::instance().webViewSetBackgroundColor(viewId, static_cast<uint32_t>(color));
    if (auto* am = ActivityManager::global()) am->requestInvalidate();
}

// android.widget.Button.<init>(android.content.Context)
static void native_Button_init(Frame* f) {
    (void)f;
}

// android.widget.Button.setText(java.lang.CharSequence)
static void native_Button_setText(Frame* f) {
    native_TextView_setText(f);
}

// android.view.View.setOnClickListener(android.view.View$OnClickListener)
static void native_View_setOnClickListener(Frame* f) {
    (void)f; // Click handling is out of scope for this milestone.
}

// android.util.Log.d/java.lang.String/java.lang.String
static void native_Log_d(Frame* f) {
    Object* tagObj = readArgObj(f, 0);
    Object* msgObj = readArgObj(f, 1);
    std::string tag = (tagObj && static_cast<StringObject*>(tagObj))
        ? static_cast<StringObject*>(tagObj)->toUtf8() : "?";
    std::string msg = (msgObj && static_cast<StringObject*>(msgObj))
        ? static_cast<StringObject*>(msgObj)->toUtf8() : "?";
    std::cout << "[AppLog/" << tag << "] " << msg << std::endl;
}
static void native_Log_i(Frame* f) {
    Object* tagObj = readArgObj(f, 0);
    Object* msgObj = readArgObj(f, 1);
    std::string tag = (tagObj && static_cast<StringObject*>(tagObj))
        ? static_cast<StringObject*>(tagObj)->toUtf8() : "?";
    std::string msg = (msgObj && static_cast<StringObject*>(msgObj))
        ? static_cast<StringObject*>(msgObj)->toUtf8() : "?";
    std::cout << "[AppInfo/" << tag << "] " << msg << std::endl;
}
static void native_Log_w(Frame* f) {
    Object* tagObj = readArgObj(f, 0);
    Object* msgObj = readArgObj(f, 1);
    std::string tag = (tagObj && static_cast<StringObject*>(tagObj))
        ? static_cast<StringObject*>(tagObj)->toUtf8() : "?";
    std::string msg = (msgObj && static_cast<StringObject*>(msgObj))
        ? static_cast<StringObject*>(msgObj)->toUtf8() : "?";
    std::cout << "[AppWarn/" << tag << "] " << msg << std::endl;
}
static void native_Log_e(Frame* f) {
    Object* tagObj = readArgObj(f, 0);
    Object* msgObj = readArgObj(f, 1);
    std::string tag = (tagObj && static_cast<StringObject*>(tagObj))
        ? static_cast<StringObject*>(tagObj)->toUtf8() : "?";
    std::string msg = (msgObj && static_cast<StringObject*>(msgObj))
        ? static_cast<StringObject*>(msgObj)->toUtf8() : "?";
    std::cerr << "[AppErr/" << tag << "] " << msg << std::endl;
}

// ----- WebView natives -----
static void native_WebView_setPadding(Frame* f) {
    Object* viewObj = readObj(f, 0);
    uint64_t viewId = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(viewObj));
    int32_t l = readArgI32(f, 0);
    int32_t t = readArgI32(f, 1);
    int32_t r = readArgI32(f, 2);
    int32_t b = readArgI32(f, 3);
    NativeViewRegistry::instance().webViewSetPadding(viewId, l, t, r, b);
}
static void native_WebView_loadUrl(Frame* f) {
    Object* viewObj = readObj(f, 0);
    Object* urlObj = readArgObj(f, 0);
    uint64_t viewId = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(viewObj));
    std::string url;
    if (urlObj) url = static_cast<StringObject*>(urlObj)->toUtf8();
    NativeViewRegistry::instance().webViewLoadUrl(viewId, url);
    if (url.find("index.html") != std::string::npos || url.find("www/") != std::string::npos) {
        std::string html = LayoutInflater::instance().readAsset("assets\\www\\index.html");
        if (!html.empty()) NativeViewRegistry::instance().webViewSetHtml(viewId, html);
    }
    if (auto* am = ActivityManager::global()) am->requestInvalidate();
}
static void native_WebView_getSettings(Frame* f) {
    Object* viewObj = readObj(f, 0);
    uint64_t viewId = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(viewObj));
    uint64_t settingsId = NativeViewRegistry::instance().webViewGetSettings(viewId);
    writeObj(f, 0, reinterpret_cast<Object*>(static_cast<uintptr_t>(settingsId)));
}
static void native_WebView_addJavascriptInterface(Frame* f) {
    Object* viewObj = readObj(f, 0);
    uint64_t viewId = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(viewObj));
    Object* nameObj = readArgObj(f, 1);
    std::string name;
    if (nameObj) name = static_cast<StringObject*>(nameObj)->toUtf8();
    NativeViewRegistry::instance().webViewAddJavascriptInterface(viewId, name);
}
static void native_WebView_setWebViewClient(Frame* f) {
    (void)f;
}
static void native_WebSettings_setJavaScriptEnabled(Frame* f) {
    Object* viewObj = readObj(f, 0);
    uint64_t viewId = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(viewObj));
    int32_t enabled = readArgI32(f, 0);
    NativeViewRegistry::instance().webViewSettingsSetJavaScriptEnabled(viewId, enabled != 0);
}

// ----- Window / Decor / Insets -----
static void native_Activity_getWindow(Frame* f) {
    Object* self = readObj(f, 0);
    uint64_t thisId = self ? static_cast<uint64_t>(reinterpret_cast<uintptr_t>(self)) : 0;
    writeObj(f, 0, makeOpaqueHandle(thisId));
}
static void native_Activity_setTitle(Frame* f) {
    Object* titleObj = readArgObj(f, 0);
    std::string title;
    if (titleObj) {
        if (auto* str = dynamic_cast<StringObject*>(titleObj)) {
            title = str->toUtf8();
        }
    }
    std::cout << "[AppTitle] " << title << std::endl;
}
static void native_Activity_getSystemService(Frame* f) {
    (void)f;
    writeObj(f, 0, makeOpaqueHandle(0xB0000001ULL));
}
static void native_Window_getDecorView(Frame* f) {
    writeObj(f, 0, makeOpaqueHandle(0xB0000002ULL));
}
static void native_WindowCompat_getInsetsController(Frame* f) {
    writeObj(f, 0, makeOpaqueHandle(0xB0000003ULL));
}

// ----- View.getLayoutParams: just return self so subsequent setLayoutParams works -----
static void native_View_getLayoutParams(Frame* f) {
    Object* viewObj = readObj(f, 0);
    writeObj(f, 0, viewObj);
}

// ----- Kotlin intrinsics -----
static void native_Intrinsics_throwUninit(Frame* /*f*/) {
    // Silently swallow LateInit exceptions: many Kotlin properties are
    // declared lateinit without ever being initialized in our test app,
    // and the spamming output makes the log unreadable.
}

// ----- Number/Integer boxing -----
static void native_Integer_valueOf(Frame* f) {
    int32_t v = readArgI32(f, 0);
    writeObj(f, 0, new BoxedIntObject(v));
}
static void native_Number_intValue(Frame* f) {
    Object* obj = readObj(f, 0);
    if (auto* boxed = dynamic_cast<BoxedIntObject*>(obj)) {
        writeU32(f, 0, static_cast<uint32_t>(boxed->value()));
        return;
    }

    uintptr_t bits = reinterpret_cast<uintptr_t>(obj);
    int32_t v = static_cast<int32_t>(bits & 0xFFFFFFFFULL);
    writeU32(f, 0, static_cast<uint32_t>(v));
}

// ----- Misc helpers -----
static void native_View_noop(Frame* /*f*/) { /* no operation */ }

static void native_Intrinsics_checkNotNull(Frame* /*f*/) {
    // Strictly speaking this throws on null. We do not track null well
    // enough; assume the bytecode has already null-checked.
}

// ----- ConnectivityManager / NetworkInfo (offline stub) -----
static void native_ConnectivityManager_getActiveNetworkInfo(Frame* f) {
    writeObj(f, 0, makeOpaqueHandle(0xB0000010ULL));
}
static void native_NetworkInfo_isConnected(Frame* f) {
    writeU32(f, 0, 1); // assume connected
}
static void native_NetworkInfo_getType(Frame* f) {
    writeU32(f, 0, 1); // WIFI
}

// ----- Convenience: read a text file at startup -----
static std::string readTextFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::stringstream ss; ss << in.rdbuf();
    return ss.str();
}

// Helpers used by installDefaults() below.
static void native_StringBuilder_toString(Frame* f) {
    writeObj(f, 0, StringObject::create(""));
}
static void native_File_exists(Frame* f) {
    writeU32(f, 0, 0); // file doesn't exist; loadLocalFrontend falls back to assets
}
static void native_File_getAbsolutePath(Frame* f) {
    writeObj(f, 0, StringObject::create(""));
}
static void native_View_noop_returning_false(Frame* f) {
    writeU32(f, 0, 0);
}

static void native_View_noop_returning_true(Frame* f) {
    writeU32(f, 0, 1);
}

static void native_View_noop_returning_zero(Frame* f) {
    writeU32(f, 0, 0);
}

static void native_View_noop_returning_null(Frame* f) {
    if (f) f->setObjectRegister(0, nullptr);
}

// Sentinel field IDs used to stash per-Intent metadata on the
// Object's objFields map. Chosen well outside any real Java field index.
constexpr uint64_t INTENT_TARGET_CLASS_FIELD = 0xE0E1E2E3ull; // String objField

// android.app.Activity.startActivity(Intent)
// Reads the target class string stashed on the Intent by Intent.setClass /
// Intent.setClassName, converts "com.foo.Bar" -> "Lcom/foo/Bar;", and asks
// the ActivityManager to launch it. Falls back to no-op if none is set.
static void native_Activity_startActivity(Frame* f) {
    Object* intent = readArgObj(f, 0);
    if (!intent) {
        std::cerr << "[startActivity] no intent arg" << std::endl;
        return;
    }

    Object* targetClass = intent->getObjField(INTENT_TARGET_CLASS_FIELD);
    if (!targetClass) {
        std::cerr << "[startActivity] no target class stashed on intent" << std::endl;
        return;
    }

    auto* targetStr = dynamic_cast<StringObject*>(targetClass);
    if (!targetStr) {
        std::cerr << "[startActivity] target not a string" << std::endl;
        return;
    }
    std::string descriptor = targetStr->toUtf8();
    if (descriptor.empty()) return;

    std::string slashed;
    for (char c : descriptor) {
        slashed += (c == '.') ? '/' : c;
    }
    std::string fullDesc = "L" + slashed + ";";

    std::cerr << "[startActivity] launching " << fullDesc << std::endl;
    if (auto* am = ActivityManager::global()) {
        am->launchActivity(fullDesc);
    } else {
        std::cerr << "[startActivity] no activity manager" << std::endl;
    }
}

static void native_Activity_startActivityForResult(Frame* f) {
    native_Activity_startActivity(f);
}

void NativeBridge::installDefaults() {
    if (installed_) return;
    installed_ = true;

    // ----- WebView -----
    registerMethod("Landroid/webkit/WebView;", "<init>", "(Landroid/content/Context;)V",
                   &native_View_init);
    registerMethod("Landroid/webkit/WebView;", "setBackgroundColor", "(I)V",
                   &native_View_setBackgroundColor);
    registerMethod("Landroid/webkit/WebView;", "setPadding", "(IIII)V",
                   &native_WebView_setPadding);
    registerMethod("Landroid/webkit/WebView;", "getLayoutParams",
                   "()Landroid/view/ViewGroup$LayoutParams;",
                   &native_View_getLayoutParams);
    registerMethod("Landroid/webkit/WebView;", "setLayoutParams",
                   "(Landroid/view/ViewGroup$LayoutParams;)V",
                   &native_View_setLayoutParams);
    registerMethod("Landroid/webkit/WebView;", "loadUrl", "(Ljava/lang/String;)V",
                   &native_WebView_loadUrl);
    registerMethod("Landroid/webkit/WebView;", "getSettings", "()Landroid/webkit/WebSettings;",
                   &native_WebView_getSettings);
    registerMethod("Landroid/webkit/WebView;", "addJavascriptInterface",
                   "(Ljava/lang/Object;Ljava/lang/String;)V",
                   &native_WebView_addJavascriptInterface);
    registerMethod("Landroid/webkit/WebView;", "setWebViewClient",
                   "(Landroid/webkit/WebViewClient;)V",
                   &native_WebView_setWebViewClient);
    registerMethod("Landroid/webkit/WebView;", "evaluateJavascript",
                   "(Ljava/lang/String;Landroid/webkit/ValueCallback;)V",
                   &native_WebView_loadUrl); // best-effort: route to loadUrl
    registerMethod("Landroid/webkit/WebView;", "postUrl",
                   "(Ljava/lang/String;[B)V",
                   &native_WebView_loadUrl);

    // ----- WebSettings -----
    registerMethod("Landroid/webkit/WebSettings;", "setJavaScriptEnabled", "(Z)V",
                   &native_WebSettings_setJavaScriptEnabled);
    registerMethod("Landroid/webkit/WebSettings;", "setDomStorageEnabled", "(Z)V",
                   &native_View_noop);
    registerMethod("Landroid/webkit/WebSettings;", "setLoadWithOverviewMode", "(Z)V",
                   &native_View_noop);
    registerMethod("Landroid/webkit/WebSettings;", "setUseWideViewPort", "(Z)V",
                   &native_View_noop);
    registerMethod("Landroid/webkit/WebSettings;", "setSupportZoom", "(Z)V",
                   &native_View_noop);
    registerMethod("Landroid/webkit/WebSettings;", "setBuiltInZoomControls", "(Z)V",
                   &native_View_noop);
    registerMethod("Landroid/webkit/WebSettings;", "setDisplayZoomControls", "(Z)V",
                   &native_View_noop);
    registerMethod("Landroid/webkit/WebSettings;", "setAllowFileAccess", "(Z)V",
                   &native_View_noop);
    registerMethod("Landroid/webkit/WebSettings;", "setAllowContentAccess", "(Z)V",
                   &native_View_noop);
    registerMethod("Landroid/webkit/WebSettings;", "setCacheMode", "(I)V",
                   &native_View_noop);

    // ----- Toolbar -----
    registerMethod("Landroidx/appcompat/widget/Toolbar;", "getLayoutParams",
                   "()Landroid/view/ViewGroup$LayoutParams;",
                   &native_View_getLayoutParams);
    registerMethod("Landroidx/appcompat/widget/Toolbar;", "setLayoutParams",
                   "(Landroid/view/ViewGroup$LayoutParams;)V",
                   &native_View_setLayoutParams);

    // ----- ImageButton -----
    registerMethod("Landroid/widget/ImageButton;", "setOnClickListener",
                   "(Landroid/view/View$OnClickListener;)V",
                   &native_View_setOnClickListener);

    // ----- ImageView -----
    registerMethod("Landroid/widget/ImageView;", "setImageResource", "(I)V",
                   &native_View_noop);

    // ----- ProgressBar -----
    registerMethod("Landroid/widget/ProgressBar;", "setMax", "(I)V",
                   &native_View_noop);
    registerMethod("Landroid/widget/ProgressBar;", "setProgress", "(I)V",
                   &native_View_noop);
    registerMethod("Landroid/widget/ProgressBar;", "setIndeterminate", "(Z)V",
                   &native_View_noop);
    registerMethod("Landroid/widget/ProgressBar;", "setVisibility", "(I)V",
                   &native_View_noop);

    // ----- android.app.Activity -----
    registerMethod("Landroid/app/Activity;", "setContentView", "(I)V",
                   &native_Activity_setContentView_int);
    registerMethod("Landroid/app/Activity;", "setContentView",
                   "(Landroid/view/View;)V", &native_Activity_setContentView_View);
    registerMethod("Landroid/app/Activity;", "findViewById",
                   "(I)Landroid/view/View;", &native_Activity_findViewById);
    registerMethod("Landroid/app/Activity;", "getSystemService",
                   "(Ljava/lang/String;)Ljava/lang/Object;",
                   &native_Activity_getSystemService);
    registerMethod("Landroid/app/Activity;", "getWindow",
                   "()Landroid/view/Window;", &native_Activity_getWindow);
    registerMethod("Landroid/app/Activity;", "setTitle",
                   "(Ljava/lang/CharSequence;)V", &native_Activity_setTitle);
    registerMethod("Landroid/app/Activity;", "setTitle", "(I)V",
                   &native_View_noop);

    // ----- android.view.View -----
    registerMethod("Landroid/view/View;", "<init>", "(Landroid/content/Context;)V",
                   &native_View_init);
    registerMethod("Landroid/view/View;", "setLayoutParams",
                   "(Landroid/view/ViewGroup$LayoutParams;)V",
                   &native_View_setLayoutParams);
    registerMethod("Landroid/view/View;", "getLayoutParams",
                   "()Landroid/view/ViewGroup$LayoutParams;",
                   &native_View_getLayoutParams);
    registerMethod("Landroid/view/View;", "setBackgroundColor", "(I)V",
                   &native_View_setBackgroundColor);
    registerMethod("Landroid/view/View;", "setOnClickListener",
                   "(Landroid/view/View$OnClickListener;)V",
                   &native_View_setOnClickListener);
    registerMethod("Landroid/view/View;", "setVisibility", "(I)V",
                   &native_View_noop);
    registerMethod("Landroid/view/View;", "setPadding", "(IIII)V",
                   &native_View_noop);
    registerMethod("Landroid/view/View;", "requestLayout", "()V",
                   &native_View_noop);
    registerMethod("Landroid/view/View;", "invalidate", "()V",
                   &native_View_noop);

    // ----- android.view.Window -----
    registerMethod("Landroid/view/Window;", "setStatusBarColor", "(I)V",
                   &native_View_noop);
    registerMethod("Landroid/view/Window;", "setNavigationBarColor", "(I)V",
                   &native_View_noop);
    registerMethod("Landroid/view/Window;", "getDecorView", "()Landroid/view/View;",
                   &native_Window_getDecorView);
    registerMethod("Landroid/view/Window;", "setContentView", "(Landroid/view/View;)V",
                   &native_Activity_setContentView_View);

    // ----- androidx.core.view.WindowCompat -----
    registerMethod("Landroidx/core/view/WindowCompat;",
                   "setDecorFitsSystemWindows",
                   "(Landroid/view/Window;Z)V",
                   &native_View_noop);
    registerMethod("Landroidx/core/view/WindowCompat;",
                   "getInsetsController",
                   "(Landroid/view/Window;Landroid/view/View;)Landroidx/core/view/WindowInsetsControllerCompat;",
                   &native_WindowCompat_getInsetsController);

    // ----- androidx.core.view.WindowInsetsControllerCompat -----
    registerMethod("Landroidx/core/view/WindowInsetsControllerCompat;",
                   "setAppearanceLightStatusBars", "(Z)V",
                   &native_View_noop);
    registerMethod("Landroidx/core/view/WindowInsetsControllerCompat;",
                   "setAppearanceLightNavigationBars", "(Z)V",
                   &native_View_noop);

    // ----- android.widget.TextView -----
    registerMethod("Landroid/widget/TextView;", "<init>", "(Landroid/content/Context;)V",
                   &native_TextView_init);
    registerMethod("Landroid/widget/TextView;", "setText", "(Ljava/lang/CharSequence;)V",
                   &native_TextView_setText);
    registerMethod("Landroid/widget/TextView;", "setTextSize", "(F)V",
                   &native_TextView_setTextSize);
    registerMethod("Landroid/widget/TextView;", "setVisibility", "(I)V",
                   &native_View_noop);

    // ----- android.widget.Button -----
    registerMethod("Landroid/widget/Button;", "<init>", "(Landroid/content/Context;)V",
                   &native_Button_init);
    registerMethod("Landroid/widget/Button;", "setText", "(Ljava/lang/CharSequence;)V",
                   &native_Button_setText);

    // ----- android.widget.LinearLayout -----
    registerMethod("Landroid/widget/LinearLayout;", "<init>", "(Landroid/content/Context;)V",
                   &native_LinearLayout_init);
    registerMethod("Landroid/widget/LinearLayout;", "setOrientation", "(I)V",
                   &native_View_noop);

    // ----- android.util.Log -----
    registerMethod("Landroid/util/Log;", "d", "(Ljava/lang/String;Ljava/lang/String;)I",
                   &native_Log_d);
    registerMethod("Landroid/util/Log;", "i", "(Ljava/lang/String;Ljava/lang/String;)I",
                   &native_Log_i);
    registerMethod("Landroid/util/Log;", "w", "(Ljava/lang/String;Ljava/lang/String;)I",
                   &native_Log_w);
    registerMethod("Landroid/util/Log;", "e", "(Ljava/lang/String;Ljava/lang/String;)I",
                   &native_Log_e);

    // ----- kotlin.jvm.internal.Intrinsics (most are called as static) -----
    registerMethod("Lkotlin/jvm/internal/Intrinsics;",
                   "checkNotNullExpressionValue",
                   "(Ljava/lang/Object;Ljava/lang/String;)V",
                   &native_Intrinsics_checkNotNull);
    registerMethod("Lkotlin/jvm/internal/Intrinsics;",
                   "checkNotNull",
                   "(Ljava/lang/Object;Ljava/lang/String;)V",
                   &native_Intrinsics_checkNotNull);
    registerMethod("Lkotlin/jvm/internal/Intrinsics;",
                   "checkParameterIsNotNull",
                   "(Ljava/lang/Object;Ljava/lang/String;)V",
                   &native_Intrinsics_checkNotNull);
    registerMethod("Lkotlin/jvm/internal/Intrinsics;",
                   "throwUninitializedPropertyAccessException",
                   "(Ljava/lang/String;)V",
                   &native_Intrinsics_throwUninit);

    // ----- Integer/Number boxing helpers -----
    registerMethod("Ljava/lang/Integer;", "valueOf", "(I)Ljava/lang/Integer;",
                   &native_Integer_valueOf);
    registerMethod("Ljava/lang/Number;", "intValue", "()I",
                   &native_Number_intValue);

    // ----- android.net.ConnectivityManager stubs -----
    registerMethod("Landroid/net/ConnectivityManager;", "getActiveNetworkInfo",
                   "()Landroid/net/NetworkInfo;",
                   &native_ConnectivityManager_getActiveNetworkInfo);
    registerMethod("Landroid/net/ConnectivityManager;", "registerDefaultNetworkCallback",
                   "(Landroid/net/ConnectivityManager$NetworkCallback;)V",
                   &native_View_noop);

    // ----- android.net.NetworkInfo stubs -----
    registerMethod("Landroid/net/NetworkInfo;", "isConnected", "()Z",
                   &native_NetworkInfo_isConnected);
    registerMethod("Landroid/net/NetworkInfo;", "getType", "()I",
                   &native_NetworkInfo_getType);

    // ----- WebView/WebSettings/WebViewClient/WebChromeClient: extra methods -----
    registerMethod("Landroid/webkit/WebView;", "setWebContentsDebuggingEnabled", "(Z)V",
                   &native_View_noop);
    registerMethod("Landroid/webkit/WebView;", "setWebChromeClient", "(Landroid/webkit/WebChromeClient;)V",
                   &native_View_noop);
    registerMethod("Landroid/webkit/WebView;", "setOnTouchListener", "(Landroid/view/View$OnTouchListener;)V",
                   &native_View_noop);
    registerMethod("Landroid/webkit/WebView;", "setOnLongClickListener", "(Landroid/view/View$OnLongClickListener;)V",
                   &native_View_noop);
    registerMethod("Landroid/webkit/WebView;", "setHorizontalScrollBarEnabled", "(Z)V",
                   &native_View_noop);
    registerMethod("Landroid/webkit/WebView;", "setVerticalScrollBarEnabled", "(Z)V",
                   &native_View_noop);
    registerMethod("Landroid/webkit/WebSettings;", "setAllowFileAccessFromFileURLs", "(Z)V",
                   &native_View_noop);
    registerMethod("Landroid/webkit/WebSettings;", "setAllowUniversalAccessFromFileURLs", "(Z)V",
                   &native_View_noop);
    registerMethod("Landroid/webkit/WebSettings;", "setMixedContentMode", "(I)V",
                   &native_View_noop);
    registerMethod("Landroid/webkit/WebSettings;", "setSupportMultipleWindows", "(Z)V",
                   &native_View_noop);
    registerMethod("Landroid/webkit/WebSettings;", "setDatabaseEnabled", "(Z)V",
                   &native_View_noop);
    registerMethod("Landroid/webkit/WebSettings;", "setSavePassword", "(Z)V",
                   &native_View_noop);
    registerMethod("Landroid/webkit/WebSettings;", "setMediaPlaybackRequiresUserGesture", "(Z)V",
                   &native_View_noop);
    registerMethod("Landroid/webkit/WebSettings;", "setTextZoom", "(I)V",
                   &native_View_noop);
    registerMethod("Landroid/webkit/WebViewClient;", "<init>", "()V",
                   &native_View_noop);
    registerMethod("Landroid/webkit/WebChromeClient;", "<init>", "()V",
                   &native_View_noop);

    registerMethod("Landroidx/core/view/ViewCompat;", "requestApplyInsets", "(Landroid/view/View;)V",
                   &native_View_noop);
    registerMethod("Landroid/net/ConnectivityManager;", "getActiveNetwork",
                   "()Landroid/net/Network;", &native_ConnectivityManager_getActiveNetworkInfo);
    registerMethod("Landroid/net/ConnectivityManager;", "unregisterNetworkCallback",
                   "(Landroid/net/ConnectivityManager$NetworkCallback;)V",
                   &native_View_noop);

    registerMethod("Landroid/graphics/Color;", "parseColor", "(Ljava/lang/String;)I",
                   &native_View_noop);
    registerMethod("Landroid/content/res/ColorStateList;", "valueOf",
                   "(I)Landroid/content/res/ColorStateList;",
                   &native_View_noop);
    registerMethod("Landroid/widget/ImageView;", "setImageTintList",
                   "(Landroid/content/res/ColorStateList;)V",
                   &native_View_noop);
    registerMethod("Landroid/widget/ImageView;", "setImageResource", "(I)V",
                   &native_View_noop);

    registerMethod("Landroidx/work/OneTimeWorkRequest$Builder;", "<init>",
                   "(Ljava/lang/Class;)V",
                   &native_View_noop);
    registerMethod("Landroidx/work/OneTimeWorkRequest$Builder;", "build",
                   "()Landroidx/work/OneTimeWorkRequest;",
                   &native_View_noop);
    registerMethod("Landroidx/work/WorkManager;", "getInstance",
                   "(Landroid/content/Context;)Landroidx/work/WorkManager;",
                   &native_View_noop);
    registerMethod("Landroidx/work/WorkManager;", "enqueueUniqueWork",
                   "(Ljava/lang/String;ILandroidx/work/WorkRequest;)Landroidx/work/Operation;",
                   &native_View_noop);

    registerMethod("Ljava/lang/StringBuilder;", "<init>", "()V",
                   &native_View_noop);
    registerMethod("Ljava/lang/StringBuilder;", "append",
                   "(Ljava/lang/String;)Ljava/lang/StringBuilder;",
                   &native_View_noop);
    registerMethod("Ljava/lang/StringBuilder;", "append",
                   "(Ljava/lang/Object;)Ljava/lang/StringBuilder;",
                   &native_View_noop);
    registerMethod("Ljava/lang/StringBuilder;", "toString", "()Ljava/lang/String;",
                   &native_StringBuilder_toString);

    registerMethod("Lokhttp3/Request$Builder;", "<init>", "()V", &native_View_noop);
    registerMethod("Lokhttp3/Request$Builder;", "url",
                   "(Ljava/lang/String;)Lokhttp3/Request$Builder;",
                   &native_View_noop);
    registerMethod("Lokhttp3/Request$Builder;", "get",
                   "()Lokhttp3/Request$Builder;",
                   &native_View_noop);
    registerMethod("Lokhttp3/Request$Builder;", "build",
                   "()Lokhttp3/Request;",
                   &native_View_noop);
    registerMethod("Lokhttp3/OkHttpClient;", "newCall",
                   "(Lokhttp3/Request;)Lokhttp3/Call;",
                   &native_View_noop);
    registerMethod("Lokhttp3/Call;", "enqueue", "(Lokhttp3/Callback;)V",
                   &native_View_noop);

    // File I/O stubs
    registerMethod("Ljava/io/File;", "<init>", "(Ljava/io/File;Ljava/lang/String;)V",
                   &native_View_noop);
    registerMethod("Ljava/io/File;", "exists", "()Z", &native_File_exists);
    registerMethod("Ljava/io/File;", "getAbsolutePath", "()Ljava/lang/String;",
                   &native_File_getAbsolutePath);
    registerMethod("Ljava/io/File;", "delete", "()Z", &native_View_noop_returning_false);

    // =====================================================================
    // Common Java/Android collection & utility classes commonly used by Android apps
    // =====================================================================

    // java.util.HashMap
    registerMethod("Ljava/util/HashMap;", "<init>", "()V", &native_View_noop);
    registerMethod("Ljava/util/HashMap;", "<init>", "(I)V", &native_View_noop);
    registerMethod("Ljava/util/HashMap;", "put",
                   "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
                   &native_View_noop);
    registerMethod("Ljava/util/HashMap;", "get",
                   "(Ljava/lang/Object;)Ljava/lang/Object;", &native_View_noop);
    registerMethod("Ljava/util/HashMap;", "containsKey",
                   "(Ljava/lang/Object;)Z", &native_View_noop_returning_false);
    registerMethod("Ljava/util/HashMap;", "remove",
                   "(Ljava/lang/Object;)Ljava/lang/Object;", &native_View_noop);
    registerMethod("Ljava/util/HashMap;", "size", "()I", &native_View_noop);
    registerMethod("Ljava/util/HashMap;", "isEmpty", "()Z",
                   &native_View_noop_returning_false);
    registerMethod("Ljava/util/HashMap;", "keySet", "()Ljava/util/Set;",
                   &native_View_noop);
    registerMethod("Ljava/util/HashMap;", "values", "()Ljava/util/Collection;",
                   &native_View_noop);
    registerMethod("Ljava/util/HashMap;", "entrySet", "()Ljava/util/Set;",
                   &native_View_noop);
    registerMethod("Ljava/util/HashMap;", "clear", "()V", &native_View_noop);
    registerMethod("Ljava/util/HashMap;", "putAll",
                   "(Ljava/util/Map;)V", &native_View_noop);

    // java.util.LinkedHashMap
    registerMethod("Ljava/util/LinkedHashMap;", "<init>", "()V", &native_View_noop);
    registerMethod("Ljava/util/LinkedHashMap;", "<init>", "(I)V", &native_View_noop);
    registerMethod("Ljava/util/LinkedHashMap;", "<init>", "(IFZ)V", &native_View_noop);
    registerMethod("Ljava/util/LinkedHashMap;", "put",
                   "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
                   &native_View_noop);
    registerMethod("Ljava/util/LinkedHashMap;", "get",
                   "(Ljava/lang/Object;)Ljava/lang/Object;", &native_View_noop);

    // java.util.ArrayList
    registerMethod("Ljava/util/ArrayList;", "<init>", "()V", &native_View_noop);
    registerMethod("Ljava/util/ArrayList;", "<init>", "(I)V", &native_View_noop);
    registerMethod("Ljava/util/ArrayList;", "add",
                   "(Ljava/lang/Object;)Z", &native_View_noop);
    registerMethod("Ljava/util/ArrayList;", "add", "(ILjava/lang/Object;)V",
                   &native_View_noop);
    registerMethod("Ljava/util/ArrayList;", "get",
                   "(I)Ljava/lang/Object;", &native_View_noop);
    registerMethod("Ljava/util/ArrayList;", "size", "()I", &native_View_noop);
    registerMethod("Ljava/util/ArrayList;", "isEmpty", "()Z",
                   &native_View_noop_returning_false);
    registerMethod("Ljava/util/ArrayList;", "remove", "(I)Ljava/lang/Object;",
                   &native_View_noop);
    registerMethod("Ljava/util/ArrayList;", "remove",
                   "(Ljava/lang/Object;)Z", &native_View_noop_returning_false);
    registerMethod("Ljava/util/ArrayList;", "clear", "()V", &native_View_noop);
    registerMethod("Ljava/util/ArrayList;", "contains",
                   "(Ljava/lang/Object;)Z", &native_View_noop_returning_false);
    registerMethod("Ljava/util/ArrayList;", "indexOf",
                   "(Ljava/lang/Object;)I", &native_View_noop);

    // java.util.LinkedList
    registerMethod("Ljava/util/LinkedList;", "<init>", "()V", &native_View_noop);
    registerMethod("Ljava/util/LinkedList;", "add",
                   "(Ljava/lang/Object;)Z", &native_View_noop);
    registerMethod("Ljava/util/LinkedList;", "size", "()I", &native_View_noop);

    // java.util.Collections (static methods)
    registerMethod("Ljava/util/Collections;", "unmodifiableList",
                   "(Ljava/util/List;)Ljava/util/List;", &native_View_noop);
    registerMethod("Ljava/util/Collections;", "unmodifiableMap",
                   "(Ljava/util/Map;)Ljava/util/Map;", &native_View_noop);
    registerMethod("Ljava/util/Collections;", "synchronizedList",
                   "(Ljava/util/List;)Ljava/util/List;", &native_View_noop);
    registerMethod("Ljava/util/Collections;", "emptyList",
                   "()Ljava/util/List;", &native_View_noop);
    registerMethod("Ljava/util/Collections;", "emptyMap",
                   "()Ljava/util/Map;", &native_View_noop);

    // java.util.HashSet
    registerMethod("Ljava/util/HashSet;", "<init>", "()V", &native_View_noop);
    registerMethod("Ljava/util/HashSet;", "add",
                   "(Ljava/lang/Object;)Z", &native_View_noop);
    registerMethod("Ljava/util/HashSet;", "contains",
                   "(Ljava/lang/Object;)Z", &native_View_noop_returning_false);
    registerMethod("Ljava/util/HashSet;", "size", "()I", &native_View_noop);
    registerMethod("Ljava/util/HashSet;", "remove",
                   "(Ljava/lang/Object;)Z", &native_View_noop_returning_false);

    // java.util.Arrays (static helpers)
    registerMethod("Ljava/util/Arrays;", "asList",
                   "([Ljava/lang/Object;)Ljava/util/List;", &native_View_noop);
    registerMethod("Ljava/util/Arrays;", "copyOf",
                   "([Ljava/lang/Object;I)[Ljava/lang/Object;", &native_View_noop);

    // android.net.Uri
    registerMethod("Landroid/net/Uri;", "parse",
                   "(Ljava/lang/String;)Landroid/net/Uri;", &native_View_noop);
    registerMethod("Landroid/net/Uri;", "toString", "()Ljava/lang/String;",
                   &native_StringBuilder_toString);
    registerMethod("Landroid/net/Uri;", "getPath", "()Ljava/lang/String;",
                   &native_StringBuilder_toString);
    registerMethod("Landroid/net/Uri;", "getLastPathSegment",
                   "()Ljava/lang/String;", &native_StringBuilder_toString);
    registerMethod("Landroid/net/Uri;", "getQueryParameter",
                   "(Ljava/lang/String;)Ljava/lang/String;", &native_View_noop);
    registerMethod("Landroid/net/Uri;", "buildUpon",
                   "()Landroid/net/Uri$Builder;", &native_View_noop);
    registerMethod("Landroid/net/Uri$Builder;", "<init>", "()V",
                   &native_View_noop);
    registerMethod("Landroid/net/Uri$Builder;", "appendPath",
                   "(Ljava/lang/String;)Landroid/net/Uri$Builder;",
                   &native_View_noop);
    registerMethod("Landroid/net/Uri$Builder;", "appendQueryParameter",
                   "(Ljava/lang/String;Ljava/lang/String;)Landroid/net/Uri$Builder;",
                   &native_View_noop);
    registerMethod("Landroid/net/Uri$Builder;", "build",
                   "()Landroid/net/Uri;", &native_View_noop);

    // android.os.Bundle
    registerMethod("Landroid/os/Bundle;", "<init>", "()V", &native_View_noop);
    registerMethod("Landroid/os/Bundle;", "putString",
                   "(Ljava/lang/String;Ljava/lang/String;)V", &native_View_noop);
    registerMethod("Landroid/os/Bundle;", "getString",
                   "(Ljava/lang/String;)Ljava/lang/String;",
                   &native_StringBuilder_toString);
    registerMethod("Landroid/os/Bundle;", "getString",
                   "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
                   &native_StringBuilder_toString);
    registerMethod("Landroid/os/Bundle;", "putInt",
                   "(Ljava/lang/String;I)V", &native_View_noop);
    registerMethod("Landroid/os/Bundle;", "getInt", "(Ljava/lang/String;)I",
                   &native_View_noop);
    registerMethod("Landroid/os/Bundle;", "putBoolean",
                   "(Ljava/lang/String;Z)V", &native_View_noop);
    registerMethod("Landroid/os/Bundle;", "getBoolean",
                   "(Ljava/lang/String;)Z", &native_View_noop_returning_false);
    registerMethod("Landroid/os/Bundle;", "containsKey",
                   "(Ljava/lang/String;)Z", &native_View_noop_returning_false);
    registerMethod("Landroid/os/Bundle;", "isEmpty", "()Z",
                   &native_View_noop_returning_false);
    registerMethod("Landroid/os/Bundle;", "clear", "()V", &native_View_noop);
    registerMethod("Landroid/os/Bundle;", "putAll",
                   "(Landroid/os/Bundle;)V", &native_View_noop);
    registerMethod("Landroid/os/Bundle;", "keySet", "()Ljava/util/Set;",
                   &native_View_noop);
    registerMethod("Landroid/os/Bundle;", "get",
                   "(Ljava/lang/String;)Ljava/lang/Object;", &native_View_noop);

    // android.os.Parcelable
    registerMethod("Landroid/os/Parcelable;", "describeContents", "()I",
                   &native_View_noop);

    // android.os.Parcelable$Creator (generic stub)
    registerMethod("Landroid/os/Parcelable$Creator;", "createFromParcel",
                   "(Landroid/os/Parcel;)Ljava/lang/Object;", &native_View_noop);

    // android.os.Parcel
    registerMethod("Landroid/os/Parcel;", "<init>", "()V", &native_View_noop);
    registerMethod("Landroid/os/Parcel;", "writeString",
                   "(Ljava/lang/String;)V", &native_View_noop);
    registerMethod("Landroid/os/Parcel;", "readString",
                   "()Ljava/lang/String;", &native_StringBuilder_toString);
    registerMethod("Landroid/os/Parcel;", "writeInt", "(I)V", &native_View_noop);
    registerMethod("Landroid/os/Parcel;", "readInt", "()I", &native_View_noop);

    // android.content.Intent
    registerMethod("Landroid/content/Intent;", "<init>", "()V", &native_View_noop);
    registerMethod("Landroid/content/Intent;", "<init>",
                   "(Landroid/content/Context;Ljava/lang/Class;)V",
                   &native_View_noop);
    registerMethod("Landroid/content/Intent;", "putExtra",
                   "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;",
                   &native_View_noop);
    registerMethod("Landroid/content/Intent;", "putExtra",
                   "(Ljava/lang/String;I)Landroid/content/Intent;",
                   &native_View_noop);
    registerMethod("Landroid/content/Intent;", "putExtra",
                   "(Ljava/lang/String;Z)Landroid/content/Intent;",
                   &native_View_noop);
    registerMethod("Landroid/content/Intent;", "putExtra",
                   "(Ljava/lang/String;Landroid/os/Bundle;)Landroid/content/Intent;",
                   &native_View_noop);
    registerMethod("Landroid/content/Intent;", "putExtra",
                   "(Ljava/lang/String;Ljava/io/Serializable;)Landroid/content/Intent;",
                   &native_View_noop);
    registerMethod("Landroid/content/Intent;", "getStringExtra",
                   "(Ljava/lang/String;)Ljava/lang/String;",
                   &native_StringBuilder_toString);
    registerMethod("Landroid/content/Intent;", "getIntExtra",
                   "(Ljava/lang/String;I)I", &native_View_noop);
    registerMethod("Landroid/content/Intent;", "getBooleanExtra",
                   "(Ljava/lang/String;Z)Z", &native_View_noop_returning_false);
    registerMethod("Landroid/content/Intent;", "getExtras",
                   "()Landroid/os/Bundle;", &native_View_noop);
    registerMethod("Landroid/content/Intent;", "getAction",
                   "()Ljava/lang/String;", &native_StringBuilder_toString);
    registerMethod("Landroid/content/Intent;", "setData",
                   "(Landroid/net/Uri;)Landroid/content/Intent;",
                   &native_View_noop);
    registerMethod("Landroid/content/Intent;", "setAction",
                   "(Ljava/lang/String;)Landroid/content/Intent;",
                   &native_View_noop);
    registerMethod("Landroid/content/Intent;", "addFlags",
                   "(I)Landroid/content/Intent;", &native_View_noop);
    registerMethod("Landroid/content/Intent;", "setClassName",
                   "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;",
                   &native_View_noop);

    // android.content.Context
    registerMethod("Landroid/content/Context;", "getSystemService",
                   "(Ljava/lang/String;)Ljava/lang/Object;",
                   &native_Activity_getSystemService);
    registerMethod("Landroid/content/Context;", "getApplicationContext",
                   "()Landroid/content/Context;", &native_View_noop);
    registerMethod("Landroid/content/Context;", "getPackageName",
                   "()Ljava/lang/String;", &native_StringBuilder_toString);
    registerMethod("Landroid/content/Context;", "startActivity",
                   "(Landroid/content/Intent;)V", &native_View_noop);
    registerMethod("Landroid/content/Context;", "getContentResolver",
                   "()Landroid/content/ContentResolver;", &native_View_noop);
    registerMethod("Landroid/content/Context;", "getResources",
                   "()Landroid/content/res/Resources;", &native_View_noop);
    registerMethod("Landroid/content/Context;", "getAssets",
                   "()Landroid/content/res/AssetManager;", &native_View_noop);
    registerMethod("Landroid/content/Context;", "getSharedPreferences",
                   "(Ljava/lang/String;I)Landroid/content/SharedPreferences;",
                   &native_View_noop);
    registerMethod("Landroid/content/Context;", "getFilesDir",
                   "()Ljava/io/File;", &native_View_noop);
    registerMethod("Landroid/content/Context;", "getCacheDir",
                   "()Ljava/io/File;", &native_View_noop);
    registerMethod("Landroid/content/Context;", "getMainLooper",
                   "()Landroid/os/Looper;", &native_View_noop);
    registerMethod("Landroid/content/Context;", "getClassLoader",
                   "()Ljava/lang/ClassLoader;", &native_View_noop);

    // android.content.SharedPreferences
    registerMethod("Landroid/content/SharedPreferences;", "edit",
                   "()Landroid/content/SharedPreferences$Editor;",
                   &native_View_noop);
    registerMethod("Landroid/content/SharedPreferences;", "getString",
                   "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
                   &native_StringBuilder_toString);
    registerMethod("Landroid/content/SharedPreferences;", "getInt",
                   "(Ljava/lang/String;I)I", &native_View_noop);
    registerMethod("Landroid/content/SharedPreferences;", "getBoolean",
                   "(Ljava/lang/String;Z)Z", &native_View_noop_returning_false);
    registerMethod("Landroid/content/SharedPreferences;", "contains",
                   "(Ljava/lang/String;)Z", &native_View_noop_returning_false);
    registerMethod("Landroid/content/SharedPreferences;", "getAll",
                   "()Ljava/util/Map;", &native_View_noop);

    // android.content.SharedPreferences$Editor
    registerMethod("Landroid/content/SharedPreferences$Editor;", "putString",
                   "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/SharedPreferences$Editor;",
                   &native_View_noop);
    registerMethod("Landroid/content/SharedPreferences$Editor;", "putInt",
                   "(Ljava/lang/String;I)Landroid/content/SharedPreferences$Editor;",
                   &native_View_noop);
    registerMethod("Landroid/content/SharedPreferences$Editor;", "putBoolean",
                   "(Ljava/lang/String;Z)Landroid/content/SharedPreferences$Editor;",
                   &native_View_noop);
    registerMethod("Landroid/content/SharedPreferences$Editor;", "remove",
                   "(Ljava/lang/String;)Landroid/content/SharedPreferences$Editor;",
                   &native_View_noop);
    registerMethod("Landroid/content/SharedPreferences$Editor;", "apply",
                   "()V", &native_View_noop);
    registerMethod("Landroid/content/SharedPreferences$Editor;", "commit",
                   "()Z", &native_View_noop);

    // android.content.res.Resources
    registerMethod("Landroid/content/res/Resources;", "getString", "(I)Ljava/lang/String;",
                   &native_StringBuilder_toString);
    registerMethod("Landroid/content/res/Resources;", "getString",
                   "(I[Ljava/lang/Object;)Ljava/lang/String;",
                   &native_StringBuilder_toString);
    registerMethod("Landroid/content/res/Resources;", "getColor",
                   "(I)I", &native_View_noop);
    registerMethod("Landroid/content/res/Resources;", "getIdentifier",
                   "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)I",
                   &native_View_noop);
    registerMethod("Landroid/content/res/Resources;", "getDisplayMetrics",
                   "()Landroid/util/DisplayMetrics;", &native_View_noop);
    registerMethod("Landroid/content/res/Resources;", "getConfiguration",
                   "()Landroid/content/res/Configuration;", &native_View_noop);
    registerMethod("Landroid/content/res/Resources;", "getLayout",
                   "(I)Landroid/content/res/XmlResourceParser;", &native_View_noop);

    // android.content.pm.PackageManager
    registerMethod("Landroid/content/pm/PackageManager;", "getPackageInfo",
                   "(Ljava/lang/String;I)Landroid/content/pm/PackageInfo;",
                   &native_View_noop);
    registerMethod("Landroid/content/pm/PackageManager;", "getApplicationInfo",
                   "(Ljava/lang/String;I)Landroid/content/pm/ApplicationInfo;",
                   &native_View_noop);
    registerMethod("Landroid/content/pm/PackageManager;", "resolveActivity",
                   "(Landroid/content/Intent;I)Landroid/content/pm/ResolveInfo;",
                   &native_View_noop);

    // android.content.res.Configuration
    registerMethod("Landroid/content/res/Configuration;", "<init>", "()V",
                   &native_View_noop);

    // android.util.DisplayMetrics
    registerMethod("Landroid/util/DisplayMetrics;", "<init>", "()V",
                   &native_View_noop);

    // android.util.Log extra overloads
    registerMethod("Landroid/util/Log;", "isLoggable",
                   "(Ljava/lang/String;I)Z", &native_View_noop_returning_false);
    registerMethod("Landroid/util/Log;", "v",
                   "(Ljava/lang/String;Ljava/lang/String;)I", &native_Log_d);
    registerMethod("Landroid/util/Log;", "println",
                   "(ILjava/lang/String;Ljava/lang/String;)V", &native_View_noop);

    // android.os.Handler extra overloads
    registerMethod("Landroid/os/Handler;", "<init>", "()V", &native_View_noop);
    registerMethod("Landroid/os/Handler;", "<init>",
                   "(Landroid/os/Looper;)V", &native_View_noop);
    registerMethod("Landroid/os/Handler;", "post",
                   "(Ljava/lang/Runnable;)Z", &native_View_noop);
    registerMethod("Landroid/os/Handler;", "removeCallbacksAndMessages",
                   "(Ljava/lang/Object;)V", &native_View_noop);
    registerMethod("Landroid/os/Handler;", "sendMessage",
                   "(Landroid/os/Message;)Z", &native_View_noop);
    registerMethod("Landroid/os/Handler;", "obtainMessage",
                   "()Landroid/os/Message;", &native_View_noop);

    // android.os.Looper
    registerMethod("Landroid/os/Looper;", "myLooper", "()Landroid/os/Looper;",
                   &native_View_noop);
    registerMethod("Landroid/os/Looper;", "getMainLooper",
                   "()Landroid/os/Looper;", &native_View_noop);
    registerMethod("Landroid/os/Looper;", "getThread", "()Ljava/lang/Thread;",
                   &native_View_noop);

    // android.os.Message
    registerMethod("Landroid/os/Message;", "<init>", "()V", &native_View_noop);
    registerMethod("Landroid/os/Message;", "obtain", "()Landroid/os/Message;",
                   &native_View_noop);
    registerMethod("Landroid/os/Message;", "obtain",
                   "(Landroid/os/Handler;)Landroid/os/Message;", &native_View_noop);
    registerMethod("Landroid/os/Message;", "obtain",
                   "(Landroid/os/Handler;I)Landroid/os/Message;", &native_View_noop);

    // java.lang.Object
    registerMethod("Ljava/lang/Object;", "<init>", "()V", &native_View_noop);
    registerMethod("Ljava/lang/Object;", "toString", "()Ljava/lang/String;",
                   &native_StringBuilder_toString);
    registerMethod("Ljava/lang/Object;", "getClass",
                   "()Ljava/lang/Class;", &native_View_noop);
    registerMethod("Ljava/lang/Object;", "hashCode", "()I", &native_View_noop);
    registerMethod("Ljava/lang/Object;", "equals",
                   "(Ljava/lang/Object;)Z", &native_View_noop_returning_false);
    registerMethod("Ljava/lang/Object;", "notify", "()V", &native_View_noop);
    registerMethod("Ljava/lang/Object;", "notifyAll", "()V", &native_View_noop);
    registerMethod("Ljava/lang/Object;", "wait", "()V", &native_View_noop);
    registerMethod("Ljava/lang/Object;", "wait", "(J)V", &native_View_noop);

    // java.lang.String
    registerMethod("Ljava/lang/String;", "<init>", "()V", &native_View_noop);
    registerMethod("Ljava/lang/String;", "length", "()I", &native_View_noop);
    registerMethod("Ljava/lang/String;", "isEmpty", "()Z",
                   &native_View_noop_returning_false);
    registerMethod("Ljava/lang/String;", "charAt", "(I)C", &native_View_noop);
    registerMethod("Ljava/lang/String;", "substring",
                   "(I)Ljava/lang/String;", &native_StringBuilder_toString);
    registerMethod("Ljava/lang/String;", "substring",
                   "(II)Ljava/lang/String;", &native_StringBuilder_toString);
    registerMethod("Ljava/lang/String;", "indexOf",
                   "(Ljava/lang/String;)I", &native_View_noop);
    registerMethod("Ljava/lang/String;", "lastIndexOf",
                   "(Ljava/lang/String;)I", &native_View_noop);
    registerMethod("Ljava/lang/String;", "equals",
                   "(Ljava/lang/Object;)Z", &native_View_noop_returning_false);
    registerMethod("Ljava/lang/String;", "equalsIgnoreCase",
                   "(Ljava/lang/String;)Z", &native_View_noop_returning_false);
    registerMethod("Ljava/lang/String;", "compareTo",
                   "(Ljava/lang/String;)I", &native_View_noop);
    registerMethod("Ljava/lang/String;", "trim", "()Ljava/lang/String;",
                   &native_StringBuilder_toString);
    registerMethod("Ljava/lang/String;", "toLowerCase",
                   "()Ljava/lang/String;", &native_StringBuilder_toString);
    registerMethod("Ljava/lang/String;", "toUpperCase",
                   "()Ljava/lang/String;", &native_StringBuilder_toString);
    registerMethod("Ljava/lang/String;", "contains",
                   "(Ljava/lang/CharSequence;)Z", &native_View_noop_returning_false);
    registerMethod("Ljava/lang/String;", "startsWith",
                   "(Ljava/lang/String;)Z", &native_View_noop_returning_false);
    registerMethod("Ljava/lang/String;", "endsWith",
                   "(Ljava/lang/String;)Z", &native_View_noop_returning_false);
    registerMethod("Ljava/lang/String;", "replace",
                   "(CC)Ljava/lang/String;", &native_StringBuilder_toString);
    registerMethod("Ljava/lang/String;", "replace",
                   "(Ljava/lang/CharSequence;Ljava/lang/CharSequence;)Ljava/lang/String;",
                   &native_StringBuilder_toString);
    registerMethod("Ljava/lang/String;", "split",
                   "(Ljava/lang/String;)[Ljava/lang/String;", &native_View_noop);
    registerMethod("Ljava/lang/String;", "getBytes",
                   "()[B", &native_View_noop);
    registerMethod("Ljava/lang/String;", "toCharArray", "()[C", &native_View_noop);
    registerMethod("Ljava/lang/String;", "valueOf",
                   "(I)Ljava/lang/String;", &native_StringBuilder_toString);
    registerMethod("Ljava/lang/String;", "valueOf",
                   "(J)Ljava/lang/String;", &native_StringBuilder_toString);
    registerMethod("Ljava/lang/String;", "valueOf",
                   "(Ljava/lang/Object;)Ljava/lang/String;",
                   &native_StringBuilder_toString);

    // java.lang.StringBuilder extra
    registerMethod("Ljava/lang/StringBuilder;", "<init>", "(I)V",
                   &native_View_noop);
    registerMethod("Ljava/lang/StringBuilder;", "<init>",
                   "(Ljava/lang/String;)V", &native_View_noop);
    registerMethod("Ljava/lang/StringBuilder;", "length", "()I",
                   &native_View_noop);
    registerMethod("Ljava/lang/StringBuilder;", "append",
                   "(C)Ljava/lang/StringBuilder;", &native_View_noop);
    registerMethod("Ljava/lang/StringBuilder;", "append",
                   "(I)Ljava/lang/StringBuilder;", &native_View_noop);
    registerMethod("Ljava/lang/StringBuilder;", "append",
                   "(J)Ljava/lang/StringBuilder;", &native_View_noop);
    registerMethod("Ljava/lang/StringBuilder;", "append",
                   "(Z)Ljava/lang/StringBuilder;", &native_View_noop);

    // java.lang.CharSequence
    registerMethod("Ljava/lang/CharSequence;", "length", "()I", &native_View_noop);
    registerMethod("Ljava/lang/CharSequence;", "charAt", "(I)C", &native_View_noop);
    registerMethod("Ljava/lang/CharSequence;", "toString",
                   "()Ljava/lang/String;", &native_StringBuilder_toString);
    registerMethod("Ljava/lang/CharSequence;", "subSequence",
                   "(II)Ljava/lang/CharSequence;", &native_View_noop);

    // java.lang.Number / Integer / Long / Float / Double / Boolean boxing
    registerMethod("Ljava/lang/Integer;", "<init>", "(I)V", &native_View_noop);
    registerMethod("Ljava/lang/Integer;", "intValue", "()I",
                   &native_Number_intValue);
    registerMethod("Ljava/lang/Integer;", "parseInt",
                   "(Ljava/lang/String;)I", &native_View_noop);
    registerMethod("Ljava/lang/Integer;", "toString",
                   "(I)Ljava/lang/String;", &native_StringBuilder_toString);
    registerMethod("Ljava/lang/Integer;", "valueOf",
                   "(Ljava/lang/String;)Ljava/lang/Integer;",
                   &native_View_noop);

    registerMethod("Ljava/lang/Long;", "valueOf",
                   "(J)Ljava/lang/Long;", &native_View_noop);
    registerMethod("Ljava/lang/Long;", "longValue", "()J", &native_View_noop);
    registerMethod("Ljava/lang/Long;", "parseLong",
                   "(Ljava/lang/String;)J", &native_View_noop);

    registerMethod("Ljava/lang/Float;", "valueOf",
                   "(F)Ljava/lang/Float;", &native_View_noop);
    registerMethod("Ljava/lang/Float;", "floatValue", "()F", &native_View_noop);

    registerMethod("Ljava/lang/Double;", "valueOf",
                   "(D)Ljava/lang/Double;", &native_View_noop);
    registerMethod("Ljava/lang/Double;", "doubleValue", "()D", &native_View_noop);

    registerMethod("Ljava/lang/Boolean;", "valueOf",
                   "(Z)Ljava/lang/Boolean;", &native_View_noop);
    registerMethod("Ljava/lang/Boolean;", "booleanValue", "()Z",
                   &native_View_noop_returning_false);
    registerMethod("Ljava/lang/Boolean;", "parseBoolean",
                   "(Ljava/lang/String;)Z", &native_View_noop_returning_false);

    registerMethod("Ljava/lang/Short;", "valueOf",
                   "(S)Ljava/lang/Short;", &native_View_noop);
    registerMethod("Ljava/lang/Short;", "shortValue", "()S", &native_View_noop);

    registerMethod("Ljava/lang/Byte;", "valueOf",
                   "(B)Ljava/lang/Byte;", &native_View_noop);
    registerMethod("Ljava/lang/Byte;", "byteValue", "()B", &native_View_noop);

    registerMethod("Ljava/lang/Character;", "valueOf",
                   "(C)Ljava/lang/Character;", &native_View_noop);

    // java.lang.Class
    registerMethod("Ljava/lang/Class;", "getName", "()Ljava/lang/String;",
                   &native_StringBuilder_toString);
    registerMethod("Ljava/lang/Class;", "getSimpleName",
                   "()Ljava/lang/String;", &native_StringBuilder_toString);
    registerMethod("Ljava/lang/Class;", "getCanonicalName",
                   "()Ljava/lang/String;", &native_StringBuilder_toString);
    registerMethod("Ljava/lang/Class;", "getSuperclass",
                   "()Ljava/lang/Class;", &native_View_noop);
    registerMethod("Ljava/lang/Class;", "forName",
                   "(Ljava/lang/String;)Ljava/lang/Class;", &native_View_noop);

    // java.lang.Throwable / Exception
    registerMethod("Ljava/lang/Throwable;", "getMessage",
                   "()Ljava/lang/String;", &native_StringBuilder_toString);
    registerMethod("Ljava/lang/Throwable;", "toString",
                   "()Ljava/lang/String;", &native_StringBuilder_toString);
    registerMethod("Ljava/lang/Throwable;", "getStackTrace",
                   "()[Ljava/lang/StackTraceElement;", &native_View_noop);
    registerMethod("Ljava/lang/Exception;", "<init>", "()V",
                   &native_View_noop);
    registerMethod("Ljava/lang/Exception;", "<init>",
                   "(Ljava/lang/String;)V", &native_View_noop);

    // java.lang.RuntimeException
    registerMethod("Ljava/lang/RuntimeException;", "<init>", "()V",
                   &native_View_noop);
    registerMethod("Ljava/lang/RuntimeException;", "<init>",
                   "(Ljava/lang/String;)V", &native_View_noop);
    registerMethod("Ljava/lang/RuntimeException;", "<init>",
                   "(Ljava/lang/Throwable;)V", &native_View_noop);

    // java.lang.IllegalArgumentException
    registerMethod("Ljava/lang/IllegalArgumentException;", "<init>", "()V",
                   &native_View_noop);
    registerMethod("Ljava/lang/IllegalArgumentException;", "<init>",
                   "(Ljava/lang/String;)V", &native_View_noop);

    // java.lang.IllegalStateException
    registerMethod("Ljava/lang/IllegalStateException;", "<init>", "()V",
                   &native_View_noop);
    registerMethod("Ljava/lang/IllegalStateException;", "<init>",
                   "(Ljava/lang/String;)V", &native_View_noop);

    // java.lang.NullPointerException
    registerMethod("Ljava/lang/NullPointerException;", "<init>", "()V",
                   &native_View_noop);
    registerMethod("Ljava/lang/NullPointerException;", "<init>",
                   "(Ljava/lang/String;)V", &native_View_noop);

    // java.lang.UnsupportedOperationException
    registerMethod("Ljava/lang/UnsupportedOperationException;", "<init>", "()V",
                   &native_View_noop);
    registerMethod("Ljava/lang/UnsupportedOperationException;", "<init>",
                   "(Ljava/lang/String;)V", &native_View_noop);

    // java.lang.IndexOutOfBoundsException
    registerMethod("Ljava/lang/IndexOutOfBoundsException;", "<init>", "()V",
                   &native_View_noop);
    registerMethod("Ljava/lang/IndexOutOfBoundsException;", "<init>",
                   "(Ljava/lang/String;)V", &native_View_noop);

    // java.lang.NumberFormatException
    registerMethod("Ljava/lang/NumberFormatException;", "<init>", "()V",
                   &native_View_noop);
    registerMethod("Ljava/lang/NumberFormatException;", "<init>",
                   "(Ljava/lang/String;)V", &native_View_noop);

    // java.lang.ClassCastException
    registerMethod("Ljava/lang/ClassCastException;", "<init>", "()V",
                   &native_View_noop);
    registerMethod("Ljava/lang/ClassCastException;", "<init>",
                   "(Ljava/lang/String;)V", &native_View_noop);

    // java.lang.Thread
    registerMethod("Ljava/lang/Thread;", "<init>", "()V", &native_View_noop);
    registerMethod("Ljava/lang/Thread;", "<init>",
                   "(Ljava/lang/Runnable;)V", &native_View_noop);
    registerMethod("Ljava/lang/Thread;", "start", "()V", &native_View_noop);
    registerMethod("Ljava/lang/Thread;", "join", "()V", &native_View_noop);
    registerMethod("Ljava/lang/Thread;", "sleep", "(J)V", &native_View_noop);
    registerMethod("Ljava/lang/Thread;", "currentThread",
                   "()Ljava/lang/Thread;", &native_View_noop);
    registerMethod("Ljava/lang/Thread;", "getName", "()Ljava/lang/String;",
                   &native_StringBuilder_toString);
    registerMethod("Ljava/lang/Thread;", "isAlive", "()Z",
                   &native_View_noop_returning_false);
    registerMethod("Ljava/lang/Thread;", "setDaemon", "(Z)V", &native_View_noop);
    registerMethod("Ljava/lang/Thread;", "getId", "()J", &native_View_noop);
    registerMethod("Ljava/lang/Thread;", "setPriority", "(I)V",
                   &native_View_noop);
    registerMethod("Ljava/lang/Thread;", "getPriority", "()I",
                   &native_View_noop);

    // java.lang.Runnable
    registerMethod("Ljava/lang/Runnable;", "run", "()V", &native_View_noop);

    // java.lang.Enum
    registerMethod("Ljava/lang/Enum;", "name", "()Ljava/lang/String;",
                   &native_StringBuilder_toString);
    registerMethod("Ljava/lang/Enum;", "ordinal", "()I", &native_View_noop);
    registerMethod("Ljava/lang/Enum;", "valueOf",
                   "(Ljava/lang/Class;Ljava/lang/String;)Ljava/lang/Enum;",
                   &native_View_noop);

    // java.util.Map / java.util.Set / java.util.List / java.util.Collection
    registerMethod("Ljava/util/Map;", "get",
                   "(Ljava/lang/Object;)Ljava/lang/Object;", &native_View_noop);
    registerMethod("Ljava/util/Map;", "put",
                   "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
                   &native_View_noop);
    registerMethod("Ljava/util/Map;", "containsKey",
                   "(Ljava/lang/Object;)Z", &native_View_noop_returning_false);
    registerMethod("Ljava/util/Map;", "size", "()I", &native_View_noop);
    registerMethod("Ljava/util/Map;", "isEmpty", "()Z",
                   &native_View_noop_returning_false);
    registerMethod("Ljava/util/Map;", "keySet", "()Ljava/util/Set;",
                   &native_View_noop);
    registerMethod("Ljava/util/Map;", "values", "()Ljava/util/Collection;",
                   &native_View_noop);
    registerMethod("Ljava/util/Map;", "entrySet", "()Ljava/util/Set;",
                   &native_View_noop);

    registerMethod("Ljava/util/List;", "size", "()I", &native_View_noop);
    registerMethod("Ljava/util/List;", "get",
                   "(I)Ljava/lang/Object;", &native_View_noop);
    registerMethod("Ljava/util/List;", "add",
                   "(Ljava/lang/Object;)Z", &native_View_noop);
    registerMethod("Ljava/util/List;", "isEmpty", "()Z",
                   &native_View_noop_returning_false);
    registerMethod("Ljava/util/List;", "contains",
                   "(Ljava/lang/Object;)Z", &native_View_noop_returning_false);
    registerMethod("Ljava/util/List;", "iterator",
                   "()Ljava/util/Iterator;", &native_View_noop);

    registerMethod("Ljava/util/Set;", "size", "()I", &native_View_noop);
    registerMethod("Ljava/util/Set;", "contains",
                   "(Ljava/lang/Object;)Z", &native_View_noop_returning_false);
    registerMethod("Ljava/util/Set;", "iterator",
                   "()Ljava/util/Iterator;", &native_View_noop);

    registerMethod("Ljava/util/Collection;", "size", "()I", &native_View_noop);
    registerMethod("Ljava/util/Collection;", "isEmpty", "()Z",
                   &native_View_noop_returning_false);

    registerMethod("Ljava/util/Iterator;", "hasNext", "()Z",
                   &native_View_noop_returning_false);
    registerMethod("Ljava/util/Iterator;", "next", "()Ljava/lang/Object;",
                   &native_View_noop);

    // java.io.Closeable / AutoCloseable
    registerMethod("Ljava/io/Closeable;", "close", "()V", &native_View_noop);
    registerMethod("Ljava/lang/AutoCloseable;", "close", "()V",
                   &native_View_noop);

    // java.io.InputStream / OutputStream
    registerMethod("Ljava/io/InputStream;", "read", "()[B", &native_View_noop);
    registerMethod("Ljava/io/InputStream;", "read", "([B)I", &native_View_noop);
    registerMethod("Ljava/io/InputStream;", "close", "()V", &native_View_noop);
    registerMethod("Ljava/io/InputStream;", "available", "()I", &native_View_noop);

    registerMethod("Ljava/io/OutputStream;", "write", "([B)V",
                   &native_View_noop);
    registerMethod("Ljava/io/OutputStream;", "flush", "()V", &native_View_noop);
    registerMethod("Ljava/io/OutputStream;", "close", "()V", &native_View_noop);

    // java.io.ByteArrayInputStream
    registerMethod("Ljava/io/ByteArrayInputStream;", "<init>", "([B)V",
                   &native_View_noop);

    // java.io.ByteArrayOutputStream
    registerMethod("Ljava/io/ByteArrayOutputStream;", "<init>", "()V",
                   &native_View_noop);
    registerMethod("Ljava/io/ByteArrayOutputStream;", "toByteArray",
                   "()[B", &native_View_noop);
    registerMethod("Ljava/io/ByteArrayOutputStream;", "toString",
                   "()Ljava/lang/String;", &native_StringBuilder_toString);

    // java.io.BufferedReader
    registerMethod("Ljava/io/BufferedReader;", "<init>",
                   "(Ljava/io/Reader;)V", &native_View_noop);
    registerMethod("Ljava/io/BufferedReader;", "readLine",
                   "()Ljava/lang/String;", &native_StringBuilder_toString);

    // java.io.InputStreamReader
    registerMethod("Ljava/io/InputStreamReader;", "<init>",
                   "(Ljava/io/InputStream;)V", &native_View_noop);

    // java.io.Reader
    registerMethod("Ljava/io/Reader;", "close", "()V", &native_View_noop);

    // java.io.Writer
    registerMethod("Ljava/io/Writer;", "close", "()V", &native_View_noop);
    registerMethod("Ljava/io/Writer;", "write", "(Ljava/lang/String;)V",
                   &native_View_noop);

    // java.io.File extra methods
    registerMethod("Ljava/io/File;", "<init>", "(Ljava/lang/String;)V",
                   &native_View_noop);
    registerMethod("Ljava/io/File;", "getName", "()Ljava/lang/String;",
                   &native_StringBuilder_toString);
    registerMethod("Ljava/io/File;", "getParent", "()Ljava/lang/String;",
                   &native_StringBuilder_toString);
    registerMethod("Ljava/io/File;", "isDirectory", "()Z",
                   &native_View_noop_returning_false);
    registerMethod("Ljava/io/File;", "isFile", "()Z",
                   &native_View_noop_returning_false);
    registerMethod("Ljava/io/File;", "length", "()J", &native_View_noop);
    registerMethod("Ljava/io/File;", "mkdirs", "()Z",
                   &native_View_noop_returning_false);

    // java.io.FileInputStream / FileOutputStream
    registerMethod("Ljava/io/FileInputStream;", "<init>",
                   "(Ljava/lang/String;)V", &native_View_noop);
    registerMethod("Ljava/io/FileInputStream;", "<init>",
                   "(Ljava/io/File;)V", &native_View_noop);
    registerMethod("Ljava/io/FileOutputStream;", "<init>",
                   "(Ljava/lang/String;)V", &native_View_noop);
    registerMethod("Ljava/io/FileOutputStream;", "<init>",
                   "(Ljava/io/File;)V", &native_View_noop);

    // android.widget.Toast
    registerMethod("Landroid/widget/Toast;", "makeText",
                   "(Landroid/content/Context;Ljava/lang/CharSequence;I)Landroid/widget/Toast;",
                   &native_View_noop);
    registerMethod("Landroid/widget/Toast;", "show", "()V", &native_View_noop);

    // android.widget.FrameLayout
    registerMethod("Landroid/widget/FrameLayout;", "<init>",
                   "(Landroid/content/Context;)V", &native_LinearLayout_init);
    registerMethod("Landroid/widget/FrameLayout;", "addView",
                   "(Landroid/view/View;)V", &native_View_noop);

    // android.view.ViewGroup
    registerMethod("Landroid/view/ViewGroup;", "addView",
                   "(Landroid/view/View;)V", &native_View_noop);
    registerMethod("Landroid/view/ViewGroup;", "removeView",
                   "(Landroid/view/View;)V", &native_View_noop);
    registerMethod("Landroid/view/ViewGroup;", "getChildCount", "()I",
                   &native_View_noop);
    registerMethod("Landroid/view/ViewGroup;", "getChildAt",
                   "(I)Landroid/view/View;", &native_View_noop);
    registerMethod("Landroid/view/ViewGroup;", "removeAllViews", "()V",
                   &native_View_noop);
    registerMethod("Landroid/view/ViewGroup;", "findViewById",
                   "(I)Landroid/view/View;", &native_Activity_findViewById);

    // android.view.ViewGroup$LayoutParams
    registerMethod("Landroid/view/ViewGroup$LayoutParams;", "<init>", "(II)V",
                   &native_View_noop);

    // android.widget.RelativeLayout
    registerMethod("Landroid/widget/RelativeLayout;", "<init>",
                   "(Landroid/content/Context;)V", &native_LinearLayout_init);
    registerMethod("Landroid/widget/RelativeLayout;", "addView",
                   "(Landroid/view/View;)V", &native_View_noop);

    // android.app.Application
    registerMethod("Landroid/app/Application;", "<init>", "()V",
                   &native_View_noop);
    registerMethod("Landroid/app/Application;", "getApplicationContext",
                   "()Landroid/content/Context;", &native_View_noop);

    // android.app.AlertDialog
    registerMethod("Landroid/app/AlertDialog;", "<init>",
                   "(Landroid/content/Context;)V", &native_View_noop);

    // android.app.Dialog
    registerMethod("Landroid/app/Dialog;", "<init>",
                   "(Landroid/content/Context;)V", &native_View_noop);
    registerMethod("Landroid/app/Dialog;", "show", "()V", &native_View_noop);
    registerMethod("Landroid/app/Dialog;", "dismiss", "()V", &native_View_noop);

    // android.os.Build
    registerMethod("Landroid/os/Build;", "getString",
                   "(Ljava/lang/String;)Ljava/lang/String;",
                   &native_StringBuilder_toString);

    // android.os.Environment
    registerMethod("Landroid/os/Environment;", "getExternalStorageDirectory",
                   "()Ljava/io/File;", &native_View_noop);

    // android.os.SystemClock
    registerMethod("Landroid/os/SystemClock;", "elapsedRealtime", "()J",
                   &native_View_noop);
    registerMethod("Landroid/os/SystemClock;", "uptimeMillis", "()J",
                   &native_View_noop);

    // android.os.Process
    registerMethod("Landroid/os/Process;", "myPid", "()I", &native_View_noop);
    registerMethod("Landroid/os/Process;", "myUid", "()I", &native_View_noop);
    registerMethod("Landroid/os/Process;", "myTid", "()I", &native_View_noop);

    // java.lang.System
    registerMethod("Ljava/lang/System;", "currentTimeMillis", "()J",
                   [](Frame* f) {
                       auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch()).count();
                       uint32_t lo = static_cast<uint32_t>(now);
                       uint32_t hi = static_cast<uint32_t>(now >> 32);
                       if (f->argRegisters.size() >= 1) {
                           // result goes into wide return
                       }
                       f->wideReturnValue = static_cast<uint64_t>(now);
                       f->hasWideReturnValue = true;
                   });
    registerMethod("Ljava/lang/System;", "nanoTime", "()J",
                   [](Frame* f) {
                       auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                           std::chrono::steady_clock::now().time_since_epoch()).count();
                       f->wideReturnValue = static_cast<uint64_t>(now);
                       f->hasWideReturnValue = true;
                   });
    registerMethod("Ljava/lang/System;", "arraycopy",
                   "(Ljava/lang/Object;ILjava/lang/Object;II)V", &native_View_noop);
    registerMethod("Ljava/lang/System;", "identityHashCode",
                   "(Ljava/lang/Object;)I", &native_View_noop);
    registerMethod("Ljava/lang/System;", "getenv",
                   "(Ljava/lang/String;)Ljava/lang/String;", &native_View_noop);
    registerMethod("Ljava/lang/System;", "getProperty",
                   "(Ljava/lang/String;)Ljava/lang/String;", &native_View_noop);
    registerMethod("Ljava/lang/System;", "getProperty",
                   "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
                   &native_View_noop);

    // java.lang.Number subclasses
    registerMethod("Ljava/lang/Number;", "floatValue", "()F",
                   [](Frame* f) { f->setRegister(0, 0); });
    registerMethod("Ljava/lang/Number;", "doubleValue", "()D",
                   [](Frame* f) { f->setWideRegister(0, 0); });
    registerMethod("Ljava/lang/Number;", "longValue", "()J",
                   [](Frame* f) { f->wideReturnValue = 0; f->hasWideReturnValue = true; });
    registerMethod("Ljava/lang/Number;", "byteValue", "()B", &native_View_noop);
    registerMethod("Ljava/lang/Number;", "shortValue", "()S", &native_View_noop);

    // Collections
    registerMethod("Ljava/util/Collections;", "singletonList",
                   "(Ljava/lang/Object;)Ljava/util/List;",
                   [](Frame* f) {
                       auto* list = ObjectModel::ArrayObject::create(
                           ObjectModel::ArrayObject::ArrayType::OBJECT, 1);
                       if (!f->argRegisters.empty())
                           list->setObject(0, f->getObjectRegister(f->argRegisters[0]));
                       f->objectReturnValue = list;
                   });
    registerMethod("Ljava/util/Collections;", "singleton",
                   "(Ljava/lang/Object;)Ljava/util/Set;", &native_View_noop);
    registerMethod("Ljava/util/Collections;", "singletonMap",
                   "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/util/Map;",
                   &native_View_noop);
    registerMethod("Ljava/util/Collections;", "unmodifiableList",
                   "(Ljava/util/List;)Ljava/util/List;",
                   [](Frame* f) {
                       if (!f->argRegisters.empty())
                           f->objectReturnValue = f->getObjectRegister(f->argRegisters[0]);
                   });
    registerMethod("Ljava/util/Collections;", "unmodifiableMap",
                   "(Ljava/util/Map;)Ljava/util/Map;",
                   [](Frame* f) {
                       if (!f->argRegisters.empty())
                           f->objectReturnValue = f->getObjectRegister(f->argRegisters[0]);
                   });
    registerMethod("Ljava/util/Collections;", "synchronizedList",
                   "(Ljava/util/List;)Ljava/util/List;",
                   [](Frame* f) {
                       if (!f->argRegisters.empty())
                           f->objectReturnValue = f->getObjectRegister(f->argRegisters[0]);
                   });
    registerMethod("Ljava/util/Collections;", "emptyList", "()Ljava/util/List;",
                   [](Frame* f) {
                       f->objectReturnValue = ObjectModel::ArrayObject::create(
                           ObjectModel::ArrayObject::ArrayType::OBJECT, 0);
                   });
    registerMethod("Ljava/util/Collections;", "emptyMap", "()Ljava/util/Map;",
                   [](Frame* f) {
                       f->objectReturnValue = ObjectModel::ArrayObject::create(
                           ObjectModel::ArrayObject::ArrayType::OBJECT, 0);
                   });
    registerMethod("Ljava/util/Collections;", "emptySet", "()Ljava/util/Set;",
                   [](Frame* f) {
                       f->objectReturnValue = ObjectModel::ArrayObject::create(
                           ObjectModel::ArrayObject::ArrayType::OBJECT, 0);
                   });


    // java.util.Random
    registerMethod("Ljava/util/Random;", "<init>", "()V", &native_View_noop);
    registerMethod("Ljava/util/Random;", "<init>", "(J)V", &native_View_noop);
    registerMethod("Ljava/util/Random;", "nextInt", "()I",
                   [](Frame* f) { f->setRegister(0, 42); });
    registerMethod("Ljava/util/Random;", "nextInt", "(I)I",
                   [](Frame* f) {
                       uint32_t bound = f->argRegisters.size() >= 1 ? f->getRegister(f->argRegisters[0]) : 1;
                       f->setRegister(0, bound > 0 ? (42 % bound) : 0);
                   });
    registerMethod("Ljava/util/Random;", "nextLong", "()J",
                   [](Frame* f) { f->wideReturnValue = 42; f->hasWideReturnValue = true; });
    registerMethod("Ljava/util/Random;", "nextFloat", "()F",
                   [](Frame* f) { f->setRegister(0, u32f(0.5f)); });
    registerMethod("Ljava/util/Random;", "nextDouble", "()D",
                   [](Frame* f) { f->setWideRegister(0, u64(0.5)); });

    // kotlin.jvm.functions.Function0-FunctionN
    registerMethod("Lkotlin/jvm/functions/Function0;", "invoke", "()Ljava/lang/Object;",
                   &native_View_noop);
    registerMethod("Lkotlin/jvm/functions/Function1;", "invoke",
                   "(Ljava/lang/Object;)Ljava/lang/Object;", &native_View_noop);
    registerMethod("Lkotlin/jvm/functions/Function2;", "invoke",
                   "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
                   &native_View_noop);
    registerMethod("Lkotlin/jvm/functions/Function3;", "invoke",
                   "(Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
                   &native_View_noop);
    registerMethod("Lkotlin/jvm/functions/FunctionN;", "invoke",
                   "([Ljava/lang/Object;)Ljava/lang/Object;", &native_View_noop);

    // Kotlin intrinsics extras
    registerMethod("Lkotlin/jvm/internal/Intrinsics;", "areEqual",
                   "(Ljava/lang/Object;Ljava/lang/Object;)Z",
                   [](Frame* f) {
                       if (f->argRegisters.size() < 2) { f->setRegister(0, 0); return; }
                       Object* a = f->getObjectRegister(f->argRegisters[0]);
                       Object* b = f->getObjectRegister(f->argRegisters[1]);
                       f->setRegister(0, (a == b) ? 1 : 0);
                   });
    registerMethod("Lkotlin/jvm/internal/Intrinsics;", "compare",
                   "(II)I",
                   [](Frame* f) {
                       int32_t a = i32(f->getRegister(f->argRegisters[0]));
                       int32_t b = i32(f->getRegister(f->argRegisters[1]));
                       f->setRegister(0, (a < b) ? -1 : (a > b) ? 1 : 0);
                   });

    // android.content.Intent extras
    registerMethod("Landroid/content/Intent;", "getSerializableExtra",
                   "(Ljava/lang/String;)Ljava/io/Serializable;", &native_View_noop);
    registerMethod("Landroid/content/Intent;", "getParcelableExtra",
                   "(Ljava/lang/String;)Landroid/os/Parcelable;", &native_View_noop);
    registerMethod("Landroid/content/Intent;", "getBundleExtra",
                   "(Ljava/lang/String;)Landroid/os/Bundle;", &native_View_noop);
    registerMethod("Landroid/content/Intent;", "getCharSequenceExtra",
                   "(Ljava/lang/String;)Ljava/lang/CharSequence;", &native_View_noop);

    // android.app.Activity extras
    registerMethod("Landroid/app/Activity;", "getIntent",
                   "()Landroid/content/Intent;", &native_View_noop);
    registerMethod("Landroid/app/Activity;", "finish", "()V", &native_View_noop);
    registerMethod("Landroid/app/Activity;", "isFinishing", "()Z",
                   [](Frame* f) { f->setRegister(0, 0); });
    registerMethod("Landroid/app/Activity;", "isTaskRoot", "()Z",
                   [](Frame* f) { f->setRegister(0, 1); });

    registerMethod("Landroid/app/Activity;", "startActivity",
                   "(Landroid/content/Intent;)V", &native_Activity_startActivity);
    registerMethod("Landroid/app/Activity;", "startActivityForResult",
                   "(Landroid/content/Intent;I)V", &native_Activity_startActivityForResult);
    registerMethod("Landroid/app/Activity;", "requestPermissions",
                   "([Ljava/lang/String;I)V", &native_View_noop);
    registerMethod("Landroid/app/Activity;", "getFragmentManager",
                   "()Landroid/app/FragmentManager;", &native_View_noop);
    registerMethod("Landroid/app/Activity;", "getSupportFragmentManager",
                   "()Landroidx/fragment/app/FragmentManager;", &native_View_noop);
    registerMethod("Landroid/app/Activity;", "checkSelfPermission",
                   "(Ljava/lang/String;)I",
                   [](Frame* f) { f->setRegister(0, 0); });
    registerMethod("Landroid/app/Activity;", "shouldShowRequestPermissionRationale",
                   "(Ljava/lang/String;)Z",
                   [](Frame* f) { f->setRegister(0, 0); });
    registerMethod("Landroid/app/Activity;", "getColor",
                   "(I)I", &native_View_noop);
    registerMethod("Landroid/app/Activity;", "getString",
                   "(I)Ljava/lang/String;", &native_View_noop);
    registerMethod("Landroid/app/Activity;", "getString",
                   "(I[Ljava/lang/Object;)Ljava/lang/String;", &native_View_noop);
    registerMethod("Landroid/app/Activity;", "getWindow",
                   "()Landroid/view/Window;", &native_Activity_getWindow);
    registerMethod("Landroid/app/Activity;", "setRequestedOrientation",
                   "(I)V", &native_View_noop);
    registerMethod("Landroid/app/Activity;", "getRequestedOrientation", "()I",
                   [](Frame* f) { f->setRegister(0, 1); });
    registerMethod("Landroid/app/Activity;", "getResources",
                   "()Landroid/content/res/Resources;", &native_View_noop);
    registerMethod("Landroid/app/Activity;", "getAssets",
                   "()Landroid/content/res/AssetManager;", &native_View_noop);
    registerMethod("Landroid/app/Activity;", "runOnUiThread",
                   "(Ljava/lang/Runnable;)V",
                   [](Frame* f) {
                       // Just run it directly since we're already on the UI thread.
                       if (!f->argRegisters.empty()) {
                           Object* runnable = f->getObjectRegister(f->argRegisters[0]);
                           if (runnable) {
                               // Find and invoke run() on the runnable
                               // For now, just do nothing to avoid recursion issues.
                           }
                       }
                   });

    // android.view.View extras
    registerMethod("Landroid/view/View;", "getContext",
                   "()Landroid/content/Context;", &native_View_noop);
    registerMethod("Landroid/view/View;", "getId", "()I",
                   [](Frame* f) { f->setRegister(0, 0); });
    registerMethod("Landroid/view/View;", "setId", "(I)V", &native_View_noop);
    registerMethod("Landroid/view/View;", "getTag", "()Ljava/lang/Object;",
                   &native_View_noop);
    registerMethod("Landroid/view/View;", "setTag", "(Ljava/lang/Object;)V",
                   &native_View_noop);
    registerMethod("Landroid/view/View;", "setTag",
                   "(ILjava/lang/Object;)V", &native_View_noop);
    registerMethod("Landroid/view/View;", "getTag",
                   "(I)Ljava/lang/Object;", &native_View_noop);
    registerMethod("Landroid/view/View;", "getVisibility", "()I",
                   [](Frame* f) { f->setRegister(0, 0); });
    registerMethod("Landroid/view/View;", "getWidth", "()I",
                   [](Frame* f) { f->setRegister(0, 800); });
    registerMethod("Landroid/view/View;", "getHeight", "()I",
                   [](Frame* f) { f->setRegister(0, 600); });
    registerMethod("Landroid/view/View;", "getMeasuredWidth", "()I",
                   [](Frame* f) { f->setRegister(0, 800); });
    registerMethod("Landroid/view/View;", "getMeasuredHeight", "()I",
                   [](Frame* f) { f->setRegister(0, 600); });
    registerMethod("Landroid/view/View;", "post",
                   "(Ljava/lang/Runnable;)Z",
                   [](Frame* f) { f->setRegister(0, 1); });
    registerMethod("Landroid/view/View;", "postDelayed",
                   "(Ljava/lang/Runnable;J)Z",
                   [](Frame* f) { f->setRegister(0, 1); });
    registerMethod("Landroid/view/View;", "removeCallbacks",
                   "(Ljava/lang/Runnable;)V", &native_View_noop);
    registerMethod("Landroid/view/View;", "requestFocus", "()Z",
                   [](Frame* f) { f->setRegister(0, 1); });
    registerMethod("Landroid/view/View;", "hasFocus", "()Z",
                   [](Frame* f) { f->setRegister(0, 0); });
    registerMethod("Landroid/view/View;", "isAttachedToWindow", "()Z",
                   [](Frame* f) { f->setRegister(0, 1); });
    registerMethod("Landroid/view/View;", "getParent",
                   "()Landroid/view/ViewParent;", &native_View_noop);
    registerMethod("Landroid/view/View;", "getBackground",
                   "()Landroid/graphics/drawable/Drawable;", &native_View_noop);
    registerMethod("Landroid/view/View;", "setBackground",
                   "(Landroid/graphics/drawable/Drawable;)V", &native_View_noop);
    registerMethod("Landroid/view/View;", "setAlpha", "(F)V", &native_View_noop);
    registerMethod("Landroid/view/View;", "setRotation", "(F)V", &native_View_noop);
    registerMethod("Landroid/view/View;", "setTranslationX", "(F)V", &native_View_noop);
    registerMethod("Landroid/view/View;", "setTranslationY", "(F)V", &native_View_noop);
    registerMethod("Landroid/view/View;", "setScaleX", "(F)V", &native_View_noop);
    registerMethod("Landroid/view/View;", "setScaleY", "(F)V", &native_View_noop);
    registerMethod("Landroid/view/View;", "animate",
                   "()Landroid/view/ViewPropertyAnimator;", &native_View_noop);

    // android.view.ViewGroup extras
    registerMethod("Landroid/view/ViewGroup;", "addView",
                   "(Landroid/view/View;)V", [](Frame* f) { /* noop */ });
    registerMethod("Landroid/view/ViewGroup;", "addView",
                   "(Landroid/view/View;I)V", [](Frame* f) { /* noop */ });
    registerMethod("Landroid/view/ViewGroup;", "addView",
                   "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
                   [](Frame* f) { /* noop */ });
    registerMethod("Landroid/view/ViewGroup;", "removeView",
                   "(Landroid/view/View;)V", [](Frame* f) { /* noop */ });
    registerMethod("Landroid/view/ViewGroup;", "removeAllViews",
                   "()V", [](Frame* f) { /* noop */ });
    registerMethod("Landroid/view/ViewGroup;", "getChildCount", "()I",
                   [](Frame* f) { f->setRegister(0, 0); });
    registerMethod("Landroid/view/ViewGroup;", "getChildAt",
                   "(I)Landroid/view/View;", &native_View_noop);

    // android.view.ViewParent
    registerMethod("Landroid/view/ViewParent;",
                   "requestLayout", "()V", &native_View_noop);

    // android.view.ViewPropertyAnimator
    registerMethod("Landroid/view/ViewPropertyAnimator;",
                   "translationX", "(F)Landroid/view/ViewPropertyAnimator;",
                   [](Frame* f) { f->objectReturnValue = f->getObjectRegister(0); });
    registerMethod("Landroid/view/ViewPropertyAnimator;",
                   "translationY", "(F)Landroid/view/ViewPropertyAnimator;",
                   [](Frame* f) { f->objectReturnValue = f->getObjectRegister(0); });
    registerMethod("Landroid/view/ViewPropertyAnimator;",
                   "scaleX", "(F)Landroid/view/ViewPropertyAnimator;",
                   [](Frame* f) { f->objectReturnValue = f->getObjectRegister(0); });
    registerMethod("Landroid/view/ViewPropertyAnimator;",
                   "scaleY", "(F)Landroid/view/ViewPropertyAnimator;",
                   [](Frame* f) { f->objectReturnValue = f->getObjectRegister(0); });
    registerMethod("Landroid/view/ViewPropertyAnimator;",
                   "alpha", "(F)Landroid/view/ViewPropertyAnimator;",
                   [](Frame* f) { f->objectReturnValue = f->getObjectRegister(0); });
    registerMethod("Landroid/view/ViewPropertyAnimator;",
                   "setDuration", "(J)Landroid/view/ViewPropertyAnimator;",
                   [](Frame* f) { f->objectReturnValue = f->getObjectRegister(0); });
    registerMethod("Landroid/view/ViewPropertyAnimator;",
                   "start", "()V", &native_View_noop);
    registerMethod("Landroid/view/ViewPropertyAnimator;",
                   "cancel", "()V", &native_View_noop);
    registerMethod("Landroid/view/ViewPropertyAnimator;",
                   "setInterpolator",
                   "(Landroid/view/animation/Interpolator;)Landroid/view/ViewPropertyAnimator;",
                   [](Frame* f) { f->objectReturnValue = f->getObjectRegister(0); });
    registerMethod("Landroid/view/ViewPropertyAnimator;",
                   "withEndAction", "(Ljava/lang/Runnable;)Landroid/view/ViewPropertyAnimator;",
                   [](Frame* f) { f->objectReturnValue = f->getObjectRegister(0); });

    // android.view.animation interpolators
    registerMethod("Landroid/view/animation/LinearInterpolator;",
                   "<init>", "()V", &native_View_noop);
    registerMethod("Landroid/view/animation/AccelerateInterpolator;",
                   "<init>", "()V", &native_View_noop);
    registerMethod("Landroid/view/animation/DecelerateInterpolator;",
                   "<init>", "()V", &native_View_noop);
    registerMethod("Landroid/view/animation/OvershootInterpolator;",
                   "<init>", "()V", &native_View_noop);
    registerMethod("Landroid/view/animation/AccelerateDecelerateInterpolator;",
                   "<init>", "()V", &native_View_noop);
    registerMethod("Landroid/view/animation/BounceInterpolator;",
                   "<init>", "()V", &native_View_noop);

    // android.content.res.Resources extras
    registerMethod("Landroid/content/res/Resources;", "getInteger",
                   "(I)I",
                   [](Frame* f) { f->setRegister(0, 0); });
    registerMethod("Landroid/content/res/Resources;", "getBoolean",
                   "(I)Z",
                   [](Frame* f) { f->setRegister(0, 0); });
    registerMethod("Landroid/content/res/Resources;", "getDimension",
                   "(I)F",
                   [](Frame* f) { f->setRegister(0, u32f(0.0f)); });
    registerMethod("Landroid/content/res/Resources;", "getDimensionPixelSize",
                   "(I)I",
                   [](Frame* f) { f->setRegister(0, 0); });
    registerMethod("Landroid/content/res/Resources;", "getDrawable",
                   "(I)Landroid/graphics/drawable/Drawable;", &native_View_noop);
    registerMethod("Landroid/content/res/Resources;", "getDrawable",
                   "(ILandroid/content/res/Resources$Theme;)Landroid/graphics/drawable/Drawable;",
                   &native_View_noop);

    // android.util.TypedValue
    registerMethod("Landroid/util/TypedValue;", "<init>", "()V", &native_View_noop);

    // android.util.AttributeSet (interface, no init)
    registerMethod("Landroid/util/AttributeSet;", "getAttributeCount", "()I",
                   [](Frame* f) { f->setRegister(0, 0); });

    // android.graphics.Paint
    registerMethod("Landroid/graphics/Paint;", "<init>", "()V", &native_View_noop);
    registerMethod("Landroid/graphics/Paint;", "<init>", "(I)V", &native_View_noop);
    registerMethod("Landroid/graphics/Paint;", "setColor", "(I)V", &native_View_noop);
    registerMethod("Landroid/graphics/Paint;", "getColor", "()I",
                   [](Frame* f) { f->setRegister(0, 0xFF000000); });
    registerMethod("Landroid/graphics/Paint;", "setTextSize", "(F)V", &native_View_noop);
    registerMethod("Landroid/graphics/Paint;", "measureText",
                   "(Ljava/lang/String;)F",
                   [](Frame* f) { f->setRegister(0, u32f(100.0f)); });

    // android.graphics.Canvas
    registerMethod("Landroid/graphics/Canvas;", "<init>",
                   "(Landroid/graphics/Bitmap;)V", &native_View_noop);
    registerMethod("Landroid/graphics/Canvas;", "drawText",
                   "(Ljava/lang/String;FFLandroid/graphics/Paint;)V", &native_View_noop);
    registerMethod("Landroid/graphics/Canvas;", "drawColor",
                   "(I)V", &native_View_noop);
    registerMethod("Landroid/graphics/Canvas;", "drawRect",
                   "(FFFFLandroid/graphics/Paint;)V", &native_View_noop);

    // android.graphics.Bitmap
    registerMethod("Landroid/graphics/Bitmap;", "createBitmap",
                   "(IILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;",
                   &native_View_noop);
    registerMethod("Landroid/graphics/Bitmap;", "getWidth", "()I",
                   [](Frame* f) { f->setRegister(0, 800); });
    registerMethod("Landroid/graphics/Bitmap;", "getHeight", "()I",
                   [](Frame* f) { f->setRegister(0, 600); });

    // android.graphics.drawable.ColorDrawable
    registerMethod("Landroid/graphics/drawable/ColorDrawable;", "<init>",
                   "()V", &native_View_noop);
    registerMethod("Landroid/graphics/drawable/ColorDrawable;", "<init>",
                   "(I)V", &native_View_noop);

    // android.view.WindowManager / LayoutParams
    registerMethod("Landroid/view/WindowManager;", "getDefaultDisplay",
                   "()Landroid/view/Display;", &native_View_noop);
    registerMethod("Landroid/view/Display;", "getMetrics",
                   "(Landroid/util/DisplayMetrics;)V", &native_View_noop);
    registerMethod("Landroid/view/Display;", "getSize",
                   "(Landroid/graphics/Point;)V",
                   [](Frame* f) {
                       if (!f->argRegisters.empty()) {
                           Object* pt = f->getObjectRegister(f->argRegisters[0]);
                           if (pt) {
                               pt->setIntField(0, 1080); // x = width
                               pt->setIntField(1, 1920); // y = height
                           }
                       }
                   });

    // android.graphics.Point
    registerMethod("Landroid/graphics/Point;", "<init>", "()V", &native_View_noop);
    registerMethod("Landroid/graphics/Point;", "<init>", "(II)V",
                   [](Frame* f) {
                       Object* self = f->getObjectRegister(0);
                       if (self && f->argRegisters.size() >= 2) {
                           self->setIntField(0, f->getRegister(f->argRegisters[0]));
                           self->setIntField(1, f->getRegister(f->argRegisters[1]));
                       }
                   });

    // android.graphics.Rect
    registerMethod("Landroid/graphics/Rect;", "<init>", "()V", &native_View_noop);
    registerMethod("Landroid/graphics/Rect;", "<init>", "(IIII)V",
                   [](Frame* f) {
                       Object* self = f->getObjectRegister(0);
                       if (self && f->argRegisters.size() >= 4) {
                           self->setIntField(0, f->getRegister(f->argRegisters[0]));
                           self->setIntField(1, f->getRegister(f->argRegisters[1]));
                           self->setIntField(2, f->getRegister(f->argRegisters[2]));
                           self->setIntField(3, f->getRegister(f->argRegisters[3]));
                       }
                   });
    registerMethod("Landroid/graphics/Rect;", "width", "()I",
                   [](Frame* f) {
                       Object* self = f->getObjectRegister(0);
                       f->setRegister(0, self ? self->getIntField(2) - self->getIntField(0) : 0);
                   });
    registerMethod("Landroid/graphics/Rect;", "height", "()I",
                   [](Frame* f) {
                       Object* self = f->getObjectRegister(0);
                       f->setRegister(0, self ? self->getIntField(3) - self->getIntField(1) : 0);
                   });

    // androidx.lifecycle
    registerMethod("Landroidx/lifecycle/Lifecycle;", "getCurrentState",
                   "()Landroidx/lifecycle/Lifecycle$State;",
                   &native_View_noop);

    // androidx.fragment.app.Fragment
    registerMethod("Landroidx/fragment/app/Fragment;", "getActivity",
                   "()Landroidx/fragment/app/FragmentActivity;", &native_View_noop);
    registerMethod("Landroidx/fragment/app/Fragment;", "getContext",
                   "()Landroid/content/Context;", &native_View_noop);
    registerMethod("Landroidx/fragment/app/Fragment;", "getArguments",
                   "()Landroid/os/Bundle;", &native_View_noop);
    registerMethod("Landroidx/fragment/app/Fragment;", "requireArguments",
                   "()Landroid/os/Bundle;", &native_View_noop);
    registerMethod("Landroidx/fragment/app/Fragment;", "getTag",
                   "()Ljava/lang/String;", &native_View_noop);
    registerMethod("Landroidx/fragment/app/Fragment;", "setArguments",
                   "(Landroid/os/Bundle;)V", &native_View_noop);

    // android.view.Choreographer
    registerMethod("Landroid/view/Choreographer;", "getInstance",
                   "()Landroid/view/Choreographer;", &native_View_noop);
    registerMethod("Landroid/view/Choreographer;", "postFrameCallback",
                   "(Landroid/view/Choreographer$FrameCallback;)V", &native_View_noop);

    // android.os.Handler extra
    registerMethod("Landroid/os/Handler;", "removeMessages",
                   "(I)V", &native_View_noop);
    registerMethod("Landroid/os/Handler;", "removeMessages",
                   "(ILjava/lang/Object;)V", &native_View_noop);
    registerMethod("Landroid/os/Handler;", "sendEmptyMessage",
                   "(I)Z", [](Frame* f) { f->setRegister(0, 1); });
    registerMethod("Landroid/os/Handler;", "sendEmptyMessageDelayed",
                   "(IJ)Z", [](Frame* f) { f->setRegister(0, 1); });
    registerMethod("Landroid/os/Handler;", "hasMessages",
                   "(I)Z", [](Frame* f) { f->setRegister(0, 0); });
    registerMethod("Landroid/os/Handler;", "obtainMessage",
                   "(I)Landroid/os/Message;", &native_View_noop);
    registerMethod("Landroid/os/Handler;", "obtainMessage",
                   "(III)Landroid/os/Message;", &native_View_noop);
    registerMethod("Landroid/os/Handler;", "obtainMessage",
                   "(ILjava/lang/Object;)Landroid/os/Message;", &native_View_noop);
    registerMethod("Landroid/os/Handler;", "getLooper",
                   "()Landroid/os/Looper;", &native_View_noop);
    registerMethod("Landroid/os/Handler;", "postDelayed",
                   "(Ljava/lang/Runnable;J)Z", [](Frame* f) { f->setRegister(0, 1); });

    // android.view.Window
    registerMethod("Landroid/view/Window;", "setBackgroundDrawable",
                   "(Landroid/graphics/drawable/Drawable;)V", &native_View_noop);
    registerMethod("Landroid/view/Window;", "setBackgroundDrawableResource",
                   "(I)V", &native_View_noop);
    registerMethod("Landroid/view/Window;", "getDecorView",
                   "()Landroid/view/View;", &native_View_noop);
    registerMethod("Landroid/view/Window;", "setFlags",
                   "(II)V", &native_View_noop);
    registerMethod("Landroid/view/Window;", "addFlags",
                   "(I)V", &native_View_noop);
    registerMethod("Landroid/view/Window;", "clearFlags",
                   "(I)V", &native_View_noop);
    registerMethod("Landroid/view/Window;", "setStatusBarColor",
                   "(I)V", &native_View_noop);
    registerMethod("Landroid/view/Window;", "setNavigationBarColor",
                   "(I)V", &native_View_noop);
    registerMethod("Landroid/view/Window;", "getAttributes",
                   "()Landroid/view/WindowManager$LayoutParams;",
                   &native_View_noop);
    registerMethod("Landroid/view/Window;", "setLayout",
                   "(II)V", &native_View_noop);
    registerMethod("Landroid/view/Window;", "setFormat",
                   "(I)V", &native_View_noop);
    registerMethod("Landroid/view/Window;", "setWindowAnimations",
                   "(I)V", &native_View_noop);
    registerMethod("Landroid/view/Window;", "getContext",
                   "()Landroid/content/Context;", &native_View_noop);

    // android.view.WindowManager.LayoutParams
    registerMethod("Landroid/view/WindowManager$LayoutParams;", "<init>",
                   "()V", &native_View_noop);
    registerMethod("Landroid/view/WindowManager$LayoutParams;", "<init>",
                   "(II)V", &native_View_noop);

    // android.content.ContextWrapper / Context extras
    registerMethod("Landroid/content/ContextWrapper;", "getBaseContext",
                   "()Landroid/content/Context;", &native_View_noop);
    registerMethod("Landroid/content/ContextWrapper;", "getResources",
                   "()Landroid/content/res/Resources;", &native_View_noop);
    registerMethod("Landroid/content/ContextWrapper;", "getAssets",
                   "()Landroid/content/res/AssetManager;", &native_View_noop);
    registerMethod("Landroid/content/ContextWrapper;", "getPackageName",
                   "()Ljava/lang/String;", &native_View_noop);
    registerMethod("Landroid/content/ContextWrapper;", "getSystemService",
                   "(Ljava/lang/String;)Ljava/lang/Object;", &native_View_noop);
    registerMethod("Landroid/content/ContextWrapper;", "getApplicationContext",
                   "()Landroid/content/Context;", &native_View_noop);
    registerMethod("Landroid/content/ContextWrapper;", "getContentResolver",
                   "()Landroid/content/ContentResolver;", &native_View_noop);
    registerMethod("Landroid/content/ContextWrapper;", "getClassLoader",
                   "()Ljava/lang/ClassLoader;", &native_View_noop);
    registerMethod("Landroid/content/ContextWrapper;", "getFilesDir",
                   "()Ljava/io/File;", &native_View_noop);
    registerMethod("Landroid/content/ContextWrapper;", "getCacheDir",
                   "()Ljava/io/File;", &native_View_noop);
    registerMethod("Landroid/content/ContextWrapper;", "getDir",
                   "(Ljava/lang/String;I)Ljava/io/File;", &native_View_noop);
    registerMethod("Landroid/content/ContextWrapper;", "getSharedPreferences",
                   "(Ljava/lang/String;I)Landroid/content/SharedPreferences;",
                   &native_View_noop);
    registerMethod("Landroid/content/ContextWrapper;", "openSharedPreferences",
                   "(Ljava/lang/String;I)Landroid/content/SharedPreferences;",
                   &native_View_noop);

    // android.content.SharedPreferences
    registerMethod("Landroid/content/SharedPreferences;", "edit",
                   "()Landroid/content/SharedPreferences$Editor;",
                   &native_View_noop);
    registerMethod("Landroid/content/SharedPreferences;", "getString",
                   "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
                   &native_View_noop);
    registerMethod("Landroid/content/SharedPreferences;", "getInt",
                   "(Ljava/lang/String;I)I", &native_View_noop);
    registerMethod("Landroid/content/SharedPreferences;", "getBoolean",
                   "(Ljava/lang/String;Z)Z",
                   [](Frame* f) {
                       uint8_t defVal = f->argRegisters.size() >= 2 ?
                           f->getRegister(f->argRegisters[1]) & 1 : 0;
                       f->setRegister(0, defVal);
                   });

    // SharedPreferences.Editor
    registerMethod("Landroid/content/SharedPreferences$Editor;", "putString",
                   "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/SharedPreferences$Editor;",
                   [](Frame* f) { f->objectReturnValue = f->getObjectRegister(0); });
    registerMethod("Landroid/content/SharedPreferences$Editor;", "putInt",
                   "(Ljava/lang/String;I)Landroid/content/SharedPreferences$Editor;",
                   [](Frame* f) { f->objectReturnValue = f->getObjectRegister(0); });
    registerMethod("Landroid/content/SharedPreferences$Editor;", "putBoolean",
                   "(Ljava/lang/String;Z)Landroid/content/SharedPreferences$Editor;",
                   [](Frame* f) { f->objectReturnValue = f->getObjectRegister(0); });
    registerMethod("Landroid/content/SharedPreferences$Editor;", "apply", "()V",
                   &native_View_noop);
    registerMethod("Landroid/content/SharedPreferences$Editor;", "commit", "()Z",
                   [](Frame* f) { f->setRegister(0, 1); });

    // android.app.Application / android.app.Application$ActivityLifecycleCallbacks
    registerMethod("Landroid/app/Application;", "getApplicationContext",
                   "()Landroid/content/Context;", &native_View_noop);
    registerMethod("Landroid/app/Application;", "registerActivityLifecycleCallbacks",
                   "(Landroid/app/Application$ActivityLifecycleCallbacks;)V",
                   &native_View_noop);

    // android.content.res.AssetManager
    registerMethod("Landroid/content/res/AssetManager;", "open",
                   "(Ljava/lang/String;)Ljava/io/InputStream;", &native_View_noop);
    registerMethod("Landroid/content/res/AssetManager;", "open",
                   "(Ljava/lang/String;I)Ljava/io/InputStream;", &native_View_noop);

    // android.content.res.Configuration
    registerMethod("Landroid/content/res/Configuration;", "<init>", "()V",
                   &native_View_noop);

    // android.content.res.Resources$Theme
    registerMethod("Landroid/content/res/Resources$Theme;", "<init>", "()V",
                   &native_View_noop);
    registerMethod("Landroid/content/res/Resources$Theme;", "obtainStyledAttributes",
                   "([I)Landroid/content/res/TypedArray;", &native_View_noop);
    registerMethod("Landroid/content/res/Resources$Theme;", "obtainStyledAttributes",
                   "(Landroid/util/AttributeSet;[I)Landroid/content/res/TypedArray;",
                   &native_View_noop);

    // android.content.res.TypedArray
    registerMethod("Landroid/content/res/TypedArray;", "getIndexCount", "()I",
                   [](Frame* f) { f->setRegister(0, 0); });
    registerMethod("Landroid/content/res/TypedArray;", "getIndex", "(I)I",
                   [](Frame* f) { f->setRegister(0, 0); });
    registerMethod("Landroid/content/res/TypedArray;", "getString",
                   "(I)Ljava/lang/String;", &native_View_noop);
    registerMethod("Landroid/content/res/TypedArray;", "getInt",
                   "(II)I", &native_View_noop);
    registerMethod("Landroid/content/res/TypedArray;", "getBoolean",
                   "(IZ)Z",
                   [](Frame* f) {
                       uint8_t defVal = f->argRegisters.size() >= 2 ?
                           f->getRegister(f->argRegisters[1]) & 1 : 0;
                       f->setRegister(0, defVal);
                   });
    registerMethod("Landroid/content/res/TypedArray;", "getLayoutDimension",
                   "(II)I", &native_View_noop);
    registerMethod("Landroid/content/res/TypedArray;", "getResourceId",
                   "(II)I", &native_View_noop);
    registerMethod("Landroid/content/res/TypedArray;", "getColor",
                   "(II)I", &native_View_noop);
    registerMethod("Landroid/content/res/TypedArray;", "getDrawable",
                   "(I)Landroid/graphics/drawable/Drawable;", &native_View_noop);
    registerMethod("Landroid/content/res/TypedArray;", "recycle", "()V",
                   &native_View_noop);

    // android.util.DisplayMetrics
    registerMethod("Landroid/util/DisplayMetrics;", "<init>", "()V",
                   &native_View_noop);

    // android.os.Bundle extras
    registerMethod("Landroid/os/Bundle;", "isEmpty", "()Z",
                   [](Frame* f) { f->setRegister(0, 1); });
    registerMethod("Landroid/os/Bundle;", "containsKey",
                   "(Ljava/lang/String;)Z",
                   [](Frame* f) { f->setRegister(0, 0); });
    registerMethod("Landroid/os/Bundle;", "getString",
                   "(Ljava/lang/String;)Ljava/lang/String;", &native_View_noop);
    registerMethod("Landroid/os/Bundle;", "getString",
                   "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
                   &native_View_noop);
    registerMethod("Landroid/os/Bundle;", "getInt",
                   "(Ljava/lang/String;)I",
                   [](Frame* f) { f->setRegister(0, 0); });
    registerMethod("Landroid/os/Bundle;", "getInt",
                   "(Ljava/lang/String;I)I", &native_View_noop);
    registerMethod("Landroid/os/Bundle;", "getBoolean",
                   "(Ljava/lang/String;)Z",
                   [](Frame* f) { f->setRegister(0, 0); });
    registerMethod("Landroid/os/Bundle;", "getParcelable",
                   "(Ljava/lang/String;)Landroid/os/Parcelable;",
                   &native_View_noop);
    registerMethod("Landroid/os/Bundle;", "putString",
                   "(Ljava/lang/String;Ljava/lang/String;)V", &native_View_noop);
    registerMethod("Landroid/os/Bundle;", "putInt",
                   "(Ljava/lang/String;I)V", &native_View_noop);
    registerMethod("Landroid/os/Bundle;", "putBoolean",
                   "(Ljava/lang/String;Z)V", &native_View_noop);
    registerMethod("Landroid/os/Bundle;", "putParcelable",
                   "(Ljava/lang/String;Landroid/os/Parcelable;)V",
                   &native_View_noop);

    // android.content.Intent extras
    registerMethod("Landroid/content/Intent;", "setClass",
                   "(Landroid/content/Context;Ljava/lang/Class;)Landroid/content/Intent;",
                   [](Frame* f) {
                       Object* intent = f->getObjectRegister(0);
                       Object* clsObj = f->getObjectRegister(argReg(f, 1));
                       // Read class descriptor from the ClassObject.
                       std::string desc;
                       if (auto* cls = dynamic_cast<ObjectModel::ClassObject*>(clsObj)) {
                           desc = cls->getDescriptor();
                       }
                       if (intent && !desc.empty()) {
                           intent->setObjField(INTENT_TARGET_CLASS_FIELD,
                               ObjectModel::StringObject::create(desc));
                       }
                       f->objectReturnValue = intent;
                   });
    registerMethod("Landroid/content/Intent;", "setClassName",
                   "(Landroid/content/Context;Ljava/lang/String;)Landroid/content/Intent;",
                   [](Frame* f) {
                       Object* intent = f->getObjectRegister(0);
                       Object* nameObj = readArgObj(f, 1);
                       if (intent && nameObj) {
                           intent->setObjField(INTENT_TARGET_CLASS_FIELD, nameObj);
                       }
                       f->objectReturnValue = intent;
                   });
    registerMethod("Landroid/content/Intent;", "setClassName",
                   "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;",
                   [](Frame* f) {
                       Object* intent = f->getObjectRegister(0);
                       Object* nameObj = readArgObj(f, 1);
                       if (intent && nameObj) {
                           intent->setObjField(INTENT_TARGET_CLASS_FIELD, nameObj);
                       }
                       f->objectReturnValue = intent;
                   });
    registerMethod("Landroid/content/Intent;", "addFlags",
                   "(I)Landroid/content/Intent;",
                   [](Frame* f) { f->objectReturnValue = f->getObjectRegister(0); });
    registerMethod("Landroid/content/Intent;", "putExtra",
                   "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;",
                   [](Frame* f) { f->objectReturnValue = f->getObjectRegister(0); });
    registerMethod("Landroid/content/Intent;", "putExtra",
                   "(Ljava/lang/String;I)Landroid/content/Intent;",
                   [](Frame* f) { f->objectReturnValue = f->getObjectRegister(0); });
    registerMethod("Landroid/content/Intent;", "putExtra",
                   "(Ljava/lang/String;Landroid/os/Parcelable;)Landroid/content/Intent;",
                   [](Frame* f) { f->objectReturnValue = f->getObjectRegister(0); });
    registerMethod("Landroid/content/Intent;", "putExtra",
                   "(Ljava/lang/String;Ljava/io/Serializable;)Landroid/content/Intent;",
                   [](Frame* f) { f->objectReturnValue = f->getObjectRegister(0); });
    registerMethod("Landroid/content/Intent;", "getAction",
                   "()Ljava/lang/String;", &native_View_noop);
    registerMethod("Landroid/content/Intent;", "getComponent",
                   "()Landroid/content/ComponentName;", &native_View_noop);
    registerMethod("Landroid/content/Intent;", "hasExtra",
                   "(Ljava/lang/String;)Z",
                   [](Frame* f) { f->setRegister(0, 0); });
    registerMethod("Landroid/content/Intent;", "getData",
                   "()Landroid/net/Uri;", &native_View_noop);

    // android.content.ComponentName
    registerMethod("Landroid/content/ComponentName;", "<init>",
                   "(Landroid/content/Context;Ljava/lang/Class;)V",
                   &native_View_noop);
    registerMethod("Landroid/content/ComponentName;", "<init>",
                   "(Ljava/lang/String;Ljava/lang/String;)V",
                   &native_View_noop);
    registerMethod("Landroid/content/ComponentName;", "getClassName",
                   "()Ljava/lang/String;", &native_View_noop);
    registerMethod("Landroid/content/ComponentName;", "getPackageName",
                   "()Ljava/lang/String;", &native_View_noop);

    // java.lang.StringBuilder extras
    registerMethod("Ljava/lang/StringBuilder;", "append",
                   "(I)Ljava/lang/StringBuilder;",
                   [](Frame* f) { f->objectReturnValue = f->getObjectRegister(0); });
    registerMethod("Ljava/lang/StringBuilder;", "append",
                   "(F)Ljava/lang/StringBuilder;",
                   [](Frame* f) { f->objectReturnValue = f->getObjectRegister(0); });
    registerMethod("Ljava/lang/StringBuilder;", "append",
                   "(D)Ljava/lang/StringBuilder;",
                   [](Frame* f) { f->objectReturnValue = f->getObjectRegister(0); });
    registerMethod("Ljava/lang/StringBuilder;", "append",
                   "(C)Ljava/lang/StringBuilder;",
                   [](Frame* f) { f->objectReturnValue = f->getObjectRegister(0); });
    registerMethod("Ljava/lang/StringBuilder;", "length", "()I",
                   [](Frame* f) { f->setRegister(0, 0); });
    registerMethod("Ljava/lang/StringBuilder;", "charAt",
                   "(I)C",
                   [](Frame* f) { f->setRegister(0, 0); });
    registerMethod("Ljava/lang/StringBuilder;", "delete",
                   "(II)Ljava/lang/StringBuilder;",
                   [](Frame* f) { f->objectReturnValue = f->getObjectRegister(0); });

    // java.io.File extras
    registerMethod("Ljava/io/File;", "<init>",
                   "(Ljava/lang/String;)V", &native_View_noop);
    registerMethod("Ljava/io/File;", "<init>",
                   "(Ljava/lang/String;Ljava/lang/String;)V", &native_View_noop);
    registerMethod("Ljava/io/File;", "exists", "()Z",
                   [](Frame* f) { f->setRegister(0, 0); });
    registerMethod("Ljava/io/File;", "mkdirs", "()Z",
                   [](Frame* f) { f->setRegister(0, 1); });
    registerMethod("Ljava/io/File;", "getPath", "()Ljava/lang/String;",
                   &native_StringBuilder_toString);
    registerMethod("Ljava/io/File;", "getAbsolutePath", "()Ljava/lang/String;",
                   &native_StringBuilder_toString);
    registerMethod("Ljava/io/File;", "getName", "()Ljava/lang/String;",
                   &native_StringBuilder_toString);

    // java.io.InputStream / ByteArrayOutputStream
    registerMethod("Ljava/io/InputStream;", "read", "([B)I",
                   [](Frame* f) { f->setRegister(0, -1); });
    registerMethod("Ljava/io/InputStream;", "close", "()V", &native_View_noop);
    registerMethod("Ljava/io/ByteArrayOutputStream;", "<init>", "()V",
                   &native_View_noop);
    registerMethod("Ljava/io/ByteArrayOutputStream;", "toByteArray", "()[B",
                   &native_View_noop);

    // java.lang.reflect.Array
    registerMethod("Ljava/lang/reflect/Array;", "newInstance",
                   "(Ljava/lang/Class;I)Ljava/lang/Object;", &native_View_noop);

    // java.lang.Class
    registerMethod("Ljava/lang/Class;", "getName", "()Ljava/lang/String;",
                   &native_StringBuilder_toString);
    registerMethod("Ljava/lang/Class;", "getSimpleName", "()Ljava/lang/String;",
                   &native_StringBuilder_toString);
    registerMethod("Ljava/lang/Class;", "getCanonicalName", "()Ljava/lang/String;",
                   &native_StringBuilder_toString);
    registerMethod("Ljava/lang/Class;", "isAssignableFrom",
                   "(Ljava/lang/Class;)Z",
                   [](Frame* f) { f->setRegister(0, 1); });
    registerMethod("Ljava/lang/Class;", "isInstance",
                   "(Ljava/lang/Object;)Z",
                   [](Frame* f) { f->setRegister(0, 1); });
    registerMethod("Ljava/lang/Class;", "getSuperclass",
                   "()Ljava/lang/Class;", &native_View_noop);
    registerMethod("Ljava/lang/Class;", "forName",
                   "(Ljava/lang/String;)Ljava/lang/Class;", &native_View_noop);

    // android.content.ContentResolver
    registerMethod("Landroid/content/ContentResolver;", "<init>", "()V",
                   &native_View_noop);

    // android.app.Application$ActivityLifecycleCallbacks (interface)
    registerMethod("Landroid/app/Application$ActivityLifecycleCallbacks;",
                   "onActivityCreated",
                   "(Landroid/app/Activity;Landroid/os/Bundle;)V", &native_View_noop);
    registerMethod("Landroid/app/Application$ActivityLifecycleCallbacks;",
                   "onActivityResumed", "(Landroid/app/Activity;)V", &native_View_noop);
    registerMethod("Landroid/app/Application$ActivityLifecycleCallbacks;",
                   "onActivityPaused", "(Landroid/app/Activity;)V", &native_View_noop);
    registerMethod("Landroid/app/Application$ActivityLifecycleCallbacks;",
                   "onActivityDestroyed", "(Landroid/app/Activity;)V", &native_View_noop);

    // android.content.res.AssetManager$AssetInputStream
    registerMethod("Landroid/content/res/AssetManager$AssetInputStream;", "close",
                   "()V", &native_View_noop);

    // ===== MISSING COLLECTION STUBS =====
    // java.util.concurrent.ConcurrentHashMap
    registerMethod("Ljava/util/concurrent/ConcurrentHashMap;", "<init>", "()V", &native_View_noop);
    registerMethod("Ljava/util/concurrent/ConcurrentHashMap;", "<init>", "(I)V", &native_View_noop);
    registerMethod("Ljava/util/concurrent/ConcurrentHashMap;", "<init>", "(IF)V", &native_View_noop);
    registerMethod("Ljava/util/concurrent/ConcurrentHashMap;", "get",
                   "(Ljava/lang/Object;)Ljava/lang/Object;", &native_View_noop_returning_null);
    registerMethod("Ljava/util/concurrent/ConcurrentHashMap;", "put",
                   "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;", &native_View_noop_returning_null);
    registerMethod("Ljava/util/concurrent/ConcurrentHashMap;", "size", "()I", &native_View_noop_returning_zero);
    registerMethod("Ljava/util/concurrent/ConcurrentHashMap;", "containsKey",
                   "(Ljava/lang/Object;)Z", &native_View_noop_returning_false);
    registerMethod("Ljava/util/concurrent/ConcurrentHashMap;", "remove",
                   "(Ljava/lang/Object;)Ljava/lang/Object;", &native_View_noop_returning_null);
    registerMethod("Ljava/util/concurrent/ConcurrentHashMap;", "isEmpty", "()Z", &native_View_noop_returning_true);
    registerMethod("Ljava/util/concurrent/ConcurrentHashMap;", "clear", "()V", &native_View_noop);

    // java.util.concurrent.CopyOnWriteArraySet
    registerMethod("Ljava/util/concurrent/CopyOnWriteArraySet;", "<init>", "()V", &native_View_noop);
    registerMethod("Ljava/util/concurrent/CopyOnWriteArraySet;", "iterator",
                   "()Ljava/util/Iterator;", &native_View_noop_returning_null);
    registerMethod("Ljava/util/concurrent/CopyOnWriteArraySet;", "add",
                   "(Ljava/lang/Object;)Z", &native_View_noop_returning_false);
    registerMethod("Ljava/util/concurrent/CopyOnWriteArraySet;", "size", "()I", &native_View_noop_returning_zero);

    // java.util.Set
    registerMethod("Ljava/util/Set;", "add", "(Ljava/lang/Object;)Z", &native_View_noop_returning_false);
    registerMethod("Ljava/util/Set;", "iterator", "()Ljava/util/Iterator;", &native_View_noop_returning_null);
    registerMethod("Ljava/util/Set;", "size", "()I", &native_View_noop_returning_zero);
    registerMethod("Ljava/util/Set;", "contains", "(Ljava/lang/Object;)Z", &native_View_noop_returning_false);

    // java.util.Collection
    registerMethod("Ljava/util/Collection;", "iterator", "()Ljava/util/Iterator;", &native_View_noop_returning_null);
    registerMethod("Ljava/util/Collection;", "size", "()I", &native_View_noop_returning_zero);
    registerMethod("Ljava/util/Collection;", "isEmpty", "()Z", &native_View_noop_returning_true);

    // java.util.concurrent.atomic.AtomicBoolean
    registerMethod("Ljava/util/concurrent/atomic/AtomicBoolean;", "<init>", "()V", &native_View_noop);
    registerMethod("Ljava/util/concurrent/atomic/AtomicBoolean;", "<init>", "(Z)V", &native_View_noop);
    registerMethod("Ljava/util/concurrent/atomic/AtomicBoolean;", "get", "()Z", &native_View_noop_returning_false);
    registerMethod("Ljava/util/concurrent/atomic/AtomicBoolean;", "set", "(Z)V", &native_View_noop);

    // java.lang.Enum
    registerMethod("Ljava/lang/Enum;", "compareTo", "(Ljava/lang/Enum;)I", [](Frame* f) {
        Object* other = f->getObjectRegister(1);
        Object* self = f->getObjectRegister(0);
        if (!self || !other) { f->setRegister(0, 0); return; }
        uint32_t sIdx = self->getIntField(0);
        uint32_t oIdx = other->getIntField(0);
        f->setRegister(0, static_cast<int32_t>(sIdx) - static_cast<int32_t>(oIdx));
    });

    // java.util.ArrayList (Collection constructor)
    registerMethod("Ljava/util/ArrayList;", "<init>", "(Ljava/util/Collection;)V", &native_View_noop);

    // ===== ANDROIDX LIFECYCLE STUBS =====
    registerMethod("Landroidx/lifecycle/LifecycleRegistry;", "handleLifecycleEvent",
                   "(Landroidx/lifecycle/Lifecycle$Event;)V", &native_View_noop);
    registerMethod("Landroidx/lifecycle/LifecycleRegistry;", "addObserver",
                   "(Landroidx/lifecycle/LifecycleObserver;)V", &native_View_noop);
    registerMethod("Landroidx/lifecycle/LifecycleRegistry;", "removeObserver",
                   "(Landroidx/lifecycle/LifecycleObserver;)V", &native_View_noop);
    registerMethod("Landroidx/lifecycle/Lifecycle;", "addObserver",
                   "(Landroidx/lifecycle/LifecycleObserver;)V", &native_View_noop);
    registerMethod("Landroidx/lifecycle/Lifecycle;", "removeObserver",
                   "(Landroidx/lifecycle/LifecycleObserver;)V", &native_View_noop);
    registerMethod("Landroidx/lifecycle/LifecycleOwner;", "getLifecycle",
                   "()Landroidx/lifecycle/Lifecycle;", &native_View_noop_returning_null);
    registerMethod("Landroidx/lifecycle/Lifecycle$State;", "isAtLeast",
                   "(Landroidx/lifecycle/Lifecycle$State;)Z", &native_View_noop_returning_false);
    registerMethod("Landroidx/lifecycle/Lifecycle$Event;", "getTargetState",
                   "()Landroidx/lifecycle/Lifecycle$State;", &native_View_noop_returning_null);

    // ===== ACTIVITY LIFECYCLE STUBS =====
    registerMethod("Landroid/app/Activity;", "onCreate", "(Landroid/os/Bundle;)V", &native_View_noop);
    registerMethod("Landroid/app/Activity;", "onStart", "()V", &native_View_noop);
    registerMethod("Landroid/app/Activity;", "onResume", "()V", &native_View_noop);
    registerMethod("Landroid/app/Activity;", "onPause", "()V", &native_View_noop);
    registerMethod("Landroid/app/Activity;", "onStop", "()V", &native_View_noop);
    registerMethod("Landroid/app/Activity;", "onDestroy", "()V", &native_View_noop);
    registerMethod("Landroid/app/Activity;", "onRestart", "()V", &native_View_noop);

    // ComponentActivity.onCreate
    registerMethod("Landroidx/activity/ComponentActivity;", "onCreate",
                   "(Landroid/os/Bundle;)V", &native_View_noop);

    // Edge-to-edge system bar styling has no Win32 equivalent in the
    // compatibility window, so these lifecycle helpers intentionally do
    // nothing while preserving the Android call contract.
    registerMethod("Landroidx/activity/EdgeToEdge;", "enable$default",
                   "(Landroidx/activity/ComponentActivity;Landroidx/activity/SystemBarStyle;Landroidx/activity/SystemBarStyle;ILjava/lang/Object;)V",
                   &native_View_noop);
    registerMethod("Landroidx/activity/EdgeToEdge;", "enable",
                   "(Landroidx/activity/ComponentActivity;Landroidx/activity/SystemBarStyle;Landroidx/activity/SystemBarStyle;)V",
                   &native_View_noop);

    // Compose content is outside the current native view implementation.
    // Keep the activity lifecycle progressing; ActivityManager supplies the
    // visible fallback hierarchy when no native content is produced.
    registerMethod("Landroidx/activity/compose/ComponentActivityKt;",
                   "setContent$default",
                   "(Landroidx/activity/ComponentActivity;Landroidx/compose/runtime/CompositionContext;Lkotlin/jvm/functions/Function2;ILjava/lang/Object;)V",
                   &native_View_noop);
    registerMethod("Landroidx/activity/compose/ComponentActivityKt;",
                   "setContent",
                   "(Landroidx/activity/ComponentActivity;Landroidx/compose/runtime/CompositionContext;Lkotlin/jvm/functions/Function2;)V",
                   &native_View_noop);

    // ReportFragment
    registerMethod("Landroidx/lifecycle/ReportFragment$Companion;", "injectIfNeededIn",
                   "(Landroid/app/Activity;)V", &native_View_noop);

    // android.app.ContextImpl constructor
    registerMethod("Landroid/app/ContextImpl;", "<init>", "()V", &native_View_noop);

}

} // namespace NativeDex::Launcher

