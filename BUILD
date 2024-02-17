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
    ],
    hdrs = [
        "ast.h",
        "parser.h",
        "scanner.h",
    ],
    deps = [
        ":memory",
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
    name = "scanner_test",
    srcs = ["scanner_test.cc"],
    deps = [
        ":parser",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "parser_test",
    srcs = ["parser_test.cc"],
    deps = [
        ":parser",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "arena-container_test",
    srcs = ["arena-container_test.cc"],
    deps = [
        ":memory",
        "@com_google_googletest//:gtest_main",
    ],
)
