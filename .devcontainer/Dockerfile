FROM mcr.microsoft.com/devcontainers/base:ubuntu-22.04

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        zsh \
        curl \
        build-essential \
        cmake \
        ninja-build \
        git \
        clang \
        clang-tidy \
        gdb \
        lldb \
        cppcheck \
        doxygen \
        graphviz \
        lcov \
        gcovr \
        flawfinder \
        python3 \
        python3-pip \
        pkg-config \
        libgtest-dev \
        libgmock-dev \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

RUN python3 -m pip install --no-cache-dir lizard

USER vscode

RUN if [ ! -d /home/vscode/.oh-my-zsh ]; then \
        sh -c "$(curl -fsSL https://raw.githubusercontent.com/ohmyzsh/ohmyzsh/master/tools/install.sh)" "" --unattended; \
    fi \
    && if [ ! -f /home/vscode/.zshrc ]; then \
        cp /home/vscode/.oh-my-zsh/templates/zshrc.zsh-template /home/vscode/.zshrc; \
    fi \
    && sed -i 's/^ZSH_THEME=.*/ZSH_THEME="jispwoso"/' /home/vscode/.zshrc \
    && sed -i 's/^plugins=.*/plugins=(git docker cmake colored-man-pages command-not-found history sudo)/' /home/vscode/.zshrc

USER root
