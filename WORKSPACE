# -*- Python -*-
workspace(name = "bant")

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

#-- ideally, we want to get things from pkg-config, but doesn't work yet.
#http_archive(
#    name = "bazel_pkg_config",
#    strip_prefix = "bazel_pkg_config-master",
#    urls = ["https://github.com/cherrry/bazel_pkg_config/archive/master.zip"],
#    sha256 = "6e2c08b957137bc793257fc4bfa577a462b0bf74914a2b349f4392157a0ed026"#,
#)

#load("@bazel_pkg_config//:pkg_config.bzl", "pkg_config")

#pkg_config(
#    name = "gtest_main",
#)

http_archive(
    name = "com_google_googletest",
    sha256 = "24564e3b712d3eb30ac9a85d92f7d720f60cc0173730ac166f27dda7fed76cb2",
    strip_prefix = "googletest-release-1.12.1",
    urls = ["https://github.com/google/googletest/archive/refs/tags/release-1.12.1.zip"],
)
