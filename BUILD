# -*- python -*-

package(
    features = ["layering_check"],
)

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
        "linecolumn-map.cc",
        "parser.cc",
        "scanner.cc",
    ],
    hdrs = [
        "ast.h",
        "linecolumn-map.h",
        "parser.h",
        "scanner.h",
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
        ":memory",
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
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
    ],
)

cc_library(
    name = "tool-header-providers",
    srcs = ["tool-header-providers.cc"],
    hdrs = ["tool-header-providers.h"],
    deps = [
        ":parser",
        ":project-parser",
        ":query-utils",
        ":types-bazel",
    ],
)

cc_library(
    name = "query-utils",
    srcs = ["query-utils.cc"],
    hdrs = ["query-utils.h"],
    deps = [
        ":parser",
        "@com_google_absl//absl/container:flat_hash_set",
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
    deps = [
        "@com_google_absl//absl/strings",
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
    deps = [
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/strings",
    ],
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
