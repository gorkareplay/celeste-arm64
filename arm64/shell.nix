# nix-shell for building and running everything in this fork on NixOS (aarch64-linux).
# Untested as a whole: written from the package lists used while getting this working.
{ pkgs ? import <nixpkgs> { } }:

pkgs.mkShell {
  packages = with pkgs; [
    git gnumake gcc binutils cmake pkg-config
    curl wget unzip rsync jq python3          # patch.sh, Implib.so, Everest installer
    mono                                       # monodis, used by arm64/make-steam-stub.sh
    dotnet-sdk_8                               # runs Everest (.NET 8)
    lua5.4 desktop-file-utils
    SDL2 vulkan-headers vulkan-loader
  ];
}
