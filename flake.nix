# run with:                nix develop
# see metadata with:       nix flake metadata
# debug with:              nix repl
#                          :lf .#
# check with:              nix flake check
# If you want to update a locked input to the latest version, you need to ask
# for it:                  nix flake lock --update-input nixpkgs
# show available packages: nix-env -qa
#                          nix search nixpkgs
# show nixos version:      nixos-version
# 

{
  description = "C++ Development with Nix 24.11";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-24.11";
    nixpkgs-unstable.url = "github:nixos/nixpkgs/nixpkgs-unstable";
  };

  outputs = { self, nixpkgs, nixpkgs-unstable }: {
    devShells = {
      x86_64-linux.default  = self.buildDevShell "x86_64-linux";
      aarch64-linux.default = self.buildDevShell "aarch64-linux";
      x86_64-darwin.default = self.buildDevShell "x86_64-darwin";
    };

    packages = {
      x86_64-linux.default = self.buildPackage "x86_64-linux";
    };
  } // {
    buildDevShell = system: let
      pkgs = import nixpkgs {
        inherit system;
        config = {
          qt.enable = true; # Enable Qt configuration globally
        };
      };
      pkgsUnstable = import nixpkgs-unstable { inherit system; };
    in
      pkgs.mkShell {

        inputsFrom = [ self.packages.${system}.default ];
        
        nativeBuildInputs = with pkgs; [
          gdb
          makeWrapper
          bashInteractive
        ];

        QT_PLUGIN_PATH = "${pkgs.qt6.qtgraphs}/lib";
        # QT_PLUGIN_PATH = "${qtbase}/${qtbase.qtPluginPrefix}";

        # # set the environment variables that Qt apps expect
        shellHook = ''
          bashdir=$(mktemp -d)
          makeWrapper "$(type -p bash)" "$bashdir/bash" "''${qtWrapperArgs[@]}"
          exec "$bashdir/bash"
        '';
      };

    buildPackage = system: let
      pkgs = import nixpkgs { inherit system; };
    in
      pkgs.qt6Packages.callPackage ./qtdeclarative.nix {};
    
  };
}
