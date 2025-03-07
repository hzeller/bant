package(
    default_applicable_licenses = ["//:license"],
    default_visibility = ["//:__subpackages__"],
)

cc_library(
    name = "source-locator",
    srcs = ["source-locator.cc"],
    hdrs = ["source-locator.h"],
)

cc_library(
    name = "linecolumn-map",
    srcs = ["linecolumn-map.cc"],
    hdrs = ["linecolumn-map.h"],
    deps = [
        ":source-locator",
        "@abseil-cpp//absl/log:check",
    ],
)

cc_test(
    name = "linecolumn-map_test",
    size = "small",
    srcs = ["linecolumn-map_test.cc"],
    deps = [
        ":linecolumn-map",
        ":source-locator",
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
        ":source-locator",
        "@abseil-cpp//absl/log:check",
    ],
)

cc_test(
    name = "named-content_test",
    srcs = ["named-content_test.cc"],
    deps = [
        ":named-content",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "parser",
    srcs = [
        "ast.cc",
        "parser.cc",
        "print-visitor.cc",
        "scanner.cc",
    ],
    hdrs = [
        "ast.h",
        "parser.h",
        "print-visitor.h",
        "scanner.h",
    ],
    deps = [
        ":linecolumn-map",
        ":named-content",
        "//bant/util:memory",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
        "@re2",
    ],
)

cc_test(
    name = "scanner_test",
    size = "small",
    srcs = ["scanner_test.cc"],
    deps = [
        ":named-content",
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
        "@abseil-cpp//absl/log:die_if_null",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "parsed-project",
    srcs = ["parsed-project.cc"],
    hdrs = ["parsed-project.h"],
    deps = [
        ":named-content",
        ":parser",
        ":source-locator",
        "//bant:builtin-macros",
        "//bant:session",
        "//bant:types",
        "//bant:types-bazel",
        "//bant:workspace",
        "//bant/explore:query-utils",
        "//bant/util:disjoint-range-map",
        "//bant/util:file-utils",
        "//bant/util:memory",
        "//bant/util:stat",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status:status",
        "@re2",
    ],
)

cc_library(
    name = "parsed-project_testutil",
    testonly = True,
    hdrs = ["parsed-project_testutil.h"],
    deps = [
        ":elaboration",
        ":parsed-project",
        "//bant:session",
        "//bant:types-bazel",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "substitute-copy",
    srcs = ["substitute-copy.cc"],
    hdrs = ["substitute-copy.h"],
    deps = [
        ":parser",
        "//bant/explore:query-utils",
        "//bant/util:memory",
    ],
)

cc_test(
    name = "substitute-copy_test",
    srcs = ["substitute-copy_test.cc"],
    deps = [
        ":named-content",
        ":parser",
        ":substitute-copy",
        "//bant/explore:query-utils",
        "//bant/util:memory",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "elaboration",
    srcs = ["elaboration.cc"],
    hdrs = ["elaboration.h"],
    deps = [
        ":macro-substitutor",
        ":parsed-project",
        ":parser",
        ":source-locator",
        ":substitute-copy",
        "//bant:session",
        "//bant:types-bazel",
        "//bant/explore:query-utils",
        "//bant/util:file-utils",
        "//bant/util:glob-match-builder",
        "//bant/util:stat",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
    ],
)

cc_test(
    name = "elaboration_test",
    srcs = ["elaboration_test.cc"],
    deps = [
        ":elaboration",
        ":parsed-project",
        ":parsed-project_testutil",
        ":parser",
        "//bant:session",
        "//bant/explore:query-utils",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings:str_format",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "macro-substitutor",
    srcs = ["macro-substitutor.cc"],
    hdrs = ["macro-substitutor.h"],
    deps = [
        ":parsed-project",
        ":parser",
        ":substitute-copy",
        "//bant:session",
        "//bant/explore:query-utils",
        "//bant/util:stat",
    ],
)

cc_test(
    name = "macro-substitutor_test",
    srcs = ["macro-substitutor_test.cc"],
    deps = [
        ":macro-substitutor",
        ":parsed-project_testutil",
        ":parser",
        "//bant:session",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)
