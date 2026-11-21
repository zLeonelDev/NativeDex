#pragma once

// Shared helpers for the portable native class library (java.lang / java.util).

#include <algorithm>
#include <atomic>
#include <functional>
#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>
#include "Vm.h"

namespace NativeDex::Runtime::BytecodeInterpreter {

using NC = NativeCall;

// ---- payloads ------------------------------------------------------------
struct ListData : ObjectModel::Object::NativeData {
    std::vector<Object*> v;
};

struct MapData : ObjectModel::Object::NativeData {
    struct Entry { Object* k; Object* v; int32_t h; bool live; };
    std::vector<Entry> entries;
    std::unordered_multimap<int32_t, size_t> index;
    size_t liveCount = 0;
    bool isSet = false;
};

struct SbData : ObjectModel::Object::NativeData {
    std::u16string s;
};

struct IterData : ObjectModel::Object::NativeData {
    std::vector<Object*> items;
    size_t pos = 0;
    size_t removed = 0;
    Object* source = nullptr;    // for remove()
};

struct EntryData : ObjectModel::Object::NativeData {
    Object* k = nullptr;
    Object* v = nullptr;
    Object* owner = nullptr;
};

struct RefData : ObjectModel::Object::NativeData {
    Object* v = nullptr;
};

struct ThreadData : ObjectModel::Object::NativeData {
    Object* target = nullptr;
    std::string name = "Thread";
    std::atomic<bool> started{false};
    std::atomic<bool> finished{false};
    int64_t id = 0;
};

// ---- string helpers -----------------------------------------------------------
std::string u16ToUtf8(const std::u16string& s);
std::string javaDoubleToString(double d);
std::string javaFloatToString(float f);
std::string javaFormat(Vm& vm, const std::string& fmt, ArrayObject* args);

inline StringObject* selfString(NC& c) {
    auto* s = dynamic_cast<StringObject*>(c.self);
    if (!s) c.vm.throwNpe("String receiver");
    return s;
}

// ---- boxed numbers ----------------------------------------------------------------
enum class BoxKind { None, Bool, Byte, Char, Short, Int, Long, Float, Double };
BoxKind boxKindOf(Object* o);
std::string boxToString(Object* o);
int32_t boxHash(Object* o);

// ---- collections ----------------------------------------------------------------------
ListData* listOf(Vm& vm, Object* o, bool create = true);
MapData* mapOf(Vm& vm, Object* o, bool create = true);
// Calls fn(item) for each element of any collection object (list/set/snapshot).
// Returns false if `o` is not a recognizable collection.
bool forEachItem(Vm& vm, Object* o, const std::function<void(Object*)>& fn);
std::vector<Object*> itemsOf(Vm& vm, Object* o);
Object* mapGet(Vm& vm, MapData* m, Object* key, bool* found = nullptr);
Object* mapPut(Vm& vm, MapData* m, Object* key, Object* val, bool* existed = nullptr);
bool mapRemove(Vm& vm, MapData* m, Object* key, Object** oldVal = nullptr);
void mapClear(MapData* m);
Object* newArrayList(Vm& vm, const std::vector<Object*>& items);

Object* frameworkAppContext();
void frameworkSetAppContext(Object* ctx);
void frameworkMarkMainThread(Vm& vm);

void registerLangNatives();
void registerUtilNatives();

} // namespace NativeDex::Runtime::BytecodeInterpreter
