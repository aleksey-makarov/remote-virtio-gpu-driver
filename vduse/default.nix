# Separately build vduse module so that it
# could be patched and it would not require kernel rebuild
#
# https://nixos.wiki/wiki/Linux_kernel
# "Patching a single In-tree kernel module"
#
{
  pkgs,
  lib,
  kernel,
}:
pkgs.stdenv.mkDerivation {
  pname = "vduse-kernel-module";
  inherit (kernel) src version postPatch nativeBuildInputs;

  kernel_dev = kernel.dev;
  kernelVersion = kernel.modDirVersion;

  modulePath = "drivers/vdpa/vdpa_user";

  buildPhase = ''
    BUILT_KERNEL=$kernel_dev/lib/modules/$kernelVersion/build

    cp $BUILT_KERNEL/Module.symvers .
    cp $BUILT_KERNEL/.config        .
    cp $kernel_dev/vmlinux          .

    make "-j$NIX_BUILD_CORES" modules_prepare
    make "-j$NIX_BUILD_CORES" M=$modulePath modules
  '';

  installPhase = ''
    make \
      INSTALL_MOD_PATH="$out" \
      XZ="xz -T$NIX_BUILD_CORES" \
      M="$modulePath" \
      modules_install
  '';

  meta = {
    description = "VDUSE (vDPA Device in Userspace) kernel module";
    license = lib.licenses.gpl2;
  };
}
