{ pkgs ? import <nixpkgs> {} }:
let
  bant_used_stdenv = pkgs.stdenv;        # no layering_check but clangd works
  #bant_used_stdenv = pkgs.llvmPackages_22.stdenv;  # layering_check works, but clangd fails to understand c++ headers.

  # Optional additional packages user want to load
  userNixPath = ./user.nix;
  userPackages =
    if builtins.pathExists userNixPath
    then
      let loaded = import userNixPath;
      in if builtins.isFunction loaded
         then loaded { inherit pkgs; }
         else loaded
    else [];
in
bant_used_stdenv.mkDerivation {
  name = "bant-build-environment";
  buildInputs = with pkgs;
    [
      bazel_8
      re2c

      bazel-buildtools  # buildifier, buildozer
    ] ++ userPackages;
  shellHook = ''
      # clang tidy: use latest.
      export CLANG_TIDY=${pkgs.llvmPackages_22.clang-tools}/bin/clang-tidy
      export CLANG_FORMAT=${pkgs.llvmPackages_22.clang-tools}/bin/clang-format
  '';
}
