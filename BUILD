# -*- python -*-
cc_binary(
    name = "bant",
    srcs = [
        "bant.cc",
    ],
    deps = [
        ":memory",
        ":parser",
        ":project-parser",
        ":tool-header-providers",
    ],
)

cc_library(
    name = "parser",
    srcs = [
        "ast.cc",
        "parser.cc",
        "scanner.cc",
        "linecolumn-map.cc"
    ],
    hdrs = [
        "ast.h",
        "parser.h",
        "scanner.h",
        "linecolumn-map.h"
    ],
    deps = [
        ":memory",
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
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "linecolumn-map_test",
    srcs = ["linecolumn-map_test.cc"],
    deps = [
        ":parser",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "project-parser",
    srcs = ["project-parser.cc"],
    hdrs = ["project-parser.h"],
    deps = [
        ":file-utils",
        ":parser",
        ":types-bazel",
    ],
)

cc_library(
    name = "tool-header-providers",
    srcs = ["tool-header-providers.cc"],
    hdrs = ["tool-header-providers.h"],
    deps = [
        ":parser",
        ":project-parser",
        ":types-bazel",
    ],
)

cc_library(
    name = "tool-dwyu",
    srcs = ["tool-dwyu.cc"],
    hdrs = ["tool-dwyu.h"],
    deps = [
        "@com_googlesource_code_re2//:re2",
    ],
)

cc_test(
    name = "tool-dwyu_test",
    srcs = ["tool-dwyu_test.cc"],
    deps = [
        ":tool-dwyu",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "types-bazel",
    hdrs = [
        "types-bazel.h",
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
    name = "file-utils",
    srcs = ["file-utils.cc"],
    hdrs = ["file-utils.h"],
)

cc_test(
    name = "arena-container_test",
    srcs = ["arena-container_test.cc"],
    deps = [
        ":memory",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)
