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

#ifndef BANT_TERM_COLOR_H
#define BANT_TERM_COLOR_H

#include <ostream>

#include "bant/session.h"

namespace bant {

class Colored {
 public:
  friend std::ostream &operator<<(std::ostream &o, const Colored &c) {
    // maybe also remember the stream to emit \033[0m when going out of scope,
    // at the end of a print expression, but that feels possibly too clever.
    if (c.escape_code_) {
      o << c.escape_code_;
    }
    return o;
  }

 protected:
  constexpr Colored(bool do_color, const char *escape_code)
      : escape_code_(do_color ? escape_code : nullptr) {}

 private:
  const char *const escape_code_;
};

class Bold : public Colored {
 public:
  explicit Bold(bool do_color) : Colored(do_color, "\033[1m") {}
  explicit Bold(const Session &s) : Bold(s.flags().do_color) {}
};

class Dim : public Colored {
 public:
  explicit Dim(bool do_color) : Colored(do_color, "\033[2m") {}
  explicit Dim(const Session &s) : Dim(s.flags().do_color) {}
};

class BoldOff : public Colored {
 public:
  explicit BoldOff(bool do_color) : Colored(do_color, "\033[22m") {}
  explicit BoldOff(const Session &s) : BoldOff(s.flags().do_color) {}
};

class Invert : public Colored {
 public:
  explicit Invert(bool do_color) : Colored(do_color, "\033[7m") {}
  explicit Invert(const Session &s) : Invert(s.flags().do_color) {}
};

class Red : public Colored {
 public:
  explicit Red(bool do_color) : Colored(do_color, "\033[31m") {}
  explicit Red(const Session &s) : Red(s.flags().do_color) {}
};

class Magenta : public Colored {
 public:
  explicit Magenta(bool do_color) : Colored(do_color, "\033[35m") {}
  explicit Magenta(const Session &s) : Magenta(s.flags().do_color) {}
};

class BlueBold : public Colored {
 public:
  explicit BlueBold(bool do_color) : Colored(do_color, "\033[34;1m") {}
  explicit BlueBold(const Session &s) : BlueBold(s.flags().do_color) {}
};

class YellowOnRedBold : public Colored {
 public:
  explicit YellowOnRedBold(bool do_color)
      : Colored(do_color, "\033[1;33;41m") {}
  explicit YellowOnRedBold(const Session &s)
      : YellowOnRedBold(s.flags().do_color) {}
};

class Norm : public Colored {
 public:
  explicit Norm(bool do_color) : Colored(do_color, "\033[0m") {}
  explicit Norm(const Session &s) : Norm(s.flags().do_color) {}
};
}  // namespace bant
#endif  // BANT_TERM_COLOR_H
