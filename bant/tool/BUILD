# -*- python -*-

package(
    default_applicable_licenses = ["//:license"],
    default_visibility = ["//bant:__pkg__"],
    features = ["layering_check"],
)

cc_library(
    name = "edit-callback",
    srcs = ["edit-callback.cc"],
    hdrs = ["edit-callback.h"],
    deps = [
        "//bant:types-bazel",
    ],
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
        ":edit-callback",
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

cc_library(
    name = "canon-targets",
    srcs = ["canon-targets.cc"],
    hdrs = ["canon-targets.h"],
    deps = [
        ":edit-callback",
        "//bant/frontend:project-parser",
        "//bant/util:query-utils",
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
