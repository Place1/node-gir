#include "values.h"
#include "namespace_loader.h"
#include "util.h"

#include <cstdlib>
#include <sstream>
#include "arguments.h"
#include "exceptions.h"
#include "type.h"
#include "types/object.h"
#include "types/param_spec.h"
#include "types/struct.h"

using namespace v8;

namespace gir {

Local<Value> GIRValue::from_g_value(const GValue *gvalue, GITypeInfo *type_info) {
    switch (G_TYPE_FUNDAMENTAL(G_VALUE_TYPE(gvalue))) {
        case G_TYPE_CHAR: {
            char str[2];
            str[0] = g_value_get_schar(gvalue);
            str[1] = '\0';
            return Nan::New(str).ToLocalChecked();
        } break;

        case G_TYPE_UCHAR: {
            char str[2];
            str[0] = g_value_get_uchar(gvalue);
            str[1] = '\0';
            return Nan::New(str).ToLocalChecked();
        } break;

        case G_TYPE_BOOLEAN:
            return Nan::New<Boolean>(g_value_get_boolean(gvalue));

        case G_TYPE_INT:
            return Nan::New(g_value_get_int(gvalue));

        case G_TYPE_UINT:
            return Nan::New(g_value_get_uint(gvalue));

        case G_TYPE_LONG:
            return Nan::New<Number>(g_value_get_long(gvalue));

        case G_TYPE_ULONG:
            return Nan::New<Number>(g_value_get_ulong(gvalue));

        case G_TYPE_INT64:
            return Nan::New<Number>(g_value_get_int64(gvalue));

        case G_TYPE_UINT64:
            return Nan::New<Number>(g_value_get_uint64(gvalue));

        case G_TYPE_ENUM:
            return Nan::New(g_value_get_enum(gvalue));

        case G_TYPE_FLAGS:
            return Nan::New(g_value_get_flags(gvalue));

        case G_TYPE_FLOAT:
            return Nan::New(g_value_get_float(gvalue));

        case G_TYPE_DOUBLE:
            return Nan::New(g_value_get_double(gvalue));

        case G_TYPE_STRING:
            return Nan::New(g_value_get_string(gvalue)).ToLocalChecked();

        case G_TYPE_PARAM:
            return GIRParamSpec::from_existing(g_value_get_param(gvalue));

        case G_TYPE_BOXED:
            if (G_VALUE_TYPE(gvalue) == G_TYPE_ARRAY) {
                throw UnsupportedGValueType("GIRValue - GValueArray conversion not supported");
            } else {
                GIBaseInfo *boxed_info = g_irepository_find_by_gtype(g_irepository_get_default(), G_VALUE_TYPE(gvalue));
                return GIRStruct::from_existing((GIRStruct *)g_value_get_boxed(gvalue), boxed_info);
            }
            break;

        case G_TYPE_OBJECT: {
            GIBaseInfo *object_info = g_irepository_find_by_gtype(g_irepository_get_default(), G_VALUE_TYPE(gvalue));
            return GIRObject::from_existing(G_OBJECT(g_value_get_object(gvalue)), object_info);
        } break;

        default:
            stringstream message;
            message << "GIRValue - conversion of input type '" << g_type_name(G_VALUE_TYPE(gvalue))
                    << "' not supported";
            throw UnsupportedGValueType(message.str());
    }
}

// TODO: refactor to follow the style that Args::ToGType does
// i.e. return a GValue and throw std::exceptions on failure
GValue GIRValue::to_g_value(Local<Value> js_value, GType g_type) {
    GValue g_value = G_VALUE_INIT;

    if (g_type == G_TYPE_INVALID || g_type == 0) {
        g_type = GIRValue::guess_type(js_value);
    }

    if (g_type == G_TYPE_INVALID) {
        throw JSValueError("Could not guess the native value type from JS");
    }

    g_value_init(&g_value, g_type);

    switch (G_TYPE_FUNDAMENTAL(g_type)) {
        case G_TYPE_INTERFACE:
        case G_TYPE_OBJECT:
            g_value_set_object(&g_value, Nan::ObjectWrap::Unwrap<GIRObject>(js_value->ToObject())->get_gobject());
            break;

        case G_TYPE_CHAR: {
            String::Utf8Value value(js_value->ToString());
            g_value_set_schar(&g_value, (*value)[0]);
        } break;

        case G_TYPE_UCHAR: {
            String::Utf8Value value(js_value->ToString());
            g_value_set_uchar(&g_value, (*value)[0]);
        } break;

        case G_TYPE_BOOLEAN:
            g_value_set_boolean(&g_value, js_value->BooleanValue());
            break;

        case G_TYPE_INT:
            g_value_set_int(&g_value, js_value->Int32Value());
            break;

        case G_TYPE_UINT:
            g_value_set_uint(&g_value, js_value->Uint32Value());
            break;

        case G_TYPE_LONG:
            g_value_set_long(&g_value, js_value->NumberValue());
            break;

        case G_TYPE_ULONG:
            g_value_set_ulong(&g_value, js_value->NumberValue());
            break;

        case G_TYPE_INT64:
            g_value_set_int64(&g_value, js_value->IntegerValue());
            break;

        case G_TYPE_UINT64:
            g_value_set_uint64(&g_value, js_value->IntegerValue());
            break;

        case G_TYPE_ENUM:
            g_value_set_enum(&g_value, js_value->IntegerValue());
            break;

        case G_TYPE_FLAGS:
            g_value_set_flags(&g_value, js_value->IntegerValue());
            break;

        case G_TYPE_FLOAT:
            g_value_set_float(&g_value, js_value->NumberValue());
            break;

        case G_TYPE_DOUBLE:
            g_value_set_double(&g_value, js_value->NumberValue());
            break;

        case G_TYPE_STRING: {
            String::Utf8Value value(js_value->ToString());
            g_value_set_string(&g_value, *value);
        } break;

        case G_TYPE_BOXED:
            if (g_type_is_a(g_type, G_TYPE_VALUE)) {
                // we have a special case for GValue itself. If we need to convert a JS
                // value into a GValue we must guess the native type. The easiest
                // way to do that is to just call this same function (to_g_value).
                // GIRValue::guess_type can't return G_TYPE_VALUE so we're save
                // from infinite recursion.
                return GIRValue::to_g_value(js_value, GIRValue::guess_type(js_value));
            } else {
                // otherwise we expect the js value to be a GIRStruct
                // FIXME: error handling for invalid unwraps
                GIRStruct *gir_struct = Nan::ObjectWrap::Unwrap<GIRStruct>(js_value->ToObject());
                g_value_set_boxed(&g_value, gir_struct->get_native_ptr());
            }
            break;

        case G_TYPE_PARAM:
        case G_TYPE_POINTER: {
            stringstream message;
            message << "Native value type '" << g_type_name(g_type) << "' is not yet supported";
            throw UnsupportedGValueType(message.str());
        } break;

        default:
            throw JSValueError("Failed to convert value");
            break;
    }
    return g_value;
}

void *GIRValue::to_c_array(Local<Value> value, GITypeInfo *type_info) {
    bool is_zero_terminated = g_type_info_is_zero_terminated(type_info);

    if (value->IsString()) {
        Local<String> string = value->ToString();
        const char *utf8_data = *Nan::Utf8String(string);
        return g_strdup(utf8_data);
    }

    if (!value->IsArray()) {
        throw JSValueError("expected value to be an array");
    }

    Local<Array> array = Local<Array>::Cast(value->ToObject());
    int length = array->Length();

    auto element_info = GIRInfoUniquePtr(g_type_info_get_param_type(type_info, 0));
    gsize element_size = get_type_size(element_info.get());

    void *result = malloc(element_size * (length + (is_zero_terminated ? 1 : 0)));

    for (int i = 0; i < length; i++) {
        auto value = array->Get(i);
        GIArgument arg = Args::type_to_g_type(*element_info.get(), value);
        void *pointer = (void *)((ulong)result + i * element_size);
        memcpy(pointer, &arg, element_size);
    }

    if (is_zero_terminated) {
        void *pointer = (void *)((ulong)result + length * element_size);
        memset(pointer, 0, element_size);
    }

    return result;
}

GArray *GIRValue::to_g_array(Local<Value> value, GITypeInfo *type_info) {
    GArray* g_array = nullptr;
    bool zero_terminated = g_type_info_is_zero_terminated(type_info);

    if (value->IsString()) {
        Local<String> string = value->ToString();
        int length = string->Length();

        if (length == 0) {
            return g_array_new(zero_terminated, true, sizeof(char));
        }

        const char *utf8_data = *Nan::Utf8String(string);
        g_array = g_array_sized_new (zero_terminated, false, sizeof (char), length);
        return g_array_append_vals(g_array, utf8_data, length);

    } else if (value->IsArray ()) {
        auto array = Local<Array>::Cast (value->ToObject ());
        int length = array->Length ();

        auto element_info = GIRInfoUniquePtr(g_type_info_get_param_type(type_info, 0));
        gsize element_size = get_type_size(element_info.get());

        // FIXME this is so wrong
        g_array = g_array_sized_new(zero_terminated, true, element_size, length);

        for (int i = 0; i < length; i++) {
            auto value = array->Get(i);
            GIArgument arg = Args::type_to_g_type(*element_info.get(), value);
            g_array_append_val(g_array, arg);
        }
    } else {
        throw JSValueError("expected an array");
    }

    return g_array;
}

GType GIRValue::guess_type(Handle<Value> value) {
    if (value->IsString()) {
        return G_TYPE_STRING;
    }

    if (value->IsArray()) {
        return G_TYPE_ARRAY;
    }

    if (value->IsBoolean()) {
        return G_TYPE_BOOLEAN;
    }

    if (value->IsInt32()) {
        return G_TYPE_INT;
    }

    if (value->IsUint32()) {
        return G_TYPE_UINT;
    }

    if (value->IsNumber()) {
        return G_TYPE_DOUBLE;
    }

    if (value->IsNull()) {
        return G_TYPE_POINTER;
    }

    return G_TYPE_INVALID;
}

} // namespace gir
