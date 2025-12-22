{ pkgs ? import <nixpkgs> {} }:
let
  bant_used_stdenv = pkgs.stdenv;        # no layering_check but clangd works
  #bant_used_stdenv = pkgs.clang19Stdenv;  # layering_check works, but clangd fails to understand c++ headers.
in
bant_used_stdenv.mkDerivation {
  name = "bant-build-environment";
  buildInputs = with pkgs;
    [
      bazel_7
      jdk11

      llvmPackages_20.clang-tools
      bazel-buildtools  # buildifier, buildozer
    ];
  shellHook = ''
      # clang tidy: use latest.
      export CLANG_TIDY=${pkgs.llvmPackages_20.clang-tools}/bin/clang-tidy
      export CLANG_FORMAT=${pkgs.llvmPackages_20.clang-tools}/bin/clang-format
  '';
}
