#pragma once

#include <girepository.h>

namespace gir {

gsize get_type_size(GITypeInfo *type_info);
gsize get_type_tag_size(GITypeTag type_tag);

} // namespace gir
