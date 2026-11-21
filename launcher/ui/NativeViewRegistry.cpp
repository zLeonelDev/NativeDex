#include "NativeViewRegistry.h"
#include "Activity.h"
#include "ActivityManager.h"
#include "../../platform/windows/graphics/Direct2DGraphics.h"
#include "../../platform/windows/input/Win32Input.h"
#include <algorithm>
#include <iostream>
#include <cstring>
#include <chrono>

namespace gfx = NativeDex::Platform::Windows;

namespace NativeDex::Launcher {

// Forward declarations for the concrete widget classes (defined below).
class TextViewImpl;
class ButtonImpl;
class LinearLayoutImpl;
class FrameLayoutImpl;
class ToolbarImpl;
class ImageButtonImpl;
class ImageViewImpl;
class ProgressBarImpl;
class WebViewImpl;

// ============================================================================
// TextView
// ============================================================================
class TextViewImpl : public View {
public:
    TextViewImpl() = default;
    void setText(const std::string& t) { text_ = t; }
    const std::string& text() const { return text_; }
    void setTextSize(float s) { textSize_ = s; }
    float getTextSize() const { return textSize_; }
    void setTextColorArgb(uint32_t c) { textColor_ = c; }
    void setGravity(int g) { (void)g; } // accepted but unused for now

