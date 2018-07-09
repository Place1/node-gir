#pragma once

#include <girepository.h>
#include <glib.h>
#include <nan.h>
#include <v8.h>
#include <vector>

#include "parameter.h"
#include "util.h"

namespace gir {

using namespace std;
using namespace v8;

class Args {
public:
    vector<Parameter *> params;

    Args(GICallableInfo *callable_info);

    void load_js_arguments(const Nan::FunctionCallbackInfo<Value> &js_callback_info);
    void load_context(GObject *this_object);
    vector<GIArgument> get_in_args();
    vector<GIArgument> get_out_args();
    vector<GIArgument> get_all_args();

private:
    GIRInfoUniquePtr callable_info;
    static GITypeTag map_g_type_tag(GITypeTag type);

public:
    static GIArgument arg_to_g_type(GIArgInfo &argument_info, Local<Value> js_value);
    static GIArgument type_to_g_type(GITypeInfo &argument_type_info, Local<Value> js_value);
    static int get_g_type_array_length(GICallableInfo *callable_info,
                                       vector<GIArgument> call_args,
                                       GIArgument *array_arg,
                                       GITypeInfo *array_arg_type);
    static Local<Value> from_g_type_array(GIArgument *arg, GIArgInfo *info, int array_length = -1);
    static GIArgument to_g_type_array(Local<Value> value, GITypeInfo *info);
    static Local<Value> from_g_type(GIArgument *arg, GITypeInfo *type_info, int array_length = -1);
};

} // namespace gir
