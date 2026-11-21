#include "NativeRegistry.h"
#include "Vm.h"
#include <fstream>
#include <mutex>
#include <set>

namespace NativeDex::Runtime::BytecodeInterpreter {

ObjectModel::StringObject* NativeCall::s(size_t k) const {
    return dynamic_cast<ObjectModel::StringObject*>(o(k));
}

std::string NativeCall::str(size_t k) const {
    auto* st = s(k);
    return st ? st->toUtf8() : std::string();
}

NativeRegistry& NativeRegistry::instance() {
    static NativeRegistry inst;
    return inst;
}

void NativeRegistry::add(const std::string& cls, const std::string& name,
                         const std::string& proto, NativeFn fn) {
    table_[Key{cls, name, proto}] = std::move(fn);
}

void NativeRegistry::addMulti(const std::vector<std::string>& classes,
                              const std::string& name, const std::string& proto,
                              NativeFn fn) {
    for (const auto& c : classes) table_[Key{c, name, proto}] = fn;
}

const NativeFn* NativeRegistry::find(const std::string& cls, const std::string& name,
                                     const std::string& proto) const {
    auto it = table_.find(Key{cls, name, proto});
    return it == table_.end() ? nullptr : &it->second;
}

void NativeRegistry::setStaticInt(const std::string& cls, const std::string& field, int64_t v) {
    staticInts_[cls + "->" + field] = v;
}

bool NativeRegistry::getStaticInt(const std::string& cls, const std::string& field, int64_t& out) const {
    auto it = staticInts_.find(cls + "->" + field);
    if (it == staticInts_.end()) return false;
    out = it->second;
    return true;
}

void NativeRegistry::setStaticString(const std::string& cls, const std::string& field, const std::string& v) {
    staticStrings_[cls + "->" + field] = v;
}

bool NativeRegistry::getStaticString(const std::string& cls, const std::string& field, std::string& out) const {
    auto it = staticStrings_.find(cls + "->" + field);
    if (it == staticStrings_.end()) return false;
    out = it->second;
    return true;
}

void NativeRegistry::setEnumOrder(const std::string& cls, const std::vector<std::string>& names) {
    enumOrders_[cls] = names;
}

int32_t NativeRegistry::enumOrdinal(const std::string& cls, const std::string& name) {
    auto& v = enumOrders_[cls];
    for (size_t i = 0; i < v.size(); ++i) if (v[i] == name) return static_cast<int32_t>(i);
    v.push_back(name);
    return static_cast<int32_t>(v.size() - 1);
}

void NativeRegistry::declareClass(const std::string& cls, const std::string& super,
                                  const std::vector<std::string>& interfaces, bool isInterface) {
    ClassDecl d;
    d.super = super;
    d.interfaces = interfaces;
    d.isInterface = isInterface;
    decls_[cls] = std::move(d);
}

const NativeRegistry::ClassDecl* NativeRegistry::findDecl(const std::string& cls) const {
    auto it = decls_.find(cls);
    return it == decls_.end() ? nullptr : &it->second;
}

void NativeRegistry::logMiss(const std::string& line) {
    static std::mutex m;
    static std::set<std::string> seen;
    std::lock_guard<std::mutex> lock(m);
    if (!seen.insert(line).second) return;
    std::ofstream f(logDir_ + "/native_bridge_missing.log", std::ios::app);
    if (f) f << line << "\n";
}

} // namespace NativeDex::Runtime::BytecodeInterpreter
