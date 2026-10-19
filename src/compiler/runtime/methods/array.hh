#pragma once

#include "engine/value.hh"
#include <vector>

namespace rma::compiler {

rma::syntax::Value arrayPush(const rma::syntax::Value &receiver,
                             const std::vector<rma::syntax::Value> &args);
rma::syntax::Value arrayPop(const rma::syntax::Value &receiver,
                            const std::vector<rma::syntax::Value> &args);

} // namespace rma::compiler
