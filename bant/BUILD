load("@com_github_google_rules_install//installer:def.bzl", "installer")

package(
    default_applicable_licenses = ["//:license"],
    default_visibility = ["//visibility:public"],
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
        "//bant/explore:dependency-graph",
        "//bant/explore:header-providers",
        "//bant/explore:query-utils",
        "//bant/frontend:parsed-project",
        "//bant/tool:canon-targets",
        "//bant/tool:dwyu",
        "//bant/tool:edit-callback",
        "//bant/util:table-printer",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "types",
    hdrs = ["types.h"],
    deps = ["@abseil-cpp//absl/container:btree"],
)

cc_library(
    name = "types-bazel",
    srcs = [
        "types-bazel.cc",
    ],
    hdrs = [
        "types-bazel.h",
    ],
    deps = ["@abseil-cpp//absl/strings"],
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

cc_library(
    name = "workspace",
    srcs = ["workspace.cc"],
    hdrs = ["workspace.h"],
    deps = [
        ":session",
        ":types",
        "//bant/explore:query-utils",
        "//bant/frontend:named-content",
        "//bant/frontend:parser",
        "//bant/util:file-utils",
        "//bant/util:memory",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "session",
    hdrs = [
        "output-format.h",
        "session.h",
    ],
    deps = [
        ":types",
        "//bant/util:stat",
    ],
)
