{
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};
    in {
      devShells.${system}.default = pkgs.mkShell.override { stdenv = pkgs.libcxxStdenv; } {
        packages = with pkgs; [
          cmake
          ninja
          pkg-config
          (gtest.override { stdenv = pkgs.libcxxStdenv; })
          wrk
          perf
          curl
        ];
      };
    };
}
