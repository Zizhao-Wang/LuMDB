#!/bin/bash

# 定义需要检查的包列表
packages=("cpufrequtils" "libgflags-dev" "pkg-config" )

# 遍历并检查每个包是否安装
for pkg in "${packages[@]}"; do
    if ! dpkg -l | grep -qw "$pkg"; then
        echo "Installing: $pkg"
        sudo apt-get install -y "$pkg"
    else
        echo "The package has been installed: $pkg"
    fi
done