{
  description = "openc2e - open-source game engine for Creatures series";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-24.05";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
      in
      {
        packages = rec {
          default = openc2e;

          openc2e = pkgs.stdenv.mkDerivation rec {
            pname = "openc2e";
            version = "0.1.0";

            src = ./.;

            nativeBuildInputs = with pkgs; [
              cmake
              pkg-config
            ];

            buildInputs = with pkgs; [
              SDL2
              SDL2_mixer
              python3
              boost
              zlib
            ];

            cmakeFlags = [
              "-DCMAKE_BUILD_TYPE=Release"
            ];

            configurePhase = ''
              cmake -B build ${toString cmakeFlags} .
            '';

            buildPhase = ''
              cmake --build build -j $NIX_BUILD_CORES
            '';

            installPhase = ''
              mkdir -p $out/bin
              cp -r build/* $out/bin/
            '';
          };
        };

        apps.default = flake-utils.lib.mkApp {
          drv = self.packages.${system}.default;
        };

        devShells.default = pkgs.mkShell {
          buildInputs = self.packages.${system}.openc2e.nativeBuildInputs ++
                        self.packages.${system}.openc2e.buildInputs ++
                        (with pkgs; [
                          # Add any additional packages needed only in the dev environment
                        ]);

          shellHook = ''
            echo "openc2e development environment"
            echo "Run 'cmake -B build ${toString self.packages.${system}.openc2e.cmakeFlags} .' to configure the project"
            echo "Run 'cmake --build build -j$(nproc)' to build the project"
          '';
        };
      }
    );
}
