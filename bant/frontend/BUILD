# -*- python -*-

package(
    default_visibility = ["//:__subpackages__"],
    features = ["layering_check"],
)

cc_library(
    name = "linecolumn-map",
    srcs = ["linecolumn-map.cc"],
    hdrs = ["linecolumn-map.h"],
)

cc_test(
    name = "linecolumn-map_test",
    srcs = ["linecolumn-map_test.cc"],
    deps = [
        ":linecolumn-map",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
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
        "//bant/util:memory",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "scanner_test",
    srcs = ["scanner_test.cc"],
    deps = [
        ":parser",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "parser_test",
    srcs = ["parser_test.cc"],
    deps = [
        ":parser",
        "//bant/util:memory",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "project-parser",
    srcs = ["project-parser.cc"],
    hdrs = ["project-parser.h"],
    deps = [
        ":linecolumn-map",
        ":parser",
        "//bant:types-bazel",
        "//bant/util:file-utils",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/time",
    ],
)
