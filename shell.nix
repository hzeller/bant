{ pkgs ? import <nixpkgs> {} }:
let
  bant_used_stdenv = pkgs.stdenv;        # no layering_check but clangd works
  #bant_used_stdenv = pkgs.llvmPackages_22.stdenv;  # layering_check works, but clangd fails to understand c++ headers.
in
bant_used_stdenv.mkDerivation {
  name = "bant-build-environment";
  buildInputs = with pkgs;
    [
      bazel_8
      re2c

      bazel-buildtools  # buildifier, buildozer
    ];
  shellHook = ''
      # clang tidy: use latest.
      export CLANG_TIDY=${pkgs.llvmPackages_22.clang-tools}/bin/clang-tidy
      export CLANG_FORMAT=${pkgs.llvmPackages_22.clang-tools}/bin/clang-format
  '';
}
