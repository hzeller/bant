load("@bazel_skylib//rules:common_settings.bzl", "bool_flag")

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

cc_binary(
    name = "bant",
    srcs = [
        "bant.cc",
        "cli-commands.cc",
        "cli-commands.h",
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
        ":build-version",
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
        "//bant/tool:compilation-db",
        "//bant/tool:dwyu",
        "//bant/tool:edit-callback",
        "//bant/tool:workspace",
        "//bant/util:file-utils",
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
        ":workspace",
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

# Due to the hermetic builds bazel offers, we can't just fill a header file
# with a git hash. We have to use the workspace stamping features bazel
# offers.
# https://docs.bazel.build/versions/master/user-manual.html#workspace_status
genrule(
    name = "version-header",
    outs = ["generated-build-version.h"],
    cmd = "sed -e 's/^BUILD/#define BANT_BUILD/p;d' bazel-out/volatile-status.txt > $@",
    stamp = 1,  # Tell bazel that we really are interested in the status file
    visibility = ["//visibility:private"],
)

cc_library(
    name = "build-version",
    hdrs = ["generated-build-version.h"],
    visibility = ["//visibility:private"],
)
