#include "platform.hh"

#include <fstream>
#include <iterator>
#include <stdexcept>
#include <sys/stat.h>

namespace rma::cli {

std::string readFile(const std::string &path) {
  std::ifstream input(path, std::ios::binary);

  if (!input) throw std::runtime_error("cannot open source file: " + path);

  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void writeExecutable(const std::string &path, const std::vector<std::uint8_t> &bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);

  if (!output) throw std::runtime_error("cannot create output file: " + path);

  output.write(reinterpret_cast<const char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));

  output.close();

  if (!output) throw std::runtime_error("failed writing output file: " + path);

  if (chmod(path.c_str(), 0755) != 0)
    throw std::runtime_error("cannot make output executable: " + path);
}

} // namespace rma::cli
