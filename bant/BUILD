load("@com_github_google_rules_install//installer:def.bzl", "installer")

package(
    default_applicable_licenses = ["//:license"],
    default_visibility = ["//visibility:public"],
    features = ["layering_check"],
)

installer(
    name = "install",
    data = [
        "//bant",
    ],
)

cc_binary(
    name = "bant",
    srcs = [
        "bant.cc",
    ],
    deps = [
        ":session",
        ":types-bazel",
        ":workspace",
        "//bant/frontend:project-parser",
        "//bant/tool:canon-targets",
        "//bant/tool:dwyu",
        "//bant/tool:edit-callback",
        "//bant/tool:header-providers",
        "//bant/util:dependency-graph",
    ],
)

cc_library(
    name = "types-bazel",
    srcs = [
        "types-bazel.cc",
    ],
    hdrs = [
        "types-bazel.h",
    ],
    deps = [
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "workspace",
    srcs = ["workspace.cc"],
    hdrs = ["workspace.h"],
    deps = [
        ":session",
        "//bant/frontend:parser",
        "//bant/util:file-utils",
        "//bant/util:memory",
        "//bant/util:query-utils",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "session",
    hdrs = ["session.h"],
    deps = ["//bant/util:stat"],
)

cc_test(
    name = "types-bazel_test",
    size = "small",
    srcs = ["types-bazel_test.cc"],
    deps = [
        ":types-bazel",
        "@abseil-cpp//absl/log:check",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)
