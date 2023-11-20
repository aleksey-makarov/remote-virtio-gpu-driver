{
  description = "Virtio Loopback Linux kernel module";

  nixConfig.bash-prompt = "[\\033[1;33mvirtio-lo\\033[0m \\w]$ ";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
    flake-utils.url = "github:numtide/flake-utils";
    nixGL = {
      url = "github:guibou/nixGL";
      inputs.nixpkgs.follows = "nixpkgs";
      inputs.flake-utils.follows = "flake-utils";
    };
    nix-vscode-extensions = {
      url = "github:nix-community/nix-vscode-extensions";
      inputs.nixpkgs.follows = "nixpkgs";
      inputs.flake-utils.follows = "flake-utils";
    };
  };

  outputs = {
    self,
    nixpkgs,
    flake-utils,
    nixGL,
    nix-vscode-extensions,
  }: let
    system = "x86_64-linux";

    myLinuxPackages = pkgs.linuxPackages_6_5;

    overlay = _: super: {
      linuxKernel =
        super.linuxKernel
        // {
          packagesFor = kernel_: ((super.linuxKernel.packagesFor kernel_).extend (lpself: lpsuper: {
            virtio-lo = lpsuper.callPackage ./default.nix {};
          }));
        };
      libvirtiolo = super.callPackage ./lib/default.nix {
          linuxPackages = myLinuxPackages;
      };
    };

    pkgs = (nixpkgs.legacyPackages.${system}.extend overlay).extend nixGL.overlay;
    pkgs-nolo = nixpkgs.legacyPackages.${system}.extend nixGL.overlay;

    extensions = nix-vscode-extensions.extensions.${system};

    inherit (pkgs) vscode-with-extensions vscodium;

    vscode = vscode-with-extensions.override {
      vscode = vscodium;
      vscodeExtensions = [
        extensions.vscode-marketplace.ms-vscode.cpptools
        extensions.vscode-marketplace.github.vscode-github-actions
        extensions.vscode-marketplace.bbenoist.nix
      ];
    };

    nixos = pkgs.nixos (import ./configuration.nix);

  in {
    overlays.${system} = {
      default = overlay;
    };

    packages.${system} = rec {
      default = libvirtiolo;
      libvirtiolo = pkgs.libvirtiolo;
      libvirtiolo-dev = pkgs.libvirtiolo.dev;
    };

    devShells.${system} = rec {
      virtio-lo = with pkgs;
        mkShell {
          packages = [vscode nixgl.nixGLMesa nixos.vm];
          inputsFrom = (builtins.attrValues self.packages.${system}) ++ myLinuxPackages.kernel.moduleBuildDependencies;
          shellHook = ''
            export VIRTIO_LOOPBACK_DRIVER_KERNEL="${myLinuxPackages.kernel.dev}/lib/modules/${myLinuxPackages.kernel.modDirVersion}/build"
            echo "VIRTIO_LOOPBACK_DRIVER_KERNEL=''$VIRTIO_LOOPBACK_DRIVER_KERNEL"
            echo
            echo "libvirtiolo: ${libvirtiolo}"
            echo "libvirtiolo.dev: ${libvirtiolo.dev}"
            echo
            echo "\"includePath\": ["
            echo "  \"''${workspaceFolder}/**\"",
            echo "  \"${myLinuxPackages.virtio-lo.dev}/include\","
            echo "  \"${myLinuxPackages.kernel.dev}/lib/modules/${myLinuxPackages.kernel.modDirVersion}/build/source/include\","
            echo "  \"${myLinuxPackages.kernel.dev}/lib/modules/${myLinuxPackages.kernel.modDirVersion}/source/arch/x86/include\","
            echo "  \"${myLinuxPackages.kernel.dev}/lib/modules/${myLinuxPackages.kernel.modDirVersion}/build/include\","
            echo "  \"${myLinuxPackages.kernel.dev}/lib/modules/${myLinuxPackages.kernel.modDirVersion}/build/arch/x86/include/generaged\""
            echo "],"
            echo '"defines": [ "__KERNEL__", "KBUILD_MODNAME=\"virtio-lo\"", "MODULE" ],'
          '';
        };
      default = virtio-lo;
    };
  };
}
