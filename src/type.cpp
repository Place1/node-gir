#include <sstream>

#include "exceptions.h"
#include "type.h"
#include "util.h"

namespace gir {

using namespace std;

gsize get_type_size(GITypeInfo *type_info) {
    gsize size = 0;

    GITypeTag type_tag = g_type_info_get_tag(type_info);

    switch (type_tag) {
        case GI_TYPE_TAG_BOOLEAN:
        case GI_TYPE_TAG_INT8:
        case GI_TYPE_TAG_UINT8:
        case GI_TYPE_TAG_INT16:
        case GI_TYPE_TAG_UINT16:
        case GI_TYPE_TAG_INT32:
        case GI_TYPE_TAG_UINT32:
        case GI_TYPE_TAG_INT64:
        case GI_TYPE_TAG_UINT64:
        case GI_TYPE_TAG_FLOAT:
        case GI_TYPE_TAG_DOUBLE:
        case GI_TYPE_TAG_GTYPE:
        case GI_TYPE_TAG_UNICHAR:
            return get_type_tag_size(type_tag);
        case GI_TYPE_TAG_INTERFACE: {
            auto info = GIRInfoUniquePtr(g_type_info_get_interface(type_info));
            GIInfoType info_type = g_base_info_get_type(info.get());

            switch (info_type) {
                case GI_INFO_TYPE_STRUCT:
                    if (g_type_info_is_pointer(type_info)) {
                        return sizeof(gpointer);
                    } else {
                        return g_struct_info_get_size(static_cast<GIStructInfo *>(info.get()));
                    }
                    break;
                case GI_INFO_TYPE_UNION:
                    if (g_type_info_is_pointer(type_info)) {
                        return sizeof(gpointer);
                    } else {
                        return g_union_info_get_size(static_cast<GIUnionInfo *>(info.get()));
                    }
                    break;
                case GI_INFO_TYPE_ENUM:
                case GI_INFO_TYPE_FLAGS:
                    if (g_type_info_is_pointer(type_info)) {
                        return sizeof(gpointer);
                    } else {
                        GITypeTag type_tag = g_enum_info_get_storage_type(static_cast<GIEnumInfo *>(info.get()));
                        return get_type_tag_size(type_tag);
                    }
                    break;
                case GI_INFO_TYPE_BOXED:
                case GI_INFO_TYPE_OBJECT:
                case GI_INFO_TYPE_INTERFACE:
                case GI_INFO_TYPE_CALLBACK:
                    return sizeof(gpointer);
                case GI_INFO_TYPE_VFUNC:
                case GI_INFO_TYPE_FUNCTION:
                case GI_INFO_TYPE_CONSTANT:
                case GI_INFO_TYPE_VALUE:
                case GI_INFO_TYPE_SIGNAL:
                case GI_INFO_TYPE_PROPERTY:
                case GI_INFO_TYPE_FIELD:
                case GI_INFO_TYPE_ARG:
                case GI_INFO_TYPE_TYPE:
                case GI_INFO_TYPE_INVALID:
                case GI_INFO_TYPE_UNRESOLVED:
                default:
                    stringstream message;
                    message << "cannot determine size of info type " << g_info_type_to_string(info_type);
                    throw UnsupportedGIType(message.str());
            }
            break;
        }
        case GI_TYPE_TAG_ARRAY:
        case GI_TYPE_TAG_VOID:
        case GI_TYPE_TAG_UTF8:
        case GI_TYPE_TAG_FILENAME:
        case GI_TYPE_TAG_GLIST:
        case GI_TYPE_TAG_GSLIST:
        case GI_TYPE_TAG_GHASH:
        case GI_TYPE_TAG_ERROR:
            return sizeof(void *);
    }
    return size;
}

gsize get_type_tag_size(GITypeTag type_tag) {
    switch (type_tag) {
        case GI_TYPE_TAG_BOOLEAN:
            return sizeof(gboolean);
        case GI_TYPE_TAG_INT8:
        case GI_TYPE_TAG_UINT8:
            return sizeof(gint8);
        case GI_TYPE_TAG_INT16:
        case GI_TYPE_TAG_UINT16:
            return sizeof(gint16);
        case GI_TYPE_TAG_INT32:
        case GI_TYPE_TAG_UINT32:
            return sizeof(gint32);
        case GI_TYPE_TAG_INT64:
        case GI_TYPE_TAG_UINT64:
            return sizeof(gint64);
        case GI_TYPE_TAG_FLOAT:
            return sizeof(gfloat);
        case GI_TYPE_TAG_DOUBLE:
            return sizeof(gdouble);
        case GI_TYPE_TAG_GTYPE:
            return sizeof(GType);
        case GI_TYPE_TAG_UNICHAR:
            return sizeof(gunichar);
        case GI_TYPE_TAG_VOID:
        case GI_TYPE_TAG_UTF8:
        case GI_TYPE_TAG_FILENAME:
        case GI_TYPE_TAG_ARRAY:
        case GI_TYPE_TAG_INTERFACE:
        case GI_TYPE_TAG_GLIST:
        case GI_TYPE_TAG_GSLIST:
        case GI_TYPE_TAG_GHASH:
        case GI_TYPE_TAG_ERROR:
        default:
            stringstream message;
            message << "unable to determine the size of " << g_type_tag_to_string(type_tag);
            throw UnsupportedGIType(message.str());
    }
}

} // namespace gir
