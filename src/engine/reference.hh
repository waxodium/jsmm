#pragma once

#include "value.hh"
#include <optional>
#include <string>
#include <utility>

namespace edon::ecma {

enum class ReferenceKind { Invalid, Binding, Property };

struct Reference {
  ReferenceKind kind = ReferenceKind::Invalid;
  std::string name;
  ObjectPtr base;
  std::optional<PropertyKey> key;
  bool strict = false;

  static Reference binding(std::string name, bool strict = false) {
    return {ReferenceKind::Binding, std::move(name), {}, std::nullopt, strict};
  }
  static Reference property(ObjectPtr base, std::string key, bool strict = false) {
    PropertyKey propertyKey(key);
    return {ReferenceKind::Property, std::move(key), std::move(base), std::move(propertyKey),
            strict};
  }
  static Reference property(ObjectPtr base, PropertyKey key, bool strict = false) {
    std::string displayName;
    if (key.isString()) displayName = key.asString();
    return {ReferenceKind::Property, std::move(displayName), std::move(base), std::move(key),
            strict};
  }
  bool isValid() const noexcept { return kind != ReferenceKind::Invalid; }
};

} // namespace edon::ecma
