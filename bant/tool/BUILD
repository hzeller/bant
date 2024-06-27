package(
    default_applicable_licenses = ["//:license"],
    default_visibility = ["//bant:__pkg__"],
)

cc_library(
    name = "edit-callback",
    srcs = ["edit-callback.cc"],
    hdrs = ["edit-callback.h"],
    deps = [
        "//bant:types-bazel",
    ],
)

cc_library(
    name = "edit-callback_testutil",
    testonly = True,
    hdrs = ["edit-callback_testutil.h"],
    deps = [
        ":edit-callback",
        "//bant:types-bazel",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/strings",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "dwyu",
    srcs = ["dwyu.cc"],
    hdrs = [
        "dwyu.h",
        "dwyu-internal.h",
    ],
    deps = [
        ":edit-callback",
        "//bant:session",
        "//bant:types",
        "//bant:types-bazel",
        "//bant:workspace",
        "//bant/explore:header-providers",
        "//bant/explore:query-utils",
        "//bant/frontend:named-content",
        "//bant/frontend:parsed-project",
        "//bant/frontend:parser",
        "//bant/util:file-utils",
        "//bant/util:stat",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/strings",
        "@re2",
    ],
)

cc_library(
    name = "canon-targets",
    srcs = ["canon-targets.cc"],
    hdrs = ["canon-targets.h"],
    deps = [
        ":edit-callback",
        "//bant:session",
        "//bant:types-bazel",
        "//bant/explore:query-utils",
        "//bant/frontend:parsed-project",
        "//bant/util:stat",
    ],
)

cc_test(
    name = "canon-targets_test",
    srcs = ["canon-targets_test.cc"],
    deps = [
        ":canon-targets",
        ":edit-callback_testutil",
        "//bant:session",
        "//bant:types-bazel",
        "//bant/frontend:parsed-project",
        "//bant/frontend:parsed-project_testutil",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "dwyu_test",
    size = "small",
    srcs = ["dwyu_test.cc"],
    deps = [
        ":dwyu",
        ":edit-callback_testutil",
        "//bant:session",
        "//bant:types-bazel",
        "//bant/frontend:named-content",
        "//bant/frontend:parsed-project",
        "//bant/frontend:parsed-project_testutil",
        "//bant/frontend:source-locator",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings:str_format",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "compilation-db",
    srcs = ["compilation-db.cc"],
    hdrs = ["compilation-db.h"],
    deps = [
        "//bant:session",
        "//bant:types-bazel",
        "//bant:workspace",
        "//bant/explore:query-utils",
        "//bant/frontend:parsed-project",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "workspace",
    srcs = ["workspace.cc"],
    hdrs = ["workspace.h"],
    deps = [
        "//bant:session",
        "//bant:types",
        "//bant:types-bazel",
        "//bant:workspace",
        "//bant/explore:query-utils",
        "//bant/frontend:parsed-project",
        "//bant/util:file-utils",
        "//bant/util:table-printer",
        "@abseil-cpp//absl/strings",
    ],
)
