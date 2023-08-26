{
  stdenv,
  lib,
  linuxPackages,
  cmake,
}:
stdenv.mkDerivation rec {
  pname = "libvirtiolo";
  version = "0.1";

  src = ./.;

  outputs = ["out" "dev"];

  # buildInputs = [];

  nativeBuildInputs = [
    cmake
    linuxPackages.kernel.dev
    linuxPackages.virtio-lo.dev
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
