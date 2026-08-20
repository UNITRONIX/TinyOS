#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$repo_root"

INSTALL=0
if [[ "${1:-}" == "--install" ]]; then
    INSTALL=1
fi

detect_os()
{
    if [[ -r /etc/os-release ]]; then
        # shellcheck disable=SC1091
        . /etc/os-release
        OS_ID="${ID:-unknown}"
        OS_PRETTY="${PRETTY_NAME:-$OS_ID}"
        return 0
    fi

    OS_ID="unknown"
    OS_PRETTY="unknown"
}

tool_available()
{
    local tool=$1
    local alt=${2:-}

    command -v "$tool" >/dev/null 2>&1 && return 0
    [[ -n "$alt" ]] && command -v "$alt" >/dev/null 2>&1
}

print_tool_line()
{
    local tool=$1
    local alt=${2:-}
    local label=$tool

    if [[ -n "$alt" ]]; then
        label="${tool} (or ${alt})"
    fi

    if tool_available "$tool" "$alt"; then
        if command -v "$tool" >/dev/null 2>&1; then
            echo "  found: $label ($(command -v "$tool"))"
        else
            echo "  found: $label ($(command -v "$alt"))"
        fi
        return 0
    fi

    echo "  missing: $label"
    return 1
}

MISSING_COUNT=0

check_tools()
{
    local missing=0

    echo "Build tools:"
    print_tool_line clang++ || missing=$((missing + 1))
    print_tool_line nasm || missing=$((missing + 1))
    print_tool_line ld.lld lld || missing=$((missing + 1))
    echo ""

    echo "Image tools:"
    print_tool_line grub2-mkrescue grub-mkrescue || missing=$((missing + 1))
    print_tool_line xorriso || missing=$((missing + 1))
    echo ""

    echo "QEMU tools:"
    print_tool_line qemu-system-i386 || missing=$((missing + 1))
    print_tool_line timeout || missing=$((missing + 1))
    echo ""

    MISSING_COUNT=$missing
}

packages_for_missing_tools()
{
    local -a packages=()

    tool_available clang++ || packages+=(clang)
    tool_available ld.lld lld || packages+=(lld)
    tool_available nasm || packages+=(nasm)
    tool_available grub2-mkrescue grub-mkrescue || packages+=(grub2-tools-extra)
    tool_available xorriso || packages+=(xorriso)
    tool_available qemu-system-i386 || packages+=(qemu-system-x86)
    command -v make >/dev/null 2>&1 || packages+=(make)

    if ((${#packages[@]} == 0)); then
        return 0
    fi

    printf '%s\n' "${packages[@]}"
}

install_hint()
{
    case "$OS_ID" in
        fedora|rhel|centos|rocky|almalinux)
            echo "Fedora install command:"
            echo "  sudo dnf install -y clang lld nasm make grub2-tools-extra xorriso qemu-system-x86"
            ;;
        debian|ubuntu|linuxmint|pop)
            echo "Debian / Ubuntu install command:"
            echo "  sudo apt install -y clang lld nasm make grub-pc-bin xorriso qemu-system-x86"
            ;;
        *)
            echo "Install: clang lld nasm make grub-mkrescue xorriso qemu-system-i386"
            ;;
    esac
}

dnf_busy_pids()
{
    pgrep -af '/usr/bin/dnf|/usr/bin/dnf5|PackageKit' 2>/dev/null || true
}

ensure_dnf_available()
{
    if [[ -z "$(dnf_busy_pids)" ]]; then
        return 0
    fi

    echo "Inny menedżer pakietów blokuje dnf:" >&2
    dnf_busy_pids >&2
    echo "" >&2
    echo "Poczekaj aż zakończy się powyższy proces albo go zatrzymaj, potem uruchom ponownie:" >&2
    echo "  scripts/tinyos-dev.sh install-deps --install" >&2
    exit 1
}

run_install()
{
    case "$OS_ID" in
        fedora|rhel|centos|rocky|almalinux)
            ensure_dnf_available

            mapfile -t packages < <(packages_for_missing_tools)
            if ((${#packages[@]} == 0)); then
                echo "Wszystkie wymagane pakiety są już zainstalowane."
                return 0
            fi

            echo "Instaluję brakujące pakiety: ${packages[*]}"
            echo "(to może potrwać kilka minut — dnf pobiera pakiety z repozytoriów)"
            echo ""

            sudo dnf install -y "${packages[@]}"
            ;;
        debian|ubuntu|linuxmint|pop)
            mapfile -t packages < <(packages_for_missing_tools)
            if ((${#packages[@]} == 0)); then
                echo "Wszystkie wymagane pakiety są już zainstalowane."
                return 0
            fi

            echo "Instaluję brakujące pakiety: ${packages[*]}"
            sudo apt install -y "${packages[@]}"
            ;;
        *)
            echo "Automatyczna instalacja nie jest skonfigurowana dla '$OS_ID'." >&2
            install_hint
            exit 2
            ;;
    esac
}

main()
{
    detect_os

    echo "TinyOS dependency check"
    echo "System: $OS_PRETTY"
    echo ""

    local missing=0
    check_tools
    missing=$MISSING_COUNT

    if [[ "$INSTALL" -eq 1 ]]; then
        if [[ "$missing" -eq 0 ]]; then
            echo "TinyOS toolchain is ready."
            exit 0
        fi

        run_install
        echo ""
        echo "Ponowne sprawdzenie..."
        echo ""

        check_tools
        missing=$MISSING_COUNT
    fi

    if [[ "$missing" -eq 0 ]]; then
        echo "TinyOS toolchain is ready."
        echo ""
        echo "Next steps:"
        echo "  scripts/tinyos-dev.sh iso"
        echo "  scripts/tinyos-dev.sh test"
        exit 0
    fi

    echo "Brakuje $missing narzędzi."
    echo ""
    install_hint
    echo ""
    echo "Szybka instalacja (wymaga sudo, ok. 1–3 min):"
    echo "  scripts/tinyos-dev.sh install-deps --install"
    exit 1
}

main "$@"
