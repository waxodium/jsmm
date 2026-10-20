#pragma once

#include "types/types.hh"

namespace rma::types {

class Unifier final {
public:
  explicit Unifier(TypeContext &context);

  bool unify(TypePtr left, TypePtr right);

private:
  bool bind(TypePtr variable, TypePtr type);
  bool occurs(int id, TypePtr type);
  bool unifyUnion(TypePtr left, TypePtr right);
  std::vector<std::pair<Type *, TypePtr>> snapshot(TypePtr left, TypePtr right);
  void restore(const std::vector<std::pair<Type *, TypePtr>> &links);

  TypeContext &context_;
};

}
