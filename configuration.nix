{
  pkgs,
  lib,
  config,
  modulesPath,
  ...
}:
with lib; {
  imports = [
    (modulesPath + "/profiles/qemu-guest.nix")
    (modulesPath + "/virtualisation/qemu-vm.nix")
  ];

  # https://nixos.wiki/wiki/Linux_kernel

  config = {
    system.stateVersion = "23.05";
    hardware.opengl.enable = true;

    # KERNEL

    boot = rec {
      extraModulePackages = [
        pkgs.linuxPackages.virtio-lo
        (pkgs.linuxPackages.vduse.overrideAttrs (_: {
          patches = [
            ./vduse/0001-vduse-Enable-GPU.patch
            ./vduse/0002-vduse-Enable-test-driver.patch
          ];
        }))
      ];
      kernelModules = ["vduse" "virtio-test" "virtio-lo"];

      # kernelParams = [ "video=1920x1080" ];
      # kernelParams = [ "drm.debug=0x1ff" ];
    };

    # from profiles/minimal.nix
    documentation.enable = false;
    documentation.doc.enable = false;
    documentation.man.enable = false;
    documentation.nixos.enable = false;
    documentation.info.enable = false;
    programs.bash.enableCompletion = false;
    programs.command-not-found.enable = false;

    programs.dconf.enable = true;

    services.getty.autologinUser = "root";

    virtualisation = {
      memorySize = 4 * 1024;
      cores = 4;
      forwardPorts = [
        {
          from = "host";
          host.port = 10022;
          guest.port = 22;
        }
      ];
      qemu = {
        options = [
          "-device virtio-vga-gl"
          "-display sdl,gl=on"

          # "-display sdl,gl=off"
          # "-vga none"
          # "-nographic"
          "-serial stdio"

          # "-chardev qemu-vdagent,id=ch1,name=vdagent,clipboard=on"
          # "-device virtio-serial-pci"
          # "-device virtserialport,chardev=ch1,id=ch1,name=com.redhat.spice.0"
        ];
      };
    };

    security.polkit.enable = true;

    networking.firewall.enable = false;

    services.openssh.enable = true;
    services.openssh.settings.PermitRootLogin = "yes";
    # services.openssh.passwordAuthentication = true;

    # services.xserver = {
    #   enable = true;
    #   displayManager = {
    #     gdm.enable = true;
    #     # FIXME: Autologin into root account does not work
    #     # autoLogin = {
    #     #   enable = true;
    #     #   # user = "guest";
    #     #   user = "root";
    #     # };
    #   };
    #   desktopManager.gnome.enable = true;
    # };

    fonts.packages = with pkgs; [
      noto-fonts
    ];

    environment.systemPackages = with pkgs; [
      vim
      micro
      wget
      mc
      tree
      tmux

      kmscube
      glmark2
      glxinfo
      mediainfo
      mesa-demos
      evtest

      weston

      iproute2

      gst_all_1.gstreamer # this is .bin (probably this is a bug)
      gst_all_1.gstreamer.out
      gst_all_1.gst-plugins-base
      gst_all_1.gst-plugins-good
      gst_all_1.gst-plugins-bad
      gst_all_1.gst-plugins-ugly
      gst_all_1.gst-devtools

      ffmpeg_6-full

      libvirtiolo
    ];

    users.mutableUsers = false;

    users.users.root = {
      password = "";
      openssh.authorizedKeys.keys = ["ssh-rsa AAAAB3NzaC1yc2EAAAADAQABAAABgQDYiMGe5zxNUAbYnJMRWrVfQrPxbPH77bpY3JvRTd2xM/Pdm+o6zbPYToJcDZWBUDO3XuQFCtrLuEGM5IBKlrf7JCsk/yeoCS8tcFjEJxMTE1FQVuwxOlrbNSDF2aeA9XpIPg2mL2JUBj6YOF141GWXNra1X/s6bOfAwmxgZw/RnPY7+6ZFFwTGgWniurc3oeCOdT09aX5RDIEUcnni8ye7fLNQJHv3egz62ORVswJ7CuLtVcdK6gMOVCeBC0DFPUkt0SXLUQUwU5HpWKB1Xx9EKWPmdlZk+0pXz14DgiGfseCbRDQGLqvHE7WxT/MxSHzLqicAlrXMAAdz3EsA2D1dTetb0d20PvViYkDYIa/phzdueM8RbzGaItPKffsMZx9aUMALnbEKeyNPUzfyLohrqT6yflZ1N3o6EWEGXTBpAnHEjYBgdWR4tcKyfBu6sjWzEYM0jnIXnbRPjdoPdg+JR4+S4MzoPDprB86Nr722Jg03xa+sQudS9IBgY8YvYwM= amakarov@NB-100862.open-synergy.com"];
    };

    users.users.guest = {
      isNormalUser = true;
      home = "/home/guest";
      description = "Guest";
      # group = "guest";
      extraGroups = ["wheel"];
      uid = 1001;
      password = "";
    };
  };
}
