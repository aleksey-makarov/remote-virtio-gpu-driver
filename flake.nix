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

    overlay = self: super: {
      linuxPackages = super.linuxPackages_6_6;
      linuxKernel =
        super.linuxKernel
        // {
          packagesFor = kernel_: ((super.linuxKernel.packagesFor kernel_).extend (lpself: lpsuper: {
            virtio-lo = lpsuper.callPackage ./modules {};
            vduse = lpsuper.callPackage ./vduse {};
          }));
        };
      libvirtiolo = super.callPackage ./lib {};

      libvirtiolo-debug = self.libvirtiolo.overrideAttrs (_: _: {
        cmakeBuildType = "Debug";
        separateDebugInfo = true;
      });
    };

    pkgs = (nixpkgs.legacyPackages.${system}.extend overlay).extend nixGL.overlay;

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

    start_gtk_test_sh = pkgs.writeShellScript "start_gtk_test.sh" ''
      export LOCALE_ARCHIVE="${pkgs.glibcLocales}/lib/locale/locale-archive";
      export GDK_GL=gles
      exec ${pkgs.nixgl.nixGLMesa}/bin/nixGLMesa ${pkgs.libvirtiolo-debug}/bin/gtk_test
    '';
  in {
    overlays.${system} = {
      default = overlay;
    };

    packages.${system} = rec {
      libvirtiolo = pkgs.libvirtiolo;
      libvirtiolo-dev = pkgs.libvirtiolo.dev;

      libvirtiolo-debug = pkgs.libvirtiolo-debug;

      virtio-lo = pkgs.linuxPackages.virtio-lo;
      virtio-lo-dev = pkgs.linuxPackages.virtio-lo.dev;

      vduse = pkgs.linuxPackages.vduse;

      default = libvirtiolo;
    };

    devShells.${system} = rec {
      virtio-lo = with pkgs;
        mkShell {
          packages = [vscode linuxPackages.virtio-lo.dev pkgs.nixgl.nixGLMesa];
          inputsFrom = [pkgs.libvirtiolo-debug] ++ linuxPackages.kernel.moduleBuildDependencies;
          shellHook = ''
            export VIRTIO_LOOPBACK_DRIVER_KERNEL="${linuxPackages.kernel.dev}/lib/modules/${linuxPackages.kernel.modDirVersion}/build"
            echo "VIRTIO_LOOPBACK_DRIVER_KERNEL=''$VIRTIO_LOOPBACK_DRIVER_KERNEL"
            echo
            echo "gtk: ${pkgs.gtk3.dev}"
            echo "nixGL: ${pkgs.nixgl.nixGLMesa}"
            echo
            echo "\"includePath\": ["
            echo "  \"''${workspaceFolder}/**\"",
            echo "  \"${linuxPackages.virtio-lo.dev}/include\","
            echo "  \"${linuxPackages.kernel.dev}/lib/modules/${linuxPackages.kernel.modDirVersion}/build/source/include\","
            echo "  \"${linuxPackages.kernel.dev}/lib/modules/${linuxPackages.kernel.modDirVersion}/source/arch/x86/include\","
            echo "  \"${linuxPackages.kernel.dev}/lib/modules/${linuxPackages.kernel.modDirVersion}/build/include\","
            echo "  \"${linuxPackages.kernel.dev}/lib/modules/${linuxPackages.kernel.modDirVersion}/build/arch/x86/include/generaged\""
            echo "],"
            echo '"defines": [ "__KERNEL__", "KBUILD_MODNAME=\"virtio-lo\"", "MODULE" ],'
          '';
        };
      default = virtio-lo;
    };

    apps.${system} = rec {
      codium = {
        type = "app";
        program = "${vscode}/bin/codium";
      };
      gtk-test = {
        type = "app";
        program = "${start_gtk_test_sh}";
      };
      default = codium;
    };
  };
}
