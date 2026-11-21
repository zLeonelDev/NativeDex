#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include "../android.os/Bundle.h"

namespace NativeDex::Android::Content {

using OS::Bundle;

class Context;

class Intent {
public:
    // Action constants
    static const std::string ACTION_MAIN;
    static const std::string ACTION_VIEW;
    static const std::string ACTION_EDIT;
    static const std::string ACTION_PICK;
    static const std::string ACTION_SEND;
    static const std::string ACTION_SEND_MULTIPLE;
    
    // Category constants
    static const std::string CATEGORY_DEFAULT;
    static const std::string CATEGORY_LAUNCHER;
    static const std::string CATEGORY_HOME;
    static const std::string CATEGORY_BROWSABLE;
    
    // Flags
    static const int FLAG_ACTIVITY_NEW_TASK = 0x10000000;
    static const int FLAG_ACTIVITY_CLEAR_TOP = 0x04000000;
    static const int FLAG_ACTIVITY_SINGLE_TOP = 0x20000000;
    static const int FLAG_ACTIVITY_REORDER_TO_FRONT = 0x00020000;
    static const int FLAG_ACTIVITY_NO_HISTORY = 0x40000000;
    static const int FLAG_ACTIVITY_EXCLUDE_FROM_RECENTS = 0x00400000;
    static const int FLAG_ACTIVITY_PREVIOUS_IS_TOP = 0x00200000;
    static const int FLAG_GRANT_READ_URI_PERMISSION = 0x00000001;
    static const int FLAG_GRANT_WRITE_URI_PERMISSION = 0x00000002;

    Intent() = default;
    Intent(const std::string& action);
    Intent(const std::string& action, const std::string& uri);
    Intent(Context* packageContext, const std::string& className);
    Intent(const Intent& other);
    Intent(Intent&& other) noexcept;
    ~Intent();
    
    Intent& operator=(const Intent& other);
    Intent& operator=(Intent&& other) noexcept;

    // Action
    Intent& setAction(const std::string& action);
    std::string getAction() const;
    bool hasAction() const { return !action_.empty(); }

    // Data/URI
    Intent& setData(const std::string& uri);
    Intent& setDataAndType(const std::string& uri, const std::string& type);
    std::string getDataString() const;
    std::string getType() const;

    // Component
    Intent& setComponent(const std::string& packageName, const std::string& className);
    Intent& setClassName(const std::string& packageName, const std::string& className);
    std::string getComponentPackage() const;
    std::string getComponentClass() const;
    bool hasComponent() const { return !componentPackage_.empty() && !componentClass_.empty(); }

    // Categories
    Intent& addCategory(const std::string& category);
    void removeCategory(const std::string& category);
    std::vector<std::string> getCategories() const;

    // Extras (Bundle)
    Intent& putExtra(const std::string& name, const std::string& value);
    Intent& putExtra(const std::string& name, int value);
    Intent& putExtra(const std::string& name, int64_t value);
    Intent& putExtra(const std::string& name, float value);
    Intent& putExtra(const std::string& name, double value);
    Intent& putExtra(const std::string& name, bool value);
    Intent& putExtra(const std::string& name, const Bundle& value);
    Intent& putExtras(const Bundle& extras);
    
    Bundle getExtras() const;
    std::string getStringExtra(const std::string& name) const;
    int getIntExtra(const std::string& name, int defaultValue = 0) const;
    int64_t getLongExtra(const std::string& name, int64_t defaultValue = 0) const;
    float getFloatExtra(const std::string& name, float defaultValue = 0.0f) const;
    double getDoubleExtra(const std::string& name, double defaultValue = 0.0) const;
    bool getBooleanExtra(const std::string& name, bool defaultValue = false) const;
    Bundle getBundleExtra(const std::string& name) const;

    // Flags
    Intent& setFlags(int flags);
    Intent& addFlags(int flags);
    int getFlags() const;

    // Type
    Intent& setType(const std::string& type);
    std::string resolveType(Context* context) const;

    // Debug
    std::string toString() const;
    std::string toUri(int flags = 0) const;
    static Intent parseUri(const std::string& uri, int flags);

private:
    std::string action_;
    std::string data_;
    std::string type_;
    std::string componentPackage_;
    std::string componentClass_;
    std::vector<std::string> categories_;
    Bundle extras_;
    int flags_ = 0;
};

} // namespace NativeDex::Android::Content