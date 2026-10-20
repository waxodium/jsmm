#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace rma::emit {

inline constexpr std::string_view entrySymbol = "rma_entry";
inline constexpr std::string_view startSymbol = "_start";
inline constexpr std::string_view allocSymbol = "rma_alloc";
inline constexpr std::string_view moduloSymbol = "rma_mod";
inline constexpr std::string_view powerSymbol = "rma_pow";
inline constexpr std::string_view boundsSymbol = "rma_oob";
inline constexpr std::string_view arenaSymbol = "rma_arena";
inline constexpr std::string_view arenaPointerSymbol = "rma_arena_pointer";

inline constexpr std::uint64_t arenaSize = 64ULL * 1024ULL * 1024ULL;

class HelperRegistry final {
public:
  void declare(std::string symbol, std::string text);
  void use(const std::string &symbol);
  [[nodiscard]] bool used(const std::string &symbol) const;
  void reset();
  [[nodiscard]] std::string assemble() const;

private:
  std::unordered_map<std::string, std::string> available_;
  std::vector<std::string> order_;
  std::vector<std::string> used_;
};

}
