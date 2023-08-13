# -*- python -*-
cc_binary(
    name = "bant",
    srcs = [
        "bant.cc",
    ],
    deps = [
        ":memory",
        ":parser",
    ],
)

cc_library(
    name = "parser",
    srcs = [
        "ast.cc",
        "scanner.cc",
        "parser.cc",
    ],
    hdrs = [
        "ast.h",
        "scanner.h",
        "parser.h",
    ],
    deps = [
        ":memory",
    ],
)

cc_library(
    name = "memory",
    hdrs = ["arena.h", "arena-container.h"],
)

cc_test(
    name = "scanner_test",
    srcs = [ "scanner_test.cc" ],
    deps = [
        ":parser",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "parser_test",
    srcs = [ "parser_test.cc" ],
    deps = [
        ":parser",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "arena-container_test",
    srcs = [ "arena-container_test.cc" ],
    deps = [
        ":memory",
        "@com_google_googletest//:gtest_main",
    ],
)
