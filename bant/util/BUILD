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
        ":table-printer",
        "//bant:session",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
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
        ":memory",
        ":stat",
        ":thread-pool",
        "//bant:session",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
    ],
)

cc_test(
    name = "file-utils_test",
    size = "small",
    srcs = ["file-utils_test.cc"],
    deps = [
        ":file-utils",
        ":memory",
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
