workspace(name = "bant")

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

# Ideally, we'd get gtest from pkg-config, but for that probably need bzlmod first.
http_archive(
    name = "com_google_googletest",
    sha256 = "24564e3b712d3eb30ac9a85d92f7d720f60cc0173730ac166f27dda7fed76cb2",
    strip_prefix = "googletest-release-1.12.1",
    urls = ["https://github.com/google/googletest/archive/refs/tags/release-1.12.1.zip"],
)

# skylib needed by absl
http_archive(
    name = "bazel_skylib",
    sha256 = "74d544d96f4a5bb630d465ca8bbcfe231e3594e5aae57e1edbf17a6eb3ca2506",
    urls = [
        "https://mirror.bazel.build/github.com/bazelbuild/bazel-skylib/releases/download/1.3.0/bazel-skylib-1.3.0.tar.gz",
        "https://github.com/bazelbuild/bazel-skylib/releases/download/1.3.0/bazel-skylib-1.3.0.tar.gz",
    ],
)

load("@bazel_skylib//:workspace.bzl", "bazel_skylib_workspace")

# absl needed by RE2
http_archive(
    name = "com_google_absl",
    sha256 = "338420448b140f0dfd1a1ea3c3ce71b3bc172071f24f4d9a57d59b45037da440",
    strip_prefix = "abseil-cpp-20240116.0",
    urls = ["https://github.com/abseil/abseil-cpp/archive/refs/tags/20240116.0.tar.gz"],
)

http_archive(
    name = "com_googlesource_code_re2",
    sha256 = "cd191a311b84fcf37310e5cd876845b4bf5aee76fdd755008eef3b6478ce07bb",
    strip_prefix = "re2-2024-02-01",
    urls = ["https://github.com/google/re2/archive/refs/tags/2024-02-01.tar.gz"],
)

# 2024-02-06. Compilation database
http_archive(
    name = "rules_compdb",
    sha256 = "70232adda61e89a4192be43b4719d35316ed7159466d0ab4f3da0ecb1fbf00b2",
    strip_prefix = "bazel-compilation-database-fa872dd80742b3dccd79a711f52f286cbde33676",
    urls = ["https://github.com/grailbio/bazel-compilation-database/archive/fa872dd80742b3dccd79a711f52f286cbde33676.tar.gz"],
)

load("@rules_compdb//:deps.bzl", "rules_compdb_deps")

rules_compdb_deps()
