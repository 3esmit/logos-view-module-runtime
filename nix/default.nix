{ pkgs, logosSdk, logosQtSdk, logosProtocol }:

pkgs.stdenv.mkDerivation {
  pname = "logos-view-module-runtime";
  version = "1.0.0";

  src = ../.;

  nativeBuildInputs = [
    pkgs.cmake
    pkgs.ninja
    pkgs.pkg-config
    pkgs.qt6.wrapQtAppsHook
  ];

  buildInputs = [
    pkgs.qt6.qtbase
    pkgs.qt6.qtremoteobjects
    pkgs.qt6.qtdeclarative
    logosSdk
    logosQtSdk
    logosProtocol
  ];

  dontStrip = true;

  preConfigure = ''
    export MACOSX_DEPLOYMENT_TARGET=12.0

    # Point CMake at the SDK store path directly. CMakeLists.txt
    # uses `find_package(logos-cpp-sdk CONFIG PATHS
    # $LOGOS_CPP_SDK_ROOT/lib/cmake/logos-cpp-sdk)`, which carries
    # include dirs + the link interface (OpenSSL, Boost, nlohmann)
    # via the imported target — no need to stage a vendored copy.
    cmakeFlagsArray+=("-DLOGOS_CPP_SDK_ROOT=${logosSdk}")
    cmakeFlagsArray+=("-DLOGOS_QT_SDK_ROOT=${logosQtSdk}")
    cmakeFlagsArray+=("-DLOGOS_PROTOCOL_ROOT=${logosProtocol}")
  '';

  cmakeFlags = [
    "-DCMAKE_BUILD_TYPE=Release"
  ];

  meta = with pkgs.lib; {
    description = "Shared runtime library for loading Logos UI modules (LogosQmlBridge, ViewModuleHost, ui-host)";
    platforms = platforms.unix;
    license = licenses.mit;
  };
}