    void onDraw(NativeDex::Platform::Windows::Direct2DGraphics& g) override {
        NativeDex::Platform::Windows::Direct2DGraphics::Rect r(0.0f, 0.0f,
                                 static_cast<float>(getWidth()),
                                 static_cast<float>(getHeight()));
        NativeDex::Platform::Windows::Direct2DGraphics::Color col = NativeDex::Platform::Windows::Direct2DGraphics::Color::fromArgb(textColor_);
        // Use the system default font; align-leading / near.
        std::wstring wtext;
        wtext.reserve(text_.size());
        for (char ch : text_) wtext.push_back(static_cast<wchar_t>(static_cast<unsigned char>(ch)));
        g.drawText(wtext, r, col, textSize_ > 0 ? textSize_ : 18.0f, L"Segoe UI", false, false,
                   DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

private:
    std::string text_;
    float textSize_ = 18.0f;
    uint32_t textColor_ = 0xFF000000; // black
};

// ============================================================================
// Button (TextView subclass for visual distinction)
// ============================================================================
class ButtonImpl : public TextViewImpl {
public:
    void onDraw(NativeDex::Platform::Windows::Direct2DGraphics& g) override {
        NativeDex::Platform::Windows::Direct2DGraphics::Rect r(0.0f, 0.0f,
                                 static_cast<float>(getWidth()),
                                 static_cast<float>(getHeight()));
        // Filled background.
        NativeDex::Platform::Windows::Direct2DGraphics::Color bgColor{0.20f, 0.45f, 0.90f, 1.0f};
        g.fillRoundedRect(r, 6.0f, 6.0f, bgColor);
        NativeDex::Platform::Windows::Direct2DGraphics::Color borderColor{0.10f, 0.30f, 0.70f, 1.0f};
        g.drawRoundedRect(r, 6.0f, 6.0f, borderColor, 1.0f);
        TextViewImpl::onDraw(g);
    }
};

// ============================================================================
// LinearLayout
// ============================================================================
class LinearLayoutImpl : public ViewGroup {
public:
    LinearLayoutImpl() = default;

    void setOrientation(int o) { orientation_ = o; } // 0=horizontal, 1=vertical

    void onMeasure(int widthSpec, int heightSpec) override {
        int widthMode = widthSpec & 0x3;
        int widthSize = widthSpec >> 2;
        int heightMode = heightSpec & 0x3;
        int heightSize = heightSpec >> 2;

        int totalW = 0, totalH = 0;
        for (auto& child : getChildren()) {
            // Children of a LinearLayout typically use WRAP_CONTENT or
            // MATCH_PARENT; we measure against the parent spec directly.
            child->measure(widthSpec, heightSpec);
            int cw = child->getMeasuredWidth();
            int ch = child->getMeasuredHeight();
            if (orientation_ == 0) { // horizontal
                totalW += cw;
                totalH = std::max(totalH, ch);
            } else {                  // vertical
                totalW = std::max(totalW, cw);
                totalH += ch;
            }
        }

        int w = totalW, h = totalH;
        if (widthMode == 0)  w = widthSize;   // EXACTLY
        if (heightMode == 0) h = heightSize;
        setMeasuredDimension(w, h);
    }

    void onLayout(bool /*changed*/, int left, int top, int right, int bottom) override {
        int cursor = 0;
        for (auto& child : getChildren()) {
            int cw = child->getMeasuredWidth();
            int ch = child->getMeasuredHeight();
            if (orientation_ == 0) {
                child->layout(left + cursor, top, left + cursor + cw, top + ch);
                cursor += cw;
            } else {
                child->layout(left, top + cursor, left + cw, top + cursor + ch);
                cursor += ch;
            }
        }
    }

    void onDraw(NativeDex::Platform::Windows::Direct2DGraphics& g) override {
        // Background already drawn by base View::draw if backgroundColor_ != 0
        // then dispatch children.
        dispatchDraw(g);
    }

    bool onTouchEvent(const NativeDex::Platform::Windows::Win32Input::AndroidMotionEvent& /*ev*/) override { return false; }

private:
    int orientation_ = 1; // vertical (matches Android default)
};

// ============================================================================
// FrameLayout
// ============================================================================
class FrameLayoutImpl : public ViewGroup {
public:
    FrameLayoutImpl() = default;
    void onMeasure(int widthSpec, int heightSpec) override {
        int widthMode = widthSpec & 0x3;
        int widthSize = widthSpec >> 2;
        int heightMode = heightSpec & 0x3;
        int heightSize = heightSpec >> 2;
        int maxW = 0, maxH = 0;
        for (auto& child : getChildren()) {
            child->measure(widthSpec, heightSpec);
            maxW = std::max(maxW, child->getMeasuredWidth());
            maxH = std::max(maxH, child->getMeasuredHeight());
        }
        int w = maxW, h = maxH;
        if (widthMode == 0)  w = widthSize;
        if (heightMode == 0) h = heightSize;
        setMeasuredDimension(w, h);
    }
    void onDraw(NativeDex::Platform::Windows::Direct2DGraphics& g) override { dispatchDraw(g); }
    bool onTouchEvent(const NativeDex::Platform::Windows::Win32Input::AndroidMotionEvent&) override { return false; }
};

// ============================================================================
// Toolbar
// ============================================================================
class ToolbarImpl : public ViewGroup {
public:
    ToolbarImpl() = default;
    void setTitle(const std::string& t) { title_ = t; }
    void setTitleColor(uint32_t c) { titleColor_ = c; }

    void onMeasure(int widthSpec, int heightSpec) override {
        int widthMode = widthSpec & 0x3;
        int widthSize = widthSpec >> 2;
        int heightMode = heightSpec & 0x3;
        int heightSize = heightSpec >> 2;
        int totalW = 0, maxH = 0;
        for (auto& child : getChildren()) {
            child->measure(widthSpec, heightSpec);
            totalW += child->getMeasuredWidth();
            maxH = std::max(maxH, child->getMeasuredHeight());
        }
        // Reserve a min height for the toolbar.
        int h = std::max(maxH, 56);
        if (heightMode == 0) h = heightSize;
        int w = (widthMode == 0) ? widthSize : totalW;
        setMeasuredDimension(w, h);
    }
    void onLayout(bool, int left, int top, int right, int bottom) override {
        int cursor = left;
        int height = bottom - top;
        for (auto& child : getChildren()) {
            int cw = child->getMeasuredWidth();
            child->layout(cursor, top, cursor + cw, top + height);
            cursor += cw;
        }
    }
    void onDraw(NativeDex::Platform::Windows::Direct2DGraphics& g) override {
        // Background
        NativeDex::Platform::Windows::Direct2DGraphics::Rect r(0, 0,
            static_cast<float>(getWidth()), static_cast<float>(getHeight()));
        NativeDex::Platform::Windows::Direct2DGraphics::Color bg{0.93f, 0.93f, 0.93f, 1.0f};
        if (backgroundColor_) bg = NativeDex::Platform::Windows::Direct2DGraphics::Color::fromArgb(backgroundColor_);
        g.fillRect(r, bg);

        // Title text
        if (!title_.empty()) {
            std::wstring wtext;
            wtext.reserve(title_.size());
            for (char ch : title_) wtext.push_back(static_cast<wchar_t>(static_cast<unsigned char>(ch)));
            auto col = NativeDex::Platform::Windows::Direct2DGraphics::Color::fromArgb(titleColor_);
            g.drawText(wtext, r, col, 18.0f, L"Segoe UI", false, false,
                DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
        dispatchDraw(g);
    }
    bool onTouchEvent(const NativeDex::Platform::Windows::Win32Input::AndroidMotionEvent&) override { return false; }
private:
    std::string title_;
    uint32_t titleColor_ = 0xFF000000;
};

// ============================================================================
// ImageButton
// ============================================================================
class ImageButtonImpl : public View {
public:
    ImageButtonImpl() = default;
    void onDraw(NativeDex::Platform::Windows::Direct2DGraphics& g) override {
        NativeDex::Platform::Windows::Direct2DGraphics::Rect r(0, 0,
            static_cast<float>(getWidth()), static_cast<float>(getHeight()));
        // Draw a circular-ish glyph as a placeholder
        NativeDex::Platform::Windows::Direct2DGraphics::Color fill{0.4f, 0.7f, 1.0f, 1.0f};
        if (backgroundColor_) fill = NativeDex::Platform::Windows::Direct2DGraphics::Color::fromArgb(backgroundColor_);
        g.fillRoundedRect(r, 4.0f, 4.0f, fill);
        NativeDex::Platform::Windows::Direct2DGraphics::Color border{0.2f, 0.4f, 0.7f, 1.0f};
        g.drawRoundedRect(r, 4.0f, 4.0f, border, 1.0f);
        // Draw a unicode arrow as placeholder icon
        std::wstring wtext;
        for (char ch : std::string("\u21BB")) wtext.push_back(static_cast<wchar_t>(static_cast<unsigned char>(ch)));
        // Use a static refresh-arrow glyph
        wtext = L"\u21BB";
        g.drawText(wtext, r, {0.0f, 0.0f, 0.0f, 1.0f}, 20.0f, L"Segoe UI", false, false,
            DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
    bool onTouchEvent(const NativeDex::Platform::Windows::Win32Input::AndroidMotionEvent&) override { return true; }
};

// ============================================================================
// ImageView
// ============================================================================
class ImageViewImpl : public View {
public:
    void onDraw(NativeDex::Platform::Windows::Direct2DGraphics& g) override {
        NativeDex::Platform::Windows::Direct2DGraphics::Rect r(0, 0,
            static_cast<float>(getWidth()), static_cast<float>(getHeight()));
        if (backgroundColor_) {
            g.fillRect(r, NativeDex::Platform::Windows::Direct2DGraphics::Color::fromArgb(backgroundColor_));
        }
    }
};

// ============================================================================
// ProgressBar
// ============================================================================
class ProgressBarImpl : public View {
public:
    void setMax(int m) { max_ = m; }
    void setProgress(int p) { progress_ = p; }
    void setIndeterminate(bool i) { indeterminate_ = i; }
    void onDraw(NativeDex::Platform::Windows::Direct2DGraphics& g) override {
        NativeDex::Platform::Windows::Direct2DGraphics::Rect r(0, 0,
            static_cast<float>(getWidth()), static_cast<float>(getHeight()));
        // Track background
        g.fillRoundedRect(r, 4.0f, 4.0f, {0.85f, 0.85f, 0.85f, 1.0f});
        if (max_ > 0 && progress_ > 0) {
            int w = (getWidth() * progress_) / max_;
            NativeDex::Platform::Windows::Direct2DGraphics::Rect fill(0, 0,
                static_cast<float>(w), static_cast<float>(getHeight()));
            g.fillRoundedRect(fill, 4.0f, 4.0f, {0.20f, 0.60f, 0.86f, 1.0f});
        } else if (indeterminate_) {
            // Animate: time-based slice. Just draw a stripe.
            static auto t0 = std::chrono::steady_clock::now();
            auto now = std::chrono::steady_clock::now();
            float f = std::chrono::duration<float>(now - t0).count();
            int stripePos = static_cast<int>(f * 100.0f) % (getWidth() + 80) - 80;
            int stripeW = 80;
            if (stripePos + stripeW > 0 && stripePos < getWidth()) {
                NativeDex::Platform::Windows::Direct2DGraphics::Rect fill(
                    static_cast<float>(std::max(0, stripePos)), 0,
                    static_cast<float>(std::min(getWidth(), stripePos + stripeW)),
                    static_cast<float>(getHeight()));
                g.fillRoundedRect(fill, 4.0f, 4.0f, {0.20f, 0.60f, 0.86f, 1.0f});
            }
        }
    }
    bool onTouchEvent(const NativeDex::Platform::Windows::Win32Input::AndroidMotionEvent&) override { return false; }
private:
    int max_ = 100;
    int progress_ = 0;
    bool indeterminate_ = false;
};

// ============================================================================
// WebView (renders the URL and a snippet of HTML content as text)
// ============================================================================
class WebViewImpl : public View {
public:
    WebViewImpl() = default;
    void setLoadedUrl(const std::string& u) { loadedUrl_ = u; }
    void setContentHtml(const std::string& h) { contentHtml_ = h; }
    void setSettingsJavaScriptEnabled(bool b) { jsEnabled_ = b; }
    const std::string& getLoadedUrl() const { return loadedUrl_; }
    const std::string& getContentHtml() const { return contentHtml_; }
    void setPadding(int l, int t, int r, int b) { padL_ = l; padT_ = t; padR_ = r; padB_ = b; }
    void setScrollY(int y) { scrollY_ = y; }
    void setJavascriptInterfaces(const std::vector<std::string>& ifs) { jsInterfaces_ = ifs; }

    void onDraw(NativeDex::Platform::Windows::Direct2DGraphics& g) override {
        NativeDex::Platform::Windows::Direct2DGraphics::Rect r(padL_, padT_,
            static_cast<float>(getWidth() - padR_),
            static_cast<float>(getHeight() - padB_));
        // Background
        NativeDex::Platform::Windows::Direct2DGraphics::Color bg{1.0f, 1.0f, 1.0f, 1.0f};
        if (backgroundColor_) bg = NativeDex::Platform::Windows::Direct2DGraphics::Color::fromArgb(backgroundColor_);
        g.fillRect(r, bg);

        // Border
        g.drawRect(r, {0.6f, 0.6f, 0.6f, 1.0f}, 1.0f);

        // Render the URL bar at the top
        std::wstring urlBar;
        urlBar.reserve(loadedUrl_.size() + 32);
        std::string header = "[WebView] " + loadedUrl_;
        for (char ch : header) urlBar.push_back(static_cast<wchar_t>(static_cast<unsigned char>(ch)));
        NativeDex::Platform::Windows::Direct2DGraphics::Rect headerRect(
            static_cast<float>(padL_ + 8), static_cast<float>(padT_ + 6),
            static_cast<float>(getWidth() - padR_ - 8), static_cast<float>(padT_ + 32));
        g.drawText(urlBar, headerRect, {0.20f, 0.40f, 0.70f, 1.0f}, 13.0f,
            L"Consolas", false, false,
            DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        // Separator line
        g.drawLine({static_cast<float>(padL_ + 8), static_cast<float>(padT_ + 36)},
                   {static_cast<float>(getWidth() - padR_ - 8), static_cast<float>(padT_ + 36)},
                   {0.7f, 0.7f, 0.7f, 1.0f}, 1.0f);

        // Render the HTML as text (each line below the bar). Strip HTML tags.
        std::wstring content;
        std::string text = stripHtml(contentHtml_);
        // Truncate to first ~30 lines, ~120 chars/line, to fit.
        int lines = 0;
        int col = 0;
        const int maxLines = 28;
        const int maxCols = 110;
        std::string currentLine;
        for (char ch : text + "\n") {
            if (ch == '\n' || col >= maxCols) {
                if (lines >= maxLines) {
                    currentLine += "..."; break;
                }
                for (char c : currentLine) content.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
                content.push_back(L'\n');
                currentLine.clear();
                col = 0;
                lines++;
                if (ch == '\n') continue;
            }
            if (ch != '\r') {
                currentLine.push_back(ch);
                col++;
            }
        }
        if (!currentLine.empty() && lines < maxLines) {
            for (char c : currentLine) content.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
        }
        NativeDex::Platform::Windows::Direct2DGraphics::Rect bodyRect(
            static_cast<float>(padL_ + 8), static_cast<float>(padT_ + 44),
            static_cast<float>(getWidth() - padR_ - 8),
            static_cast<float>(getHeight() - padB_ - 8));
        g.drawText(content, bodyRect, {0.10f, 0.10f, 0.10f, 1.0f}, 12.0f,
            L"Consolas", false, false,
            DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    }
    bool onTouchEvent(const NativeDex::Platform::Windows::Win32Input::AndroidMotionEvent&) override { return false; }

    static std::string stripHtml(const std::string& html) {
        std::string out;
        out.reserve(html.size());
        bool inTag = false;
        for (char c : html) {
            if (c == '<') { inTag = true; continue; }
            if (c == '>') { inTag = false; out.push_back(' '); continue; }
            if (!inTag) out.push_back(c);
        }
        // Compress whitespace
        std::string result;
        result.reserve(out.size());
        bool prevSpace = false;
        for (char c : out) {
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                if (!prevSpace) { result.push_back(' '); prevSpace = true; }
            } else { result.push_back(c); prevSpace = false; }
        }
        return result;
    }
private:
    std::string loadedUrl_;
    std::string contentHtml_;
    bool jsEnabled_ = false;
    int padL_ = 0, padT_ = 0, padR_ = 0, padB_ = 0;
    int scrollY_ = 0;
    std::vector<std::string> jsInterfaces_;
};

// ============================================================================
// Registry
// ============================================================================
NativeViewRegistry& NativeViewRegistry::instance() {
    static NativeViewRegistry inst;
    return inst;
}

void NativeViewRegistry::reset() {
    views_.clear();
    namedViews_.clear();
    idMap_.clear();
    currentContent_ = nullptr;
}

uint64_t NativeViewRegistry::createTextView(const std::string& text, float textSize, uint32_t colorArgb) {
    uint64_t id = ++nextId_;
    Entry e;
    auto* tv = new TextViewImpl();
    tv->setText(text);
    tv->setTextSize(textSize);
    tv->setTextColorArgb(colorArgb);
    tv->setLayoutParams(View::LayoutParams{-2, -2, 0.0f, 0}); // WRAP_CONTENT
    e.rawView = tv;
    e.view.reset(tv);
    views_[id] = std::move(e);
    return id;
}

uint64_t NativeViewRegistry::createButton(const std::string& label, uint32_t colorArgb) {
    uint64_t id = ++nextId_;
    Entry e;
    auto* b = new ButtonImpl();
    b->setText(label);
    b->setTextColorArgb(colorArgb);
    b->setLayoutParams(View::LayoutParams{-2, -2, 0.0f, 0});
    e.rawView = b;
    e.view.reset(b);
    views_[id] = std::move(e);
    return id;
}

uint64_t NativeViewRegistry::createLinearLayout(int orientation, uint32_t backgroundColor) {
    uint64_t id = ++nextId_;
    Entry e;
    auto* ll = new LinearLayoutImpl();
    ll->setOrientation(orientation);
    ll->setBackgroundColor(backgroundColor);
    ll->setLayoutParams(View::LayoutParams{-1, -1, 0.0f, 0}); // MATCH_PARENT
    e.rawView = ll;
    e.view.reset(ll);
    views_[id] = std::move(e);
    return id;
}

uint64_t NativeViewRegistry::createFrameLayout(uint32_t backgroundColor) {
    uint64_t id = ++nextId_;
    Entry e;
    auto* fl = new FrameLayoutImpl();
    fl->setBackgroundColor(backgroundColor);
    fl->setLayoutParams(View::LayoutParams{-1, -1, 0.0f, 0});
    e.rawView = fl;
    e.view.reset(fl);
    views_[id] = std::move(e);
    return id;
}

uint64_t NativeViewRegistry::createToolbar(const std::string& title, uint32_t colorArgb) {
    uint64_t id = ++nextId_;
    Entry e;
    auto* tb = new ToolbarImpl();
    tb->setTitle(title);
    tb->setTitleColor(colorArgb);
    tb->setBackgroundColor(0xFFEEEEEE);
    tb->setLayoutParams(View::LayoutParams{-1, -2, 0.0f, 0}); // MATCH_PARENT x WRAP_CONTENT
    e.rawView = tb;
    e.view.reset(tb);
    views_[id] = std::move(e);
    return id;
}

uint64_t NativeViewRegistry::createImageButton(uint32_t colorArgb) {
    uint64_t id = ++nextId_;
    Entry e;
    auto* ib = new ImageButtonImpl();
    ib->setBackgroundColor(colorArgb);
    ib->setLayoutParams(View::LayoutParams{48, 48, 0.0f, 0});
    e.rawView = ib;
    e.view.reset(ib);
    views_[id] = std::move(e);
    return id;
}

uint64_t NativeViewRegistry::createImageView() {
    uint64_t id = ++nextId_;
    Entry e;
    auto* iv = new ImageViewImpl();
    iv->setLayoutParams(View::LayoutParams{-2, -2, 0.0f, 0});
    e.rawView = iv;
    e.view.reset(iv);
    views_[id] = std::move(e);
    return id;
}

uint64_t NativeViewRegistry::createProgressBar(int max, int progress, bool indeterminate) {
    uint64_t id = ++nextId_;
    Entry e;
    auto* pb = new ProgressBarImpl();
    pb->setMax(max);
    pb->setProgress(progress);
    pb->setIndeterminate(indeterminate);
    pb->setLayoutParams(View::LayoutParams{-1, -2, 0.0f, 0});
    e.rawView = pb;
    e.view.reset(pb);
    views_[id] = std::move(e);
    return id;
}

uint64_t NativeViewRegistry::createWebView(const std::string& contentHtml, const std::string& loadedUrl) {
    uint64_t id = ++nextId_;
    Entry e;
    auto* wv = new WebViewImpl();
    wv->setContentHtml(contentHtml);
    wv->setLoadedUrl(loadedUrl);
    wv->setLayoutParams(View::LayoutParams{-1, -1, 0.0f, 0}); // MATCH_PARENT
    e.rawView = wv;
    e.view.reset(wv);
    views_[id] = std::move(e);
    return id;
}

void NativeViewRegistry::webViewLoadUrl(uint64_t viewId, const std::string& url) {
    if (auto* wv = dynamic_cast<WebViewImpl*>(resolve(viewId))) {
        wv->setLoadedUrl(url);
    }
}
void NativeViewRegistry::webViewSetHtml(uint64_t viewId, const std::string& html) {
    if (auto* wv = dynamic_cast<WebViewImpl*>(resolve(viewId))) {
        wv->setContentHtml(html);
    }
}
const std::string& NativeViewRegistry::webViewGetLoadedUrl(uint64_t viewId) const {
    static std::string empty;
    if (auto* wv = dynamic_cast<WebViewImpl*>(resolve(viewId))) return wv->getLoadedUrl();
    return empty;
}
const std::string& NativeViewRegistry::webViewGetContentHtml(uint64_t viewId) const {
    static std::string empty;
    if (auto* wv = dynamic_cast<WebViewImpl*>(resolve(viewId))) return wv->getContentHtml();
    return empty;
}
void NativeViewRegistry::webViewSetBackgroundColor(uint64_t viewId, uint32_t colorArgb) {
    if (View* v = resolve(viewId)) v->setBackgroundColor(colorArgb);
}
void NativeViewRegistry::webViewSetPadding(uint64_t viewId, int l, int t, int r, int b) {
    if (auto* wv = dynamic_cast<WebViewImpl*>(resolve(viewId))) wv->setPadding(l, t, r, b);
}
void NativeViewRegistry::webViewAddJavascriptInterface(uint64_t viewId, const std::string& name) {
    if (auto* wv = dynamic_cast<WebViewImpl*>(resolve(viewId))) {
        // Record the name; will be shown in the WebView header.
        wv->setLoadedUrl(wv->getLoadedUrl() + "  [JS:" + name + "]");
    }
}
uint64_t NativeViewRegistry::webViewGetSettings(uint64_t viewId) {
    // Return the WebView itself as the "settings" object so subsequent
    // WebSettings.setJavaScriptEnabled calls route back to it.
    return viewId;
}
void NativeViewRegistry::webViewSettingsSetJavaScriptEnabled(uint64_t viewId, bool enabled) {
    if (auto* wv = dynamic_cast<WebViewImpl*>(resolve(viewId))) wv->setSettingsJavaScriptEnabled(enabled);
}

void NativeViewRegistry::setContentView(uint64_t /*activityThis*/, uint64_t viewId) {
    View* v = resolve(viewId);
    currentContent_ = v;
    if (v) {
        // Measure + layout the freshly-inflated tree against the window size.
        ActivityManager* am = ActivityManager::global();
        int w = 800, h = 600;
        if (am) am->getWindowSize(w, h);
        v->measure((w << 2) | 0 /*EXACTLY*/, (h << 2) | 0);
        v->layout(0, 0, w, h);
    }
}

uint64_t NativeViewRegistry::findViewById(int id, const std::string& idName) {
    // Try integer id first.
    auto iti = idMap_.find(id);
    if (iti != idMap_.end() && iti->second != 0) return iti->second;
    // Then try the symbolic name.
    if (!idName.empty()) {
        auto its = namedViews_.find(idName);
        if (its != namedViews_.end()) return its->second;
    }
    return 0;
}

void NativeViewRegistry::registerIdMapping(int intId, const std::string& name, uint64_t viewId) {
    if (viewId == 0) return;
    if (intId != 0) idMap_[intId] = viewId;
    if (!name.empty()) namedViews_[name] = viewId;
}

void NativeViewRegistry::setLayoutParams(uint64_t viewId, int width, int height) {
    if (View* v = resolve(viewId)) {
        View::LayoutParams lp = v->getLayoutParams();
        lp.width = width;
        lp.height = height;
        v->setLayoutParams(lp);
    }
}

void NativeViewRegistry::setText(uint64_t viewId, const std::string& text) {
    if (View* v = resolve(viewId)) {
        if (auto* tv = dynamic_cast<TextViewImpl*>(v)) tv->setText(text);
    }
}

void NativeViewRegistry::addView(uint64_t parentId, uint64_t childId) {
    View* parent = resolve(parentId);
    if (auto* vg = dynamic_cast<ViewGroup*>(parent)) {
        auto it = views_.find(childId);
        if (it == views_.end() || !it->second.view) return;
        View* child = it->second.view.get();
        vg->addView(std::move(it->second.view));
        it->second.rawView = child;
    }
}

View* NativeViewRegistry::resolve(uint64_t viewId) const {
    if (viewId == 0) return nullptr;
    auto it = views_.find(viewId);
    if (it == views_.end()) return nullptr;
    return it->second.rawView;
}

View* NativeViewRegistry::currentContentView() const {
    return currentContent_;
}

} // namespace NativeDex::Launcher




