{ pkgs ? import <nixpkgs> {} }:
pkgs.mkShell {
  buildInputs = with pkgs;
    [
      bazel_5
      jdk11

      clang-tools_17    # clang-format, clang-tidy
      python39          # make-compilation-db.sh needs that
      bazel-buildtools  # buildifier
    ];
}
