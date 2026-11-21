// Portable java.util / java.util.concurrent native class library.

#include "NativeHelpers.h"

#include <chrono>
#include <climits>
#include <iostream>
#include <thread>

namespace NativeDex::Runtime::BytecodeInterpreter {

namespace {
NativeRegistry& REG() { return NativeRegistry::instance(); }
void A(const char* cls, const char* name, const char* proto, NativeFn fn) { REG().add(cls, name, proto, std::move(fn)); }
void M(const std::vector<std::string>& cls, const char* name, const char* proto, NativeFn fn) { REG().addMulti(cls, name, proto, std::move(fn)); }
void decl(const char* cls, const char* super, std::vector<std::string> ifs = {}) { REG().declareClass(cls, super, ifs, false); }
void declI(const char* cls, std::vector<std::string> ifs = {}) { REG().declareClass(cls, "Ljava/lang/Object;", ifs, true); }

constexpr size_t NPOS = static_cast<size_t>(-1);

size_t mapFind(Vm& vm, MapData* m, Object* key, int32_t h) {
    auto range = m->index.equal_range(h);
    for (auto it = range.first; it != range.second; ++it) {
        const auto& e = m->entries[it->second];
        if (e.live && (e.k == key || vm.javaEquals(e.k, key))) return it->second;
    }
    return NPOS;
}

void compactIfNeeded(MapData* m) {
    if (m->entries.size() < 64 || m->liveCount * 2 > m->entries.size()) return;
    std::vector<MapData::Entry> fresh;
    fresh.reserve(m->liveCount);
    for (auto& e : m->entries) if (e.live) fresh.push_back(e);
    m->entries.swap(fresh);
    m->index.clear();
    for (size_t i = 0; i < m->entries.size(); ++i) m->index.emplace(m->entries[i].h, i);
}

// Uniform view over list-backed and set-backed collections.
struct Coll {
    Vm& vm;
    ListData* l = nullptr;
    MapData* m = nullptr;
    Coll(Vm& v, Object* o) : vm(v) {
        if (!o) vm.throwNpe("collection");
        if (!o->native) {
            // Lazily initialised receiver: treat as an (empty) list.
            o->native = std::make_unique<ListData>();
        }
        l = dynamic_cast<ListData*>(o->native.get());
        m = l ? nullptr : dynamic_cast<MapData*>(o->native.get());
    }
    bool valid() const { return l || m; }
    size_t size() const { return l ? l->v.size() : (m ? m->liveCount : 0); }
    std::vector<Object*> items() const {
        std::vector<Object*> r;
        if (l) r = l->v;
        else if (m) for (auto& e : m->entries) if (e.live) r.push_back(e.k);
        return r;
    }
    bool contains(Object* o) const {
        if (l) { for (Object* x : l->v) if (x == o || vm.javaEquals(o, x)) return true; return false; }
        if (m) return mapFind(vm, m, o, vm.javaHashCode(o)) != NPOS;
        return false;
    }
    bool add(Object* o) {
        if (l) { l->v.push_back(o); return true; }
        if (m) { bool ex = false; mapPut(vm, m, o, nullptr, &ex); return !ex; }
        return false;
    }
    bool remove(Object* o) {
        if (l) {
            for (size_t i = 0; i < l->v.size(); ++i)
                if (l->v[i] == o || vm.javaEquals(o, l->v[i])) { l->v.erase(l->v.begin() + i); return true; }
            return false;
        }
        if (m) return mapRemove(vm, m, o);
        return false;
    }
    void clear() { if (l) l->v.clear(); else if (m) mapClear(m); }
};

int compareObjects(Vm& vm, Object* a, Object* b, Object* cmp) {
    if (cmp) return vm.callVirtual(cmp, "compare", "(Ljava/lang/Object;Ljava/lang/Object;)I", {Value::fromRef(a), Value::fromRef(b)}).i32();
    if (!a || !b) vm.throwNpe("compare");
    if (auto* sa = dynamic_cast<StringObject*>(a)) {
        auto* sb = dynamic_cast<StringObject*>(b);
        if (sb) return sa->compareTo(sb);
    }
    BoxKind ka = boxKindOf(a);
    if (ka != BoxKind::None && ka == boxKindOf(b)) {
        if (ka == BoxKind::Float || ka == BoxKind::Double) {
            double x, y;
            if (ka == BoxKind::Double) { std::memcpy(&x, &a->nativeHandle, 8); std::memcpy(&y, &b->nativeHandle, 8); }
            else { uint32_t p = static_cast<uint32_t>(a->nativeHandle), q = static_cast<uint32_t>(b->nativeHandle); float f1, f2; std::memcpy(&f1, &p, 4); std::memcpy(&f2, &q, 4); x = f1; y = f2; }
            return x < y ? -1 : (x > y ? 1 : 0);
        }
        int64_t x = ka == BoxKind::Long ? static_cast<int64_t>(a->nativeHandle) : static_cast<int32_t>(a->nativeHandle);
        int64_t y = ka == BoxKind::Long ? static_cast<int64_t>(b->nativeHandle) : static_cast<int32_t>(b->nativeHandle);
        return x < y ? -1 : (x > y ? 1 : 0);
    }
    return vm.callVirtual(a, "compareTo", "(Ljava/lang/Object;)I", {Value::fromRef(b)}).i32();
}

void sortItems(Vm& vm, std::vector<Object*>& v, Object* cmp) {
    std::stable_sort(v.begin(), v.end(), [&](Object* a, Object* b) { return compareObjects(vm, a, b, cmp) < 0; });
}

size_t elemSize(ArrayObject* a) {
    switch (a->getElementType()) {
        case ArrayObject::ArrayType::BYTE: case ArrayObject::ArrayType::BOOLEAN: return 1;
        case ArrayObject::ArrayType::CHAR: case ArrayObject::ArrayType::SHORT: return 2;
        case ArrayObject::ArrayType::INT: case ArrayObject::ArrayType::FLOAT: return 4;
        case ArrayObject::ArrayType::OBJECT: return sizeof(void*);
        default: return 8;
    }
}

Object* newIterator(Vm& vm, std::vector<Object*> items, Object* source) {
    Object* it = vm.newInstance("Ljava/util/ArrayList$Itr;");
    auto* d = new IterData();
    d->items = std::move(items);
    d->source = source;
    it->native.reset(d);
    return it;
}

Object* newSetFrom(Vm& vm, const char* cls, const std::vector<Object*>& items) {
    Object* o = vm.newInstance(cls);
    auto* m = new MapData();
    m->isSet = true;
    o->native.reset(m);
    for (Object* i : items) mapPut(vm, m, i, nullptr);
    return o;
}

Object* newEntry(Vm& vm, Object* k, Object* v, Object* owner) {
    Object* e = vm.newInstance("Ljava/util/HashMap$Node;");
    auto* d = new EntryData();
    d->k = k; d->v = v; d->owner = owner;
    e->native.reset(d);
    return e;
}

} // namespace

// ---------------------------------------------------------------------------
// Exported helpers
// ---------------------------------------------------------------------------
ListData* listOf(Vm& vm, Object* o, bool create) {
    if (!o) vm.throwNpe("list");
    if (!o->native && create) o->native = std::make_unique<ListData>();
    return dynamic_cast<ListData*>(o->native.get());
}

MapData* mapOf(Vm& vm, Object* o, bool create) {
    if (!o) vm.throwNpe("map");
    if (!o->native && create) o->native = std::make_unique<MapData>();
    return dynamic_cast<MapData*>(o->native.get());
}

Object* mapGet(Vm& vm, MapData* m, Object* key, bool* found) {
    size_t i = mapFind(vm, m, key, vm.javaHashCode(key));
    if (found) *found = i != NPOS;
    return i == NPOS ? nullptr : m->entries[i].v;
}

Object* mapPut(Vm& vm, MapData* m, Object* key, Object* val, bool* existed) {
    int32_t h = vm.javaHashCode(key);
    size_t i = mapFind(vm, m, key, h);
    if (existed) *existed = i != NPOS;
    if (i != NPOS) { Object* old = m->entries[i].v; m->entries[i].v = val; return old; }
    m->entries.push_back({key, val, h, true});
    m->index.emplace(h, m->entries.size() - 1);
    m->liveCount++;
    return nullptr;
}

bool mapRemove(Vm& vm, MapData* m, Object* key, Object** oldVal) {
    int32_t h = vm.javaHashCode(key);
    auto range = m->index.equal_range(h);
    for (auto it = range.first; it != range.second; ++it) {
        auto& e = m->entries[it->second];
        if (e.live && (e.k == key || vm.javaEquals(e.k, key))) {
            if (oldVal) *oldVal = e.v;
            e.live = false;
            m->index.erase(it);
            m->liveCount--;
            compactIfNeeded(m);
            return true;
        }
    }
    return false;
}

void mapClear(MapData* m) {
    m->entries.clear();
    m->index.clear();
    m->liveCount = 0;
}

Object* newArrayList(Vm& vm, const std::vector<Object*>& items) {
    Object* o = vm.newInstance("Ljava/util/ArrayList;");
    auto* d = new ListData();
    d->v = items;
    o->native.reset(d);
    return o;
}

bool forEachItem(Vm& vm, Object* o, const std::function<void(Object*)>& fn) {
    if (!o) return false;
    if (auto* a = dynamic_cast<ArrayObject*>(o)) {
        if (a->isObjectArray()) { for (size_t i = 0; i < a->length(); ++i) fn(a->getObject(i)); return true; }
        return false;
    }
    if (o->native) {
        if (auto* l = dynamic_cast<ListData*>(o->native.get())) { auto copy = l->v; for (Object* x : copy) fn(x); return true; }
        if (auto* m = dynamic_cast<MapData*>(o->native.get())) {
            std::vector<Object*> ks;
            for (auto& e : m->entries) if (e.live) ks.push_back(e.k);
            for (Object* x : ks) fn(x);
            return true;
        }
    }
    // A collection implemented in DEX code: walk its iterator().
    RuntimeClass* k = vm.classOf(o);
    Target t = vm.resolveVirtual(k, "iterator()Ljava/util/Iterator;", "iterator", "()Ljava/util/Iterator;");
    if (!t.valid()) return false;
    Object* it = vm.invokeTarget(t, nullptr, o, {}).ref;
    if (!it) return false;
    while (vm.callVirtual(it, "hasNext", "()Z").i32()) fn(vm.callVirtual(it, "next", "()Ljava/lang/Object;").ref);
    return true;
}

std::vector<Object*> itemsOf(Vm& vm, Object* o) {
    std::vector<Object*> r;
    forEachItem(vm, o, [&](Object* x) { r.push_back(x); });
    return r;
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------
void registerUtilNatives() {
    // ---- hierarchy ---------------------------------------------------------------
    declI("Ljava/util/Iterator;");
    declI("Ljava/util/ListIterator;", {"Ljava/util/Iterator;"});
    declI("Ljava/util/Collection;", {"Ljava/lang/Iterable;"});
    declI("Ljava/util/List;", {"Ljava/util/Collection;"});
    declI("Ljava/util/Set;", {"Ljava/util/Collection;"});
    declI("Ljava/util/SortedSet;", {"Ljava/util/Set;"});
    declI("Ljava/util/Queue;", {"Ljava/util/Collection;"});
    declI("Ljava/util/Deque;", {"Ljava/util/Queue;"});
    declI("Ljava/util/Map;");
    declI("Ljava/util/SortedMap;", {"Ljava/util/Map;"});
    declI("Ljava/util/Map$Entry;");
    declI("Ljava/util/Comparator;");
    declI("Ljava/util/RandomAccess;");
    declI("Ljava/util/concurrent/ConcurrentMap;", {"Ljava/util/Map;"});
    declI("Ljava/util/concurrent/Executor;");
    declI("Ljava/util/concurrent/ExecutorService;", {"Ljava/util/concurrent/Executor;"});
    declI("Ljava/util/concurrent/ScheduledExecutorService;", {"Ljava/util/concurrent/ExecutorService;"});
    declI("Ljava/util/concurrent/Future;");

    decl("Ljava/util/AbstractCollection;", "Ljava/lang/Object;", {"Ljava/util/Collection;"});
    decl("Ljava/util/AbstractList;", "Ljava/util/AbstractCollection;", {"Ljava/util/List;"});
    decl("Ljava/util/ArrayList;", "Ljava/util/AbstractList;", {"Ljava/util/List;", "Ljava/util/RandomAccess;", "Ljava/lang/Cloneable;", "Ljava/io/Serializable;"});
    decl("Ljava/util/ArrayList$Itr;", "Ljava/lang/Object;", {"Ljava/util/Iterator;", "Ljava/util/ListIterator;"});
    decl("Ljava/util/LinkedList;", "Ljava/util/AbstractList;", {"Ljava/util/List;", "Ljava/util/Deque;"});
    decl("Ljava/util/Vector;", "Ljava/util/AbstractList;", {"Ljava/util/List;", "Ljava/util/RandomAccess;"});
    decl("Ljava/util/Stack;", "Ljava/util/Vector;");
    decl("Ljava/util/ArrayDeque;", "Ljava/util/AbstractCollection;", {"Ljava/util/Deque;"});
    decl("Ljava/util/concurrent/CopyOnWriteArrayList;", "Ljava/lang/Object;", {"Ljava/util/List;", "Ljava/util/RandomAccess;"});
    decl("Ljava/util/concurrent/ConcurrentLinkedQueue;", "Ljava/util/AbstractCollection;", {"Ljava/util/Queue;"});
    decl("Ljava/util/concurrent/LinkedBlockingQueue;", "Ljava/util/AbstractCollection;", {"Ljava/util/Queue;"});
    decl("Ljava/util/concurrent/ConcurrentLinkedDeque;", "Ljava/util/AbstractCollection;", {"Ljava/util/Deque;"});
    decl("Ljava/util/AbstractSet;", "Ljava/util/AbstractCollection;", {"Ljava/util/Set;"});
    decl("Ljava/util/HashSet;", "Ljava/util/AbstractSet;", {"Ljava/util/Set;", "Ljava/lang/Cloneable;"});
    decl("Ljava/util/LinkedHashSet;", "Ljava/util/HashSet;");
    decl("Ljava/util/TreeSet;", "Ljava/util/AbstractSet;", {"Ljava/util/SortedSet;"});
    decl("Ljava/util/concurrent/CopyOnWriteArraySet;", "Ljava/util/AbstractSet;", {"Ljava/util/Set;"});
    decl("Ljava/util/concurrent/ConcurrentSkipListSet;", "Ljava/util/AbstractSet;", {"Ljava/util/Set;"});
    decl("Ljava/util/HashMap$KeySet;", "Ljava/util/AbstractSet;");
    decl("Ljava/util/HashMap$EntrySet;", "Ljava/util/AbstractSet;");
    decl("Ljava/util/HashMap$Values;", "Ljava/util/AbstractCollection;");
    decl("Ljava/util/HashMap$Node;", "Ljava/lang/Object;", {"Ljava/util/Map$Entry;"});
    decl("Ljava/util/AbstractMap$SimpleEntry;", "Ljava/lang/Object;", {"Ljava/util/Map$Entry;"});
    decl("Ljava/util/AbstractMap;", "Ljava/lang/Object;", {"Ljava/util/Map;"});
    decl("Ljava/util/HashMap;", "Ljava/util/AbstractMap;", {"Ljava/util/Map;", "Ljava/lang/Cloneable;"});
    decl("Ljava/util/LinkedHashMap;", "Ljava/util/HashMap;");
    decl("Ljava/util/TreeMap;", "Ljava/util/AbstractMap;", {"Ljava/util/SortedMap;"});
    decl("Ljava/util/Hashtable;", "Ljava/lang/Object;", {"Ljava/util/Map;"});
    decl("Ljava/util/IdentityHashMap;", "Ljava/util/AbstractMap;");
    decl("Ljava/util/WeakHashMap;", "Ljava/util/AbstractMap;");
    decl("Ljava/util/concurrent/ConcurrentHashMap;", "Ljava/util/AbstractMap;", {"Ljava/util/concurrent/ConcurrentMap;"});
    decl("Landroid/util/ArrayMap;", "Ljava/lang/Object;", {"Ljava/util/Map;"});
    decl("Ljava/util/Random;", "Ljava/lang/Object;");
    decl("Ljava/util/concurrent/atomic/AtomicReferenceArray;", "Ljava/lang/Object;");

    const std::vector<std::string> LISTS = {"Ljava/util/ArrayList;", "Ljava/util/LinkedList;", "Ljava/util/Vector;", "Ljava/util/Stack;",
        "Ljava/util/ArrayDeque;", "Ljava/util/concurrent/CopyOnWriteArrayList;", "Ljava/util/concurrent/ConcurrentLinkedQueue;",
        "Ljava/util/concurrent/LinkedBlockingQueue;", "Ljava/util/concurrent/ConcurrentLinkedDeque;"};
    const std::vector<std::string> SETS = {"Ljava/util/HashSet;", "Ljava/util/LinkedHashSet;", "Ljava/util/TreeSet;",
        "Ljava/util/concurrent/CopyOnWriteArraySet;", "Ljava/util/concurrent/ConcurrentSkipListSet;"};
    const std::vector<std::string> MAPS = {"Ljava/util/HashMap;", "Ljava/util/LinkedHashMap;", "Ljava/util/TreeMap;", "Ljava/util/Hashtable;",
        "Ljava/util/IdentityHashMap;", "Ljava/util/WeakHashMap;", "Ljava/util/concurrent/ConcurrentHashMap;", "Landroid/util/ArrayMap;"};

    // ---- constructors ---------------------------------------------------------------
    M(LISTS, "<init>", "()V", [](NC& c) { listOf(c.vm, c.self); });
    M(LISTS, "<init>", "(I)V", [](NC& c) { listOf(c.vm, c.self); });
    M(LISTS, "<init>", "(Ljava/util/Collection;)V", [](NC& c) { listOf(c.vm, c.self)->v = itemsOf(c.vm, c.o(0)); });
    M(SETS, "<init>", "()V", [](NC& c) { auto* m = mapOf(c.vm, c.self); m->isSet = true; });
    M(SETS, "<init>", "(I)V", [](NC& c) { auto* m = mapOf(c.vm, c.self); m->isSet = true; });
    M(SETS, "<init>", "(IF)V", [](NC& c) { auto* m = mapOf(c.vm, c.self); m->isSet = true; });
    M(SETS, "<init>", "(Ljava/util/Collection;)V", [](NC& c) {
        auto* m = mapOf(c.vm, c.self); m->isSet = true;
        for (Object* x : itemsOf(c.vm, c.o(0))) mapPut(c.vm, m, x, nullptr);
    });
    M(MAPS, "<init>", "()V", [](NC& c) { mapOf(c.vm, c.self); });
    M(MAPS, "<init>", "(I)V", [](NC& c) { mapOf(c.vm, c.self); });
    M(MAPS, "<init>", "(IF)V", [](NC& c) { mapOf(c.vm, c.self); });
    M(MAPS, "<init>", "(IFZ)V", [](NC& c) { mapOf(c.vm, c.self); });
    M(MAPS, "<init>", "(Ljava/util/Map;)V", [](NC& c) {
        auto* m = mapOf(c.vm, c.self);
        if (auto* src = c.o(0) ? dynamic_cast<MapData*>(c.o(0)->native.get()) : nullptr)
            for (auto& e : std::vector<MapData::Entry>(src->entries)) if (e.live) mapPut(c.vm, m, e.k, e.v);
    });
    A("Ljava/util/AbstractMap$SimpleEntry;", "<init>", "(Ljava/lang/Object;Ljava/lang/Object;)V", [](NC& c) {
        auto* d = new EntryData(); d->k = c.o(0); d->v = c.o(1); c.self->native.reset(d);
    });

    // ---- Collection (lists and sets) ------------------------------------------------------
    const char* COL = "Ljava/util/Collection;";
    A(COL, "size", "()I", [](NC& c) { c.retInt(static_cast<int32_t>(Coll(c.vm, c.self).size())); });
    A(COL, "isEmpty", "()Z", [](NC& c) { c.retBool(Coll(c.vm, c.self).size() == 0); });
    A(COL, "contains", "(Ljava/lang/Object;)Z", [](NC& c) { c.retBool(Coll(c.vm, c.self).contains(c.o(0))); });
    A(COL, "add", "(Ljava/lang/Object;)Z", [](NC& c) { c.retBool(Coll(c.vm, c.self).add(c.o(0))); });
    A(COL, "remove", "(Ljava/lang/Object;)Z", [](NC& c) { c.retBool(Coll(c.vm, c.self).remove(c.o(0))); });
    A(COL, "clear", "()V", [](NC& c) { Coll(c.vm, c.self).clear(); });
    A(COL, "addAll", "(Ljava/util/Collection;)Z", [](NC& c) {
        Coll col(c.vm, c.self);
        bool ch = false;
        for (Object* x : itemsOf(c.vm, c.o(0))) ch |= col.add(x);
        c.retBool(ch);
    });
    A(COL, "removeAll", "(Ljava/util/Collection;)Z", [](NC& c) {
        Coll col(c.vm, c.self); bool ch = false;
        for (Object* x : itemsOf(c.vm, c.o(0))) while (col.remove(x)) ch = true;
        c.retBool(ch);
    });
    A(COL, "retainAll", "(Ljava/util/Collection;)Z", [](NC& c) {
        Coll col(c.vm, c.self); Coll other(c.vm, c.o(0)); bool ch = false;
        for (Object* x : col.items()) if (!other.contains(x)) { col.remove(x); ch = true; }
        c.retBool(ch);
    });
    A(COL, "containsAll", "(Ljava/util/Collection;)Z", [](NC& c) {
        Coll col(c.vm, c.self);
        for (Object* x : itemsOf(c.vm, c.o(0))) if (!col.contains(x)) { c.retBool(false); return; }
        c.retBool(true);
    });
    A(COL, "iterator", "()Ljava/util/Iterator;", [](NC& c) { c.retObj(newIterator(c.vm, Coll(c.vm, c.self).items(), c.self)); });
    A(COL, "toArray", "()[Ljava/lang/Object;", [](NC& c) {
        auto items = Coll(c.vm, c.self).items();
        ArrayObject* a = c.vm.newArray("[Ljava/lang/Object;", static_cast<int32_t>(items.size()));
        for (size_t i = 0; i < items.size(); ++i) a->setObject(i, items[i]);
        c.retObj(a);
    });
    A(COL, "toArray", "([Ljava/lang/Object;)[Ljava/lang/Object;", [](NC& c) {
        auto items = Coll(c.vm, c.self).items();
        auto* given = dynamic_cast<ArrayObject*>(c.o(0));
        ArrayObject* a = given && given->length() >= items.size() ? given
            : c.vm.newArray(given && given->klass ? given->klass->getDescriptor() : "[Ljava/lang/Object;", static_cast<int32_t>(items.size()));
        for (size_t i = 0; i < items.size(); ++i) a->setObject(i, items[i]);
        if (a->length() > items.size()) a->setObject(items.size(), nullptr);
        c.retObj(a);
    });
    A("Ljava/lang/Iterable;", "forEach", "(Ljava/util/function/Consumer;)V", [](NC& c) {
        for (Object* x : itemsOf(c.vm, c.self)) c.vm.callVirtual(c.o(0), "accept", "(Ljava/lang/Object;)V", {Value::fromRef(x)});
    });
    A(COL, "removeIf", "(Ljava/util/function/Predicate;)Z", [](NC& c) {
        Coll col(c.vm, c.self); bool ch = false;
        for (Object* x : col.items())
            if (c.vm.callVirtual(c.o(0), "test", "(Ljava/lang/Object;)Z", {Value::fromRef(x)}).i32()) { col.remove(x); ch = true; }
        c.retBool(ch);
    });
    A("Ljava/util/AbstractCollection;", "toString", "()Ljava/lang/String;", [](NC& c) {
        std::string s = "[";
        bool first = true;
        for (Object* x : Coll(c.vm, c.self).items()) { if (!first) s += ", "; first = false; s += x == c.self ? "(this Collection)" : c.vm.javaToString(x); }
        c.retObj(c.vm.newString(s + "]"));
    });
    A("Ljava/util/AbstractList;", "equals", "(Ljava/lang/Object;)Z", [](NC& c) {
        if (c.self == c.o(0)) { c.retBool(true); return; }
        if (!c.o(0)) { c.retBool(false); return; }
        auto a = Coll(c.vm, c.self).items(); auto b = itemsOf(c.vm, c.o(0));
        if (a.size() != b.size()) { c.retBool(false); return; }
        for (size_t i = 0; i < a.size(); ++i) if (!c.vm.javaEquals(a[i], b[i])) { c.retBool(false); return; }
        c.retBool(true);
    });
    A("Ljava/util/AbstractList;", "hashCode", "()I", [](NC& c) {
        uint32_t h = 1;
        for (Object* x : Coll(c.vm, c.self).items()) h = 31u * h + static_cast<uint32_t>(c.vm.javaHashCode(x));
        c.retInt(static_cast<int32_t>(h));
    });
    A("Ljava/util/AbstractSet;", "equals", "(Ljava/lang/Object;)Z", [](NC& c) {
        if (c.self == c.o(0)) { c.retBool(true); return; }
        if (!c.o(0)) { c.retBool(false); return; }
        Coll a(c.vm, c.self); auto b = itemsOf(c.vm, c.o(0));
        if (a.size() != b.size()) { c.retBool(false); return; }
        for (Object* x : b) if (!a.contains(x)) { c.retBool(false); return; }
        c.retBool(true);
    });
    A("Ljava/util/AbstractSet;", "hashCode", "()I", [](NC& c) {
        uint32_t h = 0;
        for (Object* x : Coll(c.vm, c.self).items()) h += static_cast<uint32_t>(c.vm.javaHashCode(x));
        c.retInt(static_cast<int32_t>(h));
    });
    A("Ljava/util/concurrent/CopyOnWriteArrayList;", "toString", "()Ljava/lang/String;", [](NC& c) {
        std::string s = "["; bool first = true;
        for (Object* x : Coll(c.vm, c.self).items()) { if (!first) s += ", "; first = false; s += c.vm.javaToString(x); }
        c.retObj(c.vm.newString(s + "]"));
    });

    // ---- List ---------------------------------------------------------------------------------
    const char* LST = "Ljava/util/List;";
    auto idxCheck = [](NC& c, ListData* l, int32_t i, bool inclusive) {
        size_t lim = l->v.size() + (inclusive ? 1 : 0);
        if (i < 0 || static_cast<size_t>(i) >= lim)
            c.vm.throwNew("Ljava/lang/IndexOutOfBoundsException;", "Index " + std::to_string(i) + " out of bounds for length " + std::to_string(l->v.size()));
    };
    A(LST, "get", "(I)Ljava/lang/Object;", [idxCheck](NC& c) { auto* l = listOf(c.vm, c.self); idxCheck(c, l, c.i(0), false); c.retObj(l->v[c.i(0)]); });
    A(LST, "set", "(ILjava/lang/Object;)Ljava/lang/Object;", [idxCheck](NC& c) { auto* l = listOf(c.vm, c.self); idxCheck(c, l, c.i(0), false); Object* o = l->v[c.i(0)]; l->v[c.i(0)] = c.o(1); c.retObj(o); });
    A(LST, "add", "(ILjava/lang/Object;)V", [idxCheck](NC& c) { auto* l = listOf(c.vm, c.self); idxCheck(c, l, c.i(0), true); l->v.insert(l->v.begin() + c.i(0), c.o(1)); });
    A(LST, "remove", "(I)Ljava/lang/Object;", [idxCheck](NC& c) { auto* l = listOf(c.vm, c.self); idxCheck(c, l, c.i(0), false); Object* o = l->v[c.i(0)]; l->v.erase(l->v.begin() + c.i(0)); c.retObj(o); });
    A(LST, "indexOf", "(Ljava/lang/Object;)I", [](NC& c) {
        auto* l = listOf(c.vm, c.self);
        for (size_t i = 0; i < l->v.size(); ++i) if (l->v[i] == c.o(0) || c.vm.javaEquals(c.o(0), l->v[i])) { c.retInt(static_cast<int32_t>(i)); return; }
        c.retInt(-1);
    });
    A(LST, "lastIndexOf", "(Ljava/lang/Object;)I", [](NC& c) {
        auto* l = listOf(c.vm, c.self);
        for (size_t i = l->v.size(); i-- > 0;) if (l->v[i] == c.o(0) || c.vm.javaEquals(c.o(0), l->v[i])) { c.retInt(static_cast<int32_t>(i)); return; }
        c.retInt(-1);
    });
    A(LST, "addAll", "(ILjava/util/Collection;)Z", [idxCheck](NC& c) {
        auto* l = listOf(c.vm, c.self); idxCheck(c, l, c.i(0), true);
        auto items = itemsOf(c.vm, c.o(1));
        l->v.insert(l->v.begin() + c.i(0), items.begin(), items.end());
        c.retBool(!items.empty());
    });
    A(LST, "subList", "(II)Ljava/util/List;", [](NC& c) {
        auto* l = listOf(c.vm, c.self); int32_t b = c.i(0), e = c.i(1);
        if (b < 0 || e > static_cast<int32_t>(l->v.size()) || b > e) c.vm.throwNew("Ljava/lang/IndexOutOfBoundsException;", "fromIndex " + std::to_string(b) + " toIndex " + std::to_string(e));
        c.retObj(newArrayList(c.vm, std::vector<Object*>(l->v.begin() + b, l->v.begin() + e)));
    });
    A(LST, "sort", "(Ljava/util/Comparator;)V", [](NC& c) { auto* l = listOf(c.vm, c.self); auto copy = l->v; sortItems(c.vm, copy, c.o(0)); l->v = copy; });
    A(LST, "replaceAll", "(Ljava/util/function/UnaryOperator;)V", [](NC& c) {
        auto* l = listOf(c.vm, c.self);
        for (auto& x : l->v) x = c.vm.callVirtual(c.o(0), "apply", "(Ljava/lang/Object;)Ljava/lang/Object;", {Value::fromRef(x)}).ref;
    });
    A(LST, "listIterator", "()Ljava/util/ListIterator;", [](NC& c) { c.retObj(newIterator(c.vm, listOf(c.vm, c.self)->v, c.self)); });
    A("Ljava/util/Vector;", "addElement", "(Ljava/lang/Object;)V", [](NC& c) { listOf(c.vm, c.self)->v.push_back(c.o(0)); });
    A("Ljava/util/Vector;", "elementAt", "(I)Ljava/lang/Object;", [idxCheck](NC& c) { auto* l = listOf(c.vm, c.self); idxCheck(c, l, c.i(0), false); c.retObj(l->v[c.i(0)]); });
    A("Ljava/util/Vector;", "firstElement", "()Ljava/lang/Object;", [](NC& c) { auto* l = listOf(c.vm, c.self); if (l->v.empty()) c.vm.throwNew("Ljava/util/NoSuchElementException;"); c.retObj(l->v.front()); });
    A("Ljava/util/Vector;", "lastElement", "()Ljava/lang/Object;", [](NC& c) { auto* l = listOf(c.vm, c.self); if (l->v.empty()) c.vm.throwNew("Ljava/util/NoSuchElementException;"); c.retObj(l->v.back()); });
    A("Ljava/util/ArrayList;", "ensureCapacity", "(I)V", [](NC&) {});
    A("Ljava/util/ArrayList;", "trimToSize", "()V", [](NC&) {});
    A("Ljava/util/ArrayList;", "clone", "()Ljava/lang/Object;", [](NC& c) { c.retObj(newArrayList(c.vm, listOf(c.vm, c.self)->v)); });

    // ---- Queue / Deque / Stack ---------------------------------------------------------------------
    auto noElem = [](NC& c) { c.vm.throwNew("Ljava/util/NoSuchElementException;"); };
    const std::vector<std::string> QD = {"Ljava/util/Queue;", "Ljava/util/Deque;"};
    M(QD, "offer", "(Ljava/lang/Object;)Z", [](NC& c) { listOf(c.vm, c.self)->v.push_back(c.o(0)); c.retBool(true); });
    M(QD, "offerLast", "(Ljava/lang/Object;)Z", [](NC& c) { listOf(c.vm, c.self)->v.push_back(c.o(0)); c.retBool(true); });
    M(QD, "offerFirst", "(Ljava/lang/Object;)Z", [](NC& c) { auto* l = listOf(c.vm, c.self); l->v.insert(l->v.begin(), c.o(0)); c.retBool(true); });
    M(QD, "addFirst", "(Ljava/lang/Object;)V", [](NC& c) { auto* l = listOf(c.vm, c.self); l->v.insert(l->v.begin(), c.o(0)); });
    M(QD, "addLast", "(Ljava/lang/Object;)V", [](NC& c) { listOf(c.vm, c.self)->v.push_back(c.o(0)); });
    M(QD, "push", "(Ljava/lang/Object;)V", [](NC& c) { auto* l = listOf(c.vm, c.self); l->v.insert(l->v.begin(), c.o(0)); });
    for (const char* n : {"poll", "pollFirst"}) M(QD, n, "()Ljava/lang/Object;", [](NC& c) { auto* l = listOf(c.vm, c.self); if (l->v.empty()) { c.retObj(nullptr); return; } Object* o = l->v.front(); l->v.erase(l->v.begin()); c.retObj(o); });
    M(QD, "pollLast", "()Ljava/lang/Object;", [](NC& c) { auto* l = listOf(c.vm, c.self); if (l->v.empty()) { c.retObj(nullptr); return; } Object* o = l->v.back(); l->v.pop_back(); c.retObj(o); });
    for (const char* n : {"remove", "removeFirst", "pop", "element", "getFirst"}) {
        bool take = std::strcmp(n, "element") != 0 && std::strcmp(n, "getFirst") != 0;
        M(QD, n, "()Ljava/lang/Object;", [noElem, take](NC& c) { auto* l = listOf(c.vm, c.self); if (l->v.empty()) noElem(c); Object* o = l->v.front(); if (take) l->v.erase(l->v.begin()); c.retObj(o); });
    }
    M(QD, "removeLast", "()Ljava/lang/Object;", [noElem](NC& c) { auto* l = listOf(c.vm, c.self); if (l->v.empty()) noElem(c); Object* o = l->v.back(); l->v.pop_back(); c.retObj(o); });
    M(QD, "getLast", "()Ljava/lang/Object;", [noElem](NC& c) { auto* l = listOf(c.vm, c.self); if (l->v.empty()) noElem(c); c.retObj(l->v.back()); });
    for (const char* n : {"peek", "peekFirst"}) M(QD, n, "()Ljava/lang/Object;", [](NC& c) { auto* l = listOf(c.vm, c.self); c.retObj(l->v.empty() ? nullptr : l->v.front()); });
    M(QD, "peekLast", "()Ljava/lang/Object;", [](NC& c) { auto* l = listOf(c.vm, c.self); c.retObj(l->v.empty() ? nullptr : l->v.back()); });
    M(QD, "put", "(Ljava/lang/Object;)V", [](NC& c) { listOf(c.vm, c.self)->v.push_back(c.o(0)); });
    M({"Ljava/util/concurrent/LinkedBlockingQueue;"}, "put", "(Ljava/lang/Object;)V", [](NC& c) { listOf(c.vm, c.self)->v.push_back(c.o(0)); });
    M({"Ljava/util/concurrent/LinkedBlockingQueue;"}, "take", "()Ljava/lang/Object;", [](NC& c) {
        for (;;) {
            auto* l = listOf(c.vm, c.self);
            if (!l->v.empty()) { Object* o = l->v.front(); l->v.erase(l->v.begin()); c.retObj(o); return; }
            Vm::Unlocker u(c.vm); std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    });
    A("Ljava/util/Stack;", "push", "(Ljava/lang/Object;)Ljava/lang/Object;", [](NC& c) { listOf(c.vm, c.self)->v.push_back(c.o(0)); c.retObj(c.o(0)); });
    A("Ljava/util/Stack;", "pop", "()Ljava/lang/Object;", [](NC& c) { auto* l = listOf(c.vm, c.self); if (l->v.empty()) c.vm.throwNew("Ljava/util/EmptyStackException;"); Object* o = l->v.back(); l->v.pop_back(); c.retObj(o); });
    A("Ljava/util/Stack;", "peek", "()Ljava/lang/Object;", [](NC& c) { auto* l = listOf(c.vm, c.self); if (l->v.empty()) c.vm.throwNew("Ljava/util/EmptyStackException;"); c.retObj(l->v.back()); });
    A("Ljava/util/Stack;", "empty", "()Z", [](NC& c) { c.retBool(listOf(c.vm, c.self)->v.empty()); });
    decl("Ljava/util/EmptyStackException;", "Ljava/lang/RuntimeException;");

    // ---- Iterator ------------------------------------------------------------------------------------------
    auto itOf = [](NC& c) { return c.self->nativeAs<IterData>(); };
    A("Ljava/util/Iterator;", "hasNext", "()Z", [itOf](NC& c) { auto* d = itOf(c); c.retBool(d && d->pos < d->items.size()); });
    A("Ljava/util/Iterator;", "next", "()Ljava/lang/Object;", [itOf](NC& c) {
        auto* d = itOf(c);
        if (!d || d->pos >= d->items.size()) c.vm.throwNew("Ljava/util/NoSuchElementException;");
        c.retObj(d->items[d->pos++]);
    });
    A("Ljava/util/Iterator;", "remove", "()V", [itOf](NC& c) {
        auto* d = itOf(c);
        if (!d || d->pos == 0 || !d->source) c.vm.throwNew("Ljava/lang/IllegalStateException;");
        Object* last = d->items[d->pos - 1];
        if (auto* l = dynamic_cast<ListData*>(d->source->native.get())) {
            size_t idx = d->pos - 1 - d->removed;
            if (idx < l->v.size()) l->v.erase(l->v.begin() + idx);
            d->removed++;
        } else if (auto* m = dynamic_cast<MapData*>(d->source->native.get())) {
            mapRemove(c.vm, m, last);
        }
    });
    A("Ljava/util/ListIterator;", "hasPrevious", "()Z", [itOf](NC& c) { auto* d = itOf(c); c.retBool(d && d->pos > 0); });
    A("Ljava/util/Iterator;", "forEachRemaining", "(Ljava/util/function/Consumer;)V", [itOf](NC& c) {
        auto* d = itOf(c);
        while (d && d->pos < d->items.size()) c.vm.callVirtual(c.o(0), "accept", "(Ljava/lang/Object;)V", {Value::fromRef(d->items[d->pos++])});
    });

    // ---- Map -------------------------------------------------------------------------------------------------------
    const char* MP = "Ljava/util/Map;";
    A(MP, "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;", [](NC& c) { c.retObj(mapPut(c.vm, mapOf(c.vm, c.self), c.o(0), c.o(1))); });
    A(MP, "get", "(Ljava/lang/Object;)Ljava/lang/Object;", [](NC& c) { c.retObj(mapGet(c.vm, mapOf(c.vm, c.self), c.o(0))); });
    A(MP, "getOrDefault", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;", [](NC& c) { bool f; Object* v = mapGet(c.vm, mapOf(c.vm, c.self), c.o(0), &f); c.retObj(f ? v : c.o(1)); });
    A(MP, "containsKey", "(Ljava/lang/Object;)Z", [](NC& c) { bool f; mapGet(c.vm, mapOf(c.vm, c.self), c.o(0), &f); c.retBool(f); });
    A(MP, "containsValue", "(Ljava/lang/Object;)Z", [](NC& c) {
        for (auto& e : mapOf(c.vm, c.self)->entries) if (e.live && (e.v == c.o(0) || c.vm.javaEquals(c.o(0), e.v))) { c.retBool(true); return; }
        c.retBool(false);
    });
    A(MP, "remove", "(Ljava/lang/Object;)Ljava/lang/Object;", [](NC& c) { Object* old = nullptr; mapRemove(c.vm, mapOf(c.vm, c.self), c.o(0), &old); c.retObj(old); });
    A(MP, "size", "()I", [](NC& c) { c.retInt(static_cast<int32_t>(mapOf(c.vm, c.self)->liveCount)); });
    A(MP, "isEmpty", "()Z", [](NC& c) { c.retBool(mapOf(c.vm, c.self)->liveCount == 0); });
    A(MP, "clear", "()V", [](NC& c) { mapClear(mapOf(c.vm, c.self)); });
    A(MP, "putAll", "(Ljava/util/Map;)V", [](NC& c) {
        auto* m = mapOf(c.vm, c.self);
        auto* src = c.o(0) ? dynamic_cast<MapData*>(c.o(0)->native.get()) : nullptr;
        if (src) for (auto& e : std::vector<MapData::Entry>(src->entries)) if (e.live) mapPut(c.vm, m, e.k, e.v);
    });
    A(MP, "putIfAbsent", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;", [](NC& c) {
        auto* m = mapOf(c.vm, c.self); bool f; Object* v = mapGet(c.vm, m, c.o(0), &f);
        if (f && v) { c.retObj(v); return; }
        mapPut(c.vm, m, c.o(0), c.o(1)); c.retObj(nullptr);
    });
    A(MP, "computeIfAbsent", "(Ljava/lang/Object;Ljava/util/function/Function;)Ljava/lang/Object;", [](NC& c) {
        auto* m = mapOf(c.vm, c.self); bool f; Object* v = mapGet(c.vm, m, c.o(0), &f);
        if (f && v) { c.retObj(v); return; }
        Object* nv = c.vm.callVirtual(c.o(1), "apply", "(Ljava/lang/Object;)Ljava/lang/Object;", {Value::fromRef(c.o(0))}).ref;
        if (nv) mapPut(c.vm, m, c.o(0), nv);
        c.retObj(nv);
    });
    A(MP, "replace", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;", [](NC& c) {
        auto* m = mapOf(c.vm, c.self); bool f; Object* v = mapGet(c.vm, m, c.o(0), &f);
        if (f) mapPut(c.vm, m, c.o(0), c.o(1));
        c.retObj(f ? v : nullptr);
    });
    A(MP, "forEach", "(Ljava/util/function/BiConsumer;)V", [](NC& c) {
        auto* m = mapOf(c.vm, c.self);
        for (auto& e : std::vector<MapData::Entry>(m->entries))
            if (e.live) c.vm.callVirtual(c.o(0), "accept", "(Ljava/lang/Object;Ljava/lang/Object;)V", {Value::fromRef(e.k), Value::fromRef(e.v)});
    });
    A(MP, "keySet", "()Ljava/util/Set;", [](NC& c) {
        std::vector<Object*> ks; for (auto& e : mapOf(c.vm, c.self)->entries) if (e.live) ks.push_back(e.k);
        c.retObj(newSetFrom(c.vm, "Ljava/util/HashMap$KeySet;", ks));
    });
    A(MP, "values", "()Ljava/util/Collection;", [](NC& c) {
        std::vector<Object*> vs; for (auto& e : mapOf(c.vm, c.self)->entries) if (e.live) vs.push_back(e.v);
        Object* o = c.vm.newInstance("Ljava/util/HashMap$Values;");
        auto* d = new ListData(); d->v = vs; o->native.reset(d);
        c.retObj(o);
    });
    A(MP, "entrySet", "()Ljava/util/Set;", [](NC& c) {
        std::vector<Object*> es;
        for (auto& e : mapOf(c.vm, c.self)->entries) if (e.live) es.push_back(newEntry(c.vm, e.k, e.v, c.self));
        Object* o = c.vm.newInstance("Ljava/util/HashMap$EntrySet;");
        auto* d = new ListData(); d->v = es; o->native.reset(d);
        c.retObj(o);
    });
    A("Ljava/util/AbstractMap;", "toString", "()Ljava/lang/String;", [](NC& c) {
        std::string s = "{"; bool first = true;
        for (auto& e : mapOf(c.vm, c.self)->entries) if (e.live) { if (!first) s += ", "; first = false; s += c.vm.javaToString(e.k) + "=" + c.vm.javaToString(e.v); }
        c.retObj(c.vm.newString(s + "}"));
    });
    M({"Ljava/util/Hashtable;"}, "toString", "()Ljava/lang/String;", [](NC& c) {
        std::string s = "{"; bool first = true;
        for (auto& e : mapOf(c.vm, c.self)->entries) if (e.live) { if (!first) s += ", "; first = false; s += c.vm.javaToString(e.k) + "=" + c.vm.javaToString(e.v); }
        c.retObj(c.vm.newString(s + "}"));
    });
    A("Ljava/util/AbstractMap;", "equals", "(Ljava/lang/Object;)Z", [](NC& c) {
        if (c.self == c.o(0)) { c.retBool(true); return; }
        auto* other = c.o(0) ? dynamic_cast<MapData*>(c.o(0)->native.get()) : nullptr;
        auto* me = mapOf(c.vm, c.self);
        if (!other || other->liveCount != me->liveCount) { c.retBool(false); return; }
        for (auto& e : me->entries) if (e.live) { bool f; Object* v = mapGet(c.vm, other, e.k, &f); if (!f || !c.vm.javaEquals(e.v, v)) { c.retBool(false); return; } }
        c.retBool(true);
    });
    A("Ljava/util/AbstractMap;", "hashCode", "()I", [](NC& c) {
        uint32_t h = 0;
        for (auto& e : mapOf(c.vm, c.self)->entries) if (e.live) h += static_cast<uint32_t>(c.vm.javaHashCode(e.k) ^ c.vm.javaHashCode(e.v));
        c.retInt(static_cast<int32_t>(h));
    });
    A("Ljava/util/concurrent/ConcurrentMap;", "putIfAbsent", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;", [](NC& c) {
        auto* m = mapOf(c.vm, c.self); bool f; Object* v = mapGet(c.vm, m, c.o(0), &f);
        if (f) { c.retObj(v); return; }
        mapPut(c.vm, m, c.o(0), c.o(1)); c.retObj(nullptr);
    });

    // Map.Entry
    const char* EN = "Ljava/util/Map$Entry;";
    A(EN, "getKey", "()Ljava/lang/Object;", [](NC& c) { c.retObj(c.self->nativeAs<EntryData>()->k); });
    A(EN, "getValue", "()Ljava/lang/Object;", [](NC& c) { c.retObj(c.self->nativeAs<EntryData>()->v); });
    A(EN, "setValue", "(Ljava/lang/Object;)Ljava/lang/Object;", [](NC& c) {
        auto* d = c.self->nativeAs<EntryData>(); Object* old = d->v; d->v = c.o(0);
        if (d->owner) mapPut(c.vm, mapOf(c.vm, d->owner), d->k, d->v);
        c.retObj(old);
    });
    A("Ljava/util/HashMap$Node;", "toString", "()Ljava/lang/String;", [](NC& c) { auto* d = c.self->nativeAs<EntryData>(); c.retObj(c.vm.newString(c.vm.javaToString(d->k) + "=" + c.vm.javaToString(d->v))); });
    A("Ljava/util/HashMap$Node;", "hashCode", "()I", [](NC& c) { auto* d = c.self->nativeAs<EntryData>(); c.retInt(c.vm.javaHashCode(d->k) ^ c.vm.javaHashCode(d->v)); });
    A("Ljava/util/HashMap$Node;", "equals", "(Ljava/lang/Object;)Z", [](NC& c) {
        auto* d = c.self->nativeAs<EntryData>(); auto* o = c.o(0) ? c.o(0)->nativeAs<EntryData>() : nullptr;
        c.retBool(o && c.vm.javaEquals(d->k, o->k) && c.vm.javaEquals(d->v, o->v));
    });

    // ---- android.text.TextUtils -------------------------------------------------------
    const char* TU = "Landroid/text/TextUtils;";
    A(TU, "isEmpty", "(Ljava/lang/CharSequence;)Z", [](NC& c) { c.retBool(!c.o(0) || c.vm.javaToString(c.o(0)).empty()); });
    A(TU, "equals", "(Ljava/lang/CharSequence;Ljava/lang/CharSequence;)Z", [](NC& c) {
        if (c.o(0) == c.o(1)) { c.retBool(true); return; }
        if (!c.o(0) || !c.o(1)) { c.retBool(false); return; }
        c.retBool(c.vm.javaToString(c.o(0)) == c.vm.javaToString(c.o(1)));
    });
    A(TU, "join", "(Ljava/lang/CharSequence;Ljava/lang/Iterable;)Ljava/lang/String;", [](NC& c) {
        std::string sep = c.vm.javaToString(c.o(0)), out; bool first = true;
        forEachItem(c.vm, c.o(1), [&](Object* o) { if (!first) out += sep; first = false; out += c.vm.javaToString(o); });
        c.retObj(c.vm.newString(out));
    });
    A(TU, "join", "(Ljava/lang/CharSequence;[Ljava/lang/Object;)Ljava/lang/String;", [](NC& c) {
        std::string sep = c.vm.javaToString(c.o(0)), out; bool first = true;
        forEachItem(c.vm, c.o(1), [&](Object* o) { if (!first) out += sep; first = false; out += c.vm.javaToString(o); });
        c.retObj(c.vm.newString(out));
    });
    A(TU, "getTrimmedLength", "(Ljava/lang/CharSequence;)I", [](NC& c) {
        std::string s = c.vm.javaToString(c.o(0)); size_t b = 0, e = s.size();
        while (b < e && static_cast<unsigned char>(s[b]) <= ' ') ++b;
        while (e > b && static_cast<unsigned char>(s[e - 1]) <= ' ') --e;
        c.retInt(static_cast<int32_t>(e - b));
    });
    A(TU, "isDigitsOnly", "(Ljava/lang/CharSequence;)Z", [](NC& c) {
        std::string s = c.vm.javaToString(c.o(0));
        c.retBool(std::all_of(s.begin(), s.end(), [](char ch) { return ch >= '0' && ch <= '9'; }));
    });

    // ---- Collections / Arrays / Objects ------------------------------------------------------------------------
    const char* CS = "Ljava/util/Collections;";
    A(CS, "emptyList", "()Ljava/util/List;", [](NC& c) { c.retObj(newArrayList(c.vm, {})); });
    A(CS, "emptySet", "()Ljava/util/Set;", [](NC& c) { c.retObj(newSetFrom(c.vm, "Ljava/util/HashSet;", {})); });
    A(CS, "emptyMap", "()Ljava/util/Map;", [](NC& c) { Object* o = c.vm.newInstance("Ljava/util/HashMap;"); mapOf(c.vm, o); c.retObj(o); });
    A(CS, "singletonList", "(Ljava/lang/Object;)Ljava/util/List;", [](NC& c) { c.retObj(newArrayList(c.vm, {c.o(0)})); });
    A(CS, "singleton", "(Ljava/lang/Object;)Ljava/util/Set;", [](NC& c) { c.retObj(newSetFrom(c.vm, "Ljava/util/HashSet;", {c.o(0)})); });
    A(CS, "singletonMap", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/util/Map;", [](NC& c) { Object* o = c.vm.newInstance("Ljava/util/HashMap;"); mapPut(c.vm, mapOf(c.vm, o), c.o(0), c.o(1)); c.retObj(o); });
    for (const char* s : {"unmodifiableList:(Ljava/util/List;)Ljava/util/List;", "unmodifiableSet:(Ljava/util/Set;)Ljava/util/Set;",
                          "unmodifiableMap:(Ljava/util/Map;)Ljava/util/Map;", "unmodifiableCollection:(Ljava/util/Collection;)Ljava/util/Collection;",
                          "synchronizedList:(Ljava/util/List;)Ljava/util/List;", "synchronizedMap:(Ljava/util/Map;)Ljava/util/Map;",
                          "synchronizedSet:(Ljava/util/Set;)Ljava/util/Set;"}) {
        std::string str = s; size_t p = str.find(':');
        A(CS, str.substr(0, p).c_str(), str.substr(p + 1).c_str(), [](NC& c) { c.retObj(c.o(0)); });
    }
    A(CS, "sort", "(Ljava/util/List;)V", [](NC& c) { auto* l = listOf(c.vm, c.o(0)); auto copy = l->v; sortItems(c.vm, copy, nullptr); l->v = copy; });
    A(CS, "sort", "(Ljava/util/List;Ljava/util/Comparator;)V", [](NC& c) { auto* l = listOf(c.vm, c.o(0)); auto copy = l->v; sortItems(c.vm, copy, c.o(1)); l->v = copy; });
    A(CS, "reverse", "(Ljava/util/List;)V", [](NC& c) { auto* l = listOf(c.vm, c.o(0)); std::reverse(l->v.begin(), l->v.end()); });
    A(CS, "shuffle", "(Ljava/util/List;)V", [](NC&) {});
    A(CS, "swap", "(Ljava/util/List;II)V", [](NC& c) { auto* l = listOf(c.vm, c.o(0)); if (c.i(1) < 0 || c.i(2) < 0 || static_cast<size_t>(c.i(1)) >= l->v.size() || static_cast<size_t>(c.i(2)) >= l->v.size()) c.vm.throwNew("Ljava/lang/IndexOutOfBoundsException;"); std::swap(l->v[c.i(1)], l->v[c.i(2)]); });
    A(CS, "addAll", "(Ljava/util/Collection;[Ljava/lang/Object;)Z", [](NC& c) {
        Coll col(c.vm, c.o(0)); bool ch = false;
        if (auto* a = dynamic_cast<ArrayObject*>(c.o(1))) for (size_t i = 0; i < a->length(); ++i) ch |= col.add(a->getObject(i));
        c.retBool(ch);
    });
    A(CS, "max", "(Ljava/util/Collection;)Ljava/lang/Object;", [](NC& c) {
        auto v = itemsOf(c.vm, c.o(0)); if (v.empty()) c.vm.throwNew("Ljava/util/NoSuchElementException;");
        Object* b = v[0]; for (Object* x : v) if (compareObjects(c.vm, x, b, nullptr) > 0) b = x; c.retObj(b);
    });
    A(CS, "min", "(Ljava/util/Collection;)Ljava/lang/Object;", [](NC& c) {
        auto v = itemsOf(c.vm, c.o(0)); if (v.empty()) c.vm.throwNew("Ljava/util/NoSuchElementException;");
        Object* b = v[0]; for (Object* x : v) if (compareObjects(c.vm, x, b, nullptr) < 0) b = x; c.retObj(b);
    });
    A(CS, "emptyIterator", "()Ljava/util/Iterator;", [](NC& c) { c.retObj(newIterator(c.vm, {}, nullptr)); });
    A(CS, "nCopies", "(ILjava/lang/Object;)Ljava/util/List;", [](NC& c) { c.retObj(newArrayList(c.vm, std::vector<Object*>(static_cast<size_t>(std::max(0, c.i(0))), c.o(1)))); });

    const char* AR = "Ljava/util/Arrays;";
    A(AR, "asList", "([Ljava/lang/Object;)Ljava/util/List;", [](NC& c) {
        auto* a = dynamic_cast<ArrayObject*>(c.o(0)); if (!a) c.vm.throwNpe("asList");
        std::vector<Object*> v; for (size_t i = 0; i < a->length(); ++i) v.push_back(a->getObject(i));
        c.retObj(newArrayList(c.vm, v));
    });
    struct ArrKind { const char* d; };
    for (const char* d : {"[B", "[C", "[S", "[I", "[J", "[F", "[D", "[Z", "[Ljava/lang/Object;"}) {
        std::string desc = d;
        std::string ret = desc == "[Ljava/lang/Object;" ? "[Ljava/lang/Object;" : desc;
        A(AR, "copyOf", ("(" + desc + "I)" + ret).c_str(), [desc](NC& c) {
            auto* a = dynamic_cast<ArrayObject*>(c.o(0)); if (!a) c.vm.throwNpe("copyOf");
            if (c.i(1) < 0) c.vm.throwNew("Ljava/lang/NegativeArraySizeException;", std::to_string(c.i(1)));
            ArrayObject* n = c.vm.newArray(a->klass->getDescriptor(), c.i(1));
            size_t es = elemSize(a);
            std::memcpy(n->data(), a->data(), es * std::min<size_t>(a->length(), static_cast<size_t>(c.i(1))));
            c.retObj(n);
        });
        A(AR, "copyOfRange", ("(" + desc + "II)" + ret).c_str(), [](NC& c) {
            auto* a = dynamic_cast<ArrayObject*>(c.o(0)); if (!a) c.vm.throwNpe("copyOfRange");
            int32_t from = c.i(1), to = c.i(2);
            if (from > to) c.vm.throwNew("Ljava/lang/IllegalArgumentException;", std::to_string(from) + " > " + std::to_string(to));
            if (from < 0 || static_cast<size_t>(from) > a->length()) c.vm.throwNew("Ljava/lang/ArrayIndexOutOfBoundsException;", "from=" + std::to_string(from));
            ArrayObject* n = c.vm.newArray(a->klass->getDescriptor(), to - from);
            size_t es = elemSize(a);
            size_t cnt = std::min<size_t>(static_cast<size_t>(to - from), a->length() - static_cast<size_t>(from));
            std::memcpy(n->data(), static_cast<uint8_t*>(a->data()) + static_cast<size_t>(from) * es, es * cnt);
            c.retObj(n);
        });
        if (desc != "[Ljava/lang/Object;") {
            std::string elem = desc.substr(1);
            A(AR, "fill", ("(" + desc + elem + ")V").c_str(), [](NC& c) {
                auto* a = dynamic_cast<ArrayObject*>(c.o(0)); if (!a) c.vm.throwNpe("fill");
                size_t es = elemSize(a); uint64_t v = c.args[1].bits;
                for (size_t i = 0; i < a->length(); ++i) std::memcpy(static_cast<uint8_t*>(a->data()) + i * es, &v, es);
            });
        }
        A(AR, "equals", ("(" + desc + desc + ")Z").c_str(), [](NC& c) {
            auto* a = dynamic_cast<ArrayObject*>(c.o(0)); auto* b = dynamic_cast<ArrayObject*>(c.o(1));
            if (a == b) { c.retBool(true); return; }
            if (!a || !b || a->length() != b->length()) { c.retBool(false); return; }
            if (a->isObjectArray()) { for (size_t i = 0; i < a->length(); ++i) if (!c.vm.javaEquals(a->getObject(i), b->getObject(i))) { c.retBool(false); return; } c.retBool(true); return; }
            c.retBool(std::memcmp(a->data(), b->data(), elemSize(a) * a->length()) == 0);
        });
        A(AR, "toString", ("(" + desc + ")Ljava/lang/String;").c_str(), [](NC& c) {
            auto* a = dynamic_cast<ArrayObject*>(c.o(0)); if (!a) { c.retObj(c.vm.newString("null")); return; }
            std::string s = "[";
            for (size_t i = 0; i < a->length(); ++i) {
                if (i) s += ", ";
                switch (a->getElementType()) {
                    case ArrayObject::ArrayType::OBJECT: s += c.vm.javaToString(a->getObject(i)); break;
                    case ArrayObject::ArrayType::INT: s += std::to_string(a->get<int32_t>(i)); break;
                    case ArrayObject::ArrayType::LONG: s += std::to_string(a->get<int64_t>(i)); break;
                    case ArrayObject::ArrayType::SHORT: s += std::to_string(a->get<int16_t>(i)); break;
                    case ArrayObject::ArrayType::BYTE: s += std::to_string(a->get<int8_t>(i)); break;
                    case ArrayObject::ArrayType::BOOLEAN: s += a->get<uint8_t>(i) ? "true" : "false"; break;
                    case ArrayObject::ArrayType::CHAR: s += u16ToUtf8(std::u16string(1, a->get<char16_t>(i))); break;
                    case ArrayObject::ArrayType::FLOAT: s += javaFloatToString(a->get<float>(i)); break;
                    case ArrayObject::ArrayType::DOUBLE: s += javaDoubleToString(a->get<double>(i)); break;
                }
            }
            c.retObj(c.vm.newString(s + "]"));
        });
    }
    A(AR, "sort", "([I)V", [](NC& c) { auto* a = dynamic_cast<ArrayObject*>(c.o(0)); if (!a) c.vm.throwNpe("sort"); auto* p = static_cast<int32_t*>(a->data()); std::sort(p, p + a->length()); });
    A(AR, "sort", "([J)V", [](NC& c) { auto* a = dynamic_cast<ArrayObject*>(c.o(0)); if (!a) c.vm.throwNpe("sort"); auto* p = static_cast<int64_t*>(a->data()); std::sort(p, p + a->length()); });
    A(AR, "sort", "([C)V", [](NC& c) { auto* a = dynamic_cast<ArrayObject*>(c.o(0)); if (!a) c.vm.throwNpe("sort"); auto* p = static_cast<uint16_t*>(a->data()); std::sort(p, p + a->length()); });
    A(AR, "sort", "([B)V", [](NC& c) { auto* a = dynamic_cast<ArrayObject*>(c.o(0)); if (!a) c.vm.throwNpe("sort"); auto* p = static_cast<int8_t*>(a->data()); std::sort(p, p + a->length()); });
    A(AR, "sort", "([Ljava/lang/Object;)V", [](NC& c) {
        auto* a = dynamic_cast<ArrayObject*>(c.o(0)); if (!a) c.vm.throwNpe("sort");
        std::vector<Object*> v; for (size_t i = 0; i < a->length(); ++i) v.push_back(a->getObject(i));
        sortItems(c.vm, v, nullptr); for (size_t i = 0; i < v.size(); ++i) a->setObject(i, v[i]);
    });
    A(AR, "sort", "([Ljava/lang/Object;Ljava/util/Comparator;)V", [](NC& c) {
        auto* a = dynamic_cast<ArrayObject*>(c.o(0)); if (!a) c.vm.throwNpe("sort");
        std::vector<Object*> v; for (size_t i = 0; i < a->length(); ++i) v.push_back(a->getObject(i));
        sortItems(c.vm, v, c.o(1)); for (size_t i = 0; i < v.size(); ++i) a->setObject(i, v[i]);
    });
    A(AR, "hashCode", "([I)I", [](NC& c) { auto* a = dynamic_cast<ArrayObject*>(c.o(0)); if (!a) { c.retInt(0); return; } uint32_t h = 1; for (size_t i = 0; i < a->length(); ++i) h = 31u * h + static_cast<uint32_t>(a->get<int32_t>(i)); c.retInt(static_cast<int32_t>(h)); });
    A(AR, "hashCode", "([Ljava/lang/Object;)I", [](NC& c) { auto* a = dynamic_cast<ArrayObject*>(c.o(0)); if (!a) { c.retInt(0); return; } uint32_t h = 1; for (size_t i = 0; i < a->length(); ++i) h = 31u * h + static_cast<uint32_t>(c.vm.javaHashCode(a->getObject(i))); c.retInt(static_cast<int32_t>(h)); });

    const char* OB = "Ljava/util/Objects;";
    A(OB, "equals", "(Ljava/lang/Object;Ljava/lang/Object;)Z", [](NC& c) { c.retBool(c.vm.javaEquals(c.o(0), c.o(1))); });
    A(OB, "hashCode", "(Ljava/lang/Object;)I", [](NC& c) { c.retInt(c.o(0) ? c.vm.javaHashCode(c.o(0)) : 0); });
    A(OB, "hash", "([Ljava/lang/Object;)I", [](NC& c) {
        auto* a = dynamic_cast<ArrayObject*>(c.o(0)); if (!a) { c.retInt(0); return; }
        uint32_t h = 1; for (size_t i = 0; i < a->length(); ++i) h = 31u * h + static_cast<uint32_t>(a->getObject(i) ? c.vm.javaHashCode(a->getObject(i)) : 0);
        c.retInt(static_cast<int32_t>(h));
    });
    A(OB, "toString", "(Ljava/lang/Object;)Ljava/lang/String;", [](NC& c) { c.retObj(c.vm.newString(c.vm.javaToString(c.o(0)))); });
    A(OB, "toString", "(Ljava/lang/Object;Ljava/lang/String;)Ljava/lang/String;", [](NC& c) { c.retObj(c.o(0) ? c.vm.newString(c.vm.javaToString(c.o(0))) : c.o(1)); });
    A(OB, "requireNonNull", "(Ljava/lang/Object;)Ljava/lang/Object;", [](NC& c) { if (!c.o(0)) c.vm.throwNpe(); c.retObj(c.o(0)); });
    A(OB, "requireNonNull", "(Ljava/lang/Object;Ljava/lang/String;)Ljava/lang/Object;", [](NC& c) { if (!c.o(0)) c.vm.throwNpe(c.str(1)); c.retObj(c.o(0)); });
    A(OB, "isNull", "(Ljava/lang/Object;)Z", [](NC& c) { c.retBool(!c.o(0)); });
    A(OB, "nonNull", "(Ljava/lang/Object;)Z", [](NC& c) { c.retBool(c.o(0) != nullptr); });
    A(OB, "requireNonNullElse", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;", [](NC& c) { c.retObj(c.o(0) ? c.o(0) : c.o(1)); });

    // ---- java.util.Random (same LCG as the JDK) -----------------------------------------------------------------------
    auto seedOf = [](NC& c) -> uint64_t& { return c.self->nativeHandle; };
    auto next = [seedOf](NC& c, int bits) -> int32_t {
        uint64_t& s = seedOf(c);
        s = (s * 0x5DEECE66DULL + 0xBULL) & ((1ULL << 48) - 1);
        return static_cast<int32_t>(static_cast<int64_t>(s) >> (48 - bits));
    };
    A("Ljava/util/Random;", "<init>", "()V", [seedOf](NC& c) {
        seedOf(c) = (static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()) ^ 0x5DEECE66DULL) & ((1ULL << 48) - 1);
    });
    A("Ljava/util/Random;", "<init>", "(J)V", [seedOf](NC& c) { seedOf(c) = (static_cast<uint64_t>(c.j(0)) ^ 0x5DEECE66DULL) & ((1ULL << 48) - 1); });
    A("Ljava/util/Random;", "setSeed", "(J)V", [seedOf](NC& c) { seedOf(c) = (static_cast<uint64_t>(c.j(0)) ^ 0x5DEECE66DULL) & ((1ULL << 48) - 1); });
    A("Ljava/util/Random;", "nextInt", "()I", [next](NC& c) { c.retInt(next(c, 32)); });
    A("Ljava/util/Random;", "nextInt", "(I)I", [next](NC& c) {
        int32_t bound = c.i(0);
        if (bound <= 0) c.vm.throwNew("Ljava/lang/IllegalArgumentException;", "bound must be positive");
        if ((bound & -bound) == bound) { c.retInt(static_cast<int32_t>((static_cast<int64_t>(bound) * static_cast<int64_t>(next(c, 31))) >> 31)); return; }
        int32_t bits, val;
        do { bits = next(c, 31); val = bits % bound; } while (bits - val + (bound - 1) < 0);
        c.retInt(val);
    });
    A("Ljava/util/Random;", "nextLong", "()J", [next](NC& c) { int64_t hi = next(c, 32); int64_t lo = next(c, 32); c.retLong((hi << 32) + lo); });
    A("Ljava/util/Random;", "nextBoolean", "()Z", [next](NC& c) { c.retBool(next(c, 1) != 0); });
    A("Ljava/util/Random;", "nextDouble", "()D", [next](NC& c) { int64_t a = next(c, 26); int64_t b = next(c, 27); c.retDouble(static_cast<double>((a << 27) + b) * (1.0 / static_cast<double>(1LL << 53))); });
    A("Ljava/util/Random;", "nextFloat", "()F", [next](NC& c) { c.retFloat(static_cast<float>(next(c, 24)) / static_cast<float>(1 << 24)); });

    decl("Ljava/util/concurrent/ThreadLocalRandom;", "Ljava/util/Random;");
    A("Ljava/util/concurrent/ThreadLocalRandom;", "current", "()Ljava/util/concurrent/ThreadLocalRandom;", [](NC& c) {
        static Object* inst = nullptr;
        if (!inst) {
            inst = c.vm.newInstance("Ljava/util/concurrent/ThreadLocalRandom;");
            inst->nativeHandle = (static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()) ^ 0x5DEECE66DULL) & ((1ULL << 48) - 1);
        }
        c.retObj(inst);
    });
    A("Ljava/util/Random;", "nextInt", "(II)I", [next](NC& c) {
        int32_t lo = c.i(0), hi = c.i(1);
        if (lo >= hi) c.vm.throwNew("Ljava/lang/IllegalArgumentException;", "bound must be greater than origin");
        uint32_t range = static_cast<uint32_t>(hi) - static_cast<uint32_t>(lo);
        c.retInt(static_cast<int32_t>(static_cast<uint32_t>(lo) + static_cast<uint32_t>(next(c, 31)) % range));
    });
    A("Ljava/util/Random;", "nextLong", "(J)J", [next](NC& c) {
        int64_t bound = c.j(0); if (bound <= 0) c.vm.throwNew("Ljava/lang/IllegalArgumentException;", "bound must be positive");
        int64_t hi = next(c, 32); int64_t lo = next(c, 32);
        c.retLong(static_cast<int64_t>((static_cast<uint64_t>((hi << 32) + lo) >> 1) % static_cast<uint64_t>(bound)));
    });

    // ---- concurrency utilities: executors, futures, latches -------------------------------------------------------------
    struct FutureData : ObjectModel::Object::NativeData { Object* result = nullptr; std::atomic<bool> done{false}; Object* error = nullptr; };
    decl("Ljava/util/concurrent/ThreadPoolExecutor;", "Ljava/lang/Object;", {"Ljava/util/concurrent/ScheduledExecutorService;"});
    decl("Ljava/util/concurrent/FutureTask;", "Ljava/lang/Object;", {"Ljava/util/concurrent/Future;", "Ljava/lang/Runnable;"});
    decl("Ljava/util/concurrent/CountDownLatch;", "Ljava/lang/Object;");
    for (const char* n : {"newFixedThreadPool:(I)", "newSingleThreadExecutor:()", "newCachedThreadPool:()", "newScheduledThreadPool:(I)", "newSingleThreadScheduledExecutor:()", "newWorkStealingPool:()"}) {
        std::string s = n; size_t p = s.find(':');
        std::string ret = s.find("Scheduled") != std::string::npos ? "Ljava/util/concurrent/ScheduledExecutorService;" : "Ljava/util/concurrent/ExecutorService;";
        A("Ljava/util/concurrent/Executors;", s.substr(0, p).c_str(), (s.substr(p + 1) + ret).c_str(), [](NC& c) { c.retObj(c.vm.newInstance("Ljava/util/concurrent/ThreadPoolExecutor;")); });
    }
    auto runAsync = [](NC& c, Object* task, bool callable, Object* futureObj) {
        Vm* vm = &c.vm;
        vm->spawnThread([vm, task, callable, futureObj]() {
            Object* res = nullptr;
            bool ok = vm->runGuarded([&] {
                if (callable) res = vm->callVirtual(task, "call", "()Ljava/lang/Object;").ref;
                else vm->callVirtual(task, "run", "()V");
            }, "executor task");
            if (futureObj) {
                auto* f = futureObj->nativeAs<FutureData>();
                f->result = res; if (!ok) f->error = res;
                f->done = true;
            }
        });
    };
    decl("Ljava/util/concurrent/ScheduledThreadPoolExecutor;", "Ljava/util/concurrent/ThreadPoolExecutor;");
    A("Ljava/util/concurrent/ScheduledThreadPoolExecutor;", "<init>", "(I)V", [](NC&) {});
    A("Ljava/util/concurrent/ScheduledThreadPoolExecutor;", "<init>", "(ILjava/util/concurrent/ThreadFactory;)V", [](NC&) {});
    A("Ljava/util/concurrent/ScheduledThreadPoolExecutor;", "execute", "(Ljava/lang/Runnable;)V", [runAsync](NC& c) { runAsync(c, c.o(0), false, nullptr); });
    A("Ljava/util/concurrent/ScheduledExecutorService;", "schedule", "(Ljava/lang/Runnable;JLjava/util/concurrent/TimeUnit;)Ljava/util/concurrent/ScheduledFuture;", [](NC& c) {
        Vm* vm = &c.vm; Object* task = c.o(0);
        int64_t ms = std::min<int64_t>(c.j(1), 1000);
        vm->post([vm, task]() { vm->callVirtual(task, "run", "()V"); }, ms);
        c.retObj(c.vm.newInstance("Ljava/util/concurrent/FutureTask;"));
    });
    A("Ljava/util/concurrent/Executor;", "execute", "(Ljava/lang/Runnable;)V", [runAsync](NC& c) { runAsync(c, c.o(0), false, nullptr); });
    auto submit = [runAsync](NC& c, bool callable) {
        Object* fut = c.vm.newInstance("Ljava/util/concurrent/FutureTask;");
        fut->native = std::make_unique<FutureData>();
        runAsync(c, c.o(0), callable, fut);
        c.retObj(fut);
    };
    A("Ljava/util/concurrent/ExecutorService;", "submit", "(Ljava/lang/Runnable;)Ljava/util/concurrent/Future;", [submit](NC& c) { submit(c, false); });
    A("Ljava/util/concurrent/ExecutorService;", "submit", "(Ljava/util/concurrent/Callable;)Ljava/util/concurrent/Future;", [submit](NC& c) { submit(c, true); });
    A("Ljava/util/concurrent/ExecutorService;", "shutdown", "()V", [](NC&) {});
    A("Ljava/util/concurrent/ExecutorService;", "shutdownNow", "()Ljava/util/List;", [](NC& c) { c.retObj(newArrayList(c.vm, {})); });
    A("Ljava/util/concurrent/ExecutorService;", "isShutdown", "()Z", [](NC& c) { c.retBool(false); });
    A("Ljava/util/concurrent/ExecutorService;", "awaitTermination", "(JLjava/util/concurrent/TimeUnit;)Z", [](NC& c) { c.retBool(true); });
    A("Ljava/util/concurrent/Future;", "get", "()Ljava/lang/Object;", [](NC& c) {
        auto* f = c.self->nativeAs<FutureData>();
        while (f && !f->done) { Vm::Unlocker u(c.vm); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
        c.retObj(f ? f->result : nullptr);
    });
    A("Ljava/util/concurrent/Future;", "isDone", "()Z", [](NC& c) { auto* f = c.self->nativeAs<FutureData>(); c.retBool(!f || f->done); });
    A("Ljava/util/concurrent/Future;", "cancel", "(Z)Z", [](NC& c) { c.retBool(false); });
    A("Ljava/util/concurrent/Future;", "isCancelled", "()Z", [](NC& c) { c.retBool(false); });
    A("Ljava/util/concurrent/CountDownLatch;", "<init>", "(I)V", [](NC& c) { c.self->nativeHandle = static_cast<uint32_t>(c.i(0)); });
    A("Ljava/util/concurrent/CountDownLatch;", "countDown", "()V", [](NC& c) { if (c.self->nativeHandle) c.self->nativeHandle--; });
    A("Ljava/util/concurrent/CountDownLatch;", "getCount", "()J", [](NC& c) { c.retLong(static_cast<int64_t>(c.self->nativeHandle)); });
    A("Ljava/util/concurrent/CountDownLatch;", "await", "()V", [](NC& c) {
        while (c.self->nativeHandle) { Vm::Unlocker u(c.vm); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    });
    A("Ljava/util/concurrent/CountDownLatch;", "await", "(JLjava/util/concurrent/TimeUnit;)Z", [](NC& c) {
        for (int i = 0; i < 200 && c.self->nativeHandle; ++i) { Vm::Unlocker u(c.vm); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
        c.retBool(c.self->nativeHandle == 0);
    });
}

} // namespace NativeDex::Runtime::BytecodeInterpreter
