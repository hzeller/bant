// bant - Bazel Navigation Tool
// Copyright (C) 2026 Henner Zeller <h.zeller@acm.org>
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

// Until we have re2c in BCR, let's manually do this
// re2c -T bant/tool/cc-preprocessor.re > bant/tool/cc-preprocessor.cc//

#ifndef BANT_CC_PREPROCESSOR_H
#define BANT_CC_PREPROCESSOR_H

#include <string_view>
#include <vector>

#include "bant/tool/preprocess-utils.h"

namespace bant {
// Preproccess source and extract includes (description of TaggedInclude
// see in public ExtractCCIncludes()).
// Gets initial macro definitions, which are updated when processing
// `#define` statements.
std::vector<TaggedInclude> PreprocessInternal(std::string_view source,
                                              DefineMap &defines);
}  // namespace bant
#endif  // BANT_CC_PREPROCESSOR_H
