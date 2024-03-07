# -*- python -*-

package(
    default_applicable_licenses = ["//:license"],
    default_visibility = ["//bant:__pkg__"],
    features = ["layering_check"],
)

cc_library(
    name = "header-providers",
    srcs = ["header-providers.cc"],
    hdrs = ["header-providers.h"],
    deps = [
        "//bant:types-bazel",
        "//bant/frontend:parser",
        "//bant/frontend:project-parser",
        "//bant/util:query-utils",
        "@abseil-cpp//absl/strings:str_format",
    ],
)

cc_library(
    name = "dwyu",
    srcs = ["dwyu.cc"],
    hdrs = ["dwyu.h"],
    deps = [
        ":header-providers",
        "//bant:types-bazel",
        "//bant/frontend:project-parser",
        "//bant/util:file-utils",
        "//bant/util:query-utils",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@re2",
    ],
)

cc_test(
    name = "dwyu_test",
    size = "small",
    srcs = ["dwyu_test.cc"],
    deps = [
        ":dwyu",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)
