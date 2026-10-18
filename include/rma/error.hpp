#pragma once

#include <cstdint>
#include <string_view>

namespace rma {

enum class Error : std::uint16_t {
  None = 0,

  InvalidArgument = 1,
  InvalidState = 2,

  IoOpen = 10,
  IoRead = 11,
  IoWrite = 12,
  IoPermission = 13,

  Lexical = 20,
  Syntax = 21,
  Semantic = 22,
  CodeGeneration = 23,
  ExecutableFormat = 24,

  Unsupported = 30,
  Internal = 255
};

[[nodiscard]] constexpr std::string_view toString(Error error) noexcept {
  switch (error) {
  case Error::None: return "none";
  case Error::InvalidArgument: return "invalid_argument";
  case Error::InvalidState: return "invalid_state";
  case Error::IoOpen: return "io_open";
  case Error::IoRead: return "io_read";
  case Error::IoWrite: return "io_write";
  case Error::IoPermission: return "io_permission";
  case Error::Lexical: return "lexical";
  case Error::Syntax: return "syntax";
  case Error::Semantic: return "semantic";
  case Error::CodeGeneration: return "code_generation";
  case Error::ExecutableFormat: return "executable_format";
  case Error::Unsupported: return "unsupported";
  case Error::Internal: return "internal";
  }

  return "unknown";
}

} // namespace rma
