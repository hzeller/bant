{ pkgs ? import <nixpkgs> {} }:
let
  bant_used_stdenv = pkgs.stdenv;

  # Testing with specific compilers
  #bant_used_stdenv = pkgs.gcc13Stdenv;
  #bant_used_stdenv = pkgs.clang13Stdenv;
in
bant_used_stdenv.mkDerivation {
  name = "bant-build-environment";
  buildInputs = with pkgs;
    [
      bazel_4
      jdk11

      # WORKSPACE pkg-config test
      #pkg-config
      #gtest

      clang-tools_14    # clang-format, clang-tidy
      bazel-buildtools  # buildifier
    ];
}
