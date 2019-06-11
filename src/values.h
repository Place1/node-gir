#pragma once

#include <girepository.h>
#include <glib.h>
#include <v8.h>

namespace gir {

using namespace v8;

class GIRValue {
public:
    static GValue to_g_value(Local<Value> value, GType g_type);
    static Local<Value> from_g_value(const GValue *v, GITypeInfo *type_info);

    static void *to_c_array(Local<Value> value, GITypeInfo *type_info);
    static GArray *to_g_array(Local<Value> value, GITypeInfo *type_info);

private:
    static GType guess_type(Local<Value> value);
};

} // namespace gir
