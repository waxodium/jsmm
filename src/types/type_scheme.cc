#include "types/type_scheme.hh"

#include "types/substitution.hh"

#include <set>

namespace jsmm::types {

TypePtr instantiate(const TypeScheme &scheme, TypeContext &context) {
  if (scheme.variables.empty()) return scheme.type;
  Substitution mapping;
  for (const int id : scheme.variables) mapping[id] = context.variable();
  return substitute(scheme.type, mapping, context);
}

TypeScheme generalize(TypePtr type, const std::vector<TypePtr> &environment, TypeContext &context) {
  std::set<int> environmentVariables;
  for (const auto &entry : environment)
    for (const int id : freeTypeVariables(entry, context)) environmentVariables.insert(id);
  std::vector<int> quantified;
  for (const int id : freeTypeVariables(type, context))
    if (!environmentVariables.contains(id)) quantified.push_back(id);
  return {std::move(quantified), std::move(type)};
}

} // namespace jsmm::types
