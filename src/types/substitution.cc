#include "types/substitution.hh"

#include <set>

namespace jsmm::types {

std::vector<int> freeTypeVariables(TypePtr type, TypeContext &context) {
  std::set<int> collected;
  std::vector<TypePtr> worklist{std::move(type)};
  while (!worklist.empty()) {
    TypePtr current = context.resolve(worklist.back());
    worklist.pop_back();
    if (current == nullptr) continue;
    switch (current->kind) {
    case TypeKind::Variable: collected.insert(current->id); break;
    case TypeKind::Function:
      worklist.insert(worklist.end(), current->parameters.begin(), current->parameters.end());
      worklist.push_back(current->result);
      break;
    case TypeKind::Struct:
      for (const auto &field : current->fields) worklist.push_back(field.second);
      break;
    case TypeKind::Array: worklist.push_back(current->element); break;
    case TypeKind::Union:
      worklist.insert(worklist.end(), current->members.begin(), current->members.end());
      break;
    default: break;
    }
  }
  return {collected.begin(), collected.end()};
}

TypePtr substitute(TypePtr type, const Substitution &mapping, TypeContext &context) {
  type = context.resolve(std::move(type));
  if (type == nullptr) return nullptr;
  switch (type->kind) {
  case TypeKind::Variable: {
    const auto found = mapping.find(type->id);
    if (found != mapping.end()) return found->second;
    return type;
  }
  case TypeKind::Function: {
    std::vector<TypePtr> parameters;
    parameters.reserve(type->parameters.size());
    for (const auto &parameter : type->parameters)
      parameters.push_back(substitute(parameter, mapping, context));
    return context.functionType(std::move(parameters), substitute(type->result, mapping, context));
  }
  case TypeKind::Struct: {
    std::vector<std::pair<std::string, TypePtr>> fields;
    fields.reserve(type->fields.size());
    for (const auto &field : type->fields)
      fields.emplace_back(field.first, substitute(field.second, mapping, context));
    return context.structType(std::move(fields));
  }
  case TypeKind::Array: return context.arrayType(substitute(type->element, mapping, context));
  case TypeKind::Union: {
    std::vector<TypePtr> members;
    members.reserve(type->members.size());
    for (const auto &member : type->members)
      members.push_back(substitute(member, mapping, context));
    return context.unionType(std::move(members));
  }
  default: return type;
  }
}

} // namespace jsmm::types
