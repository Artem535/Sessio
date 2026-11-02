#!/usr/bin/env bash
set -euo pipefail
project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT
# Antora 3 requires a directory .git; Codex linked worktrees use a .git file.
# A temporary documentation-only repository includes the current uncommitted pages.
mkdir -p "$scratch/docs"
cp -R "$project_root/docs/antora" "$scratch/docs/antora"
cp "$project_root/antora-playbook.yml" "$scratch/antora-playbook.yml"
git -C "$scratch" init --quiet
git -C "$scratch" add docs/antora
git -C "$scratch" -c user.name='Documentation build' -c user.email='docs@localhost' commit --quiet -m 'Build documentation snapshot'
cd "$scratch"
npx --yes --package @antora/cli@3.1.12 --package @antora/site-generator@3.1.12 antora antora-playbook.yml --to-dir "$project_root/build-antora" --log-failure-level warn
