#pragma once

#include <string>
#include <string_view>

namespace jsmm::mode {

enum class Mode { Unsafe, Safe };

struct Capabilities {
  bool pointerArithmetic = false;
  bool pointerToPointer = false;
  bool uncheckedDereference = false;
};

struct ModeSpec {
  Mode mode;
  Capabilities capabilities;
};

// Unsafe mode policy (all capabilities enabled)
constexpr ModeSpec kUnsafePolicy{Mode::Unsafe, Capabilities{true, true, true}};

// Safe mode policy (capabilities gated by mode checks)
constexpr ModeSpec kSafePolicy{Mode::Safe, Capabilities{false, false, false}};

const ModeSpec &policyFor(Mode mode);
bool directiveNamesMode(std::string_view raw, Mode &mode);
bool looksLikeDirective(std::string_view raw);
std::string_view directiveName(std::string_view raw);
std::string availableDirectives();

} // namespace jsmm::mode