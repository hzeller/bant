// -*- mode: c++; fill-column: 9999; -*-
// Until we have re2c in BCR, let's manually do this
// re2c -T bant/tool/cc-preprocessor.re > bant/tool/cc-preprocessor.cc//

#include "bant/tool/preprocess-utils.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "absl/strings/str_cat.h"

namespace bant {
std::vector<TaggedInclude> PreprocessInternal(std::string_view source,
                                              const DefineMap &d) {
  DefineMap defines(d);
  struct BranchState {
    bool is_active;
    bool is_ambiguous;
  };
  std::vector<BranchState> condition_stack;
  bool current_emitting = true;

  auto update_emitting_state = [&]() {
    current_emitting =
      condition_stack.empty() ? true : condition_stack.back().is_active;
  };

  auto is_ambiguous = [&]() {
    return condition_stack.empty() ? false
                                   : condition_stack.back().is_ambiguous;
  };

  std::vector<TaggedInclude> result;
  const char *YYCURSOR = source.data();
  const char *YYLIMIT = source.data() + source.size();
  const char *YYMARKER;
  const char *token_start;
  const char *yyt1, *yyt2, *yyt3;
  const char *k_start, *k_end;
  const char *v_start, *v_end;
  const char *b_start, *b_end;
  const char *i_start, *i_end;

  while (YYCURSOR < YYLIMIT) {
    token_start = YYCURSOR;

    /*!re2c
        re2c:define:YYCTYPE = char;
        re2c:yyfill:enable  = 0;
        re2c:define:YYLESSTHAN = "YYLIMIT - YYCURSOR < @@";

        // Match preprocessor directives
        [ \t]* "#" [ \t]* "if" [ \t]+(@v_start[a-zA-Z0-9_]+@v_end)
        {
          const std::string_view value(v_start, v_end - v_start);
          auto parsed = ParsePreprocessValue(value, defines);
          bool is_active = current_emitting && parsed.is_on;
          condition_stack.emplace_back(is_active, parsed.is_ambiguous);
          update_emitting_state();

          continue;
        }

        [ \t]* "#" [ \t]* (@k_start("ifdef"|"ifndef")@k_end) [ \t]+(@v_start[a-zA-Z0-9_]+@v_end)
        {
          const std::string_view keyword(k_start, k_end - k_start);
          const std::string_view macro(v_start, v_end - v_start);

          const bool is_ifndef = (keyword == "ifndef");
          const bool macro_known = defines.contains(macro);
          bool is_active = current_emitting && (is_ifndef ^ macro_known);
          condition_stack.emplace_back(is_active, !macro_known);
          update_emitting_state();

          continue;
        }

        [ \t]* "#" [ \t]* "else"
        {
          if (!condition_stack.empty()) {
            condition_stack.back().is_active ^= true;
            update_emitting_state();
          }
          continue;
        }

        [ \t]* "#" [ \t]* "endif"
        {
          if (!condition_stack.empty()) {
            condition_stack.pop_back();
            update_emitting_state();
          }
          continue;
        }

        [ \t]* "#" [ \t]* "define" [ \t]+ (@k_start[a-zA-Z0-9_]+@k_end) [ \t]+ (@v_start[a-zA-Z0-9_]+@v_end)
        {
          const std::string_view macro(k_start, k_end - k_start);
          const std::string_view value(v_start, v_end - v_start);
          if (current_emitting) {
            defines[macro] = ParsePreprocessValue(value, defines).is_on;
          }
          continue;
        }

        // Match include.
        [ \t]* "#" [ \t]* "include" [ \t]* (@b_start[<"]@b_end)(@i_start[^>"]*@i_end)[">]
        {
          const std::string_view bracket(b_start, b_end - b_start);
          const std::string_view inc(i_start, i_end - i_start);
          if (current_emitting || is_ambiguous()) {
             result.emplace_back(inc, bracket[0] == '<', !current_emitting);
          }
          continue;
        }

        // Skip EOL comments.
        "//"
	{
            while (YYCURSOR < YYLIMIT && *YYCURSOR != '\n') { YYCURSOR++; }
            continue;
        }

        // skip block comments
        "/*"
	{
          // Fast-forward until end of block comment
          while (YYCURSOR < YYLIMIT) {
            if (*YYCURSOR == '*' && (YYCURSOR + 1 < YYLIMIT) && *(YYCURSOR + 1) == '/') {
              YYCURSOR += 2;
              break;
            }
            YYCURSOR++;
          }
          continue;
        }

        // skip raw string literals
        "R\"" [a-zA-Z0-9_\-.?*+^$()\[\]{}|]* "("
	{
            std::string_view opening_match(token_start, YYCURSOR - token_start);
            std::string_view delim = opening_match.substr(2, opening_match.size() - 3);

            std::string close_seq = absl::StrCat(")", delim, "\"");
            std::string_view remainder(YYCURSOR, YYLIMIT - YYCURSOR);

            size_t close_pos = remainder.find(close_seq);
            if (close_pos != std::string_view::npos) {
                YYCURSOR += close_pos + close_seq.size();
            } else {
                YYCURSOR = YYLIMIT;
            }
            continue;
        }

        // fallback any other
        * {
            continue;
        }
    */
  }
  return result;
}
}  // namespace bant
