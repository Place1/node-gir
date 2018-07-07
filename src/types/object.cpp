#include <iostream>
#include <string>

#include "closure.h"
#include "namespace_loader.h"
#include "object.h"
#include "types/function.h"
#include "util.h"
#include "values.h"

#include <node.h>
#include <cstring>

using namespace v8;
using namespace std;

namespace gir {

// initialize static properties
std::vector<ObjectFunctionTemplate *> GIRObject::templates;
std::set<GIRObject *> GIRObject::instances;

GIRObject::GIRObject(GIObjectInfo *object_info, map<string, GValue> &properties) {
    this->info = object_info;

    if (g_object_info_get_abstract(this->info)) {
        this->obj = nullptr;
    } else {
        GType object_type = g_registered_type_info_get_g_type(this->info);

// create the native object!
#if GLIB_CHECK_VERSION(2, 54, 0)
        vector<string> property_names = Util::extract_keys(properties);
        vector<GValue> property_values = Util::extract_values(properties);
        vector<const char *> property_names_cstr = Util::strings_to_cstrings(property_names);
        this->obj = g_object_new_with_properties(object_type,
                                                 properties.size(),
                                                 property_names_cstr.data(),
                                                 property_values.data());
#else
        vector<GParameter> parameters;
        parameters.reserve(properties.size());
        for (auto const &prop : properties) {
            GParameter param;
            param.name = prop.first.c_str();
            param.value = prop.second;
            parameters.push_back(param);
        }
        this->obj = G_OBJECT(g_object_newv(object_type, parameters.size(), parameters.data()));
#endif
    }
}

GObject *GIRObject::get_gobject() {
    return this->obj;
}

Local<Value> GIRObject::from_existing(GObject *existing_gobject, GIObjectInfo *object_info) {
    // sanity check our parameters
    if (existing_gobject == nullptr || !G_IS_OBJECT(existing_gobject)) {
        return Nan::Undefined(); // FIXME: perhaps throw an error?
    }

    // if there's already an existing Wrapper (instance) then return that
    MaybeLocal<Value> existing_gir_object = GIRObject::get_instance(existing_gobject);
    if (!existing_gir_object.IsEmpty()) {
        return existing_gir_object.ToLocalChecked();
    }

    // find/create an object template, then initialize it with the existing GObject
    ObjectFunctionTemplate *oft = GIRObject::find_or_create_template_from_object_info(object_info);
    Local<Function> instance_constructor = Nan::GetFunction(Nan::New(oft->object_template)).ToLocalChecked();
    Local<Object> instance = Nan::NewInstance(instance_constructor).ToLocalChecked();
    GIRObject *gir_wrapper = ObjectWrap::Unwrap<GIRObject>(instance);
    gir_wrapper->info = oft->info;
    gir_wrapper->obj = existing_gobject; // TODO: should we g_object_ref()?
    return instance;
}

map<string, GValue> GIRObject::parse_constructor_argument(Local<Object> properties_object, GIObjectInfo *object_info) {
    auto properties = map<string, GValue>();

    Local<Array> property_names = properties_object->GetPropertyNames();
    for (size_t i = 0; i < property_names->Length(); i++) {
        Local<String> property_name = property_names->Get(i)->ToString();
        String::Utf8Value property_name_cstr(property_name);
        GType property_g_type = GIRObject::get_object_property_type(object_info, *property_name_cstr);
        GValue g_value = GIRValue::to_g_value(properties_object->Get(property_name), property_g_type);
        properties.insert(pair<string, GValue>(string(*property_name_cstr), g_value));
    }

    return properties;
}

GType GIRObject::get_object_property_type(GIObjectInfo *object_info, const char *property_name) {
    void *klass = g_type_class_ref(g_registered_type_info_get_g_type(object_info));
    GParamSpec *param_spec = g_object_class_find_property(G_OBJECT_CLASS(klass), property_name);
    g_type_class_unref(klass);
    if (param_spec == nullptr) {
        return G_TYPE_INVALID; // signal that the type doesn't exist or is invalid
                               // because we can't find it!
    }
    return G_PARAM_SPEC_VALUE_TYPE(param_spec);
}

ObjectFunctionTemplate *GIRObject::create_object_template(GIObjectInfo *object_info) {
    Local<External> object_info_extern = Nan::New<External>((void *)g_base_info_ref(object_info));
    Local<FunctionTemplate> object_template = Nan::New<FunctionTemplate>(GIRObject::constructor, object_info_extern);

    ObjectFunctionTemplate *oft = new ObjectFunctionTemplate(); // TODO: where do we deallocate? When the
                                                                // namespace object (the node module) is
                                                                // collected?
    g_base_info_ref(object_info); // ref the info because we're storing an reference on 'oft'
    oft->info = object_info;
    oft->object_template = PersistentFunctionTemplate(object_template); // TODO: refactor oft->object_template to
                                                                        // 'object_template' for consistency in naming!
                                                                        // function can be confusing!
    oft->type = g_registered_type_info_get_g_type(object_info);
    oft->type_name = (char *)g_base_info_get_name(object_info);
    oft->namespace_ = (char *)g_base_info_get_namespace(object_info);
    GIRObject::templates.push_back(oft);

    // set the class name
    object_template->SetClassName(Nan::New(oft->type_name).ToLocalChecked());

    // Create instance template
    v8::Local<v8::ObjectTemplate> object_instance_template = object_template->InstanceTemplate();
    object_instance_template->SetInternalFieldCount(1);
    // Create external to hold GIBaseInfo and set it
    v8::Handle<v8::External> info_handle = Nan::New<v8::External>((void *)g_base_info_ref(oft->info));
    // Set properties handlers
    SetNamedPropertyHandler(object_instance_template,
                            GIRObject::property_get_handler,
                            GIRObject::property_set_handler,
                            GIRObject::property_query_handler,
                            nullptr,
                            nullptr,
                            info_handle);

    int number_of_constants = g_object_info_get_n_constants(oft->info);
    for (int i = 0; i < number_of_constants; i++) {
        // TODO: after loading various libraries there was never an object with
        // constants :/
        GIConstantInfo *constant = g_object_info_get_constant(oft->info, i);
        object_template->Set(Nan::New(g_base_info_get_name(constant)).ToLocalChecked(),
                             Nan::New(i)); // TODO: i'm fairly sure we shouldn't be setting just the
                                           // index on the object, but rather the actual value of
                                           // the constant?
        g_base_info_unref(constant);
    }

    GIRObject::register_methods(oft->info, oft->namespace_, object_template);
    GIRObject::set_custom_prototype_methods(object_template);
    GIRObject::extend_parent(object_template, oft->info);

    return oft;
}

Local<Object> GIRObject::prepare(GIObjectInfo *object_info) {
    ObjectFunctionTemplate *oft = find_or_create_template_from_object_info(object_info);

    // we want to return a Local<Object> that is our JS class. Remember that JS
    // classes are just functions! There is a 'NewInstance()' method on
    // v8::Function!
    return Nan::New(oft->object_template)->GetFunction();
}

void GIRObject::extend_parent(Local<FunctionTemplate> &object_template, GIObjectInfo *object_info) {
    GIObjectInfo *parent_object_info = g_object_info_get_parent(object_info);

    // if there is no parent to extend, then we can just no-op (do nothing).
    if (!parent_object_info) {
        return;
    }

    // find or create an ObjectFunctionTemplate for the parent object
    ObjectFunctionTemplate *oft = GIRObject::find_or_create_template_from_object_info(parent_object_info);

    // make the input object template inherit (extend) the parent object
    object_template->Inherit(Nan::New(oft->object_template));

    // cleanup
    g_base_info_unref(parent_object_info);
}

ObjectFunctionTemplate *GIRObject::find_template_from_object_info(GIObjectInfo *object_info) {
    for (auto oft : GIRObject::templates) {
        if (g_base_info_equal(object_info, oft->info)) {
            return oft;
        }
    }
    return nullptr;
}

ObjectFunctionTemplate *GIRObject::find_or_create_template_from_object_info(GIObjectInfo *object_info) {
    ObjectFunctionTemplate *oft = GIRObject::find_template_from_object_info(object_info);
    if (oft == nullptr) {
        return GIRObject::create_object_template(object_info);
    }
    return oft;
}

void GIRObject::set_custom_prototype_methods(Local<FunctionTemplate> &object_template) {
    // Add our 'connect' method to the target.
    // This method is used to connect signals to the underlying gobject.
    Nan::SetPrototypeMethod(object_template, "connect", GIRObject::connect);

    // Add the 'disconnect' method to the target.
    // This method is used to disconnect signals connected using 'connect()'
    Nan::SetPrototypeMethod(object_template, "disconnect", GIRObject::disconnect);
}

MaybeLocal<Value> GIRObject::get_instance(GObject *obj) {
    for (GIRObject *gir_object : GIRObject::instances) {
        if (gir_object->obj == obj) {
            return MaybeLocal<Value>(gir_object->handle());
        }
    }
    return MaybeLocal<Value>();
}

GIPropertyInfo *GIRObject::find_property(GIObjectInfo *object_info,
                                         char *property_name) { // FIXME: use a unique_pointer with std::move to return
                                                                // ownership
    int num_properties = g_object_info_get_n_properties(object_info);
    for (int i = 0; i < num_properties; i++) {
        GIPropertyInfo *prop = g_object_info_get_property(object_info, i);
        if (strcmp(g_base_info_get_name(prop), property_name) == 0) {
            return prop;
        }
        g_base_info_unref(prop);
    }
    return nullptr;
}

void GIRObject::register_methods(GIObjectInfo *object_info,
                                 const char *namespace_,
                                 Handle<FunctionTemplate> &object_template) {
    int num_methods = 0;
    if (GI_IS_OBJECT_INFO(object_info)) {
        num_methods = g_object_info_get_n_methods(object_info);
    } else {
        num_methods = g_interface_info_get_n_methods(object_info);
    }

    for (int i = 0; i < num_methods; i++) {
        GIFunctionInfo *function_info = nullptr; // FIXME: use GIRInfoUniquePtr
        if (GI_IS_OBJECT_INFO(object_info)) {
            function_info = g_object_info_get_method(object_info, i);
        } else {
            function_info = g_interface_info_get_method(object_info, i);
        }
        GIRObject::set_method(object_template, function_info); // FIXME: if this throws then we leak function_info
        g_base_info_unref(function_info);
    }
}

/**
 * this function will use the GIFunctionInfo (which describes a native function)
 * to define either a static or prototype method on the target, depending on the
 * flags of the GIFunctionInfo.
 * It will also apply a snake_case to camelCase conversion to function name.
 */
void GIRObject::set_method(Local<FunctionTemplate> &target, GIFunctionInfo *function_info) {
    const char *native_name = g_base_info_get_name(function_info);
    string js_name = Util::to_camel_case(std::string(native_name));
    Local<String> js_function_name = Nan::New(js_name.c_str()).ToLocalChecked();
    if (g_function_info_get_flags(function_info) & GI_FUNCTION_IS_METHOD) {
        // if the function is a method, then we want to set it on the prototype
        // of the target, as a GI_FUNCTION_IS_METHOD is an instance method.
        target->PrototypeTemplate()->Set(js_function_name, GIRFunction::create_method(function_info));
    } else {
        // else if it's not a method, then we want to set it as a static function
        // on the target itgir_object (not the prototype)
        target->Set(js_function_name, GIRFunction::create_function(function_info));
    }
}

NAN_METHOD(GIRObject::constructor) {
    if (!(info.Length() == 0 || info.Length() == 1)) {
        Nan::ThrowTypeError("constructors must take 0 or 1 argument");
        return;
    }

    // get our user-data that was originally put on the function when it was created.
    Local<External> object_info_extern = Local<External>::Cast(info.Data());
    GIObjectInfo *object_info = (GIObjectInfo *)object_info_extern->Value();

    if (object_info == nullptr) {
        Nan::ThrowError("no type information available for object constructor! this is likely a "
                        "bug with node-gir!");
        return;
    }

    map<string, GValue> properties;
    if (info.Length() == 1 && info[0]->IsObject()) {
        properties = GIRObject::parse_constructor_argument(info[0]->ToObject(), object_info);
    }

    GIRObject *obj = new GIRObject(object_info, properties);
    obj->Wrap(info.This());
    GIRObject::instances.insert(obj);
    info.GetReturnValue().Set(info.This());
}

NAN_PROPERTY_QUERY(GIRObject::property_query_handler) {
    // FIXME: implement this
    String::Utf8Value _name(property);
    info.GetReturnValue().Set(Nan::New(0));
}

NAN_PROPERTY_GETTER(GIRObject::property_get_handler) {
    String::Utf8Value _name(property);
    Handle<External> info_ptr = Handle<External>::Cast(info.Data());
    GIBaseInfo *base_info = (GIBaseInfo *)info_ptr->Value();
    if (base_info != nullptr) {
        GIRObject *that = Nan::ObjectWrap::Unwrap<GIRObject>(info.This()->ToObject());
        GParamSpec *pspec = g_object_class_find_property(G_OBJECT_GET_CLASS(that->obj), *_name);
        if (pspec) {
            // Property is not readable
            if (!(pspec->flags & G_PARAM_READABLE)) {
                Nan::ThrowTypeError("property is not readable");
            }
            GIPropertyInfo *prop_info = GIRObject::find_property(base_info, *_name);
            GType value_type = G_TYPE_FUNDAMENTAL(pspec->value_type);
            GValue gvalue = {0, {{0}}};
            g_value_init(&gvalue, pspec->value_type);
            g_object_get_property(G_OBJECT(that->obj), *_name, &gvalue);
            Local<Value> res = GIRValue::from_g_value(&gvalue, prop_info);
            if (value_type != G_TYPE_OBJECT && value_type != G_TYPE_BOXED) {
                g_value_unset(&gvalue);
            }
            if (prop_info) {
                g_base_info_unref(prop_info);
            }
            info.GetReturnValue().Set(res);
            return;
        }
    }

    // Fallback to defaults
    info.GetReturnValue().Set(info.This()->GetPrototype()->ToObject()->Get(property));
}

NAN_PROPERTY_SETTER(GIRObject::property_set_handler) {
    String::Utf8Value property_name(property);

    v8::Handle<v8::External> info_ptr = v8::Handle<v8::External>::Cast(info.Data());
    GIBaseInfo *base_info = (GIBaseInfo *)info_ptr->Value();
    if (base_info != nullptr) {
        GIRObject *that = Nan::ObjectWrap::Unwrap<GIRObject>(info.This()->ToObject());
        GParamSpec *pspec = g_object_class_find_property(G_OBJECT_GET_CLASS(that->obj), *property_name);
        if (pspec) {
            // Property is not readable
            if (!(pspec->flags & G_PARAM_WRITABLE)) {
                Nan::ThrowTypeError("property is not writable");
            }

            GValue g_value = GIRValue::to_g_value(value, pspec->value_type);
            g_object_set_property(that->obj, *property_name, &g_value);
            g_value_unset(&g_value);
            return;
        }
    }

    // Fallback to defaults
    info.This()->GetPrototype()->ToObject()->Set(property, value);
}

/**
 * This method will connect a signal to the underlying gobject
 * using a GIRClosure (a custom GClosure we've written to support
 * JS callback's)
 */
NAN_METHOD(GIRObject::connect) {
    if (info.Length() != 2 || !info[0]->IsString() || !info[1]->IsFunction()) {
        Nan::ThrowError("Invalid arguments: expected (string, Function)");
        return;
    }
    GIRObject *gir_object = Nan::ObjectWrap::Unwrap<GIRObject>(info.This()->ToObject());
    Nan::Utf8String nan_signal_name(info[0]->ToString());
    char *signal_name = *nan_signal_name;
    Local<Function> callback = Nan::To<Function>(info[1]).ToLocalChecked();

    // parse the signal id and detail (whatever that is) from the gobject we're wrapping
    guint signal_id;
    GQuark detail;
    if (!g_signal_parse_name(signal_name, G_OBJECT_TYPE(gir_object->obj), &signal_id, &detail, TRUE)) {
        Nan::ThrowError("unknown signal name");
        return;
    }

    // create a closure that will manage the signal callback to JS callback for us
    GClosure *closure = GIRClosure::create_signal_closure(signal_id, signal_name, callback);
    if (closure == nullptr) {
        Nan::ThrowError("unknown signal");
        return;
    }

    // connect the closure to the signal using the signal_id and detail we've already found
    gulong handle_id = g_signal_connect_closure_by_id(gir_object->obj,
                                                      signal_id,
                                                      detail,
                                                      closure,
                                                      FALSE); // TODO: support connecting with after=TRUE

    // return the signal connection ID back to JS.
    info.GetReturnValue().Set(Nan::New((uint32_t)handle_id));
}

NAN_METHOD(GIRObject::disconnect) {
    if (info.Length() != 1 || !info[0]->IsNumber()) {
        Nan::ThrowTypeError("Invalid argument's, expected 1 number arg!");
        return;
    }
    gulong signal_handler_id = Nan::To<uint32_t>(info[0]).FromJust();
    GIRObject *that = Nan::ObjectWrap::Unwrap<GIRObject>(info.This());
    g_signal_handler_disconnect(that->obj, signal_handler_id);
    info.GetReturnValue().Set(Nan::Undefined());
}

} // namespace gir
