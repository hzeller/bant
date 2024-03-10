load("@rules_license//rules:license.bzl", "license")

package(
    default_applicable_licenses = [":license"],
    features = ["layering_check"],
)

# Machine-readable license specification.
license(
    name = "license",
    package_name = "bant",
    license_kind = "@rules_license//licenses/spdx:GPL-2.0",
    license_text = "LICENSE",
)
