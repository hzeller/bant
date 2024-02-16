#pragma once

#include <stdio.h>

#include <map>
#include <string>

#include "project-parser.h"

namespace bant {
using HeaderToTargetMap = std::map<std::string, std::string>;
HeaderToTargetMap ExtractHeaderToLibMapping(const ParsedProject &project);

void PrintLibraryHeaders(FILE *out, const ParsedProject &project);
}  // namespace bant
