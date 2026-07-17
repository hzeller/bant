load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(
    default_applicable_licenses = ["//:license"],
    default_visibility = ["//:__subpackages__"],
)

cc_library(
    name = "memory",
    hdrs = [
        "arena.h",
        "arena-container.h",
    ],
)

cc_library(
    name = "stat",
    srcs = ["stat.cc"],
    hdrs = ["stat.h"],
    deps = [
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/time",
    ],
)

cc_test(
    name = "arena-container_test",
    size = "small",
    srcs = ["arena-container_test.cc"],
    deps = [
        ":memory",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "table-printer",
    srcs = ["table-printer.cc"],
    hdrs = ["table-printer.h"],
    deps = [
        ":grep-highlighter",
        "//bant:session",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
    ],
)

cc_library(
    name = "disjoint-range-map",
    hdrs = ["disjoint-range-map.h"],
    deps = ["@abseil-cpp//absl/container:btree"],
)

cc_test(
    name = "disjoint-range-map_test",
    srcs = ["disjoint-range-map_test.cc"],
    deps = [
        ":disjoint-range-map",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "table-printer_test",
    srcs = ["table-printer_test.cc"],
    deps = [
        ":grep-highlighter",
        ":table-printer",
        "//bant:session",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "term-color",
    hdrs = ["term-color.h"],
    deps = ["//bant:session"],
)

cc_library(
    name = "thread-pool",
    srcs = ["thread-pool.cc"],
    hdrs = ["thread-pool.h"],
    deps = [
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/synchronization",
    ],
)

cc_test(
    name = "thread-pool_test",
    srcs = ["thread-pool_test.cc"],
    deps = [
        ":thread-pool",
        "@abseil-cpp//absl/time",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "file-utils",
    srcs = [
        # TODO: break up (currently cyclicly dependent on each other)
        "file-utils.cc",
        "filesystem-prewarm-cache.cc",
        "filesystem.cc",
    ],
    hdrs = [
        "file-utils.h",
        "filesystem.h",
        "filesystem-prewarm-cache.h",
    ],
    deps = [
        ":glob-match-builder",
        ":stat",
        ":thread-pool",
        "//bant:session",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/container:node_hash_map",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:resize_and_overwrite",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_test(
    name = "file-utils_test",
    size = "small",
    srcs = ["file-utils_test.cc"],
    deps = [
        ":file-utils",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "glob-match-builder",
    srcs = ["glob-match-builder.cc"],
    hdrs = ["glob-match-builder.h"],
    deps = [
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/strings",
        "@re2",
    ],
)

cc_test(
    name = "glob-match-builder_test",
    size = "small",
    srcs = ["glob-match-builder_test.cc"],
    deps = [
        ":glob-match-builder",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "file-test-util",
    testonly = True,
    hdrs = ["file-test-util.h"],
    deps = [
        ":file-utils",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "filesystem_test",
    srcs = ["filesystem_test.cc"],
    deps = [
        ":file-test-util",
        ":file-utils",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "grep-highlighter",
    srcs = ["grep-highlighter.cc"],
    hdrs = ["grep-highlighter.h"],
    deps = [
        ":text-decorator",
        "//bant:session",
        "@abseil-cpp//absl/log:check",
        "@re2",
    ],
)

cc_test(
    name = "grep-highlighter_test",
    srcs = ["grep-highlighter_test.cc"],
    deps = [
        ":grep-highlighter",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "text-template",
    srcs = ["text-template.cc"],
    hdrs = ["text-template.h"],
    deps = [
        "@abseil-cpp//absl/log:check",
        "@re2",
    ],
)

cc_test(
    name = "text-template_test",
    srcs = ["text-template_test.cc"],
    deps = [
        ":text-template",
        "@abseil-cpp//absl/log:check",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "hyperlink-builder",
    srcs = ["hyperlink-builder.cc"],
    hdrs = ["hyperlink-builder.h"],
    deps = [
        ":text-template",
        "//bant:types-bazel",
        "//bant:workspace",
        "//bant/frontend:source-locator",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/strings",
    ],
)

cc_test(
    name = "hyperlink-builder_test",
    srcs = ["hyperlink-builder_test.cc"],
    deps = [
        ":hyperlink-builder",
        "//bant:workspace",
        "//bant/frontend:source-locator",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "text-decorator",
    srcs = ["text-decorator.cc"],
    hdrs = ["text-decorator.h"],
)

cc_test(
    name = "text-decorator_test",
    srcs = ["text-decorator_test.cc"],
    deps = [
        ":text-decorator",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)
