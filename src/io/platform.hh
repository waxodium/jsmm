#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace jsmm::io {

void writeFile(const std::string &path, std::string_view content);
void removeFile(const std::string &path);
int runCommand(const std::vector<std::string> &command);

} // namespace jsmm::io
