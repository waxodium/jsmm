#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rma::cli {

std::string readFile(const std::string &path);

void writeExecutable(const std::string &path, const std::vector<std::uint8_t> &bytes);

} 
