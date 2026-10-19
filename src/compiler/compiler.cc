#include "compiler.hh"
#include "codegen.hh"
#include "elf.hh"
#include "engine/lexer.hh"
#include "parser.hh"
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <utility>
namespace rma::compiler {
std::vector<std::uint8_t> compileSource(std::string_view source) {
  std::string input(source);
  syntax::Lexer lexer(input);
  auto tokens = lexer.tokenize();
  if (lexer.failed()) {
    const auto &e = lexer.error();
    throw std::runtime_error("lexer error at " + std::to_string(e.position.line) + ":" +
                             std::to_string(e.position.column) + ": " + e.message);
  }
  Parser parser(std::move(tokens));
  auto program = parser.parse();
  auto image = CodeGenerator{}.generate(program);
  return ElfWriter{}.write(image);
}
void Compiler::compileFile(const std::string &inPath, const std::string &outPath) const {
  std::ifstream in(inPath, std::ios::binary);
  if (!in) throw std::runtime_error("cannot open source file: " + inPath);
  std::string source{std::istreambuf_iterator<char>(in), {}};
  auto bytes = compileSource(source);
  std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
  if (!out) throw std::runtime_error("cannot create output file: " + outPath);
  out.write(reinterpret_cast<const char *>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
  out.close();
  if (!out) throw std::runtime_error("failed writing output file: " + outPath);
  if (chmod(outPath.c_str(), 0755) != 0)
    throw std::runtime_error("cannot make output executable: " + outPath);
}
} // namespace rma::compiler
