package(
    default_applicable_licenses = ["//:license"],
    default_visibility = ["//bant:__pkg__"],
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
    name = "dwyu",
    srcs = ["dwyu.cc"],
    hdrs = ["dwyu.h"],
    deps = [
        ":edit-callback",
        "//bant:session",
        "//bant:types-bazel",
        "//bant/explore:header-providers",
        "//bant/explore:query-utils",
        "//bant/frontend:named-content",
        "//bant/frontend:parsed-project",
        "//bant/util:file-utils",
        "//bant/util:stat",
        "@abseil-cpp//absl/strings",
        "@re2",
    ],
)

cc_library(
    name = "canon-targets",
    srcs = ["canon-targets.cc"],
    hdrs = ["canon-targets.h"],
    deps = [
        ":edit-callback",
        "//bant:session",
        "//bant:types-bazel",
        "//bant/explore:query-utils",
        "//bant/frontend:parsed-project",
        "//bant/util:stat",
    ],
)

cc_test(
    name = "dwyu_test",
    size = "small",
    srcs = ["dwyu_test.cc"],
    deps = [
        ":dwyu",
        "//bant/frontend:linecolumn-map",
        "//bant/frontend:named-content",
        "@abseil-cpp//absl/log:check",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)
