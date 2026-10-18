#pragma once

#include "value.hh"
#include <optional>
#include <vector>

namespace edon::ecma {

class ObjectOperations final {
public:
  static bool sameKey(const PropertyKey &a, const PropertyKey &b);
  static std::optional<PropertyDescriptor> getOwnProperty(const ObjectPtr &object,
                                                          const PropertyKey &key);
  static bool defineOwnProperty(const ObjectPtr &object, const PropertyKey &key,
                                const PropertyDescriptor &descriptor);
  static bool defineDataProperty(const ObjectPtr &object, const PropertyKey &key, Value value,
                                 bool writable = true, bool enumerable = true,
                                 bool configurable = true);
  static bool hasProperty(const ObjectPtr &object, const PropertyKey &key);
  static Value get(const ObjectPtr &object, const PropertyKey &key,
                   const Value &receiver = Value::undefined());
  static bool set(const ObjectPtr &object, const PropertyKey &key, const Value &value,
                  const Value &receiver);
  static bool deleteProperty(const ObjectPtr &object, const PropertyKey &key);
  static std::vector<PropertyKey> ownPropertyKeys(const ObjectPtr &object);
  static bool setPrototypeOf(const ObjectPtr &object, ObjectPtr prototype);
  static bool preventExtensions(const ObjectPtr &object);

private:
  static std::optional<std::size_t> findOwnIndex(const ObjectPtr &object, const PropertyKey &key);
  static bool compatible(const ObjectPtr &object, const PropertyDescriptor &current,
                         const PropertyDescriptor &next);
  static Value defaultReceiver(const ObjectPtr &object, const Value &receiver);
};

} // namespace edon::ecma
