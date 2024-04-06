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
        "//bant:session",
        "//bant:types-bazel",
        "//bant/frontend:parser",
        "//bant/frontend:project-parser",
        "//bant/util:query-utils",
        "//bant/util:table-printer",
        "@abseil-cpp//absl/container:btree",
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
        "//bant:session",
        "//bant:types-bazel",
        "//bant/frontend:named-content",
        "//bant/frontend:project-parser",
        "//bant/util:file-utils",
        "//bant/util:query-utils",
        "//bant/util:stat",
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
        "//bant:types-bazel",
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
        "//bant/frontend:linecolumn-map",
        "//bant/frontend:named-content",
        "@abseil-cpp//absl/log:check",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)
