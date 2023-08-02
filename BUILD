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
    hdrs = ["arena.h"],
)
