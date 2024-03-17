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

      clang-tools_17    # clang-format, clang-tidy
      bazel-buildtools  # buildifier, buildozer
    ];
}
