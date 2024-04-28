package(
    default_applicable_licenses = ["//:license"],
    default_visibility = ["//:__subpackages__"],
)

cc_library(
    name = "dependency-graph",
    srcs = ["dependency-graph.cc"],
    hdrs = ["dependency-graph.h"],
    deps = [
        ":query-utils",
        "//bant:session",
        "//bant:types",
        "//bant:types-bazel",
        "//bant:workspace",
        "//bant/frontend:parsed-project",
        "//bant/util:file-utils",
    ],
)

cc_library(
    name = "query-utils",
    srcs = ["query-utils.cc"],
    hdrs = ["query-utils.h"],
    deps = [
        "//bant/frontend:parser",
        "@abseil-cpp//absl/container:flat_hash_set",
    ],
)

cc_test(
    name = "query-utils_test",
    srcs = ["query-utils_test.cc"],
    deps = [
        ":query-utils",
        "//bant/frontend:parsed-project_testutil",
        "//bant/frontend:parsed-project",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "header-providers",
    srcs = ["header-providers.cc"],
    hdrs = ["header-providers.h"],
    deps = [
        ":query-utils",
        "//bant:session",
        "//bant:types",
        "//bant:types-bazel",
        "//bant/frontend:parsed-project",
        "//bant/util:table-printer",
    ],
)