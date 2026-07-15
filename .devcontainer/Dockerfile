FROM debian:bookworm-slim

ARG USERNAME=vscode
ARG USER_UID=1000
ARG USER_GID=1000

# Minimal base — install only what we need to bootstrap the rest.
# - ca-certificates: TLS for astral.sh / registry.npmjs.org
# - curl / gnupg:     download the uv installer and the NodeSource apt key
# - git / ssh / sudo: dev workflow basics, private GitHub repos, passwordless sudo
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates \
        curl \
        git \
        gnupg \
        openssh-client \
        sudo \
    && rm -rf /var/lib/apt/lists/*

# Node.js 22.x LTS (Jod) — required by pi-coding-agent >= 0.75.0 (Node >= 22.19.0).
RUN curl -fsSL https://deb.nodesource.com/setup_22.x | bash - \
    && apt-get install -y --no-install-recommends nodejs \
    && rm -rf /var/lib/apt/lists/* \
    && node --version \
    && npm --version

# Non-root user expected by devcontainer (remoteUser: vscode).
RUN groupadd --gid $USER_GID $USERNAME \
    && useradd  --uid $USER_UID --gid $USER_GID -m -s /bin/bash $USERNAME \
    && echo "$USERNAME ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/$USERNAME \
    && chmod 0440 /etc/sudoers.d/$USERNAME

# uv — install to the default $HOME/.local/bin, then move to /usr/local/bin so
# it is on PATH for every user, every shell, and every devcontainer lifecycle
# command. The install script's INSTALL_DIR env var is no longer respected in
# recent versions, so we move the binaries manually.
RUN curl -LsSf https://astral.sh/uv/install.sh | sh \
    && install -m 0755 $HOME/.local/bin/uv  /usr/local/bin/uv  \
    && install -m 0755 $HOME/.local/bin/uvx /usr/local/bin/uvx \
    && uv --version \
    && rm -rf $HOME/.local/bin

# Install pi-coding-agent globally so binary is on system PATH. Do not install
# Pi packages here: Docker build cannot write to persisted PI_CODING_AGENT_DIR
# volume used at runtime. post-create.sh handles packages.
RUN npm install -g @earendil-works/pi-coding-agent \
    && command -v pi \
    && npm cache clean --force

USER $USERNAME

ENV PATH="/home/$USERNAME/.local/bin:${PATH}"

# Pin pioarduino Core for reproducible CLI and VS Code extension behavior. uv
# owns managed Python runtime and isolated tool environment.
RUN uv python install 3.13 \
    && uv tool install --python 3.13 pioarduino==6.1.19 \
    && pio --version

WORKDIR /workspaces
