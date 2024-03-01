workspace(name = "bant")

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

# Ideally, we'd get gtest from pkg-config, but for that probably need bzlmod first.
http_archive(
    name = "googletest",
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

http_archive(
    name = "abseil-cpp",
    sha256 = "3c743204df78366ad2eaf236d6631d83f6bc928d1705dd0000b872e53b73dc6a",
    strip_prefix = "abseil-cpp-20240116.1",
    urls = ["https://github.com/abseil/abseil-cpp/archive/refs/tags/20240116.1.tar.gz"],
)

http_archive(
    name = "re2",
    sha256 = "7b2b3aa8241eac25f674e5b5b2e23d4ac4f0a8891418a2661869f736f03f57f4",
    strip_prefix = "re2-2024-03-01",
    urls = ["https://github.com/google/re2/archive/refs/tags/2024-03-01.tar.gz"],
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

# 'make install' equivalent rule 2023-02-21
http_archive(
    name = "com_github_google_rules_install",
    sha256 = "aba3c1ae179beb92c1fc4502d66d7d7c648f90eb51897aa4b0ae4a76ce225eec",
    strip_prefix = "bazel_rules_install-6001facc1a96bafed0e414a529b11c1819f0cdbe",
    urls = ["https://github.com/google/bazel_rules_install/archive/6001facc1a96bafed0e414a529b11c1819f0cdbe.zip"],
)

load("@com_github_google_rules_install//:deps.bzl", "install_rules_dependencies")

install_rules_dependencies()

load("@com_github_google_rules_install//:setup.bzl", "install_rules_setup")

install_rules_setup()

http_archive(
    name = "rules_license",
    sha256 = "241b06f3097fd186ff468832150d6cc142247dc42a32aaefb56d0099895fd229",
    urls = [
        "https://github.com/bazelbuild/rules_license/releases/download/0.0.4/rules_license-0.0.8.tar.gz",
        "https://mirror.bazel.build/github.com/bazelbuild/rules_license/releases/download/0.0.8/rules_license-0.0.8.tar.gz",
    ],
)
