{
  stdenv,
  lib,
  linuxHeaders,
  linuxPackages,
  cmake,
  pkg-config,
  libnl,
}:
stdenv.mkDerivation rec {
  pname = "libvirtiolo";
  version = "0.1";

  src = ./.;

  outputs = ["out" "dev"];

  # buildInputs = [];

  nativeBuildInputs = [
    cmake
    pkg-config
    linuxHeaders
    linuxPackages.virtio-lo.dev
    libnl.dev
  ];

  meta = with lib; {
    description = "VIRTIO loopback library";
    homepage = "https://www.opensynergy.com/";
    license = licenses.mit;
    maintainers = [
      {
        email = "alm@opensynergy.com";
        name = "Aleksei Makarov";
        github = "aleksey.makarov";
        githubId = 19228987;
      }
    ];
    platforms = platforms.linux;
  };
}
