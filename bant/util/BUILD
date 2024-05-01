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
    name = "file-utils",
    srcs = ["file-utils.cc"],
    hdrs = ["file-utils.h"],
    deps = [
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/container:flat_hash_set",
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
