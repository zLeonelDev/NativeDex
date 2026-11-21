#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "Activity.h"  // for View / ViewGroup / ActivityManager forward

namespace NativeDex::Launcher {

// A globally-accessible registry that maps a JVM object identity (the raw
// pointer value of an Object*) to a C++ View. The bytecode interpreter can
// hand a "View" (an Object*) back from native methods like setContentView and
// findViewById; this registry is what gives that opaque pointer meaning so
// the ActivityManager can render it.
//
// The lifetime of these proxy views is owned by the registry, which is
// destroyed when the launcher exits.
class NativeViewRegistry {
public:
    static NativeViewRegistry& instance();

    // Create a TextView with a text string. Returns the opaque object id.
    uint64_t createTextView(const std::string& text, float textSize, uint32_t colorArgb);

    // Create a Button with a label. Returns the opaque object id.
    uint64_t createButton(const std::string& label, uint32_t colorArgb);

    // Create a LinearLayout container. Returns the opaque object id.
    uint64_t createLinearLayout(int orientation, uint32_t backgroundColor);

    // Create a FrameLayout container.
    uint64_t createFrameLayout(uint32_t backgroundColor);

    // Create a Toolbar (rendered as a horizontal bar with optional title).
    uint64_t createToolbar(const std::string& title, uint32_t colorArgb);

    // Create an ImageButton (square with a placeholder glyph).
    uint64_t createImageButton(uint32_t colorArgb);

    // Create a ProgressBar (renders as a horizontal bar with optional progress).
    uint64_t createProgressBar(int max, int progress, bool indeterminate);

    // Create an ImageView (no specific rendering, used for layout structure).
    uint64_t createImageView();

    // Create a WebView. contentHtml is the HTML to render (rendered as text
    // in this minimal build since we have no HTML engine).
    uint64_t createWebView(const std::string& contentHtml, const std::string& loadedUrl);

    // WebView-specific operations.
    void webViewLoadUrl(uint64_t viewId, const std::string& url);
    void webViewSetHtml(uint64_t viewId, const std::string& html);
    const std::string& webViewGetLoadedUrl(uint64_t viewId) const;
    const std::string& webViewGetContentHtml(uint64_t viewId) const;
    void webViewSetBackgroundColor(uint64_t viewId, uint32_t colorArgb);
    void webViewSetPadding(uint64_t viewId, int l, int t, int r, int b);
    void webViewAddJavascriptInterface(uint64_t viewId, const std::string& name);
    uint64_t webViewGetSettings(uint64_t viewId);
    void webViewSettingsSetJavaScriptEnabled(uint64_t viewId, bool enabled);

    // Set the content view of a "this Activity" (opaque id). The activity id
    // is unused for now but kept for symmetry with the future real Activity
    // object; the most-recent setContentView is what ActivityManager renders.
    void setContentView(uint64_t /*activityThis*/, uint64_t viewId);

    // findViewById: looks up the named view registered during layout inflation.
    uint64_t findViewById(int id, const std::string& idName);

    // Register a view by integer resource id (e.g. R.id.webview -> 0x7f0a0023)
    // and by symbolic name (e.g. "webview"). Either or both keys can be used.
    void registerIdMapping(int intId, const std::string& name, uint64_t viewId);

    // Set layout params on a view (WRAP_CONTENT=-2 / MATCH_PARENT=-1).
    void setLayoutParams(uint64_t viewId, int width, int height);

    // Set text on a TextView/Button (re-applies after inflation).
    void setText(uint64_t viewId, const std::string& text);

    // Add a child to a parent.
    void addView(uint64_t parentId, uint64_t childId);

    // Resolve an opaque id back to a View*. Returns nullptr if unknown.
    View* resolve(uint64_t viewId) const;

    // Returns the most recently set content view (the one ActivityManager
    // should render).
    View* currentContentView() const;

    // Clear the registry (called on activity finish / new launch).
    void reset();

private:
    NativeViewRegistry() = default;
    NativeViewRegistry(const NativeViewRegistry&) = delete;
    NativeViewRegistry& operator=(const NativeViewRegistry&) = delete;

    uint64_t nextId_ = 0xA0000000ULL; // avoid colliding with potential real heap ptrs

    struct Entry {
        std::unique_ptr<View> view;
        View* rawView = nullptr;
    };
    std::unordered_map<uint64_t, Entry> views_;
    std::unordered_map<std::string, uint64_t> namedViews_;
    std::unordered_map<int, uint64_t> idMap_;
    View* currentContent_ = nullptr;
};

} // namespace NativeDex::Launcher
