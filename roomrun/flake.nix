{
  description = "Room Run procedural level generator (prototype)";
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAll = f: nixpkgs.lib.genAttrs systems (s: f nixpkgs.legacyPackages.${s});
    in {
      devShells = forAll (pkgs: {
        default = pkgs.mkShell { packages = with pkgs; [ cmake ninja gcc librsvg ]; };
      });
      packages = forAll (pkgs: {
        default = pkgs.stdenv.mkDerivation {
          pname = "roomrun";
          version = "0.1.0";
          src = ./.;
          nativeBuildInputs = with pkgs; [ cmake ninja ];
          doCheck = true;
          installPhase = ''
            mkdir -p $out/bin $out/share/roomrun
            cp roomrun-gen roomrun-batch $out/bin/
            cp -r $src/data $out/share/roomrun/
          '';
        };
      });
    };
}
