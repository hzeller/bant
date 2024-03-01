# -*- python -*-

package(
    default_applicable_licenses = ["//:license"],
    default_visibility = ["//:__subpackages__"],
    features = ["layering_check"],
)

cc_library(
    name = "query-utils",
    srcs = ["query-utils.cc"],
    hdrs = ["query-utils.h"],
    deps = [
        "//bant/frontend:parser",
        "@abseil-cpp//absl/container:flat_hash_set",
    ],
)

cc_library(
    name = "memory",
    hdrs = [
        "arena.h",
        "arena-container.h",
    ],
)

cc_test(
    name = "arena-container_test",
    srcs = ["arena-container_test.cc"],
    deps = [
        ":memory",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "file-utils",
    srcs = ["file-utils.cc"],
    hdrs = ["file-utils.h"],
    deps = [
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/strings",
    ],
)
