# Base image: Microsoft's official C++ devcontainer image.
# Already includes: build-essential, cmake, clang, gcc, gdb, lldb, llvm, vcpkg.
FROM mcr.microsoft.com/devcontainers/cpp:1-ubuntu-24.04

# Install Ninja and firewall tools.
# - ninja-build: the CMake generator.
# "apt-get" update Fetches the latest package lists from Debian/Ubuntu repositories
# "apt-get install" Installs the packages. "-y" auto-confirms prompts. "--no-install-recommends" tells apt to install only what's strictly required
# "apt-get clean" Once installed, the original package files serve no purpose inside the image.
# "rm -rf /var/lib/apt/lists/*" Deletes the package lists. These are only needed during installation
RUN apt-get update && apt-get install -y --no-install-recommends \
    ninja-build \
    iptables \
    ipset \
    iproute2 \
    jq \
    dnsutils \
    aggregate \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

# JUCE Linux build dependencies are NOT installed yet.
# When JUCE is added, the dependency list in docs/setup.md needs to be verified against JUCE's official Linux build docs

# Install the firewall script and grant the non-root vscode user
COPY project-firewall.sh /usr/local/bin/project-firewall.sh
RUN chmod +x /usr/local/bin/project-firewall.sh \
    && echo "vscode ALL=(root) NOPASSWD: /usr/local/bin/project-firewall.sh" > /etc/sudoers.d/vscode-firewall \
    && chmod 0440 /etc/sudoers.d/vscode-firewall

# Make clang the default C/C++ compiler per setup doc.
ENV CC=clang
ENV CXX=clang++