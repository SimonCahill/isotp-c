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

# Pin oh-my-zsh to a specific commit to avoid supply-chain risks from curl|sh.
# OMZ_COMMIT was pinned on 2026-03-10 (master HEAD at that date).
# Update OMZ_COMMIT to a newer known-good commit hash when upgrading oh-my-zsh.
ARG OMZ_COMMIT=8df5c1b18b1393dc5046c729094f897bd3636a9b
RUN if [ ! -d /home/vscode/.oh-my-zsh ]; then \
        git init /home/vscode/.oh-my-zsh \
        && git -C /home/vscode/.oh-my-zsh fetch --depth 1 \
            https://github.com/ohmyzsh/ohmyzsh.git "${OMZ_COMMIT}" \
        && git -C /home/vscode/.oh-my-zsh checkout FETCH_HEAD; \
    fi \
    && if [ ! -f /home/vscode/.zshrc ]; then \
        cp /home/vscode/.oh-my-zsh/templates/zshrc.zsh-template /home/vscode/.zshrc; \
    fi \
    && sed -i 's/^ZSH_THEME=.*/ZSH_THEME="jispwoso"/' /home/vscode/.zshrc \
    && sed -i 's/^plugins=.*/plugins=(git docker cmake colored-man-pages command-not-found history sudo)/' /home/vscode/.zshrc

USER root
