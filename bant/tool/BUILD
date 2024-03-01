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
        "@com_googlesource_code_re2//:re2",
    ],
)

cc_test(
    name = "dwyu_test",
    srcs = ["dwyu_test.cc"],
    deps = [
        ":dwyu",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)
