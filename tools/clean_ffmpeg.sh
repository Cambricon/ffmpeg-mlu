#!/bin/bash

set -e

# Check if the script is run with sudo privileges
if [ "$(id -u)" -ne 0 ]; then
  echo "Please run this script with sudo privileges."
  exit 1
fi

# Detect the OS distribution
if [ -f /etc/os-release ]; then
  . /etc/os-release
  OS=$ID
else
  echo "Unable to detect the operating system."
  exit 1
fi

# Function to clean FFmpeg on Ubuntu
clean_ubuntu() {
  echo "Detected Ubuntu system, starting cleanup..."

  # Remove ffmpeg binary
  echo "Removing ffmpeg binary..."
  apt-get remove --purge -y ffmpeg

  # Remove libav related libraries
  echo "Removing libav related libraries..."
  apt-get remove --purge -y libavcodec* libavdevice* libavfilter* libavformat* libavresample* libavutil* libswscale* libswresample*

  # Remove leftover configuration files and dependencies
  echo "Removing leftover configuration files and dependencies..."
  apt-get autoremove --purge -y
  apt-get clean

  # Remove potential ffmpeg files
  echo "Removing potential ffmpeg files..."
  rm -rf /usr/local/bin/ffmpeg /usr/local/bin/ffplay /usr/local/bin/ffprobe
  rm -rf /usr/local/lib/libav* /usr/local/lib/libsw* /usr/local/lib/libpostproc*
  rm -rf /usr/bin/ffmpeg /usr/bin/ffplay /usr/bin/ffprobe
  rm -rf /usr/lib/libav* /usr/lib/libsw* /usr/lib/libpostproc*
  rm -rf /usr/local/include/libav* /usr/local/include/libsw* /usr/local/include/libpostproc*
  rm -rf /usr/include/libav* /usr/include/libsw* /usr/include/libpostproc*
  rm -rf /usr/lib/x86_64-linux-gnu/libav* /usr/lib/x86_64-linux-gnu/libsw* /usr/lib/x86_64-linux-gnu/libpostproc*
  rm -rf /usr/share/ffmpeg
}

# Function to clean FFmpeg on CentOS
clean_centos() {
  echo "Detected CentOS system, starting cleanup..."

  # Remove ffmpeg binary
  echo "Removing ffmpeg binary..."
  yum remove -y ffmpeg

  # Remove libav related libraries
  echo "Removing libav related libraries..."
  yum remove -y libavcodec libavdevice libavfilter libavformat libavresample libavutil libswscale libswresample

  # Remove leftover configuration files and dependencies
  echo "Removing leftover configuration files and dependencies..."
  yum autoremove -y
  yum clean all

  # Remove potential ffmpeg files
  echo "Removing potential ffmpeg files..."
  rm -rf /usr/local/bin/ffmpeg /usr/local/bin/ffplay /usr/local/bin/ffprobe
  rm -rf /usr/local/lib/libav* /usr/local/lib/libsw* /usr/local/lib/libpostproc*
  rm -rf /usr/local/include/libav* /usr/local/include/libsw* /usr/local/include/libpostproc*
  rm -rf /usr/bin/ffmpeg /usr/bin/ffplay /usr/bin/ffprobe
  rm -rf /usr/lib/libav* /usr/lib/libsw* /usr/lib/libpostproc*
  rm -rf /usr/include/libav* /usr/include/libsw* /usr/include/libpostproc*
  rm -rf /usr/share/ffmpeg
}

# Function to clean common files in /usr excluding /usr/local/neuware
clean_common_files() {
  echo "Checking for any remaining ffmpeg related files in /usr, excluding /usr/local/neuware..."
  find /usr -path /usr/local/neuware -prune -o -name '*ffmpeg*' -exec rm -rf {} \;
  find /usr -path /usr/local/neuware -prune -o -name '*avconv*' -exec rm -rf {} \;
  echo "FFmpeg and related files in /usr have been completely removed."
}

# Call the appropriate clean function based on detected OS
case "$OS" in
  ubuntu)
    clean_ubuntu
    ;;
  debian)
    clean_ubuntu
    ;;
  centos)
    clean_centos
    ;;
  kylin)
    clean_centos
    ;;
  *)
    echo "Unsupported operating system: $OS"
    exit 1
    ;;
esac

# Call the common file cleaning function
clean_common_files

exit 0
