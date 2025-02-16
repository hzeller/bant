{ pkgs ? import <nixpkgs> {} }:
let
  bant_used_stdenv = pkgs.stdenv;        # no layering_check but clangd works
  #bant_used_stdenv = pkgs.clang17Stdenv;  # layering_check works, but clangd fails to understand c++ headers.
in
bant_used_stdenv.mkDerivation {
  name = "bant-build-environment";
  buildInputs = with pkgs;
    [
      bazel_6
      jdk11

      clang-tools_17    # for clang-format
      clang-tools_19    # for clang-tidy
      bazel-buildtools  # buildifier, buildozer
    ];
  shellHook = ''
      # clang tidy: use latest.
      export CLANG_TIDY=${pkgs.clang-tools_19}/bin/clang-tidy

      # There is too much volatility between even micro-versions of
      # clang-format 18. Let's use 17 for now.
      export CLANG_FORMAT=${pkgs.clang-tools_17}/bin/clang-format
  '';
}
