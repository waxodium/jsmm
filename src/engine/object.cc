#include "object.hh"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <limits>

namespace edon::ecma {

bool ObjectOperations::sameKey(const PropertyKey &a, const PropertyKey &b) {
  if (a.value.index() != b.value.index()) return false;
  if (a.isString()) return a.asString() == b.asString();
  return a.asSymbol() == b.asSymbol();
}

std::optional<std::size_t> ObjectOperations::findOwnIndex(const ObjectPtr &object,
                                                          const PropertyKey &key) {
  if (!object) return std::nullopt;
  for (std::size_t i = 0; i < object->properties.size(); ++i) {
    if (sameKey(object->properties[i].first, key)) return i;
  }
  return std::nullopt;
}

std::optional<PropertyDescriptor> ObjectOperations::getOwnProperty(const ObjectPtr &object,
                                                                   const PropertyKey &key) {
  const auto index = findOwnIndex(object, key);
  if (!index) return std::nullopt;
  return object->properties[*index].second;
}

bool ObjectOperations::compatible(const ObjectPtr &object, const PropertyDescriptor &current,
                                  const PropertyDescriptor &next) {
  if (current.configurable) return true;
  if (next.configurable || next.enumerable != current.enumerable || next.kind != current.kind)
    return false;
  if (current.kind == DescriptorKind::Data && !current.writable) {
    if (next.writable) return false;
    const auto &a = current.value.storage();
    const auto &b = next.value.storage();
    if (a.index() != b.index()) return false;
    if (next.value.isObject()) {
      if (next.value.asObject() != current.value.asObject()) return false;
    } else if (next.value.isSymbol()) {
      if (next.value.asSymbol() != current.value.asSymbol()) return false;
    } else if (next.value.isNumber()) {
      const double x = next.value.asNumber(), y = current.value.asNumber();
      if (!(x == y || (std::isnan(x) && std::isnan(y)))) return false;
    } else if (next.value.isString()) {
      if (next.value.asString() != current.value.asString()) return false;
    } else if (next.value.isBoolean() && next.value.asBoolean() != current.value.asBoolean()) {
      return false;
    }
  }
  if (current.kind == DescriptorKind::Accessor &&
      (next.getter != current.getter || next.setter != current.setter))
    return false;
  (void)object;
  return true;
}

bool ObjectOperations::defineOwnProperty(const ObjectPtr &object, const PropertyKey &key,
                                         const PropertyDescriptor &descriptor) {
  if (!object) return false;
  const auto index = findOwnIndex(object, key);
  if (!index) {
    if (!object->extensible) return false;
    object->properties.emplace_back(key, descriptor);
    return true;
  }
  const auto &current = object->properties[*index].second;
  if (!compatible(object, current, descriptor)) return false;
  object->properties[*index].second = descriptor;
  return true;
}

bool ObjectOperations::defineDataProperty(const ObjectPtr &object, const PropertyKey &key,
                                          Value value, bool writable, bool enumerable,
                                          bool configurable) {
  return defineOwnProperty(
      object, key, PropertyDescriptor::data(std::move(value), writable, enumerable, configurable));
}

bool ObjectOperations::hasProperty(const ObjectPtr &object, const PropertyKey &key) {
  for (auto current = object; current; current = current->prototype) {
    if (findOwnIndex(current, key)) return true;
  }
  return false;
}

Value ObjectOperations::defaultReceiver(const ObjectPtr &object, const Value &receiver) {
  if (!receiver.isUndefined()) return receiver;
  return Value(object);
}

Value ObjectOperations::get(const ObjectPtr &object, const PropertyKey &key,
                            const Value &receiver) {
  for (auto current = object; current; current = current->prototype) {
    const auto index = findOwnIndex(current, key);
    if (!index) continue;
    const auto &descriptor = current->properties[*index].second;
    if (descriptor.kind == DescriptorKind::Data) return descriptor.value;
    if (!descriptor.getter) return Value::undefined();
    return (*descriptor.getter)(defaultReceiver(object, receiver));
  }
  return Value::undefined();
}

bool ObjectOperations::set(const ObjectPtr &object, const PropertyKey &key, const Value &value,
                           const Value &receiver) {
  if (!object) return false;
  auto own = getOwnProperty(object, key);
  if (!own) {
    if (object->prototype)
      return set(object->prototype, key, value, defaultReceiver(object, receiver));
    own = PropertyDescriptor::data(Value::undefined(), true, true, true);
  }
  if (own->kind == DescriptorKind::Accessor) {
    if (!own->setter) return false;
    (*own->setter)(defaultReceiver(object, receiver), value);
    return true;
  }
  if (!own->writable) return false;

  const Value actualReceiver = defaultReceiver(object, receiver);
  if (!actualReceiver.isObject() || !actualReceiver.asObject()) return false;
  const auto receiverObject = actualReceiver.asObject();
  auto existing = getOwnProperty(receiverObject, key);
  if (existing) {
    if (existing->kind == DescriptorKind::Accessor || !existing->writable) return false;
    existing->value = value;
    return defineOwnProperty(receiverObject, key, *existing);
  }
  return defineOwnProperty(receiverObject, key, PropertyDescriptor::data(value, true, true, true));
}

bool ObjectOperations::deleteProperty(const ObjectPtr &object, const PropertyKey &key) {
  const auto index = findOwnIndex(object, key);
  if (!index) return true;
  if (!object->properties[*index].second.configurable) return false;
  object->properties.erase(object->properties.begin() + static_cast<std::ptrdiff_t>(*index));
  return true;
}

std::vector<PropertyKey> ObjectOperations::ownPropertyKeys(const ObjectPtr &object) {
  std::vector<PropertyKey> indices;
  std::vector<PropertyKey> strings;
  std::vector<PropertyKey> symbols;
  if (!object) return {};

  auto arrayIndex = [](const std::string &text, std::uint32_t &index) {
    if (text.empty() || (text.size() > 1 && text.front() == '0')) return false;
    std::uint64_t parsed = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
        parsed > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) - 1)
      return false;
    index = static_cast<std::uint32_t>(parsed);
    return true;
  };

  std::vector<std::pair<std::uint32_t, PropertyKey>> orderedIndices;
  for (const auto &property : object->properties) {
    if (!property.first.isString()) {
      symbols.push_back(property.first);
      continue;
    }
    std::uint32_t index = 0;
    if (arrayIndex(property.first.asString(), index))
      orderedIndices.emplace_back(index, property.first);
    else
      strings.push_back(property.first);
  }
  std::sort(orderedIndices.begin(), orderedIndices.end(),
            [](const auto &a, const auto &b) { return a.first < b.first; });
  indices.reserve(orderedIndices.size());
  for (const auto &entry : orderedIndices) indices.push_back(entry.second);

  indices.insert(indices.end(), strings.begin(), strings.end());
  indices.insert(indices.end(), symbols.begin(), symbols.end());
  return indices;
}

bool ObjectOperations::setPrototypeOf(const ObjectPtr &object, ObjectPtr prototype) {
  if (!object) return false;
  if (object->prototype == prototype) return true;
  if (!object->extensible) return false;
  for (auto current = prototype; current; current = current->prototype) {
    if (current == object) return false;
  }
  object->prototype = std::move(prototype);
  return true;
}

bool ObjectOperations::preventExtensions(const ObjectPtr &object) {
  if (!object) return false;
  object->extensible = false;
  return true;
}

} // namespace edon::ecma
