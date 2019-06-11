#include "arguments.h"

#include <cstring>
#include <sstream>
#include <vector>
#include "closure.h"
#include "exceptions.h"
#include "type.h"
#include "types/object.h"
#include "types/struct.h"
#include "util.h"
#include "values.h"

namespace gir {

using namespace v8;

Args::Args(GICallableInfo *callable_info) : callable_info(callable_info) {
    g_base_info_ref(callable_info); // because we keep a reference to the info we need to tell glib
}

/**
 * This method, given a JS function call object will load each JS argument
 * into Args datastructure (in and out arguments). Each JS argument value
 * is converted to it's native equivalient. This method will also handle
 * native "out" arguments even though they aren't passed in via JS function
 * calls.
 * @param js_callback_info is a JS function call info object
 */
void Args::load_js_arguments(const Nan::FunctionCallbackInfo<v8::Value> &js_callback_info) {
    // get the number of arguments the native function requires
    guint8 gi_argc = g_callable_info_get_n_args(this->callable_info.get());

    // for every expected native argument, we'll take a given JS argument and
    // convert it into a GIArgument, adding it to the in/out args array depending
    // on it's direction.
    for (guint8 i = 0; i < gi_argc; i++) {
        GIArgInfo *argument_info = g_callable_info_get_arg(this->callable_info.get(), i);

        switch (g_arg_info_get_direction(argument_info)) {
            case GI_DIRECTION_IN:
                this->params.push_back(new InParameter(Args::arg_to_g_type(*argument_info, js_callback_info[i])));
                break;

            case GI_DIRECTION_OUT:
                this->params.push_back(new OutParameter(argument_info));
                break;

            case GI_DIRECTION_INOUT:
                this->params.push_back(new InOutParameter(Args::arg_to_g_type(*argument_info, js_callback_info[i])));
                break;
        }

        // if (argument_direction == GI_DIRECTION_OUT) {
        //     GIArgument argument =
        //     this->out.push_back(argument);
        //     this->all.push_back(argument);
        // }

        // if (argument_direction == GI_DIRECTION_INOUT) {
        //     GIArgument argument = Args::arg_to_g_type(argument_info, js_callback_info[i]);
        //     this->in.push_back(argument);

        //     // TODO: is it correct to handle INOUT arguments like IN args?
        //     // do we need to handle callee (native) allocates or empty input
        //     // GIArguments like we do with OUT args? i'm just assuming this is how it
        //     // should work (treating it like an IN arg). Hopfully I can find some
        //     // examples to make some test cases asserting the correct behaviour
        //     this->out.push_back(argument);

        //     this->all.push_back(argument);
        // }
    }
}

/**
 * This function loads the context (i.e. this value of `this`) into the native call arguments.
 * By convention, the context value (a GIRObject in JS or a GObject in native) is put at the
 * start (position 0) of the function call's "in" arguments.
 */
void Args::load_context(GObject *this_object) {
    GIArgument this_object_argument = {
        .v_pointer = this_object,
    };
    this->params.insert(this->params.begin(), new InParameter(this_object_argument));
}

vector<GIArgument> Args::get_in_args() {
    vector<Parameter *> in_params;
    copy_if(this->params.begin(), this->params.end(), back_inserter(in_params), [](auto param) {
        return Util::instance_of<InParameter>(param) || Util::instance_of<InOutParameter>(param);
    });
    vector<GIArgument> in_args;
    transform(in_params.begin(), in_params.end(), back_inserter(in_args), [](auto param) {
        return param->get_argument();
    });
    return in_args;
}

vector<GIArgument> Args::get_out_args() {
    vector<Parameter *> out_params;
    copy_if(this->params.begin(), this->params.end(), back_inserter(out_params), [](auto param) {
        return Util::instance_of<OutParameter>(param) || Util::instance_of<InOutParameter>(param);
    });
    vector<GIArgument> in_args;
    transform(out_params.begin(), out_params.end(), back_inserter(in_args), [](auto param) {
        return param->get_argument();
    });
    return in_args;
}

vector<GIArgument> Args::get_all_args() {
    vector<GIArgument> args;
    transform(this->params.begin(), this->params.end(), back_inserter(args), [](auto param) {
        return param->get_argument();
    });
    return args;
}

GIArgument Args::arg_to_g_type(GIArgInfo &argument_info, Local<Value> js_value) {
    GITypeInfo argument_type_info;
    g_arg_info_load_type(&argument_info, &argument_type_info);
    GITypeTag argument_type_tag = g_type_info_get_tag(&argument_type_info);

    if (js_value->IsNullOrUndefined()) {
        if (g_arg_info_may_be_null(&argument_info) || argument_type_tag == GI_TYPE_TAG_VOID) {
            GIArgument argument_value;
            argument_value.v_pointer = nullptr;
            argument_value.v_string = nullptr;
            return argument_value;
        }
        stringstream message;
        message << "Argument '" << g_base_info_get_name(&argument_info) << "' may not be null or undefined";
        throw JSArgumentTypeError(message.str());
    }

    try {
        return Args::type_to_g_type(argument_type_info, js_value);
    } catch (JSArgumentTypeError &error) {
        // we want to nicely format all type errors so we'll catch them and rethrow
        // using a nice message
        Nan::Utf8String js_type_name(js_value->TypeOf(Isolate::GetCurrent()));
        stringstream message;
        message << "Expected type '" << g_type_tag_to_string(argument_type_tag);
        message << "' for Argument '" << g_base_info_get_name(&argument_info);
        message << "' but got type '" << *js_type_name << "'";
        throw JSArgumentTypeError(message.str());
    }
}

GIArgument Args::type_to_g_type(GITypeInfo &argument_type_info, Local<Value> js_value) {
    GITypeTag argument_type_tag = g_type_info_get_tag(&argument_type_info);

    // if the arg type is a GTYPE (which is an integer)
    // then we want to pretend it's a GI_TYPE_TAG_INTX
    // where x is the sizeof the GTYPE. This helper function
    // does the mapping for us.
    if (argument_type_tag == GI_TYPE_TAG_GTYPE) {
        argument_type_tag = Args::map_g_type_tag(argument_type_tag);
    }

    // this is what we'll return after we correctly
    // set it's values depending on the argument_type_tag
    // also, we'll make sure it's pointers are null initially!
    GIArgument argument_value;
    argument_value.v_pointer = nullptr;
    argument_value.v_string = nullptr;

    switch (argument_type_tag) {
        case GI_TYPE_TAG_VOID:
            argument_value.v_pointer = nullptr;
            break;

        case GI_TYPE_TAG_BOOLEAN:
            argument_value.v_boolean = js_value->ToBoolean()->Value();
            break;

        case GI_TYPE_TAG_INT8:
            argument_value.v_uint8 = js_value->NumberValue();
            break;

        case GI_TYPE_TAG_UINT8:
            argument_value.v_uint8 = js_value->NumberValue();
            break;

        case GI_TYPE_TAG_INT16:
            argument_value.v_int16 = js_value->NumberValue();
            break;

        case GI_TYPE_TAG_UINT16:
            argument_value.v_uint16 = js_value->NumberValue();
            break;

        case GI_TYPE_TAG_INT32:
            argument_value.v_int32 = js_value->Int32Value();
            break;

        case GI_TYPE_TAG_UINT32:
            argument_value.v_uint32 = js_value->Uint32Value();
            break;

        case GI_TYPE_TAG_INT64:
            argument_value.v_int64 = js_value->IntegerValue();
            break;

        case GI_TYPE_TAG_UINT64:
            argument_value.v_uint64 = js_value->IntegerValue();
            break;

        case GI_TYPE_TAG_FLOAT:
            argument_value.v_float = js_value->NumberValue();
            break;

        case GI_TYPE_TAG_DOUBLE:
            argument_value.v_double = js_value->NumberValue();
            break;

        case GI_TYPE_TAG_ARRAY:
            return Args::to_g_type_array(js_value, &argument_type_info);

        case GI_TYPE_TAG_UTF8:
        case GI_TYPE_TAG_FILENAME:
            if (!js_value->IsString()) {
                throw JSArgumentTypeError();
            } else {
                Nan::Utf8String js_string(js_value->ToString());
                // FIXME: memory leak as we never g_free(.v_string)
                // ideally the memory would be freed when the GIArgument
                // was destroyed. Perhaps we need to use a unique_ptr
                // with a custom deleter?
                // I think it's clear that the memory needs to be owned by
                // the GIArgument (or value ToGType returns) because
                // we can't expect callers to know they are responsible
                // for deallocation, and we can't expect to borrow the
                // memory (if we don't copy the string then tests start
                // failing with corrupt memory rather than the original strings).
                argument_value.v_string = strdup(*js_string);
            }
            break;

        case GI_TYPE_TAG_INTERFACE: {
            auto interface_info = GIRInfoUniquePtr(g_type_info_get_interface(&argument_type_info));
            GIInfoType interface_type = g_base_info_get_type(interface_info.get());

            switch (interface_type) {
                case GI_INFO_TYPE_OBJECT:
                    // if the interface type is an object, then we expect
                    // the JS value to be a GIRObject so we can unwrap it
                    // and pass the GObject pointer to the GIArgument's v_pointer.
                    if (!js_value->IsObject()) {
                        throw JSArgumentTypeError();
                    } else {
                        GIRObject *gir_object = Nan::ObjectWrap::Unwrap<GIRObject>(js_value->ToObject());
                        argument_value.v_pointer = gir_object->get_gobject();
                    }
                    break;

                case GI_INFO_TYPE_VALUE:
                case GI_INFO_TYPE_STRUCT:
                case GI_INFO_TYPE_UNION:
                case GI_INFO_TYPE_BOXED: {
                    GType g_type = g_registered_type_info_get_g_type(interface_info.get());
                    if (g_type_is_a(g_type, G_TYPE_VALUE)) {
                        GValue gvalue = GIRValue::to_g_value(js_value, g_type);
                        argument_value.v_pointer = g_boxed_copy(g_type, &gvalue); // FIXME: should we copy? where do
                                                                                  // we deallocate?
                    } else {
                        GIRStruct *gir_struct = Nan::ObjectWrap::Unwrap<GIRStruct>(js_value->ToObject());
                        argument_value.v_pointer = gir_struct->get_native_ptr();
                    }
                } break;

                case GI_INFO_TYPE_FLAGS:
                case GI_INFO_TYPE_ENUM:
                    argument_value.v_int = js_value->IntegerValue();
                    break;

                case GI_INFO_TYPE_CALLBACK:
                    if (js_value->IsFunction()) {
                        auto closure = GIRClosure::create_ffi(interface_info.get(), js_value.As<Function>());
                        argument_value.v_pointer = closure;
                    } else {
                        throw JSArgumentTypeError();
                    }
                    break;

                default:
                    stringstream message;
                    message << "argument's with interface type \"" << g_info_type_to_string(interface_type)
                            << "\" is unsupported.";
                    throw UnsupportedGIType(message.str());
            }
        } break;

        default:
            stringstream message;
            message << "argument type \"" << g_type_tag_to_string(argument_type_tag) << "\" is unsupported.";
            throw UnsupportedGIType(message.str());
    }

    return argument_value;
}

int Args::get_g_type_array_length(GICallableInfo *callable_info,
                                  vector<GIArgument> call_args,
                                  GIArgument *array_arg,
                                  GITypeInfo *array_arg_type) {
    int length_arg_pos = g_type_info_get_array_length(array_arg_type);
    if (length_arg_pos == -1) {
        return -1;
    }
    GIArgInfo length_arg;
    g_callable_info_load_arg(callable_info, length_arg_pos, &length_arg);
    GIDirection length_direction = g_arg_info_get_direction(&length_arg);
    if (length_direction == GI_DIRECTION_OUT || length_direction == GI_DIRECTION_INOUT) {
        return *static_cast<int *>(call_args[length_arg_pos].v_pointer);
    }
    return call_args[length_arg_pos].v_int;
}

Local<Value> Args::from_g_type_array(GIArgument *arg, GITypeInfo *type, int array_length) {
    GIArrayType array_type_info = g_type_info_get_array_type(type);
    auto element_type_info = GIRInfoUniquePtr(g_type_info_get_param_type(type, 0));

    void *native_array = arg->v_pointer;
    int length = array_length;
    gsize element_size = get_type_size(element_type_info.get());

    switch (array_type_info) {
        case GI_ARRAY_TYPE_C:
            if (length == -1) {
                // first we handle the case where there was no array length
                // provided when calling this method.
                // this is the case when the array comes from a function call
                // that doesn't have an out argument for the array's length.
                // i.e. it returns a null terminated array or a fixed length array.
                if (g_type_info_is_zero_terminated(type)) {
                    // if the array is null terminated we can use a standard
                    // string length method to find the array length
                    length = g_strv_length(static_cast<gchar **>(native_array));
                } else {
                    // otherwise, if there's no length (length == -1) and
                    // the array wasn't null terminated, it must have a fixed
                    // size.
                    length = g_type_info_get_array_fixed_size(type);
                    if (length == -1) {
                        // otherwise, if the array wasn't fixed size then we've exhausted
                        // all the ways we available to find the array length.
                        stringstream message;
                        message << "unable to determine array length for C array";
                        throw UnsupportedGIType(message.str());
                    }
                }
            }
            break;
        case GI_ARRAY_TYPE_ARRAY:
        case GI_ARRAY_TYPE_BYTE_ARRAY: {
            GArray *g_array = static_cast<GArray *>(native_array);
            native_array = g_array->data;
            length = g_array->len;
            element_size = g_array_get_element_size(g_array);
        } break;
        case GI_ARRAY_TYPE_PTR_ARRAY: {
            GPtrArray *ptr_array = static_cast<GPtrArray *>(native_array);
            native_array = ptr_array->pdata;
            length = ptr_array->len;
            element_size = sizeof(gpointer);
            break;
        }
        default:
            throw UnsupportedGIType("cannot convert native array type");
    }

    if (native_array == nullptr || length == 0) {
        return Nan::New<Array>(); // an empty array
    }

    GIArgument element;
    Local<Array> js_array = Nan::New<Array>();
    for (int i = 0; i < length; i++) {
        // void** pointer = static_cast<void**>(static_cast<ulong>(native_array) + i * element_size);
        void **pointer = static_cast<void **>(native_array + i * element_size);
        memcpy(&element, pointer, element_size);
        Nan::Set(js_array, i, Args::from_g_type(&element, element_type_info.get()));
    }
    return js_array;
}

GIArgument Args::to_g_type_array(Local<Value> value, GITypeInfo *info) {
    GIArrayType array_type = g_type_info_get_array_type(info);
    GIArgument arg;
    switch (array_type) {
        case GI_ARRAY_TYPE_C:
            // TODO: what deallocates the memory created by GIRValue::to_c_array
            arg.v_pointer = GIRValue::to_c_array(value, info);
            break;
        case GI_ARRAY_TYPE_ARRAY:
        case GI_ARRAY_TYPE_BYTE_ARRAY:
            // TODO: what deallocates the memory created by GIRValue::to_g_array
            arg.v_pointer = GIRValue::to_g_array(value, info);
            break;
        default:
            throw UnsupportedGIType("unsupported array type");
    }
    return arg;
}

// TODO: refactor this function and most of the code below this.
// can we reuse code from GIRValue?
Local<Value> Args::from_g_type(GIArgument *arg, GITypeInfo *type, int array_length) {
    GITypeTag tag = g_type_info_get_tag(type);

    switch (tag) {
        case GI_TYPE_TAG_VOID:
            return Nan::Undefined();

        case GI_TYPE_TAG_BOOLEAN:
            return Nan::New<Boolean>(arg->v_boolean);

        case GI_TYPE_TAG_INT8:
            return Nan::New(arg->v_int8);

        case GI_TYPE_TAG_UINT8:
            return Nan::New(arg->v_uint8);

        case GI_TYPE_TAG_INT16:
            return Nan::New(arg->v_int16);

        case GI_TYPE_TAG_UINT16:
            return Nan::New(arg->v_uint16);

        case GI_TYPE_TAG_INT32:
            return Nan::New(arg->v_int32);

        case GI_TYPE_TAG_UINT32:
            return Nan::New(arg->v_uint32);

        case GI_TYPE_TAG_INT64:
            // the ECMA script spec doesn't support int64 values
            // and V8 internally stores them as doubles.
            // This will lose precision if the original value
            // has more than 53 significant binary digits.
            return Nan::New(static_cast<double>(arg->v_int64));

        case GI_TYPE_TAG_UINT64:
            // the ECMA script spec doesn't support uint64 values
            // and V8 internally stores them as doubles.
            // This will lose precision if the original value
            // has more than 53 significant binary digits.
            return Nan::New(static_cast<double>(arg->v_uint64));

        case GI_TYPE_TAG_FLOAT:
            return Nan::New(arg->v_float);

        case GI_TYPE_TAG_DOUBLE:
            return Nan::New(arg->v_double);

        case GI_TYPE_TAG_GTYPE:
            return Nan::New(arg->v_uint);

        case GI_TYPE_TAG_UTF8:
            return Nan::New(arg->v_string).ToLocalChecked();

        case GI_TYPE_TAG_FILENAME:
            return Nan::New(arg->v_string).ToLocalChecked();

        case GI_TYPE_TAG_ARRAY:
            return Args::from_g_type_array(arg, type, array_length);

        case GI_TYPE_TAG_INTERFACE: {
            GIBaseInfo *interface_info = g_type_info_get_interface(type);
            GIInfoType interface_type = g_base_info_get_type(interface_info);
            switch (interface_type) {
                case GI_INFO_TYPE_OBJECT:
                    if (arg->v_pointer == nullptr) {
                        return Nan::Null();
                    }
                    return GIRObject::from_existing(G_OBJECT(arg->v_pointer), interface_info);

                case GI_INFO_TYPE_INTERFACE:
                case GI_INFO_TYPE_UNION:
                case GI_INFO_TYPE_STRUCT:
                case GI_INFO_TYPE_BOXED:
                    if (arg->v_pointer == nullptr) {
                        return Nan::Null();
                    }
                    return GIRStruct::from_existing(arg->v_pointer, interface_info);

                case GI_INFO_TYPE_VALUE:
                    return GIRValue::from_g_value(static_cast<GValue *>(arg->v_pointer), nullptr);

                case GI_INFO_TYPE_FLAGS:
                case GI_INFO_TYPE_ENUM:
                    return Nan::New(arg->v_int);

                // unsupported ...
                default:
                    stringstream message;
                    message << "cannot convert '" << g_info_type_to_string(interface_type) << "' to a JS value";
                    throw UnsupportedGIType(message.str());
            }
        } break;

        case GI_TYPE_TAG_GLIST:
            return Nan::Undefined();
        case GI_TYPE_TAG_GSLIST:
            return Nan::Undefined();
        case GI_TYPE_TAG_GHASH:
            return Nan::Undefined();
        case GI_TYPE_TAG_ERROR:
            return Nan::Undefined();
        case GI_TYPE_TAG_UNICHAR:
            return Nan::Undefined();
        default:
            return Nan::Undefined();
    }
}

GITypeTag Args::map_g_type_tag(GITypeTag type) {
    if (type == GI_TYPE_TAG_GTYPE) {
        switch (sizeof(GType)) {
            case 1:
                return GI_TYPE_TAG_UINT8;
            case 2:
                return GI_TYPE_TAG_UINT16;
            case 4:
                return GI_TYPE_TAG_UINT32;
            case 8:
                return GI_TYPE_TAG_UINT64;
            default:
                g_assert_not_reached();
        }
    }
    return type;
}

} // namespace gir
