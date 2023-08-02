# -*- python -*-
cc_binary(
    name = "parser",
    srcs = [
        "parser.cc",
        "parser.h",
    ],
    deps = [
        ":memory",
        ":scanner",
    ],
)

cc_library(
    name = "scanner",
    srcs = [
        "ast.cc",
        "scanner.cc",
    ],
    hdrs = [
        "ast.h",
        "scanner.h",
    ],
    deps = [
        ":memory",
    ],
)

cc_library(
    name = "memory",
    hdrs = ["arena.h"],
)
