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
    name = "file-utils",
    srcs = ["file-utils.cc"],
    hdrs = ["file-utils.h"],
    deps = ["@abseil-cpp//absl/container:flat_hash_set"],
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
    name = "resolve-packages",
    srcs = ["resolve-packages.cc"],
    hdrs = ["resolve-packages.h"],
    deps = [
        ":file-utils",
        ":query-utils",
        "//bant:types-bazel",
        "//bant/frontend:project-parser",
        "@abseil-cpp//absl/strings",
    ],
)
