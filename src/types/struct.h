#pragma once

#include <girepository.h>
#include <glib.h>
#include <internal/PersistentObjectStore.h>
#include <nan.h>
#include <v8.h>
#include <vector>
#include "util.h"

namespace gir {

using namespace v8;

// Use the managed trait as this is for storing structs managed by Gtk main loop
using PersistentFunctionTemplate = Nan::Persistent<FunctionTemplate, CopyablePersistentTraits<FunctionTemplate>>;

class GIRStruct;

class GIRStruct : public Nan::ObjectWrap {
public:
    gpointer get_native_ptr();

    static Local<Function> prepare(GIStructInfo *info);
    static Local<Value> from_existing(gpointer boxed_c_structure, GIStructInfo *info);

private:
    static PersistentObjectStore<GType, PersistentFunctionTemplate> prepared_js_classes;

    gpointer boxed_c_structure = nullptr;
    GIRInfoUniquePtr struct_info = nullptr;

    // when we create GIRStructs in `prepare()` we sometimes
    // allocate memory for the struct ourselves (rather than using
    // `g_boxed_copy()` so we need to remember which we did so we
    // can clean up appropriately)
    bool slice_allocated = false;

    static GIRInfoUniquePtr find_native_constructor(GIStructInfo *struct_info);
    static void register_methods(GIStructInfo *info, const char *namespace_, Local<FunctionTemplate> object_template);
    static NAN_METHOD(constructor);
    static NAN_METHOD(call_method);
    static NAN_PROPERTY_GETTER(property_get_handler);
    static NAN_PROPERTY_SETTER(property_set_handler);
    static NAN_PROPERTY_QUERY(property_query_handler);

    GIRStruct() = default;
    ~GIRStruct();
};

} // namespace gir
