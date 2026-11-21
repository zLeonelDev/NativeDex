// java.lang.reflect and java.io.File basics.

#include "NativeHelpers.h"

#include <filesystem>
#include <fstream>

namespace NativeDex::Runtime::BytecodeInterpreter {

namespace {
NativeRegistry& REG() { return NativeRegistry::instance(); }
void A(const char* cls, const char* name, const char* proto, NativeFn fn) { REG().add(cls, name, proto, std::move(fn)); }
void decl(const char* cls, const char* super, std::vector<std::string> ifs = {}) { REG().declareClass(cls, super, ifs, false); }

struct FieldData : ObjectModel::Object::NativeData {
    const RField* f = nullptr;
};
struct MethodData : ObjectModel::Object::NativeData {
    const RMethod* m = nullptr;
};
struct FileData : ObjectModel::Object::NativeData {
    std::string path;
};
struct ReaderData : ObjectModel::Object::NativeData {
    std::shared_ptr<std::ifstream> in;
};

const char* boxFor(char c) {
    switch (c) {
        case 'I': return "Ljava/lang/Integer;";
        case 'J': return "Ljava/lang/Long;";
        case 'Z': return "Ljava/lang/Boolean;";
        case 'B': return "Ljava/lang/Byte;";
        case 'C': return "Ljava/lang/Character;";
        case 'S': return "Ljava/lang/Short;";
        case 'F': return "Ljava/lang/Float;";
        case 'D': return "Ljava/lang/Double;";
        default: return nullptr;
    }
}

Object* boxByDescriptor(Vm& vm, const std::string& type, uint64_t bits) {
    const char* b = type.empty() ? nullptr : boxFor(type[0]);
    if (!b) return nullptr;
    uint64_t v = (type[0] == 'J' || type[0] == 'D') ? bits : (bits & 0xFFFFFFFFull);
    return vm.box(b, v);
}

std::vector<std::string> paramDescriptors(const std::string& proto) {
    std::vector<std::string> out;
    size_t i = 1;
    while (i < proto.size() && proto[i] != ')') {
        size_t s = i;
        while (proto[i] == '[') ++i;
        if (proto[i] == 'L') i = proto.find(';', i) + 1; else ++i;
        out.push_back(proto.substr(s, i - s));
    }
    return out;
}

Object* newField(Vm& vm, const RField* f) {
    Object* o = vm.newInstance("Ljava/lang/reflect/Field;");
    auto* d = new FieldData(); d->f = f; o->native.reset(d);
    return o;
}
Object* newMethod(Vm& vm, const RMethod* m, bool ctor) {
    Object* o = vm.newInstance(ctor ? "Ljava/lang/reflect/Constructor;" : "Ljava/lang/reflect/Method;");
    auto* d = new MethodData(); d->m = m; o->native.reset(d);
    return o;
}

bool paramsMatch(const RMethod* m, ArrayObject* types) {
    auto ps = paramDescriptors(m->proto);
    size_t n = types ? types->length() : 0;
    if (ps.size() != n) return false;
    for (size_t i = 0; i < n; ++i) {
        auto* k = dynamic_cast<RuntimeClass*>(types->getObject(i));
        if (!k || k->getDescriptor() != ps[i]) return false;
    }
    return true;
}

Object::FieldSlot* fieldSlot(NC& c, const RField* f, Object* target) {
    if (f->isStatic) { c.vm.ensureInitialized(f->declaring); return &f->declaring->statics[f->slot]; }
    if (!target) c.vm.throwNpe("Field.get on null receiver");
    if (f->slot >= target->fieldSlots.size()) target->fieldSlots.resize(f->slot + 1);
    return &target->fieldSlots[f->slot];
}

Value unboxArg(Vm& vm, Kind k, Object* o) {
    Value v;
    if (k == Kind::Ref) { v.ref = o; return v; }
    if (!o) vm.throwNew("Ljava/lang/IllegalArgumentException;", "null for primitive parameter");
    v.bits = o->nativeHandle;
    return v;
}

std::string pathOf(NC& c, Object* o) {
    auto* d = o ? o->nativeAs<FileData>() : nullptr;
    return d ? d->path : std::string();
}
Object* newFile(Vm& vm, const std::string& path) {
    Object* f = vm.newInstance("Ljava/io/File;");
    auto* d = new FileData(); d->path = path; f->native.reset(d);
    return f;
}
} // namespace

void registerReflectNatives() {
    decl("Ljava/lang/reflect/AccessibleObject;", "Ljava/lang/Object;");
    decl("Ljava/lang/reflect/Field;", "Ljava/lang/reflect/AccessibleObject;");
    decl("Ljava/lang/reflect/Method;", "Ljava/lang/reflect/AccessibleObject;");
    decl("Ljava/lang/reflect/Constructor;", "Ljava/lang/reflect/AccessibleObject;");
    decl("Ljava/io/File;", "Ljava/lang/Object;", {"Ljava/lang/Comparable;", "Ljava/io/Serializable;"});
    decl("Ljava/io/Reader;", "Ljava/lang/Object;", {"Ljava/io/Closeable;"});
    decl("Ljava/io/InputStreamReader;", "Ljava/io/Reader;");
    decl("Ljava/io/FileReader;", "Ljava/io/InputStreamReader;");
    decl("Ljava/io/BufferedReader;", "Ljava/io/Reader;");

    A("Ljava/lang/reflect/AccessibleObject;", "setAccessible", "(Z)V", [](NC&) {});
    A("Ljava/lang/reflect/AccessibleObject;", "isAccessible", "()Z", [](NC& c) { c.retBool(true); });
    A("Ljava/lang/reflect/AccessibleObject;", "trySetAccessible", "()Z", [](NC& c) { c.retBool(true); });

    // ---- Class -> Field/Method/Constructor ---------------------------------------------------
    auto findField = [](NC& c) {
        auto* k = static_cast<RuntimeClass*>(c.self);
        std::string name = c.str(0);
        for (RuntimeClass* t = k; t; t = t->parent)
            for (const auto& f : t->fields) if (f.name == name) { c.retObj(newField(c.vm, &f)); return; }
        c.vm.throwNew("Ljava/lang/NoSuchFieldException;", name);
    };
    A("Ljava/lang/Class;", "getDeclaredField", "(Ljava/lang/String;)Ljava/lang/reflect/Field;", findField);
    A("Ljava/lang/Class;", "getField", "(Ljava/lang/String;)Ljava/lang/reflect/Field;", findField);
    auto allFields = [](NC& c, bool inherit) {
        auto* k = static_cast<RuntimeClass*>(c.self);
        std::vector<const RField*> v;
        for (RuntimeClass* t = k; t; t = inherit ? t->parent : nullptr) for (const auto& f : t->fields) v.push_back(&f);
        ArrayObject* a = c.vm.newArray("[Ljava/lang/reflect/Field;", static_cast<int32_t>(v.size()));
        for (size_t i = 0; i < v.size(); ++i) a->setObject(i, newField(c.vm, v[i]));
        c.retObj(a);
    };
    A("Ljava/lang/Class;", "getDeclaredFields", "()[Ljava/lang/reflect/Field;", [allFields](NC& c) { allFields(c, false); });
    A("Ljava/lang/Class;", "getFields", "()[Ljava/lang/reflect/Field;", [allFields](NC& c) { allFields(c, true); });

    auto findMethod = [](NC& c) {
        auto* k = static_cast<RuntimeClass*>(c.self);
        std::string name = c.str(0);
        auto* types = dynamic_cast<ArrayObject*>(c.o(1));
        for (RuntimeClass* t = k; t; t = t->parent)
            for (const auto& m : t->methods) if (m.name == name && paramsMatch(&m, types)) { c.retObj(newMethod(c.vm, &m, false)); return; }
        c.vm.throwNew("Ljava/lang/NoSuchMethodException;", k->getClassName() + "." + name);
    };
    A("Ljava/lang/Class;", "getDeclaredMethod", "(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;", findMethod);
    A("Ljava/lang/Class;", "getMethod", "(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;", findMethod);
    auto allMethods = [](NC& c, bool inherit) {
        auto* k = static_cast<RuntimeClass*>(c.self);
        std::vector<const RMethod*> v;
        for (RuntimeClass* t = k; t; t = inherit ? t->parent : nullptr)
            for (const auto& m : t->methods) if (m.name != "<init>" && m.name != "<clinit>") v.push_back(&m);
        ArrayObject* a = c.vm.newArray("[Ljava/lang/reflect/Method;", static_cast<int32_t>(v.size()));
        for (size_t i = 0; i < v.size(); ++i) a->setObject(i, newMethod(c.vm, v[i], false));
        c.retObj(a);
    };
    A("Ljava/lang/Class;", "getDeclaredMethods", "()[Ljava/lang/reflect/Method;", [allMethods](NC& c) { allMethods(c, false); });
    A("Ljava/lang/Class;", "getMethods", "()[Ljava/lang/reflect/Method;", [allMethods](NC& c) { allMethods(c, true); });
    auto findCtor = [](NC& c) {
        auto* k = static_cast<RuntimeClass*>(c.self);
        auto* types = dynamic_cast<ArrayObject*>(c.o(0));
        for (const auto& m : k->methods) if (m.name == "<init>" && paramsMatch(&m, types)) { c.retObj(newMethod(c.vm, &m, true)); return; }
        c.vm.throwNew("Ljava/lang/NoSuchMethodException;", k->getClassName() + ".<init>");
    };
    A("Ljava/lang/Class;", "getDeclaredConstructor", "([Ljava/lang/Class;)Ljava/lang/reflect/Constructor;", findCtor);
    A("Ljava/lang/Class;", "getConstructor", "([Ljava/lang/Class;)Ljava/lang/reflect/Constructor;", findCtor);
    A("Ljava/lang/Class;", "newInstance", "()Ljava/lang/Object;", [](NC& c) {
        auto* k = static_cast<RuntimeClass*>(c.self);
        c.vm.ensureInitialized(k);
        Object* o = c.vm.newInstance(k);
        const RMethod* ctor = c.vm.findMethod(k, "<init>", "()V");
        if (ctor && ctor->owner == k) c.vm.invoke(ctor, o, {});
        else { Target t = c.vm.resolveVirtual(k, "<init>()V", "<init>", "()V"); if (t.valid()) c.vm.invokeTarget(t, nullptr, o, {}); }
        c.retObj(o);
    });

    // ---- Field ---------------------------------------------------------------------------------------
    auto fd = [](NC& c) { return c.self->nativeAs<FieldData>()->f; };
    A("Ljava/lang/reflect/Field;", "getName", "()Ljava/lang/String;", [fd](NC& c) { c.retObj(c.vm.newString(fd(c)->name)); });
    A("Ljava/lang/reflect/Field;", "getModifiers", "()I", [fd](NC& c) { c.retInt(static_cast<int32_t>(fd(c)->flags & 0xFFFF)); });
    A("Ljava/lang/reflect/Field;", "getType", "()Ljava/lang/Class;", [fd](NC& c) { c.retObj(c.vm.findClass(fd(c)->type)); });
    A("Ljava/lang/reflect/Field;", "getDeclaringClass", "()Ljava/lang/Class;", [fd](NC& c) { c.retObj(fd(c)->declaring); });
    A("Ljava/lang/reflect/Field;", "get", "(Ljava/lang/Object;)Ljava/lang/Object;", [fd](NC& c) {
        const RField* f = fd(c); auto* s = fieldSlot(c, f, c.o(0));
        c.retObj(f->kind == Kind::Ref ? s->ref : boxByDescriptor(c.vm, f->type, s->bits));
    });
    A("Ljava/lang/reflect/Field;", "set", "(Ljava/lang/Object;Ljava/lang/Object;)V", [fd](NC& c) {
        const RField* f = fd(c); auto* s = fieldSlot(c, f, c.o(0));
        if (f->kind == Kind::Ref) s->ref = c.o(1);
        else { if (!c.o(1)) c.vm.throwNew("Ljava/lang/IllegalArgumentException;", "null"); s->bits = c.o(1)->nativeHandle; }
    });
    for (const auto& g : std::vector<std::pair<const char*, const char*>>{{"getInt", "I"}, {"getLong", "J"}, {"getBoolean", "Z"}, {"getFloat", "F"}, {"getDouble", "D"}}) {
        std::string p = std::string("(Ljava/lang/Object;)") + g.second;
        A("Ljava/lang/reflect/Field;", g.first, p.c_str(), [fd](NC& c) { auto* s = fieldSlot(c, fd(c), c.o(0)); Value v; v.bits = s->bits; c.ret = v; });
    }
    for (const auto& g : std::vector<std::pair<const char*, const char*>>{{"setInt", "I"}, {"setLong", "J"}, {"setBoolean", "Z"}, {"setFloat", "F"}, {"setDouble", "D"}}) {
        std::string p = std::string("(Ljava/lang/Object;") + g.second + ")V";
        A("Ljava/lang/reflect/Field;", g.first, p.c_str(), [fd](NC& c) { fieldSlot(c, fd(c), c.o(0))->bits = c.args[1].bits; });
    }

    // ---- Method / Constructor ------------------------------------------------------------------------------
    auto md = [](NC& c) { return c.self->nativeAs<MethodData>()->m; };
    A("Ljava/lang/reflect/Method;", "getName", "()Ljava/lang/String;", [md](NC& c) { c.retObj(c.vm.newString(md(c)->name)); });
    A("Ljava/lang/reflect/Method;", "getModifiers", "()I", [md](NC& c) { c.retInt(static_cast<int32_t>(md(c)->flags & 0xFFFF)); });
    A("Ljava/lang/reflect/Method;", "getDeclaringClass", "()Ljava/lang/Class;", [md](NC& c) { c.retObj(md(c)->owner); });
    A("Ljava/lang/reflect/Method;", "getReturnType", "()Ljava/lang/Class;", [md](NC& c) { c.retObj(c.vm.findClass(md(c)->proto.substr(md(c)->proto.find(')') + 1))); });
    A("Ljava/lang/reflect/Method;", "getParameterTypes", "()[Ljava/lang/Class;", [md](NC& c) {
        auto ps = paramDescriptors(md(c)->proto);
        ArrayObject* a = c.vm.newArray("[Ljava/lang/Class;", static_cast<int32_t>(ps.size()));
        for (size_t i = 0; i < ps.size(); ++i) a->setObject(i, c.vm.findClass(ps[i]));
        c.retObj(a);
    });
    A("Ljava/lang/reflect/Method;", "getParameterCount", "()I", [md](NC& c) { c.retInt(static_cast<int32_t>(md(c)->sig.params.size())); });
    A("Ljava/lang/reflect/Method;", "invoke", "(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;", [md](NC& c) {
        const RMethod* m = md(c);
        auto* args = dynamic_cast<ArrayObject*>(c.o(1));
        std::vector<Value> vals;
        for (size_t i = 0; i < m->sig.params.size(); ++i)
            vals.push_back(unboxArg(c.vm, m->sig.params[i], args && i < args->length() ? args->getObject(i) : nullptr));
        Value r;
        if (m->isStatic()) { c.vm.ensureInitialized(m->owner); r = c.vm.invoke(m, nullptr, vals); }
        else {
            if (!c.o(0)) c.vm.throwNpe("Method.invoke receiver");
            Target t = c.vm.resolveVirtual(c.vm.classOf(c.o(0)), m->key, m->name, m->proto);
            r = t.valid() ? c.vm.invokeTarget(t, nullptr, c.o(0), vals) : c.vm.invoke(m, c.o(0), vals);
        }
        std::string ret = m->proto.substr(m->proto.find(')') + 1);
        if (ret == "V") c.retObj(nullptr);
        else if (ret[0] == 'L' || ret[0] == '[') c.retObj(r.ref);
        else c.retObj(boxByDescriptor(c.vm, ret, r.bits));
    });
    A("Ljava/lang/reflect/Constructor;", "newInstance", "([Ljava/lang/Object;)Ljava/lang/Object;", [md](NC& c) {
        const RMethod* m = md(c);
        auto* args = dynamic_cast<ArrayObject*>(c.o(0));
        std::vector<Value> vals;
        for (size_t i = 0; i < m->sig.params.size(); ++i)
            vals.push_back(unboxArg(c.vm, m->sig.params[i], args && i < args->length() ? args->getObject(i) : nullptr));
        c.vm.ensureInitialized(m->owner);
        Object* o = c.vm.newInstance(m->owner);
        c.vm.invoke(m, o, vals);
        c.retObj(o);
    });
    A("Ljava/lang/reflect/Constructor;", "setAccessible", "(Z)V", [](NC&) {});

    // ---- java.io.File ---------------------------------------------------------------------------------------------
    namespace fs = std::filesystem;
    A("Ljava/io/File;", "<init>", "(Ljava/lang/String;)V", [](NC& c) { auto* d = new FileData(); d->path = c.str(0); c.self->native.reset(d); });
    A("Ljava/io/File;", "<init>", "(Ljava/io/File;Ljava/lang/String;)V", [](NC& c) {
        auto* d = new FileData(); std::string p = pathOf(c, c.o(0)); d->path = p.empty() ? c.str(1) : p + "/" + c.str(1); c.self->native.reset(d);
    });
    A("Ljava/io/File;", "<init>", "(Ljava/lang/String;Ljava/lang/String;)V", [](NC& c) {
        auto* d = new FileData(); d->path = c.str(0) + "/" + c.str(1); c.self->native.reset(d);
    });
    A("Ljava/io/File;", "getPath", "()Ljava/lang/String;", [](NC& c) { c.retObj(c.vm.newString(pathOf(c, c.self))); });
    A("Ljava/io/File;", "getAbsolutePath", "()Ljava/lang/String;", [](NC& c) { c.retObj(c.vm.newString(pathOf(c, c.self))); });
    A("Ljava/io/File;", "getCanonicalPath", "()Ljava/lang/String;", [](NC& c) { c.retObj(c.vm.newString(pathOf(c, c.self))); });
    A("Ljava/io/File;", "toString", "()Ljava/lang/String;", [](NC& c) { c.retObj(c.vm.newString(pathOf(c, c.self))); });
    A("Ljava/io/File;", "getName", "()Ljava/lang/String;", [](NC& c) { std::string p = pathOf(c, c.self); size_t s = p.find_last_of('/'); c.retObj(c.vm.newString(s == std::string::npos ? p : p.substr(s + 1))); });
    A("Ljava/io/File;", "getParent", "()Ljava/lang/String;", [](NC& c) { std::string p = pathOf(c, c.self); size_t s = p.find_last_of('/'); c.retObj(s == std::string::npos ? nullptr : c.vm.newString(p.substr(0, s))); });
    A("Ljava/io/File;", "getParentFile", "()Ljava/io/File;", [](NC& c) { std::string p = pathOf(c, c.self); size_t s = p.find_last_of('/'); c.retObj(s == std::string::npos ? nullptr : newFile(c.vm, p.substr(0, s))); });
    A("Ljava/io/File;", "exists", "()Z", [](NC& c) { std::error_code ec; c.retBool(fs::exists(pathOf(c, c.self), ec)); });
    A("Ljava/io/File;", "isDirectory", "()Z", [](NC& c) { std::error_code ec; c.retBool(fs::is_directory(pathOf(c, c.self), ec)); });
    A("Ljava/io/File;", "isFile", "()Z", [](NC& c) { std::error_code ec; c.retBool(fs::is_regular_file(pathOf(c, c.self), ec)); });
    A("Ljava/io/File;", "canRead", "()Z", [](NC& c) { std::error_code ec; c.retBool(fs::exists(pathOf(c, c.self), ec)); });
    A("Ljava/io/File;", "canWrite", "()Z", [](NC& c) { c.retBool(true); });
    A("Ljava/io/File;", "length", "()J", [](NC& c) { std::error_code ec; auto n = fs::file_size(pathOf(c, c.self), ec); c.retLong(ec ? 0 : static_cast<int64_t>(n)); });
    A("Ljava/io/File;", "mkdirs", "()Z", [](NC& c) { std::error_code ec; c.retBool(fs::create_directories(pathOf(c, c.self), ec)); });
    A("Ljava/io/File;", "mkdir", "()Z", [](NC& c) { std::error_code ec; c.retBool(fs::create_directory(pathOf(c, c.self), ec)); });
    A("Ljava/io/File;", "delete", "()Z", [](NC& c) { std::error_code ec; c.retBool(fs::remove(pathOf(c, c.self), ec)); });
    A("Ljava/io/File;", "createNewFile", "()Z", [](NC& c) {
        std::string p = pathOf(c, c.self); std::error_code ec;
        if (fs::exists(p, ec)) { c.retBool(false); return; }
        std::ofstream(p).put('\0'); fs::resize_file(p, 0, ec); c.retBool(true);
    });
    A("Ljava/io/File;", "listFiles", "()[Ljava/io/File;", [](NC& c) {
        std::vector<std::string> v; std::error_code ec;
        for (auto& e : fs::directory_iterator(pathOf(c, c.self), ec)) v.push_back(e.path().generic_string());
        ArrayObject* a = c.vm.newArray("[Ljava/io/File;", static_cast<int32_t>(v.size()));
        for (size_t i = 0; i < v.size(); ++i) a->setObject(i, newFile(c.vm, v[i]));
        c.retObj(a);
    });
    A("Ljava/io/File;", "hashCode", "()I", [](NC& c) { c.retInt(static_cast<int32_t>(std::hash<std::string>()(pathOf(c, c.self)))); });
    A("Ljava/io/File;", "equals", "(Ljava/lang/Object;)Z", [](NC& c) { c.retBool(c.o(0) && pathOf(c, c.self) == pathOf(c, c.o(0))); });

    // Context directories live under data/<package>/
    auto dir = [](const char* sub) {
        return [sub](NC& c) {
            std::string p = "data/" + c.vm.props["package"] + "/" + sub;
            std::error_code ec; std::filesystem::create_directories(p, ec);
            c.retObj(newFile(c.vm, p));
        };
    };
    for (const char* cls : {"Landroid/content/Context;", "Landroid/content/ContextWrapper;", "Landroid/view/ContextThemeWrapper;", "Landroid/app/Application;", "Landroid/app/Activity;"}) {
        A(cls, "getDataDir", "()Ljava/io/File;", dir(""));
        A(cls, "getFilesDir", "()Ljava/io/File;", dir("files"));
        A(cls, "getCacheDir", "()Ljava/io/File;", dir("cache"));
        A(cls, "getCodeCacheDir", "()Ljava/io/File;", dir("code_cache"));
        A(cls, "getNoBackupFilesDir", "()Ljava/io/File;", dir("no_backup"));
        A(cls, "getExternalFilesDir", "(Ljava/lang/String;)Ljava/io/File;", dir("external"));
        A(cls, "getExternalCacheDir", "()Ljava/io/File;", dir("external_cache"));
    }
    A("Landroid/os/Process;", "myPid", "()I", [](NC& c) { c.retInt(4242); });
    A("Landroid/os/Process;", "myUid", "()I", [](NC& c) { c.retInt(10000); });
    A("Landroid/os/Process;", "myTid", "()I", [](NC& c) { c.retInt(4242); });
    A("Landroid/os/Process;", "setThreadPriority", "(I)V", [](NC&) {});
    A("Landroid/os/Process;", "is64Bit", "()Z", [](NC& c) { c.retBool(true); });

    // ---- minimal readers ------------------------------------------------------------------------------------------
    auto rd = [](NC& c) -> ReaderData* { if (!c.self->native) c.self->native = std::make_unique<ReaderData>(); return c.self->nativeAs<ReaderData>(); };
    A("Ljava/io/FileReader;", "<init>", "(Ljava/lang/String;)V", [rd](NC& c) {
        auto in = std::make_shared<std::ifstream>(c.str(0));
        if (!in->good()) c.vm.throwNew("Ljava/io/FileNotFoundException;", c.str(0) + " (No such file or directory)");
        rd(c)->in = in;
    });
    A("Ljava/io/FileReader;", "<init>", "(Ljava/io/File;)V", [rd](NC& c) {
        auto in = std::make_shared<std::ifstream>(pathOf(c, c.o(0)));
        if (!in->good()) c.vm.throwNew("Ljava/io/FileNotFoundException;", pathOf(c, c.o(0)) + " (No such file or directory)");
        rd(c)->in = in;
    });
    A("Ljava/io/BufferedReader;", "<init>", "(Ljava/io/Reader;)V", [rd](NC& c) { if (c.o(0) && c.o(0)->native) rd(c)->in = c.o(0)->nativeAs<ReaderData>()->in; });
    A("Ljava/io/BufferedReader;", "<init>", "(Ljava/io/Reader;I)V", [rd](NC& c) { if (c.o(0) && c.o(0)->native) rd(c)->in = c.o(0)->nativeAs<ReaderData>()->in; });
    A("Ljava/io/BufferedReader;", "readLine", "()Ljava/lang/String;", [rd](NC& c) {
        auto* d = rd(c); std::string line;
        if (!d->in || !std::getline(*d->in, line)) { c.retObj(nullptr); return; }
        if (!line.empty() && line.back() == '\r') line.pop_back();
        c.retObj(c.vm.newString(line));
    });
    A("Ljava/io/Reader;", "close", "()V", [rd](NC& c) { rd(c)->in.reset(); });
    A("Ljava/io/Reader;", "read", "()I", [rd](NC& c) { auto* d = rd(c); int ch = d->in ? d->in->get() : -1; c.retInt(ch == EOF ? -1 : ch); });
}

} // namespace NativeDex::Runtime::BytecodeInterpreter
