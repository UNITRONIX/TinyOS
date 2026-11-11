#!/usr/bin/env bash
# Run the exact command sequence the CI workflow runs, locally.
#
# Usage: scripts/ci-local.sh
set -eu

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

steps="
prepare-test-env
test-host
iso
test-boot
test-minimal
test-memory-scaling
"

for step in $steps; do
    echo "=== make $step ==="
    make "$step"
done

echo "=== make ARCH=x86_64 iso ==="
make ARCH=x86_64 iso

echo "=== make test-boot-x86_64 ==="
make test-boot-x86_64

for step in test-virtio-block test-ata-block install-profile-check tapp-trust-test; do
    echo "=== make $step ==="
    make "$step"
done

echo "CI sequence passed"
