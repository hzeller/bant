{ pkgs ? import <nixpkgs> {} }:
let
  #bant_used_stdenv = pkgs.stdenv;
  #bant_used_stdenv = pkgs.gcc13Stdenv;
  bant_used_stdenv = pkgs.clang16Stdenv;  # makes layering_check work
in
bant_used_stdenv.mkDerivation {
  name = "bant-build-environment";
  buildInputs = with pkgs;
    [
      bazel_5
      jdk11

      clang-tools_17    # clang-format, clang-tidy
      python39          # make-compilation-db.sh needs that
      bazel-buildtools  # buildifier
    ];
}
