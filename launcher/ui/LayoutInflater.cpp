#include "LayoutInflater.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <iostream>

#include "../core/ApkResources.h"

namespace NativeDex::Launcher {

LayoutInflater& LayoutInflater::instance() {
    static LayoutInflater inst;
    return inst;
}

void LayoutInflater::reset() { descriptors_.clear(); }

std::string LayoutInflater::descriptorOf(uint64_t viewId) const {
    auto it = descriptors_.find(viewId);
    return it == descriptors_.end() ? std::string("Landroid/view/View;") : it->second;
}

void LayoutInflater::setDescriptor(uint64_t viewId, const std::string& d) { descriptors_[viewId] = d; }

namespace {

const std::string* findAttr(const XmlNode& node, const char* name) {
    std::string a = std::string("android:") + name;
    for (const auto& at : node.attributes) {
        if (at.name == a || at.name == name) return &at.value;
    }
    return nullptr;
}

bool parseRef(const std::string& v, uint32_t& id) {
    if (v.size() > 3 && (v[0] == '@' || v[0] == '?') && v[1] == '0' && v[2] == 'x') {
        id = static_cast<uint32_t>(std::strtoul(v.c_str() + 1, nullptr, 16));
        return true;
    }
    return false;
}

std::string resolveString(const std::string& v) {
    uint32_t id;
    if (parseRef(v, id)) return ApkResources::instance().getString(id);
    return v;
}

// "16.000000dp" / "12sp" / "8px" / plain number -> pixels
float parseDimension(const std::string& v, bool& ok) {
    ok = true;
    char* end = nullptr;
    float n = std::strtof(v.c_str(), &end);
    if (end == v.c_str()) { ok = false; return 0; }
    std::string unit(end);
    if (unit == "dp" || unit == "dip" || unit == "sp") return n * kDensity;
    return n;
}

int resolveDimension(const std::string& v, int fallback) {
    if (v == "match_parent" || v == "fill_parent") return -1;
    if (v == "wrap_content") return -2;
    uint32_t id;
    std::string s = v;
    if (parseRef(v, id)) {
        auto val = ApkResources::instance().resolve(id);
        if (!val.found || val.type != 0x05) return fallback;
        static const float radix[] = {1.0f / (1 << 8), 1.0f / (1 << 15), 1.0f / (1 << 23), 1.0f / (1u << 31)};
        float f = static_cast<float>(static_cast<int32_t>(val.data & 0xFFFFFF00u)) * radix[(val.data >> 4) & 3];
        unsigned unit = val.data & 0xF;
        return static_cast<int>((unit == 1 || unit == 2) ? f * kDensity : f);
    }
    bool ok;
    float px = parseDimension(s, ok);
    return ok ? static_cast<int>(px) : fallback;
}

bool resolveColor(const std::string& v, uint32_t& argb) {
    uint32_t id;
    if (parseRef(v, id)) {
        auto val = ApkResources::instance().resolve(id);
        if (val.found && val.type >= 0x1C && val.type <= 0x1F) { argb = val.data; return true; }
        return false;
    }
    if (!v.empty() && v[0] == '#') {
        argb = static_cast<uint32_t>(std::strtoul(v.c_str() + 1, nullptr, 16));
        if (v.size() <= 7) argb |= 0xFF000000u;   // #RRGGBB
        return true;
    }
    return false;
}

bool resolveInt(const std::string& v, int& out) {
    if (v.empty()) return false;
    char* end = nullptr;
    long n = std::strtol(v.c_str(), &end, 0);
    if (end == v.c_str()) return false;
    out = static_cast<int>(n);
    return true;
}

std::string toDescriptor(const std::string& tag) {
    std::string t = tag;
    for (auto& c : t) if (c == '.') c = '/';
    if (tag.find('.') == std::string::npos) {
        static const char* views[] = {"View", "ViewGroup", "ViewStub", "SurfaceView", "TextureView"};
        for (const char* v : views) if (tag == v) return "Landroid/view/" + tag + ";";
        return "Landroid/widget/" + tag + ";";
    }
    return "L" + t + ";";
}

} // namespace

uint64_t LayoutInflater::inflateById(uint32_t resId) {
    std::string path = ApkResources::instance().filePath(resId);
    if (path.empty()) {
        std::cerr << "[LayoutInflater] layout resource 0x" << std::hex << resId << std::dec
                  << " not found in resource table" << std::endl;
        return 0;
    }
    return inflateFile(path);
}

uint64_t LayoutInflater::inflateFile(const std::string& path) {
    std::vector<uint8_t> bytes = ApkResources::instance().readFile(path);
    if (bytes.empty()) {
        std::cerr << "[LayoutInflater] cannot read " << path << std::endl;
        return 0;
    }
    std::string data(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    auto root = BinaryAndroidXmlParser::parseLayout(data);
    if (!root) {
        std::cerr << "[LayoutInflater] cannot parse " << path << std::endl;
        return 0;
    }
    return inflateNode(*root, 0);
}

uint64_t LayoutInflater::inflateNode(const XmlNode& node, int depth) {
    if (depth > 64) return 0;
    auto& reg = NativeViewRegistry::instance();

    std::string tag = node.name;
    std::string shortName = tag.substr(tag.find_last_of('.') == std::string::npos ? 0 : tag.find_last_of('.') + 1);

    // <include layout="@layout/x"/>
    if (tag == "include") {
        if (const std::string* l = findAttr(node, "layout")) {
            uint32_t id;
            if (parseRef(*l, id)) return inflateById(id);
        }
        return 0;
    }

    uint32_t bg = 0;
    bool hasBg = false;
    if (const std::string* v = findAttr(node, "background")) hasBg = resolveColor(*v, bg);
    uint32_t textColor = 0xFF000000u;
    if (const std::string* v = findAttr(node, "textColor")) resolveColor(*v, textColor);
    std::string text;
    if (const std::string* v = findAttr(node, "text")) text = resolveString(*v);
    std::string title;
    if (const std::string* v = findAttr(node, "title")) title = resolveString(*v);
    float textSize = 18.0f;
    if (const std::string* v = findAttr(node, "textSize")) { int d = resolveDimension(*v, 0); if (d > 0) textSize = static_cast<float>(d); }
    int maxVal = 100, progress = 0;
    bool indeterminate = false;
    if (const std::string* v = findAttr(node, "max")) resolveInt(*v, maxVal);
    if (const std::string* v = findAttr(node, "progress")) resolveInt(*v, progress);
    if (const std::string* v = findAttr(node, "indeterminate")) indeterminate = (*v == "true");
    int orientation = 0;                               // LinearLayout default is horizontal
    if (const std::string* v = findAttr(node, "orientation")) { int o; if (resolveInt(*v, o)) orientation = o; }

    uint64_t viewId = 0;
    std::string desc = toDescriptor(tag);
    bool isGroup = false;

    if (shortName == "LinearLayout" || shortName == "RadioGroup") {
        viewId = reg.createLinearLayout(orientation, hasBg ? bg : 0);
        isGroup = true;
    } else if (shortName == "Toolbar") {
        viewId = reg.createToolbar(title.empty() ? text : title, textColor);
        isGroup = true;
    } else if (shortName == "ImageButton") {
        viewId = reg.createImageButton(hasBg ? bg : 0xFF888888u);
    } else if (shortName == "ImageView") {
        viewId = reg.createImageView();
    } else if (shortName == "ProgressBar") {
        viewId = reg.createProgressBar(maxVal, progress, indeterminate);
    } else if (shortName == "WebView") {
        viewId = reg.createWebView("", "");
    } else if (shortName == "Button" || shortName == "CompoundButton" || shortName == "ToggleButton") {
        viewId = reg.createButton(text, textColor);
    } else if (shortName == "TextView" || shortName == "EditText" || shortName == "CheckBox" || shortName == "Switch" ||
               shortName == "RadioButton" || shortName == "AutoCompleteTextView" || shortName == "AppCompatTextView") {
        viewId = reg.createTextView(text, textSize, textColor);
    } else if (shortName == "FrameLayout" || shortName == "ScrollView" || shortName == "HorizontalScrollView" ||
               shortName == "RelativeLayout" || shortName == "ConstraintLayout" || shortName == "CoordinatorLayout" ||
               shortName == "NestedScrollView" || shortName == "TableLayout" || shortName == "TableRow" ||
               shortName == "GridLayout" || shortName == "ViewGroup" || !node.children.empty()) {
        viewId = reg.createFrameLayout(hasBg ? bg : 0);
        isGroup = true;
    } else {
        // Unknown leaf view (custom view, SurfaceView, ...): an empty placeholder.
        viewId = reg.createImageView();
    }
    if (viewId == 0) return 0;
    descriptors_[viewId] = desc;

    int w = -2, h = -2;
    if (const std::string* v = findAttr(node, "layout_width")) w = resolveDimension(*v, -2);
    if (const std::string* v = findAttr(node, "layout_height")) h = resolveDimension(*v, -2);
    reg.setLayoutParams(viewId, w, h);

    if (View* view = reg.resolve(viewId)) {
        if (hasBg) view->setBackgroundColor(bg);
        if (const std::string* v = findAttr(node, "layout_weight")) view->getLayoutParams().weight = std::strtof(v->c_str(), nullptr);
        if (const std::string* v = findAttr(node, "visibility")) {
            int vis;
            if (resolveInt(*v, vis)) view->setVisibility(vis == 8 ? View::GONE : (vis == 4 ? View::INVISIBLE : View::VISIBLE));
        }
        auto margin = [&](const char* n, int& slot) { if (const std::string* v = findAttr(node, n)) slot = resolveDimension(*v, 0); };
        auto& lp = view->getLayoutParams();
        if (const std::string* v = findAttr(node, "layout_margin")) { int m = resolveDimension(*v, 0); lp.marginLeft = lp.marginTop = lp.marginRight = lp.marginBottom = m; }
        margin("layout_marginLeft", lp.marginLeft);
        margin("layout_marginStart", lp.marginLeft);
        margin("layout_marginTop", lp.marginTop);
        margin("layout_marginRight", lp.marginRight);
        margin("layout_marginEnd", lp.marginRight);
        margin("layout_marginBottom", lp.marginBottom);
    }

    if (const std::string* v = findAttr(node, "id")) {
        uint32_t rid;
        if (parseRef(*v, rid)) {
            std::string name = ApkResources::instance().entryName(rid);
            reg.registerIdMapping(static_cast<int>(rid), name, viewId);
            if (View* view = reg.resolve(viewId)) view->setId(static_cast<int>(rid));
        }
    }

    for (const auto& child : node.children) {
        uint64_t childId = inflateNode(child, depth + 1);
        if (childId != 0 && isGroup) reg.addView(viewId, childId);
    }
    return viewId;
}

} // namespace NativeDex::Launcher
