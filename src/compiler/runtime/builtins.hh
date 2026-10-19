#pragma once

#include "engine/value.hh"
#include <optional>
#include <string>
#include <vector>

namespace rma::compiler {

std::optional<rma::syntax::Value> invokeBuiltinMethod(const rma::syntax::Value &receiver,
                                                      const std::string &method,
                                                      const std::vector<rma::syntax::Value> &args);

void initializeBuiltins();

} // namespace rma::compiler
