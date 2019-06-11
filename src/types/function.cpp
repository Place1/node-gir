#include "function.h"
#include "exceptions.h"
#include "namespace_loader.h"
#include "object.h"
#include "util.h"

#include <nan.h>
#include <node.h>
#include <cstring>

using namespace std;
using namespace v8;

namespace gir {

Local<Function> GIRFunction::prepare(GIFunctionInfo *function_info) {
    // Create new function
    Local<FunctionTemplate> js_function_template = GIRFunction::create_function(function_info);
    Local<Function> js_function = js_function_template->GetFunction();

    // Set the function name
    const char *native_name = g_base_info_get_name(function_info);
    string js_name = Util::to_camel_case(string(native_name));
    js_function->SetName(Nan::New(js_name.c_str()).ToLocalChecked());

    return js_function;
}

Local<FunctionTemplate> GIRFunction::create_function(GIFunctionInfo *function_info) {
    Local<External> function_info_extern = Nan::New<External>((void *)g_base_info_ref(function_info));
    Local<FunctionTemplate> function_template = Nan::New<FunctionTemplate>(GIRFunction::InvokeFunction,
                                                                           function_info_extern);
    return function_template;
}

// FIXME: this needs to be refactored to support anyone creating a function
// that executes the native function specified by GIFunctionInfo with a given GObject
// not just GIRObject's as is the case currently with GIRFunction::InvokeMethod!
Local<FunctionTemplate> GIRFunction::create_method(GIFunctionInfo *function_info) {
    Local<External> function_info_extern = Nan::New<External>((void *)g_base_info_ref(function_info));
    Local<FunctionTemplate> function_template = Nan::New<FunctionTemplate>(GIRFunction::InvokeMethod,
                                                                           function_info_extern);
    return function_template;
}

NAN_METHOD(GIRFunction::InvokeFunction) {
    Local<External> function_info_extern = Local<External>::Cast(info.Data());
    GIFunctionInfo *function_info = (GIFunctionInfo *)function_info_extern->Value();
    Local<Value> js_func_result = GIRFunction::call(nullptr, function_info, info);
    info.GetReturnValue().Set(js_func_result);
}

NAN_METHOD(GIRFunction::InvokeMethod) {
    if (!info.This()->IsObject()) {
        Nan::ThrowTypeError("the value of 'this' is not an object");
        return;
    }

    GIRObject *that = Nan::ObjectWrap::Unwrap<GIRObject>(info.This()->ToObject());
    GObject *native_object = that->get_gobject();
    Local<External> function_info_extern = Local<External>::Cast(info.Data());
    GIFunctionInfo *function_info = (GIFunctionInfo *)function_info_extern->Value();

    Local<Value> js_func_result = GIRFunction::call(native_object, function_info, info);
    info.GetReturnValue().Set(js_func_result);
}

GIArgument GIRFunction::call_native(GIFunctionInfo *function_info, Args &args) {
    GIArgument return_value;
    GError *error = nullptr;

    auto in_args = args.get_in_args();
    auto out_args = args.get_out_args();
    g_function_info_invoke(function_info,
                           in_args.data(),
                           in_args.size(),
                           out_args.data(),
                           out_args.size(),
                           &return_value,
                           &error);

    if (error != nullptr) {
        string message = string(error->message);
        g_error_free(error);
        throw NativeGError(message); // FIXME: we might leak here if the native
                                     // function allocated memory inside
                                     // return_value and gave us ownership.
    }

    return return_value;
}

Local<Value> GIRFunction::call(GObject *obj,
                               GIFunctionInfo *function_info,
                               const Nan::FunctionCallbackInfo<v8::Value> &js_callback_info) {
    // we want to catch any errors we may encounter so we can throw them as JS
    // errors
    try {
        // create the arguments for the native function
        Args args = Args(function_info);
        args.load_js_arguments(js_callback_info);
        if (g_callable_info_is_method(function_info)) {
            if (obj != nullptr) {
                args.load_context(obj);
            } else {
                throw JSArgumentTypeError("method calls require a native object as the value of 'this'");
            }
        }

        // call the native function. CallNative is just a small wrapper to help with
        // handling native errors and return values.
        GIArgument result = GIRFunction::call_native(function_info, args);

        // handle the return value that we should pass back to JS.
        // there are some rules to decide how to handle there output from the native
        // function so we'll use a helper function to handle that logic for us.
        Local<Value> js_return_value = GIRFunction::js_return_value_from_native_call(function_info, args, result);
        return js_return_value;
    } catch (exception &error) {
        // if any exception happens we want to translate it to a JS error and return
        // undefined.
        Nan::ThrowError(error.what());
        return Nan::Undefined();
    }
}

/**
 * This function applies some rules as to how return values from a native call should
 * map to the JS function's return values. These rules should follow GJS exactly, if they
 * do not then we should consider it a bug.
 * Rules:
 * - If the native function has a return value and no out-args then just return the return value
 * - If the native function has no return value (skipped or void) and 1 out-arg, return just the out arg as a value
 * - If the native function has no return value (skipped or void) and more than 1 out-arg then return them as an array:
 * [out-arg-1, out-arg-2, ..., out-arg-n]
 * - If the native function has a return value and 1 or more out-args then return them as an array with the return value
 * in position 0: [return-value, out-arg-1, out-arg-2, ..., out-arg-n]
 */
Local<Value> GIRFunction::js_return_value_from_native_call(GIFunctionInfo *function_info,
                                                           Args &args,
                                                           GIArgument &native_call_result) {
    // load the function's return type info
    GITypeInfo return_type_info;
    g_callable_info_load_return_type(function_info, &return_type_info);

    auto out_args = args.get_out_args();
    auto all_args = args.get_all_args();

    // if the function's metadata says to skip the return value (meaning the
    // return value is only useful in C) or the return value is void, then we can
    // skip the return value when determining what should be returned from native
    // to JS.
    bool skip_return_value = g_callable_info_skip_return(function_info) ||
                             g_type_info_get_tag(&return_type_info) == GI_TYPE_TAG_VOID;
    int number_of_return_values = skip_return_value ? out_args.size() : out_args.size() + 1;

    Local<Array> js_result_array = Nan::New<Array>(number_of_return_values);

    // if we should NOT skip the native return value, then we should convert it to
    // JS and set it in position 0 of the returned value array
    if (!skip_return_value) {
        Local<Value> js_return_value = Args::from_g_type(&native_call_result, &return_type_info);
        js_result_array->Set(0, js_return_value);
    }

    // We need to handle OUT arguments from the native call.
    // If we had some out args then
    // foreach native argument, if it's an out arg, grab the next out arg from
    // args and add it to the next position in the js_result_array. The code here
    // is a bit confusing because `g_callable_info_load_arg(info, index)` requires
    // the argument index where the index is relative to ALL arguments where as
    // args.out.data() is an array of just the OUT args (doesn't include IN args)
    // so we can't just loop over args.out :(
    int js_results_array_pos = skip_return_value ? 0 : 1; // if there is a return_value then we need to
                                                          // offset the out args by 1 i.e.
                                                          // [return_value, out-arg-1, out-arg-2, ...]
    int next_out_arg_pos = 0;
    if (out_args.size() > 0) {
        for (int i = 0; i < g_callable_info_get_n_args(function_info); i++) {
            GIArgInfo argument_info;
            g_callable_info_load_arg(function_info, i, &argument_info);
            GIDirection argument_direction = g_arg_info_get_direction(&argument_info);
            if (argument_direction == GI_DIRECTION_OUT || argument_direction == GI_DIRECTION_INOUT) {
                GITypeInfo out_arg_type_info;
                g_arg_info_load_type(&argument_info, &out_arg_type_info);
                int length = Args::get_g_type_array_length(function_info,
                                                           all_args,
                                                           &out_args[next_out_arg_pos],
                                                           &out_arg_type_info);
                js_result_array->Set(js_results_array_pos,
                                     Args::from_g_type(static_cast<GIArgument *>(out_args[next_out_arg_pos].v_pointer),
                                                       &out_arg_type_info,
                                                       length));
                next_out_arg_pos += 1;
                js_results_array_pos += 1;
            }
        }
    }

    // based on the number of return values from the native function call we'll
    // decide what to send back to JS
    switch (number_of_return_values) {
        case 0:
            // if there is no return value (skipped return value, void return, and no
            // out-args) then return `undefined`
            return Nan::Undefined();

        case 1:
            // if there's only 1 return value we need to send it to JS as a Value (not
            // an array with 1 value in it)
            return js_result_array->Get(0);

        default:
            // else there must be more then 1 return value meaning we should return
            // them as a JS array
            return js_result_array;
    }
}

} // namespace gir
