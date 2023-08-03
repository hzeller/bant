#include <fstream>
#include <optional>

#include "parser.h"

std::optional<std::string> ReadFileToString(const char *filename) {
  std::ifstream is(filename, std::ifstream::binary);
  if (!is.good()) return std::nullopt;
  std::string result;
  char buffer[4096];
  for (;;) {
    is.read(buffer, sizeof(buffer));
    result.append(buffer, is.gcount());
    if (!is.good()) break;
  }
  return result;
}

static int usage(const char *prog) {
  fprintf(stderr, "Usage: %s <filename> [<filename>...]\n", prog);
  return 1;
}

int main(int argc, char *argv[]) {
  if (argc <= 1) return usage(argv[0]);
  int file_count = 0;
  int file_error_count = 0;

  Arena arena(1 << 16);
  for (int i = 1; i < argc; ++i) {
    const char *const filename = argv[i];
    std::optional<std::string> content = ReadFileToString(filename);
    if (!content.has_value()) {
      std::cerr << "Could not read " << filename << "\n";
      ++file_error_count;
      continue;
    }
    ++file_count;
    Scanner scanner(*content);
    Parser parser(&scanner, &arena, filename, std::cerr);
    List *const statements = parser.parse();
    if (statements) {
      std::cerr << "------- file " << filename << "\n";
      PrintVisitor printer(std::cout);
      statements->Accept(&printer);
      std::cout << "\n";
    }
    if (parser.parse_error()) {
      const Token last = parser.lastToken();
      std::cout << filename << ":" << scanner.GetPos(last.text)
                << " FAILED AT '" << last.text << "' ----------------- \n";
      ++file_error_count;
    }
  }

  fprintf(stderr, "Scanned %d files; %d file with issues.\n", file_count,
          file_error_count);

  return file_error_count;
}
