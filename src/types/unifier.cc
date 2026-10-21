#include "types/unifier.hh"

#include <set>
#include <unordered_set>
#include <vector>

namespace rma::types {

Unifier::Unifier(TypeContext &context) : context_(context) {}

bool Unifier::occurs(int id, TypePtr type) {
  std::vector<TypePtr> worklist{context_.resolve(std::move(type))};
  std::unordered_set<int> visited;
  while (!worklist.empty()) {
    TypePtr current = context_.resolve(worklist.back());
    worklist.pop_back();
    if (current == nullptr) continue;
    if (current->kind == TypeKind::Variable) {
      if (current->id == id) return true;
      continue;
    }
    if (!visited.insert(current->id).second) continue;
    switch (current->kind) {
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
  return false;
}

bool Unifier::bind(TypePtr variable, TypePtr type) {
  if (type->kind == TypeKind::Variable && type->id == variable->id) return true;
  if (occurs(variable->id, type)) return false;
  variable->link = std::move(type);
  return true;
}

std::vector<std::pair<Type *, TypePtr>> Unifier::snapshot(TypePtr left, TypePtr right) {
  std::vector<std::pair<Type *, TypePtr>> links;
  std::unordered_set<int> visited;
  std::vector<TypePtr> worklist{std::move(left), std::move(right)};
  while (!worklist.empty()) {
    TypePtr current = context_.resolve(worklist.back());
    worklist.pop_back();
    if (current == nullptr) continue;
    if (current->kind == TypeKind::Variable) {
      links.emplace_back(current.get(), current->link);
      continue;
    }
    if (!visited.insert(current->id).second) continue;
    switch (current->kind) {
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
  return links;
}

void Unifier::restore(const std::vector<std::pair<Type *, TypePtr>> &links) {
  for (const auto &entry : links) entry.first->link = entry.second;
}

bool Unifier::unifyUnion(TypePtr left, TypePtr right) {
  const auto links = snapshot(left, right);
  bool ok = true;
  if (left->kind == TypeKind::Union && right->kind == TypeKind::Union) {
    for (const auto &member : left->members) {
      bool matched = false;
      for (const auto &candidate : right->members) {
        if (unify(member, candidate)) {
          matched = true;
          break;
        }
      }
      if (!matched) {
        ok = false;
        break;
      }
    }
    if (ok) {
      for (const auto &member : right->members) {
        bool matched = false;
        for (const auto &candidate : left->members) {
          if (unify(member, candidate)) {
            matched = true;
            break;
          }
        }
        if (!matched) {
          ok = false;
          break;
        }
      }
    }
  } else {
    TypePtr other = left->kind == TypeKind::Union ? right : left;
    TypePtr unionType = left->kind == TypeKind::Union ? left : right;
    for (const auto &member : unionType->members) {
      if (!unify(member, other)) {
        ok = false;
        break;
      }
    }
  }
  if (!ok) restore(links);
  return ok;
}

bool Unifier::unify(TypePtr left, TypePtr right) {
  left = context_.resolve(std::move(left));
  right = context_.resolve(std::move(right));
  if (left == nullptr || right == nullptr) return false;
  if (left == right) return true;
  if (left->kind == TypeKind::Never || right->kind == TypeKind::Never) return true;
  if (left->kind == TypeKind::Variable) return bind(left, right);
  if (right->kind == TypeKind::Variable) return bind(right, left);
  if (left->kind == TypeKind::Union || right->kind == TypeKind::Union)
    return unifyUnion(left, right);
  if (left->kind != right->kind) return false;
  switch (left->kind) {
  case TypeKind::Function: {
    if (left->parameters.size() != right->parameters.size()) return false;
    for (std::size_t i = 0; i < left->parameters.size(); ++i)
      if (!unify(left->parameters[i], right->parameters[i])) return false;
    return unify(left->result, right->result);
  }
  case TypeKind::Struct: {
    if (left->fields.size() != right->fields.size()) return false;
    for (const auto &leftField : left->fields) {
      bool matched = false;
      for (const auto &rightField : right->fields) {
        if (leftField.first == rightField.first) {
          matched = true;
          if (!unify(leftField.second, rightField.second)) return false;
          break;
        }
      }
      if (!matched) return false;
    }
    return true;
  }
  case TypeKind::Array: return unify(left->element, right->element);
  default: return false;
  }
}

} // namespace rma::types
