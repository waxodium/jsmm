#pragma once

#include "types/types.hh"

namespace jsmm::types {

class Unifier final {
public:
  explicit Unifier(TypeContext &context);

  bool unify(TypePtr left, TypePtr right);
  bool assignable(TypePtr value, TypePtr target);

private:
  bool bind(TypePtr variable, TypePtr type);
  bool occurs(int id, TypePtr type);
  bool unifyUnion(TypePtr left, TypePtr right);
  bool membersMatch(const std::vector<TypePtr> &from, const std::vector<TypePtr> &to);
  bool memberAssignable(TypePtr value, const std::vector<TypePtr> &candidates);
  std::vector<std::pair<Type *, TypePtr>> snapshot(TypePtr left, TypePtr right);
  void restore(const std::vector<std::pair<Type *, TypePtr>> &links);

  TypeContext &context_;
};

} // namespace jsmm::types
