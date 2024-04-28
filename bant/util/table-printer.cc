// bant - Bazel Navigation Tool
// Copyright (C) 2024 Henner Zeller <h.zeller@acm.org>
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
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

#include "bant/util/table-printer.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include "absl/log/check.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_format.h"
#include "bant/output-format.h"

namespace bant {
namespace {
// The AlignedTextColumnPrinter needs to collect all the rows first to know
// how wide columns are.
class AlignedTextColumnPrinter : public TablePrinter {
 public:
  AlignedTextColumnPrinter(std::ostream &out,
                           const std::vector<std::string> &headers)
      : out_(out), headers_(headers), widths_(headers.size()) {}

  void AddRow(const std::vector<std::string> &row) final {
    CHECK_EQ(row.size(), widths_.size());
    // We exclude the last column width, as we don't want that to to be padded
    for (size_t i = 0; i < widths_.size() - 1; ++i) {
      widths_[i] = std::max(widths_[i], (int)row[i].length());
    }
    buffer_.push_back(row);
  }

  void Finish() final {
    for (const auto &row : buffer_) {
      for (size_t i = 0; i < widths_.size(); ++i) {
        out_ << absl::StrFormat("%*s", -widths_[i] - 1, row[i]);
      }
      out_ << "\n";
    }
  }

 private:
  std::ostream &out_;
  const std::vector<std::string> headers_;
  // Buffer to keep while determining the print width;
  std::vector<int> widths_;
  std::vector<std::vector<std::string>> buffer_;
};

class SExprTablePrinter : public TablePrinter {
 public:
  SExprTablePrinter(std::ostream &out, const std::vector<std::string> &headers,
                    bool as_plist)
      : out_(out), as_plist_(as_plist), headers_(headers) {
    out_ << "(";
  }

  void AddRow(const std::vector<std::string> &row) final {
    out_ << (row_printed_ ? "\n (" : "(");
    for (size_t c = 0; c < row.size(); ++c) {
      if (c != 0) out_ << " ";
      if (as_plist_) out_ << ":" << headers_[c] << " ";
      out_ << "\"" << absl::CEscape(row[c]) << "\"";
    }
    out_ << ")";
    row_printed_ = true;
  }

  void Finish() final { out_ << ")\n"; }

 private:
  std::ostream &out_;
  const bool as_plist_;
  const std::vector<std::string> headers_;
  bool row_printed_ = false;
};

class JSonTablePrinter : public TablePrinter {
 public:
  JSonTablePrinter(std::ostream &out, const std::vector<std::string> &headers)
      : out_(out), headers_(headers) {}

  void AddRow(const std::vector<std::string> &row) final {
    out_ << "{";
    for (size_t c = 0; c < row.size(); ++c) {
      if (c != 0) out_ << ", ";
      out_ << "\"" << absl::CEscape(headers_[c]) << "\": ";
      out_ << "\"" << absl::CEscape(row[c]) << "\"";
    }
    out_ << "}\n";
  }

  void Finish() final {}

 private:
  std::ostream &out_;
  const std::vector<std::string> headers_;
};

class CSVTablePrinter : public TablePrinter {
 public:
  CSVTablePrinter(std::ostream &out, const std::vector<std::string> &headers)
      : out_(out) {
    for (size_t c = 0; c < headers.size(); ++c) {
      if (c != 0) out_ << ",";
      out_ << "\"" << absl::CEscape(headers[c]) << "\"";
    }
    out_ << "\n";
  }

  void AddRow(const std::vector<std::string> &row) final {
    for (size_t c = 0; c < row.size(); ++c) {
      if (c != 0) out_ << ",";
      out_ << "\"" << absl::CEscape(row[c]) << "\"";
    }
    out_ << "\n";
  }

  void Finish() final {}

 private:
  std::ostream &out_;
};
}  // namespace

std::unique_ptr<TablePrinter> TablePrinter::Create(
  std::ostream &out, OutputFormat format,
  const std::vector<std::string> &headers) {
  switch (format) {
  case OutputFormat::kSExpr:
  case OutputFormat::kPList:
    return std::make_unique<SExprTablePrinter>(out, headers,
                                               format == OutputFormat::kPList);
  case OutputFormat::kJSON:
    return std::make_unique<JSonTablePrinter>(out, headers);
  case OutputFormat::kCSV:
    return std::make_unique<CSVTablePrinter>(out, headers);
  default: return std::make_unique<AlignedTextColumnPrinter>(out, headers);
  }
}
}  // namespace bant
