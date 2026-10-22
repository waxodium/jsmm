#pragma once

#include "types/types.hh"

#include <map>
#include <vector>

namespace jsmm::types {

using Substitution = std::map<int, TypePtr>;

std::vector<int> freeTypeVariables(TypePtr type, TypeContext &context);
TypePtr substitute(TypePtr type, const Substitution &mapping, TypeContext &context);

} // namespace jsmm::types
