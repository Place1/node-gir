#include <girepository.h>

#include "types/param_spec.h"

namespace gir {

Nan::Persistent<Function> GIRParamSpec::instance_constructor;

Local<Function> GIRParamSpec::get_js_constructor() {
    if (GIRParamSpec::instance_constructor.IsEmpty()) {
        Local<FunctionTemplate> object_template = Nan::New<FunctionTemplate>();
        object_template->SetClassName(Nan::New("GParam").ToLocalChecked());
        object_template->InstanceTemplate()->SetInternalFieldCount(1);
        GIRParamSpec::instance_constructor.Reset(Nan::GetFunction(object_template).ToLocalChecked());
    }
    return Nan::New(GIRParamSpec::instance_constructor);
}

Local<Value> GIRParamSpec::from_existing(GParamSpec *param_spec) {
    GIRParamSpec *gir_param_spec = new GIRParamSpec();
    g_param_spec_ref(param_spec);
    gir_param_spec->param_spec = param_spec;
    Local<Object> instance = Nan::NewInstance(GIRParamSpec::get_js_constructor()).ToLocalChecked();
    gir_param_spec->Wrap(instance);
    return instance;
}

GIRParamSpec::~GIRParamSpec() {
    if (this->param_spec != nullptr) {
        g_param_spec_unref(this->param_spec);
    }
}

} // namespace gir
