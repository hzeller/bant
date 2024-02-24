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
        ":project-parser",
        ":tool-dwyu",
        ":tool-header-providers",
    ],
)

cc_library(
    name = "linecolumn-map",
    srcs = ["linecolumn-map.cc"],
    hdrs = ["linecolumn-map.h"],
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
        ":memory",
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
        ":linecolumn-map",
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
        ":linecolumn-map",
        ":parser",
        ":types-bazel",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/time",
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
        ":file-utils",
        ":project-parser",
        ":query-utils",
        ":tool-header-providers",
        ":types-bazel",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/time",
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
    srcs = [
        "types-bazel.cc",
    ],
    hdrs = [
        "types-bazel.h",
    ],
    deps = [
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "types-bazel_test",
    srcs = ["types-bazel_test.cc"],
    deps = [
        ":types-bazel",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
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
