#!/bin/bash
set -e
WORK_DIR=$(dirname $(readlink -f "${BASH_SOURCE[0]}") )

# Check system
if [ -f /etc/os-release ]; then
    . /etc/os-release
    OS=$ID
    VERSION_ID=$VERSION_ID
else
    echo "Unsupported operating system"
    exit 1
fi
if [[ "$OS" != "ubuntu" && "$OS" != "centos" && "$OS" != "kylin" ]]; then
    echo "Unsupported operating system: $OS"
    exit 1
fi

# Check whether into Docker
if [ -f /.dockerenv ]; then
    IN_DOCKER=true
else
    IN_DOCKER=false
fi

# Install necessary dependencies
install_dependencies() {
    if [ "$IN_DOCKER" = true ] && [ "$(id -u)" != "0" ]; then
        echo "Running inside Docker as non-root user. Please ensure the necessary dependencies are installed manually."
    fi

    SUDO=sudo
    if [ "$(id -u)" = "0" ]; then
        SUDO=""
    fi
    if [ "$OS" == "ubuntu" ]; then
        $SUDO apt-get update
        $SUDO apt-get install -y autoconf automake build-essential cmake git-core libtool pkg-config wget
        $SUDO apt-get install -y libass-dev libfreetype6-dev libsdl2-dev libva-dev libvdpau-dev libvorbis-dev libxcb1-dev libxcb-shm0-dev libxcb-xfixes0-dev texinfo zlib1g-dev yasm nasm libmp3lame-dev
    elif [ "$OS" == "centos" ]; then
        $SUDO yum install -y epel-release
        $SUDO yum groupinstall -y "Development Tools"
        $SUDO yum install -y autoconf automake cmake git libtool pkgconfig
        $SUDO yum install -y freetype-devel libass-devel libva-devel libvdpau-devel libvorbis-devel libxcb-devel libX11-devel libXext-devel libXfixes-devel zlib-devel yasm nasm lame lame-devel
    elif [ "$OS" == "kylin" ]; then
        $SUDO apt-get update
        $SUDO apt-get install -y autoconf automake build-essential cmake git libtool pkg-config wget
        $SUDO apt-get install -y libass-dev libfreetype6-dev libsdl2-dev libva-dev libvdpau-dev libvorbis-dev libxcb1-dev libxcb-shm0-dev libxcb-xfixes0-dev texinfo zlib1g-dev yasm nasm libmp3lame-dev
        # $SUDO yum update -y
        # $SUDO yum install -y autoconf automake cmake git libtool pkgconfig wget
        # $SUDO yum install -y freetype-devel libass-devel libva-devel libvdpau-devel libvorbis-devel libxcb-devel libX11-devel libXext-devel libXfixes-devel zlib-devel yasm nasm lame lame-devel
    fi
}

# Main
install_dependencies

# Check yasm and nasm
if ! command -v yasm &> /dev/null && ! command -v nasm &> /dev/null; then
    echo "Error: yasm or nams instal failed!!!!!!"
fi

echo ""
echo "FFmpeg dependencies has been installed successfully"
