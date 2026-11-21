#include "Intent.h"
#include <sstream>

namespace NativeDex::Android::Content {

const std::string Intent::ACTION_MAIN = "android.intent.action.MAIN";
const std::string Intent::ACTION_VIEW = "android.intent.action.VIEW";
const std::string Intent::ACTION_EDIT = "android.intent.action.EDIT";
const std::string Intent::ACTION_PICK = "android.intent.action.PICK";
const std::string Intent::ACTION_SEND = "android.intent.action.SEND";
const std::string Intent::ACTION_SEND_MULTIPLE = "android.intent.action.SEND_MULTIPLE";

const std::string Intent::CATEGORY_DEFAULT = "android.intent.category.DEFAULT";
const std::string Intent::CATEGORY_LAUNCHER = "android.intent.category.LAUNCHER";
const std::string Intent::CATEGORY_HOME = "android.intent.category.HOME";
const std::string Intent::CATEGORY_BROWSABLE = "android.intent.category.BROWSABLE";



Intent::Intent(const std::string& action) : action_(action) {}

Intent::Intent(const std::string& action, const std::string& uri) : action_(action), data_(uri) {}

Intent::Intent(Context* packageContext, const std::string& className) {
    // Would resolve component from context
}

Intent::Intent(const Intent& other) 
    : action_(other.action_)
    , data_(other.data_)
    , type_(other.type_)
    , componentPackage_(other.componentPackage_)
    , componentClass_(other.componentClass_)
    , categories_(other.categories_)
    , extras_(other.extras_)
    , flags_(other.flags_) {}

Intent::Intent(Intent&& other) noexcept
    : action_(std::move(other.action_))
    , data_(std::move(other.data_))
    , type_(std::move(other.type_))
    , componentPackage_(std::move(other.componentPackage_))
    , componentClass_(std::move(other.componentClass_))
    , categories_(std::move(other.categories_))
    , extras_(std::move(other.extras_))
    , flags_(other.flags_) {}

Intent::~Intent() = default;

Intent& Intent::operator=(const Intent& other) {
    if (this != &other) {
        action_ = other.action_;
        data_ = other.data_;
        type_ = other.type_;
        componentPackage_ = other.componentPackage_;
        componentClass_ = other.componentClass_;
        categories_ = other.categories_;
        extras_ = other.extras_;
        flags_ = other.flags_;
    }
    return *this;
}

Intent& Intent::operator=(Intent&& other) noexcept {
    if (this != &other) {
        action_ = std::move(other.action_);
        data_ = std::move(other.data_);
        type_ = std::move(other.type_);
        componentPackage_ = std::move(other.componentPackage_);
        componentClass_ = std::move(other.componentClass_);
        categories_ = std::move(other.categories_);
        extras_ = std::move(other.extras_);
        flags_ = other.flags_;
    }
    return *this;
}

Intent& Intent::setAction(const std::string& action) {
    action_ = action;
    return *this;
}

std::string Intent::getAction() const {
    return action_;
}

Intent& Intent::setData(const std::string& uri) {
    data_ = uri;
    type_.clear();
    return *this;
}

Intent& Intent::setDataAndType(const std::string& uri, const std::string& type) {
    data_ = uri;
    type_ = type;
    return *this;
}

std::string Intent::getDataString() const {
    return data_;
}

std::string Intent::getType() const {
    return type_;
}

Intent& Intent::setComponent(const std::string& packageName, const std::string& className) {
    componentPackage_ = packageName;
    componentClass_ = className;
    return *this;
}

Intent& Intent::setClassName(const std::string& packageName, const std::string& className) {
    return setComponent(packageName, className);
}

std::string Intent::getComponentPackage() const {
    return componentPackage_;
}

std::string Intent::getComponentClass() const {
    return componentClass_;
}

Intent& Intent::addCategory(const std::string& category) {
    if (std::find(categories_.begin(), categories_.end(), category) == categories_.end()) {
        categories_.push_back(category);
    }
    return *this;
}

void Intent::removeCategory(const std::string& category) {
    categories_.erase(std::remove(categories_.begin(), categories_.end(), category), categories_.end());
}

std::vector<std::string> Intent::getCategories() const {
    return categories_;
}

Intent& Intent::putExtra(const std::string& name, const std::string& value) {
    extras_.putString(name, value);
    return *this;
}

Intent& Intent::putExtra(const std::string& name, int value) {
    extras_.putInt(name, value);
    return *this;
}

Intent& Intent::putExtra(const std::string& name, int64_t value) {
    extras_.putLong(name, value);
    return *this;
}

Intent& Intent::putExtra(const std::string& name, float value) {
    extras_.putFloat(name, value);
    return *this;
}

Intent& Intent::putExtra(const std::string& name, double value) {
    extras_.putDouble(name, value);
    return *this;
}

Intent& Intent::putExtra(const std::string& name, bool value) {
    extras_.putBoolean(name, value);
    return *this;
}

Intent& Intent::putExtra(const std::string& name, const Bundle& value) {
    extras_.putBundle(name, value);
    return *this;
}

Intent& Intent::putExtras(const Bundle& extras) {
    // Would merge bundles
    return *this;
}

Bundle Intent::getExtras() const {
    return extras_;
}

std::string Intent::getStringExtra(const std::string& name) const {
    return extras_.getString(name);
}

int Intent::getIntExtra(const std::string& name, int defaultValue) const {
    return extras_.getInt(name, defaultValue);
}

int64_t Intent::getLongExtra(const std::string& name, int64_t defaultValue) const {
    return extras_.getLong(name, defaultValue);
}

float Intent::getFloatExtra(const std::string& name, float defaultValue) const {
    return extras_.getFloat(name, defaultValue);
}

double Intent::getDoubleExtra(const std::string& name, double defaultValue) const {
    return extras_.getDouble(name, defaultValue);
}

bool Intent::getBooleanExtra(const std::string& name, bool defaultValue) const {
    return extras_.getBoolean(name, defaultValue);
}

Bundle Intent::getBundleExtra(const std::string& name) const {
    return extras_.getBundle(name);
}

Intent& Intent::setFlags(int flags) {
    flags_ = flags;
    return *this;
}

Intent& Intent::addFlags(int flags) {
    flags_ |= flags;
    return *this;
}

int Intent::getFlags() const {
    return flags_;
}

Intent& Intent::setType(const std::string& type) {
    type_ = type;
    return *this;
}

std::string Intent::resolveType(Context* context) const {
    if (!type_.empty()) return type_;
    // Would resolve from content resolver
    return "";
}

std::string Intent::toString() const {
    std::ostringstream oss;
    oss << "Intent { act=" << action_;
    if (!data_.empty()) oss << " dat=" << data_;
    if (!type_.empty()) oss << " typ=" << type_;
    if (!componentPackage_.empty() && !componentClass_.empty()) {
        oss << " cmp=" << componentPackage_ << "/" << componentClass_;
    }
    if (!categories_.empty()) {
        oss << " cat=[";
        for (size_t i = 0; i < categories_.size(); ++i) {
            if (i > 0) oss << ",";
            oss << categories_[i];
        }
        oss << "]";
    }
    if (flags_ != 0) {
        oss << " flg=0x" << std::hex << flags_ << std::dec;
    }
    oss << " }";
    return oss.str();
}

std::string Intent::toUri(int flags) const {
    // Simplified
    return toString();
}

Intent Intent::parseUri(const std::string& uri, int flags) {
    // Simplified
    return Intent();
}

} // namespace NativeDex::Android::Content