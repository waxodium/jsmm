#pragma once

#include "types/types.hh"

#include <vector>

namespace rma::types {

struct TypeScheme {
  std::vector<int> variables;
  TypePtr type;
};

TypePtr instantiate(const TypeScheme &scheme, TypeContext &context);
TypeScheme generalize(TypePtr type, const std::vector<TypePtr> &environment, TypeContext &context);

} // namespace rma::types
