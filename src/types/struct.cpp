#include <cstring>
#include <iostream>
#include <sstream>

#include "arguments.h"
#include "function.h"
#include "struct.h"
#include "util.h"
#include "values.h"

#include <nan.h>
#include <node.h>

namespace gir {

using namespace v8;
using namespace std;

PersistentObjectStore<GType, PersistentFunctionTemplate> GIRStruct::prepared_js_classes;

gpointer GIRStruct::get_native_ptr() {
    return this->boxed_c_structure;
}

Local<Value> GIRStruct::from_existing(gpointer c_structure, GIStructInfo *info) {
    GType gtype = g_registered_type_info_get_g_type(info);
    Local<Function> klass;
    if (GIRStruct::prepared_js_classes.exists(gtype)) {
        PersistentFunctionTemplate cached_js_class = GIRStruct::prepared_js_classes.at(gtype);
        auto function_template = Nan::New(cached_js_class);
        klass = function_template->GetFunction();
    } else {
        klass = GIRStruct::prepare(info);
    }
    Local<Object> instance = Nan::NewInstance(klass).ToLocalChecked();
    GIRStruct *gir_struct = Nan::ObjectWrap::Unwrap<GIRStruct>(instance);
    if (g_base_info_get_type(info) == GI_INFO_TYPE_BOXED) {
        // copy the boxed value
        gir_struct->boxed_c_structure = g_boxed_copy(gtype, c_structure);
    } else {
        // allocate directly and copy the struct
        gsize struct_size = g_struct_info_get_size(info);
        gir_struct->boxed_c_structure = g_slice_alloc0(struct_size);
        gir_struct->slice_allocated = true;
        memcpy(gir_struct->boxed_c_structure, c_structure, struct_size);
    }
    return instance;
}

GIRStruct::~GIRStruct() {
    if (this->boxed_c_structure != nullptr && this->struct_info != nullptr) {
        if (this->slice_allocated) {
            g_slice_free1(g_struct_info_get_size(this->struct_info.get()), this->boxed_c_structure);
        } else {
            GType boxed_type = g_registered_type_info_get_g_type(this->struct_info.get());
            g_boxed_free(boxed_type, this->boxed_c_structure);
        }
    }
}

Local<Function> GIRStruct::prepare(GIStructInfo *info) {
    char *name = (char *)g_base_info_get_name(info);
    const char *namespace_ = g_base_info_get_namespace(info);
    g_base_info_ref(info);

    // create a v8 external to reference the GIStructInfo
    Local<External> struct_info_extern = Nan::New<External>((void *)g_base_info_ref(info));

    // create the struct's constructor
    // GIRStruct::constructor is expecting the GIStructInfo to be attached
    // to the JS function (constructor)
    Local<FunctionTemplate> object_template = Nan::New<FunctionTemplate>(GIRStruct::constructor, struct_info_extern);
    GIRStruct::prepared_js_classes.insert(
        make_pair(g_registered_type_info_get_g_type(info), PersistentFunctionTemplate(object_template)));

    object_template->SetClassName(Nan::New(name).ToLocalChecked());

    // Create instance template
    v8::Local<v8::ObjectTemplate> object_instance_template = object_template->InstanceTemplate();
    object_instance_template->SetInternalFieldCount(1);

    SetNamedPropertyHandler(object_instance_template,
                            GIRStruct::property_get_handler,
                            GIRStruct::property_set_handler,
                            GIRStruct::property_query_handler);

    GIRStruct::register_methods(info, namespace_, object_template);

    return object_template->GetFunction();
}

void GIRStruct::register_methods(GIStructInfo *info, const char *namespace_, Handle<FunctionTemplate> object_template) {
    int number_of_methods = g_struct_info_get_n_methods(info);
    for (int i = 0; i < number_of_methods; i++) {
        GIFunctionInfo *func = g_struct_info_get_method(info, i);
        const char *native_func_name = g_base_info_get_name(func);
        string js_func_name = Util::to_camel_case(string(native_func_name));
        Local<String> function_name = Nan::New(js_func_name.c_str()).ToLocalChecked();
        GIFunctionInfoFlags func_flag = g_function_info_get_flags(func);

        if ((func_flag & GI_FUNCTION_IS_CONSTRUCTOR)) {
            Local<FunctionTemplate> callback_func = GIRFunction::create_function(func);
            object_template->Set(function_name, callback_func);
        } else {
            // TODO: refactor GIRFunction::CreateMethod() to support more than GIRObject so
            // we can reuse that logic in here and keep is DRY!
            Local<External> function_info_extern = Nan::New<External>((void *)g_base_info_ref(func));
            Local<FunctionTemplate> method_template = Nan::New<FunctionTemplate>(GIRStruct::call_method,
                                                                                 function_info_extern);
            object_template->PrototypeTemplate()->Set(function_name, method_template);
        }
        g_base_info_unref(func);
    }
}

/**
 * This method finds a struct's constructor method.
 * I.e. a method that is flagged as a GI_FUNCTION_IS_CONSTRUCTOR
 * and has 0 arguments.
 * If this method isn't found, then it returns any method named "new".
 * Otherwise it returns a nullptr (no default constructor).
 */
GIRInfoUniquePtr GIRStruct::find_native_constructor(GIStructInfo *struct_info) {
    int num_methods = g_struct_info_get_n_methods(struct_info);

    // look for a 0 argument constructor method, return it if one exists
    for (int i = 0; i < num_methods; i++) {
        auto function_info = GIRInfoUniquePtr(g_struct_info_get_method(struct_info, i));
        GIFunctionInfoFlags flags = g_function_info_get_flags(function_info.get());
        if (flags & GI_FUNCTION_IS_CONSTRUCTOR) { // if the function is a constructor
            if (g_callable_info_get_n_args(function_info.get()) == 0) {
                return function_info;
            }
        }
    }

    // otherwise look for a default constructor
    // it'll be a nullptr if it doesn't exist
    return GIRInfoUniquePtr(g_struct_info_find_method(struct_info, "new"));
}

NAN_METHOD(GIRStruct::constructor) {
    Local<External> struct_info_extern = Local<External>::Cast(info.Data());
    GIStructInfo *struct_info = (GIStructInfo *)struct_info_extern->Value();
    GIRStruct *obj = new GIRStruct();
    obj->struct_info = GIRInfoUniquePtr(struct_info);

    GIRInfoUniquePtr func = GIRStruct::find_native_constructor(struct_info);
    if (func != nullptr) {
        try {
            Args args = Args(func.get());
            args.load_js_arguments(info);
            GIArgument result = GIRFunction::call_native(func.get(), args);
            obj->boxed_c_structure = result.v_pointer;
        } catch (exception &error) {
            Nan::ThrowError(error.what());
            return;
        }
    } else {
        obj->boxed_c_structure = g_slice_alloc0(g_struct_info_get_size(struct_info));
        obj->slice_allocated = true;
    }

    obj->Wrap(info.This());

    // if we allocated the struct directly and if a 'properties'
    // object was passed to the constructor, then use the object
    // to set inital values for properties on the struct
    if (obj->slice_allocated && info.Length() == 1 && info[0]->IsObject()) {
        Local<Object> properties = info[0]->ToObject();
        Local<Array> property_names = properties->GetPropertyNames();
        for (size_t i = 0; i < property_names->Length(); i++) {
            Local<String> property_name = property_names->Get(i)->ToString();
            Local<Value> property_value = properties->Get(property_name);
            obj->handle()->Set(property_name, property_value);
        }
    }

    info.GetReturnValue().Set(info.This());
}

NAN_METHOD(GIRStruct::call_method) {
    Local<External> function_info_extern = Local<External>::Cast(info.Data());
    GIFunctionInfo *function_info = (GIFunctionInfo *)function_info_extern->Value();
    GIRStruct *that = Nan::ObjectWrap::Unwrap<GIRStruct>(info.This()->ToObject());
    Local<Value> result = GIRFunction::call((GObject *)that->boxed_c_structure, function_info, info);
    info.GetReturnValue().Set(result);
}

NAN_PROPERTY_GETTER(GIRStruct::property_get_handler) {
    GIRStruct *gir_struct = Nan::ObjectWrap::Unwrap<GIRStruct>(info.This());
    auto field_info = GIRInfoUniquePtr(
        g_struct_info_find_field(gir_struct->struct_info.get(), *String::Utf8Value(property)));

    // if the field doesn't exist on the native object then just return
    // whatever is set for that property on the JS object
    if (field_info == nullptr) {
        info.GetReturnValue().Set(info.This()->GetPrototype()->ToObject()->Get(property));
        return;
    }

    // throw a JS error if the field isn't readable
    if (!(g_field_info_get_flags(field_info.get()) & GI_FIELD_IS_READABLE)) {
        stringstream message;
        message << "property '" << g_base_info_get_name(field_info.get()) << "' is not readable";
        Nan::ThrowError(Nan::New(message.str()).ToLocalChecked());
        return;
    }

    // otherwise we can get the native field's property and return it to JS
    GIArgument native_field_value;
    bool successfully_retrieved = g_field_info_get_field(field_info.get(),
                                                         gir_struct->boxed_c_structure,
                                                         &native_field_value);
    if (!successfully_retrieved) {
        stringstream message;
        message << "reading property '" << g_base_info_get_name(field_info.get()) << "' failed with an unknown error";
        Nan::ThrowError(Nan::New(message.str()).ToLocalChecked());
        return;
    }

    // converty the native value to a JS value
    auto type_info = GIRInfoUniquePtr(g_field_info_get_type(field_info.get()));
    Local<Value> res = Args::from_g_type(&native_field_value, type_info.get());
    info.GetReturnValue().Set(res);
    return;
}

NAN_PROPERTY_SETTER(GIRStruct::property_set_handler) {
    GIRStruct *gir_struct = Nan::ObjectWrap::Unwrap<GIRStruct>(info.This());
    String::Utf8Value property_name(property);
    auto field_info = GIRInfoUniquePtr(g_struct_info_find_field(gir_struct->struct_info.get(), *property_name));

    // if the native field doesn't exist then just set on the regular JS object
    if (field_info == nullptr) {
        info.This()->GetPrototype()->ToObject()->Set(property, value);
        return;
    }

    // throw a JS error if the field isn't writable
    if (!(g_field_info_get_flags(field_info.get()) & GI_FIELD_IS_WRITABLE)) {
        stringstream message;
        message << "property '" << g_base_info_get_name(field_info.get()) << "' is not writable";
        Nan::ThrowError(Nan::New(message.str()).ToLocalChecked());
        return;
    }

    // otherwise set the native field
    auto type_info = GIRInfoUniquePtr(g_field_info_get_type(field_info.get()));
    GIArgument native_value = Args::type_to_g_type(*type_info, value);
    bool successfully_set = g_field_info_set_field(field_info.get(), gir_struct->boxed_c_structure, &native_value);
    if (!successfully_set) {
        stringstream message;
        message << "setting property '" << g_base_info_get_name(field_info.get()) << "' failed with an unknown error";
        Nan::ThrowError(Nan::New(message.str()).ToLocalChecked());
        return;
    }
}

NAN_PROPERTY_QUERY(GIRStruct::property_query_handler) {
    // FIXME: I have no idea what this is suppose to do
    info.GetReturnValue().Set(Nan::New(0));
}

} // namespace gir
