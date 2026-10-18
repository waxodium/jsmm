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

namespace rma::compiler {

void Compiler::compileFile(const std::string &inputPath, const std::string &outputPath) const {
  std::ifstream input(inputPath, std::ios::binary);
  if (!input) throw std::runtime_error("cannot open source file: " + inputPath);
  const std::string source{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};

  edon::ecma::Lexer lexer(source);
  auto tokens = lexer.tokenize();
  if (lexer.failed()) {
    const auto &error = lexer.error();
    throw std::runtime_error("lexer error at " + std::to_string(error.position.line) + ":" +
                             std::to_string(error.position.column) + ": " + error.message);
  }
  Parser parser(std::move(tokens));
  const Program program = parser.parse();
  const NativeImage image = CodeGenerator{}.generate(program);
  const auto executable = ElfWriter{}.write(image);

  std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
  if (!output) throw std::runtime_error("cannot create output file: " + outputPath);
  output.write(reinterpret_cast<const char *>(executable.data()),
               static_cast<std::streamsize>(executable.size()));
  output.close();
  if (!output) throw std::runtime_error("failed writing output file: " + outputPath);
  if (chmod(outputPath.c_str(), 0755) != 0)
    throw std::runtime_error("cannot make output executable: " + outputPath);
}

} // namespace rma::compiler
