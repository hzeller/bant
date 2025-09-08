load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(
    default_applicable_licenses = ["//:license"],
    default_visibility = ["//:__subpackages__"],
)

cc_library(
    name = "dependency-graph",
    srcs = ["dependency-graph.cc"],
    hdrs = ["dependency-graph.h"],
    deps = [
        ":header-providers",
        ":query-utils",
        "//bant:session",
        "//bant:types",
        "//bant:types-bazel",
        "//bant:workspace",
        "//bant/frontend:elaboration",
        "//bant/frontend:parsed-project",
        "//bant/frontend:parser",
        "//bant/util:file-utils",
        "//bant/util:stat",
        "//bant/util:table-printer",
        "//bant/util:thread-pool",
    ],
)

cc_library(
    name = "query-utils",
    srcs = ["query-utils.cc"],
    hdrs = ["query-utils.h"],
    deps = [
        "//bant/frontend:parser",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
    ],
)

cc_test(
    name = "query-utils_test",
    srcs = ["query-utils_test.cc"],
    deps = [
        ":query-utils",
        "//bant/frontend:parsed-project",
        "//bant/frontend:parsed-project_testutil",
        "//bant/frontend:parser",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

# TODO: rename header-providers source and the overall library
cc_library(
    name = "header-providers",
    srcs = [
        "aliased-by.cc",
        "header-providers.cc",
    ],
    hdrs = [
        "aliased-by.h",
        "header-providers.h",
    ],
    deps = [
        ":query-utils",
        "//bant:session",
        "//bant:types",
        "//bant:types-bazel",
        "//bant/frontend:parsed-project",
        "//bant/util:table-printer",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/strings",
    ],
)

cc_test(
    name = "header-providers_test",
    srcs = ["header-providers_test.cc"],
    deps = [
        ":header-providers",
        "//bant:types-bazel",
        "//bant/frontend:parsed-project_testutil",
        "@abseil-cpp//absl/log:check",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)
