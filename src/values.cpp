#include "values.h"
#include "namespace_loader.h"
#include "util.h"

#include <cstdlib>
#include "types/object.h"
#include "types/struct.h"

using namespace v8;

namespace gir {

Handle<Value> GIRValue::from_g_value(GValue *v, GIBaseInfo *base_info) {
    GType type = G_VALUE_TYPE(v);
    Handle<Value> value = Nan::Undefined();
    const char *tmpstr;
    char *str;
    GIBaseInfo *boxed_info;

    switch (G_TYPE_FUNDAMENTAL(type)) {
        case G_TYPE_CHAR:
            str = new char[2];
            str[0] = g_value_get_schar(v);
            str[1] = '\0';
            value = Nan::New<String>(str).ToLocalChecked();
            delete[] str;
            return value;

        case G_TYPE_UCHAR:
            str = new char[2];
            str[0] = g_value_get_uchar(v);
            str[1] = '\0';
            value = Nan::New<String>(str).ToLocalChecked();
            delete[] str;
            return value;

        case G_TYPE_BOOLEAN:
            return Nan::New<Boolean>(g_value_get_boolean(v));

        case G_TYPE_INT:
            return Nan::New<Number>(g_value_get_int(v));

        case G_TYPE_UINT:
            return Nan::New<Number>(g_value_get_uint(v));

        case G_TYPE_LONG:
            return Nan::New<Number>(g_value_get_long(v));

        case G_TYPE_ULONG:
            return Nan::New<Number>(g_value_get_ulong(v));

        case G_TYPE_INT64:
            return Nan::New<Number>(g_value_get_int64(v));

        case G_TYPE_UINT64:
            return Nan::New<Number>(g_value_get_uint64(v));

        case G_TYPE_ENUM:
            return Nan::New<Number>(g_value_get_enum(v));

        case G_TYPE_FLAGS:
            return Nan::New<Number>(g_value_get_flags(v));

        case G_TYPE_FLOAT:
            return Nan::New<Number>(g_value_get_float(v));

        case G_TYPE_DOUBLE:
            return Nan::New<Number>(g_value_get_double(v));

        case G_TYPE_STRING:
            tmpstr = g_value_get_string(v);
            return Nan::New<String>(tmpstr ? tmpstr : "").ToLocalChecked();

        case G_TYPE_BOXED:
            if (G_VALUE_TYPE(v) == G_TYPE_ARRAY) {
                Nan::ThrowError("GIRValue - GValueArray conversion not supported");
            } else {
                // Handle C structure held by boxed type
                if (base_info == nullptr)
                    Nan::ThrowError("GIRValue - missed base_info for boxed type");
                boxed_info = g_irepository_find_by_gtype(g_irepository_get_default(), G_VALUE_TYPE(v));
                return GIRStruct::from_existing((GIRStruct *)g_value_get_boxed(v), boxed_info);
            }
            break;

        case G_TYPE_OBJECT:
            return GIRObject::from_existing(G_OBJECT(g_value_get_object(v)), type);

        default:
            Nan::ThrowError("GIRValue - conversion of '%s' type not supported");
    }

    return value;
}

// TODO: refactor to follow the style that Args::ToGType does
// i.e. return a GValue and throw std::exceptions on failure
bool GIRValue::to_g_value(Handle<Value> value, GType type, GValue *v) {
    if (type == G_TYPE_INVALID || type == 0) {
        type = GIRValue::guess_type(value);
    }
    if (type == 0) {
        return false;
    }

    g_value_init(v, type);

    switch (G_TYPE_FUNDAMENTAL(type)) {
        case G_TYPE_INTERFACE:
        case G_TYPE_OBJECT:
            if (value->IsObject()) {
                g_value_set_object(v, Nan::ObjectWrap::Unwrap<GIRObject>(value->ToObject())->get_gobject());
                return true;
            }
            break;

        case G_TYPE_CHAR:
            if (value->IsString()) {
                String::Utf8Value str(value);
                g_value_set_schar(v, (*str)[0]);
                return true;
            }
            break;

        case G_TYPE_UCHAR:
            if (value->IsString()) {
                String::Utf8Value str(value);
                g_value_set_schar(v, (*str)[0]);
                return true;
            }
            break;

        case G_TYPE_BOOLEAN:
            if (value->IsBoolean()) {
                g_value_set_boolean(v, value->ToBoolean()->IsTrue());
                return true;
            }
            break;

        case G_TYPE_INT:
            if (value->IsNumber()) {
                g_value_set_int(v, Nan::To<int32_t>(value).FromJust());
                return true;
            }
            break;

        case G_TYPE_UINT:
            if (value->IsNumber()) {
                g_value_set_uint(v, value->NumberValue());
                return true;
            }
            break;

        case G_TYPE_LONG:
            if (value->IsNumber()) {
                g_value_set_long(v, value->NumberValue());
                return true;
            }
            break;

        case G_TYPE_ULONG:
            if (value->IsNumber()) {
                g_value_set_ulong(v, value->NumberValue());
                return true;
            }
            break;

        case G_TYPE_INT64:
            if (value->IsNumber()) {
                g_value_set_int64(v, value->NumberValue());
                return true;
            }
            break;

        case G_TYPE_UINT64:
            if (value->IsNumber()) {
                g_value_set_uint64(v, value->NumberValue());
                return true;
            }
            break;

        case G_TYPE_ENUM:
            if (value->IsNumber()) {
                g_value_set_enum(v, value->NumberValue());
                return true;
            }
            break;

        case G_TYPE_FLAGS:
            if (value->IsNumber()) {
                g_value_set_flags(v, value->NumberValue());
                return true;
            }
            break;

        case G_TYPE_FLOAT:
            if (value->IsNumber()) {
                g_value_set_float(v, value->NumberValue());
                return true;
            }
            break;

        case G_TYPE_DOUBLE:
            if (value->IsNumber()) {
                g_value_set_double(v, (gdouble)value->NumberValue());
                return true;
            }
            break;

        case G_TYPE_STRING:
            if (value->IsString()) {
                g_value_set_string(v, *String::Utf8Value(value->ToString()));
                return true;
            }
            break;

        case G_TYPE_POINTER:
            break;

        case G_TYPE_BOXED:
            g_value_set_boxed(v, Nan::ObjectWrap::Unwrap<GIRStruct>(value->ToObject())->c_structure);
            return true;

        case G_TYPE_PARAM:
            break;

        default:
            Nan::ThrowError("Failed to convert value");
            return false;
    }
    return false;
}

GType GIRValue::guess_type(Handle<Value> value) {
    if (value->IsString()) {
        return G_TYPE_STRING;
    } else if (value->IsArray()) {
        return G_TYPE_ARRAY;
    } else if (value->IsBoolean()) {
        return G_TYPE_BOOLEAN;
    } else if (value->IsInt32()) {
        return G_TYPE_INT;
    } else if (value->IsUint32()) {
        return G_TYPE_UINT;
    } else if (value->IsNumber()) {
        return G_TYPE_DOUBLE;
    }
    return G_TYPE_INVALID;
}

} // namespace gir
