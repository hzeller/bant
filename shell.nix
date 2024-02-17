{ pkgs ? import <nixpkgs> {} }:
pkgs.mkShell {
  buildInputs = with pkgs;
    [
      bazel_5
      jdk11

      clang-tools_17    # clang-format, clang-tidy
      bazel-buildtools  # buildifier
    ];
}
