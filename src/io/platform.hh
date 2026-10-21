#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace rma::io {

void writeFile(const std::string &path, std::string_view content);
void removeFile(const std::string &path);
int runCommand(const std::vector<std::string> &command);

} // namespace rma::io
