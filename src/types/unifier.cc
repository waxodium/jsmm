#include "types/unifier.hh"

#include <set>
#include <unordered_set>
#include <vector>

namespace jsmm::types {

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

bool Unifier::membersMatch(const std::vector<TypePtr> &from, const std::vector<TypePtr> &to) {
  for (const auto &member : from) {
    bool matched = false;
    for (const auto &candidate : to) {
      const auto attempt = snapshot(member, candidate);
      if (unify(member, candidate)) {
        matched = true;
        break;
      }
      restore(attempt);
    }
    if (!matched) return false;
  }
  return true;
}

bool Unifier::assignable(TypePtr value, TypePtr target) {
  value = context_.resolve(std::move(value));
  target = context_.resolve(std::move(target));
  if (value == nullptr || target == nullptr) return false;
  if (value == target) return true;
  if (value->kind == TypeKind::Never || target->kind == TypeKind::Never) return true;
  if (value->kind == TypeKind::Variable) return bind(value, target);
  if (target->kind == TypeKind::Variable) return bind(target, value);

  const bool valueUnion = value->kind == TypeKind::Union;
  const bool targetUnion = target->kind == TypeKind::Union;

  if (valueUnion && targetUnion) {
    const auto links = snapshot(value, target);
    for (const auto &member : value->members) {
      if (!memberAssignable(member, target->members)) {
        restore(links);
        return false;
      }
    }
    return true;
  }
  if (targetUnion) {
    const auto links = snapshot(value, target);
    for (const auto &candidate : target->members) {
      const auto attempt = snapshot(value, candidate);
      if (assignable(value, candidate)) return true;
      restore(attempt);
    }
    restore(links);
    return false;
  }
  if (valueUnion) {
    const auto links = snapshot(value, target);
    for (const auto &member : value->members) {
      if (!assignable(member, target)) {
        restore(links);
        return false;
      }
    }
    return true;
  }
  return unify(value, target);
}

bool Unifier::memberAssignable(TypePtr value, const std::vector<TypePtr> &candidates) {
  for (const auto &candidate : candidates) {
    const auto attempt = snapshot(value, candidate);
    if (assignable(value, candidate)) return true;
    restore(attempt);
  }
  return false;
}

bool Unifier::unifyUnion(TypePtr left, TypePtr right) {
  const auto links = snapshot(left, right);
  bool ok = true;
  if (left->kind == TypeKind::Union && right->kind == TypeKind::Union) {
    ok = membersMatch(left->members, right->members) && membersMatch(right->members, left->members);
  } else {
    const TypePtr other = left->kind == TypeKind::Union ? right : left;
    const TypePtr unionType = left->kind == TypeKind::Union ? left : right;
    ok = false;
    for (const auto &member : unionType->members) {
      const auto attempt = snapshot(member, other);
      if (unify(member, other)) {
        ok = true;
        break;
      }
      restore(attempt);
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

} // namespace jsmm::types
