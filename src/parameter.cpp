#include <sstream>

#include "exceptions.h"
#include "parameter.h"

namespace gir {

using namespace std;

InParameter::~InParameter() {}

GIArgument InParameter::get_argument() {
    return this->arg;
}

OutParameter::OutParameter(GIArgInfo *info) {
    g_base_info_ref(info);
    this->info = info;
    this->argument = GIArgument{};
    GITypeInfo argument_type_info;
    g_arg_info_load_type(this->info, &argument_type_info);
    if (g_arg_info_is_caller_allocates(this->info)) {
        GITypeTag arg_type_tag = g_type_info_get_tag(&argument_type_info);
        // If the caller is responsible for allocating the out arguments memeory
        // then we'll have to look up the argument's type infomation and allocate
        // a slice of memory for the GIArgument's .v_pointer (native function will
        // fill it up)
        if (arg_type_tag == GI_TYPE_TAG_INTERFACE) {
            GIRInfoUniquePtr argument_interface_info = GIRInfoUniquePtr(g_type_info_get_interface(&argument_type_info));
            GIInfoType argument_interface_type = g_base_info_get_type(argument_interface_info.get());
            gsize argument_size;

            if (argument_interface_type == GI_INFO_TYPE_STRUCT) {
                argument_size = g_struct_info_get_size(static_cast<GIStructInfo *>(argument_interface_info.get()));
            } else if (argument_interface_type == GI_INFO_TYPE_UNION) {
                argument_size = g_union_info_get_size(static_cast<GIUnionInfo *>(argument_interface_info.get()));
            } else {
                stringstream message;
                message << "type \"" << g_type_tag_to_string(arg_type_tag) << "\" for out caller-allocates";
                message << " Expected a struct or union.";
                throw UnsupportedGIType(message.str());
            }

            // FIXME: who deallocates?
            // I imagine the original function caller (in JS land) will need
            // to use the structure that the native function puts into this
            // slice of memory, meaning we can't deallocate when Args is destroyed.
            // Perhaps we should research into GJS and PyGObject to understand
            // the problem of "out arguments with caller allocation" better.
            // Some thoughts:
            // 1. if the data is **copied** into a gir object/struct when passed back
            //    to JS then we can safely implement a custom deleter for Args.out
            //    that cleans this up.
            // 2. if the pointer to the data is passed to a gir object/struct when
            //    passed back to JS then that JS object should own it and be
            //    responsible for cleaning it up.
            // * both choices have caveats so it's worth understanding the
            // implications
            //   of each, or other options to solve this leak!
            // * from reading GJS code, it seems like they copy the data before
            // passing
            // * to JS meaning option 1.
            this->argument.v_pointer = g_slice_alloc0(argument_size);
            return;
        } else {
            stringstream message;
            message << "type \"" << g_type_tag_to_string(arg_type_tag) << "\" for out caller-allocates";
            throw UnsupportedGIType(message.str());
        }
    } else {
        // if the caller doesn't allocate then we need to create an empty GIArgument on the .v_pointer
        // for the callee to dump their result into.
        this->argument.v_pointer = new GIArgument{};
        return;
    }
    return;
}

OutParameter::~OutParameter() {
    if (this->info != nullptr) {
        g_base_info_unref(this->info);
    }
    if (this->argument.v_pointer != nullptr) {
        // TODO: free data depending on ownership rules encoded in type info.
    }
}

GIArgument OutParameter::get_argument() {
    return this->argument;
}

InOutParameter::InOutParameter(GIArgument) : arg(arg) {
    this->out_arg = GIArgument{};
    this->out_arg.v_pointer = this->arg.v_pointer;
    this->arg.v_pointer = &this->out_arg;
}

GIArgument InOutParameter::get_argument() {
    return this->arg;
}

} // namespace gir
