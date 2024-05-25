load("@bazel_skylib//rules:common_settings.bzl", "bool_flag")
load("@com_github_google_rules_install//installer:def.bzl", "installer")

package(
    default_applicable_licenses = ["//:license"],
    default_visibility = ["//visibility:public"],
)

bool_flag(
    name = "create_static_linked_executables",
    build_setting_default = False,
)

config_setting(
    name = "static_linked_executables",
    flag_values = {":create_static_linked_executables": "true"},
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
    features = select({
        "//bant:static_linked_executables": [
            "fully_static_link",
            "-supports_start_end_lib",
        ],
        "//conditions:default": [],
    }),
    linkopts = select({
        "//bant:static_linked_executables": ["-fuse-ld=bfd"],
        "//conditions:default": [],
    }),
    deps = [
        ":session",
        ":types",
        ":types-bazel",
        ":workspace",
        "//bant/explore:dependency-graph",
        "//bant/explore:header-providers",
        "//bant/explore:query-utils",
        "//bant/frontend:elaboration",
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
    deps = [
        "@abseil-cpp//absl/strings",
        "@re2",
    ],
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
