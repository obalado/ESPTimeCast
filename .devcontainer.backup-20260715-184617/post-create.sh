#!/bin/bash
set -euxo pipefail

# Heavy tool installs (uv, pi-coding-agent, and pioarduino) are baked into image.
# Pi packages install here so they land in persisted PI_CODING_AGENT_DIR volume.

git config --global user.name "obalado"
git config --global user.email "obalado@users.noreply.github.com"

# Keep project AGENTS.md synced inside the container. apply.sh seeds this file
# from the termona-skills package root and this step overwrites the workspace
# copy on every devcontainer create/recreate.
if [ -f .devcontainer/AGENTS.md ]; then
  cp .devcontainer/AGENTS.md AGENTS.md
  echo "Overwrote project AGENTS.md from .devcontainer/AGENTS.md"
else
  echo ".devcontainer/AGENTS.md not found; leaving project AGENTS.md unchanged"
fi

PERSIST=/workspaces/pi-tools
PUBLIC_PI_PACKAGES=(
  "npm:@narumitw/pi-codex-usage"
  "git:github.com/obalado/pi-hashline-context-edit"
)
PRIVATE_SSH_PI_PACKAGES=(
  "git:git@github.com:obalado/termona-skills.git|termona-skills"
)

# Pi agent state persists in named volume mounted by devcontainer.json.
mkdir -p "$PERSIST/pi/agent"
export PI_CODING_AGENT_DIR="$PERSIST/pi/agent"

# Persist pi-agent var for interactive shells (idempotent).
# remoteEnv in devcontainer.json only applies to VSCode's integrated
# terminal, not to shells opened via OrbStack console / docker exec.
if ! grep -q "PI_CODING_AGENT_DIR" ~/.bashrc 2>/dev/null; then
  cat >> ~/.bashrc << EOF
export PI_CODING_AGENT_DIR="$PERSIST/pi/agent"
EOF
fi

install_or_update_pi_package() {
  local package="$1"
  local marker="${2:-$package}"

  if pi list --no-approve 2>/dev/null | grep -Fq "$marker"; then
    echo "Updating $package"
    if ! pi update "$package" --no-approve; then
      echo "$package update failed; run manually later: pi update $package"
    fi
  else
    echo "Installing $package"
    if ! pi install "$package" --no-approve; then
      echo "$package install skipped; run manually later: pi install $package"
    fi
  fi
}

# Trust GitHub for SSH-based private package installs. SSH credentials should
# come from VS Code SSH-agent forwarding; private keys are never copied here.
mkdir -p ~/.ssh
chmod 700 ~/.ssh
if ! ssh-keygen -F github.com >/dev/null 2>&1; then
  ssh-keyscan github.com >> ~/.ssh/known_hosts 2>/dev/null || echo "Could not add github.com to known_hosts"
fi
chmod 600 ~/.ssh/known_hosts 2>/dev/null || true

# Public Pi packages. Keep non-fatal so network/npm/GitHub issues do not break
# devcontainer creation; user can rerun printed commands later.
for package in "${PUBLIC_PI_PACKAGES[@]}"; do
  install_or_update_pi_package "$package"
done

SSH_READY=1
if [ -z "${SSH_AUTH_SOCK:-}" ]; then
  echo "SSH_AUTH_SOCK is not set; VS Code SSH-agent forwarding may be disabled"
  SSH_READY=0
elif ! ssh-add -l >/dev/null 2>&1; then
  echo "SSH agent is present but has no usable identities; load a GitHub key on the host with ssh-add"
  SSH_READY=0
fi

# Private SSH Pi packages. Keep non-fatal so missing SSH credentials, GitHub, or
# a temporary network issue do not break devcontainer setup.
for entry in "${PRIVATE_SSH_PI_PACKAGES[@]}"; do
  package="${entry%%|*}"
  marker="${entry#*|}"

  if [ "$SSH_READY" -ne 1 ]; then
    echo "$package install/update skipped; run manually later after fixing SSH: pi install $package"
  else
    install_or_update_pi_package "$package" "$marker"
  fi
done

if [ -f platformio.ini ]; then
  echo "pioarduino project detected; installing declared packages"
  if ! pio pkg install; then
    echo "pioarduino package installation failed; run manually later: pio pkg install"
  fi
else
  echo "No platformio.ini found; skipping pioarduino package installation"
fi
