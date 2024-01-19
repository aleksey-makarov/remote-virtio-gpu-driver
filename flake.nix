{
  description = "Virtio Loopback Linux kernel module";

  nixConfig.bash-prompt = "virtio-lo";
  nixConfig.bash-prompt-prefix = "[\\033[1;33m";
  nixConfig.bash-prompt-suffix = "\\033[0m \\w]$ ";

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
    virglrenderer-debug-flake = {
      url = "git+file:/home/amakarov/work/virglrenderer";
      inputs.nixpkgs.follows = "nixpkgs";
      inputs.flake-utils.follows = "flake-utils";
      inputs.nixGL.follows = "nixGL";
    };
    uhmitest = {
      url = "github:aleksey-makarov/uhmitest";
      inputs.nixpkgs.follows = "nixpkgs";
      inputs.flake-utils.follows = "flake-utils";
      inputs.nix-vscode-extensions.follows = "nix-vscode-extensions";
    };
  };

  outputs = {
    self,
    nixpkgs,
    flake-utils,
    nixGL,
    nix-vscode-extensions,
    virglrenderer-debug-flake,
    uhmitest,
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

      libvirtiolo-debug =
        (self.libvirtiolo.overrideAttrs (_: _: {
          cmakeBuildType = "Debug";
          separateDebugInfo = true;
        }))
        .override {
          virglrenderer = self.virglrenderer-debug;
        };

      # virglrenderer-debug = (virglrenderer-debug-flake.overlays.default self super).virglrenderer;

      # virglrenderer-debug = super.virglrenderer.overrideAttrs (_: _: {
      #   mesonFlags = ["-Dtracing=stderr"];
      # });

      virglrenderer-debug = super.virglrenderer;

      uhmitest = uhmitest.packages.${system}.uhmitest;
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

    startvm_sh = pkgs.writeShellScript "startvm.sh" ''
      ${pkgs.coreutils}/bin/mkdir -p ./xchg

      TMPDIR=''$(pwd)
      USE_TMPDIR=1
      export TMPDIR USE_TMPDIR

      TTY_FILE="./xchg/tty.sh"
      read -r rows cols <<< "''$(${pkgs.coreutils}/bin/stty size)"

      cat << EOF > "''${TTY_FILE}"
      export TERM=xterm-256color
      stty rows ''$rows cols ''$cols
      reset
      EOF

      ${pkgs.coreutils}/bin/stty intr ^] # send INTR with Control-]
      ${pkgs.nixgl.nixGLMesa}/bin/nixGLMesa ${nixos.vm}/bin/run-nixos-vm
      ${pkgs.coreutils}/bin/stty intr ^c
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

      virglrenderer-debug = pkgs.virglrenderer-debug;

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
      startvm = {
        type = "app";
        program = "${startvm_sh}";
      };
      test_virglrenderer = {
        type = "app";
        program = "${startvm_sh}";
      };
      default = startvm;
    };
  };
}
