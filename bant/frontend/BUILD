package(
    default_applicable_licenses = ["//:license"],
    default_visibility = ["//:__subpackages__"],
    features = ["layering_check"],
)

cc_library(
    name = "linecolumn-map",
    srcs = ["linecolumn-map.cc"],
    hdrs = ["linecolumn-map.h"],
    deps = ["@abseil-cpp//absl/log:check"],
)

cc_test(
    name = "linecolumn-map_test",
    size = "small",
    srcs = ["linecolumn-map_test.cc"],
    deps = [
        ":linecolumn-map",
        "@abseil-cpp//absl/log:check",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "named-content",
    srcs = ["named-content.cc"],
    hdrs = ["named-content.h"],
    deps = [
        ":linecolumn-map",
        "@abseil-cpp//absl/log:check",
    ],
)

cc_library(
    name = "parser",
    srcs = [
        "ast.cc",
        "parser.cc",
        "scanner.cc",
    ],
    hdrs = [
        "ast.h",
        "parser.h",
        "scanner.h",
    ],
    deps = [
        ":linecolumn-map",
        ":named-content",
        "//bant/util:memory",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
    ],
)

cc_test(
    name = "scanner_test",
    size = "small",
    srcs = ["scanner_test.cc"],
    deps = [
        ":parser",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "parser_test",
    size = "small",
    srcs = ["parser_test.cc"],
    deps = [
        ":named-content",
        ":parser",
        "//bant/util:memory",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "project-parser",
    srcs = ["project-parser.cc"],
    hdrs = ["project-parser.h"],
    deps = [
        ":named-content",
        ":parser",
        "//bant:session",
        "//bant:types-bazel",
        "//bant:workspace",
        "//bant/util:file-utils",
        "//bant/util:query-utils",
        "//bant/util:stat",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
    ],
)
