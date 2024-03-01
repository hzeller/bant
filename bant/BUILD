# -*- python -*-

package(
    default_visibility = ["//visibility:public"],
    features = ["layering_check"],
)

cc_binary(
    name = "bant",
    srcs = [
        "bant.cc",
    ],
    deps = [
        "//bant/frontend:project-parser",
        "//bant/tool:dwyu",
        "//bant/tool:header-providers",
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
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "types-bazel_test",
    srcs = ["types-bazel_test.cc"],
    deps = [
        ":types-bazel",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)
