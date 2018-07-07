#pragma once

#include <ffi.h>
#include <girepository.h>
#include <girffi.h>
#include <nan.h>
#include <node.h>
#include <string>
#include "types/object.h"
#include "util.h"

namespace gir {

using namespace std;
using namespace v8;

using PersistentFunction = Nan::Persistent<Function, CopyablePersistentTraits<Function>>;

class GIRClosure {
private:
    GClosure closure;
    GIRInfoUniquePtr callable_info;
    PersistentFunction callback;

public:
    static GClosure *create_signal_closure(guint signal_id, char *signal_name, Local<Function> callback);
    static ffi_closure *create_ffi(GICallableInfo *callable_info, Local<Function> callback);

private:
    GIRClosure() = default;
    static GClosure *create(GICallableInfo *callable_info, Local<Function> callback);
    static void closure_marshal(GClosure *closure,
                                GValue *return_value,
                                guint n_param_values,
                                const GValue *param_values,
                                gpointer invocation_hint,
                                gpointer marshal_data);
    static GIRInfoUniquePtr find_signal_callable_info(guint signal_id, char *signal_name);
    static void finalize_handler(gpointer notify_data, GClosure *closure);
    static void ffi_closure_callback(ffi_cif *cif, void *result, void **args, gpointer user_data);
};

} // namespace gir
