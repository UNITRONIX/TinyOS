#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$repo_root"

BOOT_LOG="${TINYOS_BOOT_LOG:-build/boot-smoke.log}"
QEMU_MEMORY="${TINYOS_QEMU_MEMORY:-32M}"
BOOT_TIMEOUT="${TINYOS_BOOT_TIMEOUT:-8s}"

usage()
{
    cat <<'USAGE'
TinyOS local development helper

Usage:
  scripts/tinyos-dev.sh <command> [args]

Commands:
  help              Show this help
  install-deps      Check or install host build dependencies
  check             Verify build and test toolchain (make prepare-test-env)
  build             Build kernel only (make all)
  iso               Build bootable ISO (make iso)
  terminal-iso      Build terminal-only ISO (make terminal-only-iso)
  run               Run TinyOS interactively with VGA (make run)
  run-serial        Run headless with serial on stdout (make run-headless)
  debug-run         Run debug ISO with serial checkpoints (make debug-run)
  test              Boot smoke test (make test-boot)
  test-gate         Full stability + security gate (required before closing a change scope)
  test-terminal     Terminal-only boot smoke test
  test-stability    Longer boot smoke test (make test-stability)
  test-minimal      Minimum RAM envelope test (make test-minimal)
  clean             Remove build artifacts (make clean)
  log               Show the latest boot smoke log
  log-tail          Follow boot smoke log updates (requires inotifywait)
  qemu              Run custom QEMU command (pass args after --)

Environment variables:
  TINYOS_BOOT_LOG       Boot log path (default: build/boot-smoke.log)
  TINYOS_QEMU_MEMORY    QEMU RAM for custom runs (default: 32M)
  TINYOS_BOOT_TIMEOUT   Boot test timeout (default: 8s)

Examples:
  scripts/tinyos-dev.sh check
  scripts/tinyos-dev.sh iso && scripts/tinyos-dev.sh test
  scripts/tinyos-dev.sh run-serial
  scripts/tinyos-dev.sh qemu -- -cdrom build/tinyos.iso -m 64M -serial stdio -display none
USAGE
}

require_make()
{
    if ! command -v make >/dev/null 2>&1; then
        echo "Missing required tool: make" >&2
        exit 2
    fi
}

run_make()
{
    require_make
    make "$@"
}

cmd="${1:-help}"
shift || true

case "$cmd" in
    help|-h|--help)
        usage
        ;;
    install-deps)
        bash scripts/install-deps.sh "$@"
        ;;
    check|env)
        run_make prepare-test-env
        ;;
    build)
        run_make all
        ;;
    iso)
        run_make iso
        ;;
    terminal-iso)
        run_make terminal-only-iso
        ;;
    run)
        run_make run
        ;;
    run-serial|serial)
        run_make run-headless
        ;;
    debug-run|debug)
        run_make debug-run
        ;;
    test|test-boot)
        run_make test-boot
        ;;
    test-gate|gate)
        run_make test-gate
        ;;
    test-terminal|test-terminal-boot)
        run_make test-terminal-boot
        ;;
    test-stability|stability)
        run_make test-stability
        ;;
    test-minimal|minimal)
        run_make test-minimal
        ;;
    clean)
        run_make clean
        ;;
    log)
        if [[ ! -f "$BOOT_LOG" ]]; then
            echo "No boot log at $BOOT_LOG. Run 'scripts/tinyos-dev.sh test' first." >&2
            exit 1
        fi
        cat "$BOOT_LOG"
        ;;
    log-tail)
        if [[ ! -f "$BOOT_LOG" ]]; then
            echo "No boot log at $BOOT_LOG." >&2
            exit 1
        fi
        if command -v inotifywait >/dev/null 2>&1; then
            tail -n 40 -f "$BOOT_LOG" &
            tail_pid=$!
            inotifywait -e modify "$BOOT_LOG" >/dev/null 2>&1 || true
            kill "$tail_pid" 2>/dev/null || true
        else
            tail -f "$BOOT_LOG"
        fi
        ;;
    qemu)
        if [[ "${1:-}" == "--" ]]; then
            shift
        fi
        if [[ $# -eq 0 ]]; then
            echo "Pass QEMU arguments after '--'." >&2
            exit 2
        fi
        require_make
        make check-qemu-tools
        exec qemu-system-i386 "$@"
        ;;
    *)
        echo "Unknown command: $cmd" >&2
        usage >&2
        exit 2
        ;;
esac
