#pragma once

#include "girepository.h"
#include "util.h"

namespace gir {

class Parameter {
public:
    virtual GIArgument get_argument() = 0;
};

class InParameter : public Parameter {
private:
    GIArgument arg;

public:
    InParameter(GIArgument arg) : arg(arg){};
    ~InParameter();
    GIArgument get_argument();
};

class OutParameter : public Parameter {
private:
    GIArgInfo *info = nullptr;
    GIArgument argument;

public:
    OutParameter(GIArgInfo *info);
    ~OutParameter();
    GIArgument get_argument();
};

class InOutParameter : public Parameter {
private:
    GIArgument arg;
    GIArgument out_arg;

public:
    InOutParameter(GIArgument arg);
    GIArgument get_argument();
};

} // namespace gir
