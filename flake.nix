{
  description = "logos-view-module-runtime — shared library for loading and running Logos UI modules";

  inputs = {
    logos-nix.url = "github:logos-co/logos-nix";
    nixpkgs.follows = "logos-nix/nixpkgs";
    logos-cpp-sdk = {
      url = "github:3esmit/logos-cpp-sdk?rev=ec020bd06776a71b204406384db2b194001bc543";
      inputs.logos-nix.follows = "logos-nix";
    };
    logos-protocol = {
      url = "github:3esmit/logos-protocol?rev=6086c922bf27ea53e073e92c997421c6e91baacd";
      inputs.logos-nix.follows = "logos-nix";
      inputs.nixpkgs.follows = "nixpkgs";
    };
    logos-qt-sdk = {
      url = "github:3esmit/logos-qt-sdk?rev=f6ba4309758755a0517eaed106d97df003cd9808";
      inputs.logos-nix.follows = "logos-nix";
      inputs.nixpkgs.follows = "nixpkgs";
      inputs.logos-protocol.follows = "logos-protocol";
      inputs.logos-cpp-sdk.follows = "logos-cpp-sdk";
    };
  };

  outputs = { self, nixpkgs, logos-nix, logos-cpp-sdk, logos-protocol, logos-qt-sdk }:
    let
      systems = [ "aarch64-darwin" "x86_64-darwin" "aarch64-linux" "x86_64-linux" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f {
        inherit system;
        pkgs = import nixpkgs { inherit system; };
        logosSdk = logos-cpp-sdk.packages.${system}.default;
        logosQtSdk = logos-qt-sdk.packages.${system}.default;
        logosProtocol = logos-protocol.packages.${system}.default;
      });
    in
    {
      packages = forAllSystems ({ pkgs, logosSdk, logosQtSdk, logosProtocol, ... }: {
        default = import ./nix/default.nix { inherit pkgs logosSdk logosQtSdk logosProtocol; };
        tests = import ./nix/test.nix { inherit pkgs logosSdk logosQtSdk logosProtocol; };
      });

      checks = forAllSystems ({ pkgs, logosSdk, logosQtSdk, logosProtocol, ... }: {
        default = import ./nix/test.nix { inherit pkgs logosSdk logosQtSdk logosProtocol; };
      });

      devShells = forAllSystems ({ pkgs, logosSdk, logosQtSdk, logosProtocol, ... }: {
        default = pkgs.mkShell {
          nativeBuildInputs = [ pkgs.cmake pkgs.ninja pkgs.pkg-config ];
          buildInputs = [
            pkgs.qt6.qtbase
            pkgs.qt6.qtremoteobjects
            pkgs.qt6.qtdeclarative
          ];
          shellHook = ''
            export LOGOS_CPP_SDK_ROOT="${logosSdk}"
            export LOGOS_QT_SDK_ROOT="${logosQtSdk}"
            export LOGOS_PROTOCOL_ROOT="${logosProtocol}"
            echo "logos-view-module-runtime dev shell"
          '';
        };
      });
    };
}
