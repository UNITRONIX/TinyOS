#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$repo_root"

usage()
{
    cat <<'USAGE'
TinyOS image helper

Usage:
  scripts/tinyos-image.sh plan
    scripts/tinyos-image.sh provision-plan
    scripts/tinyos-image.sh install-plan
  scripts/tinyos-image.sh check-profile [profile]
    scripts/tinyos-image.sh check-install-profile [profile]
    scripts/tinyos-image.sh check-app [app.manifest|package.tapp]
    scripts/tinyos-image.sh pack-app [app.manifest] [output.tapp]
    scripts/tinyos-image.sh keygen-app [private-key.pem] [public-key.pem]
    scripts/tinyos-image.sh sign-app <package.tapp> <private-key.pem> [signature] [receipt]
    scripts/tinyos-image.sh verify-app [package.tapp] [public-key.pem] [signature]
    scripts/tinyos-image.sh trust-app [public-key.pem] [trust-manifest]
  scripts/tinyos-image.sh build [output.iso]
  scripts/tinyos-image.sh manifest <image> [manifest]
  scripts/tinyos-image.sh keygen [age-secret-key]
  scripts/tinyos-image.sh sign <image> <private-key.pem> [signature]
  scripts/tinyos-image.sh encrypt <image> <age-recipient-file> [encrypted-output]
    scripts/tinyos-image.sh deploy-check <image>
  scripts/tinyos-image.sh deploy <image> <user@host:path>

Notes:
  - build uses the existing TinyOS Makefile and produces a bootable ISO copy.
    - .tapp is currently a signed-manifest package contract, not a dynamic loader.
  - keygen/encrypt prefer age keys for small, simple deployment encryption.
  - sign uses openssl and should be paired with target-side verification later.
  - deploy currently uses scp/ssh transport from the host workstation.
USAGE
}

need_tool()
{
    local tool=$1
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "Missing required tool: $tool" >&2
        exit 2
    fi
}

timestamp_utc()
{
    date -u +%Y%m%dT%H%M%SZ
}

sha256_value()
{
    local file=$1
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$file" | awk '{ print $1 }'
        return
    fi

    if command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$file" | awk '{ print $1 }'
        return
    fi

    echo "Missing sha256sum or shasum." >&2
    exit 2
}

sha256_stdin_value()
{
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum | awk '{ print $1 }'
        return
    fi

    if command -v shasum >/dev/null 2>&1; then
        shasum -a 256 | awk '{ print $1 }'
        return
    fi

    echo "Missing sha256sum or shasum." >&2
    exit 2
}

validate_private_key()
{
    local private_key=$1
    need_tool openssl
    openssl pkey -in "$private_key" -noout >/dev/null 2>&1
}

validate_public_key()
{
    local public_key=$1
    need_tool openssl
    openssl pkey -pubin -in "$public_key" -noout >/dev/null 2>&1
}

public_key_fingerprint()
{
    local public_key=$1
    validate_public_key "$public_key"
    openssl pkey -pubin -in "$public_key" -outform DER 2>/dev/null | sha256_stdin_value
}

profile_field_value()
{
    local key=$1
    local file=$2
    awk -F= -v key="$key" '$1 == key { print $2; exit }' "$file"
}

print_plan()
{
    cat <<'PLAN'
TinyOS secure image plan:
    0. provision-plan - show the project provisioning workbench plan
  1. check-profile  - validate system.profile basics
    2. check-app      - validate app manifest or .tapp package basics
    3. pack-app       - create a .tapp package envelope
    4. keygen-app     - create an app signing key pair without overwriting an existing pair
    5. trust-app      - record app public key fingerprint
    6. sign-app       - sign a .tapp package and write a receipt
    7. verify-app     - check .tapp policy fields and optional signature
    8. build          - compile TinyOS and copy a bootable ISO
    9. manifest       - record hash and deployment metadata
 10. keygen         - create an age deployment key pair
 11. sign           - sign image or manifest with a developer key
 12. encrypt        - encrypt image for a target recipient
 13. deploy-check   - require signed and encrypted artifacts before transport
 14. deploy         - send image through SSH/SCP transport
 15. target-verify  - future TinyOS-side signature/policy verification
 16. rollback       - future TinyOS-side rollback slot activation
PLAN
}

print_provision_plan()
{
    cat <<'PLAN'
TinyOS provisioning workbench plan:
  1. provisioninit      - create an isolated project workspace folder
  2. provisionconfig    - set encryption, signing, API, terminal and remote defaults
  3. provisionvariant   - define target variants with RAM, ROM, display and feature budgets
  4. provisionresources - estimate RAM, ROM/image and package footprint for a variant
  5. provisionui        - future color terminal workbench for project provisioning
  6. remoteaccess       - opt-in SSH/SFTP access to the project workspace or deploy folder
  7. build/sign/encrypt - produce deployable artifacts using the project defaults
  8. deploy-check       - reject unsigned or plaintext remote deployment artifacts

Current host command status:
  ready: check-profile, check-app, pack-app, keygen-app, trust-app, sign-app, verify-app
  ready: build, manifest, keygen, sign, encrypt, deploy-check, deploy
  planned: provisioninit, provisionconfig, provisionvariant, provisionresources, provisionui, remoteaccess

Default safety policy:
  encryption=required for remote provisioning
  remote.access=disabled until configured
  private keys stay outside deployed images
PLAN
}

print_install_plan()
{
    cat <<'PLAN'
TinyOS installed-system plan:
  1. check-install-profile - validate installer inputs without writing disks
  2. installinfo           - show the installed-system contract inside TinyOS
  3. install mock          - future terminal installer receipt in RAMFS
  4. disk image            - future host-side persistent disk image output
  5. disk boot test        - future QEMU boot from installed TinyOS disk

Current host command status:
  ready: install-plan, check-install-profile
  planned: install, installcheck, hostname, netconfig, passwd, whoami, id

Default safety policy:
  credential.bootstrap=prompt
  security.password_hashing=required
  security.plaintext_secrets=forbidden
  provisioning.remote_access=disabled
PLAN
}

check_profile()
{
    local profile=${1:-examples/system.profile}
    if [[ ! -f "$profile" ]]; then
        echo "Profile not found: $profile" >&2
        exit 1
    fi

    local required_patterns=(
        '^profile.name='
        '^target.arch='
        '^image.boot='
        '^provision.workspace='
        '^provision.isolation='
        '^devices.variants='
        '^resources.ram_budget_mib='
        '^resources.rom_budget_mib='
        '^apps.required='
        '^security.signing='
        '^security.trust_store='
        '^security.encryption='
        '^remote.access='
    )

    local pattern
    for pattern in "${required_patterns[@]}"; do
        if ! grep -q "$pattern" "$profile"; then
            echo "Profile check failed: missing $pattern" >&2
            exit 1
        fi
    done

    echo "Profile check passed: $profile"
}

check_install_profile()
{
    local profile=${1:-examples/install.profile}
    if [[ ! -f "$profile" ]]; then
        echo "Install profile not found: $profile" >&2
        exit 1
    fi

    local required_patterns=(
        '^profile.name='
        '^install.mode='
        '^install.media='
        '^install.target='
        '^target.arch='
        '^target.platform='
        '^disk.target='
        '^disk.partition='
        '^device.name='
        '^network.mode='
        '^user.name='
        '^credential.bootstrap='
        '^admin.mode='
        '^security.password_hashing='
        '^security.plaintext_secrets='
        '^provisioning.encryption='
        '^provisioning.remote_access='
    )

    local pattern
    for pattern in "${required_patterns[@]}"; do
        if ! grep -q "$pattern" "$profile"; then
            echo "Install profile check failed: missing $pattern" >&2
            exit 1
        fi
    done

    if grep -Eq '(^|[.])password=' "$profile"; then
        echo "Install profile check failed: plaintext password fields are forbidden." >&2
        exit 1
    fi

    local bootstrap
    local admin_mode
    local password_hashing
    local plaintext_secrets
    local encryption
    local remote_access
    bootstrap=$(profile_field_value credential.bootstrap "$profile")
    admin_mode=$(profile_field_value admin.mode "$profile")
    password_hashing=$(profile_field_value security.password_hashing "$profile")
    plaintext_secrets=$(profile_field_value security.plaintext_secrets "$profile")
    encryption=$(profile_field_value provisioning.encryption "$profile")
    remote_access=$(profile_field_value provisioning.remote_access "$profile")

    if [[ "$bootstrap" != "prompt" ]]; then
        echo "Install profile check failed: credential.bootstrap must be prompt." >&2
        exit 1
    fi

    case "$admin_mode" in
        disabled|same-bootstrap-secret|separate-secret|key-only)
            ;;
        *)
            echo "Install profile check failed: invalid admin.mode: $admin_mode" >&2
            exit 1
            ;;
    esac

    if [[ "$password_hashing" != "required" ]]; then
        echo "Install profile check failed: security.password_hashing must be required." >&2
        exit 1
    fi

    if [[ "$plaintext_secrets" != "forbidden" ]]; then
        echo "Install profile check failed: security.plaintext_secrets must be forbidden." >&2
        exit 1
    fi

    if [[ "$encryption" != "required" ]]; then
        echo "Install profile check failed: provisioning.encryption must be required." >&2
        exit 1
    fi

    if [[ "$remote_access" != "disabled" ]]; then
        echo "Install profile check failed: provisioning.remote_access must default to disabled." >&2
        exit 1
    fi

    echo "Install profile check passed: $profile"
    if [[ "$admin_mode" == "same-bootstrap-secret" ]]; then
        echo "Install profile warning: shared bootstrap secret is for development or single-user profiles only."
    fi
}

check_app_manifest()
{
    local app_manifest=${1:-examples/app.manifest}
    if [[ ! -f "$app_manifest" ]]; then
        echo "App manifest not found: $app_manifest" >&2
        exit 1
    fi

    local required_patterns=(
        '^app.name='
        '^app.version='
        '^app.runtime='
        '^app.entry='
        '^app.capabilities='
        '^app.trust='
    )

    local pattern
    for pattern in "${required_patterns[@]}"; do
        if ! grep -q "$pattern" "$app_manifest"; then
            echo "App manifest check failed: missing $pattern" >&2
            exit 1
        fi
    done

    if [[ "$app_manifest" == *.tapp ]]; then
        local tapp_patterns=(
            '^tinyos.tapp.version='
            '^tinyos.tapp.kind='
            '^tinyos.tapp.source_sha256='
            '^tinyos.tapp.signature_policy='
            '^tinyos.tapp.signature_state='
            '^tinyos.tapp.payload_policy='
            '^tinyos.tapp.payload_state='
        )

        for pattern in "${tapp_patterns[@]}"; do
            if ! grep -q "$pattern" "$app_manifest"; then
                echo "TAPP check failed: missing $pattern" >&2
                exit 1
            fi
        done
    fi

    echo "App manifest check passed: $app_manifest"
}

manifest_value()
{
    local key=$1
    local file=$2
    awk -F= -v key="$key" '$1 == key { print $2; exit }' "$file"
}

pack_app()
{
    local app_manifest=${1:-examples/app.manifest}
    local output=${2:-}
    check_app_manifest "$app_manifest" >/dev/null

    local app_name
    app_name=$(manifest_value app.name "$app_manifest")
    if [[ -z "$app_name" ]]; then
        echo "App manifest has empty app.name" >&2
        exit 1
    fi

    if [[ -z "$output" ]]; then
        output="build/apps/$app_name.tapp"
    fi

    mkdir -p "$(dirname "$output")"
    local hash
    hash=$(sha256_value "$app_manifest")
    {
        echo "tinyos.tapp.version=0"
        echo "tinyos.tapp.kind=manifest-envelope"
        echo "tinyos.tapp.source=$app_manifest"
        echo "tinyos.tapp.source_sha256=$hash"
        echo "tinyos.tapp.generated_utc=$(timestamp_utc)"
        echo "tinyos.tapp.signature_policy=required"
        echo "tinyos.tapp.signature_state=unsigned"
        echo "tinyos.tapp.payload_policy=hash-required"
        echo "tinyos.tapp.payload_state=external"
        cat "$app_manifest"
    } > "$output"

    check_app_manifest "$output" >/dev/null
    echo "Wrote TAPP package: $output"
    echo "Source SHA256: $hash"
}

keygen_app()
{
    local private_key=${1:-build/keys/tapp-dev-private.pem}
    local public_key=${2:-build/keys/tapp-dev-public.pem}
    need_tool openssl
    if [[ -e "$private_key" || -e "$public_key" ]]; then
        if [[ -f "$private_key" && -f "$public_key" ]]; then
            validate_private_key "$private_key"
            validate_public_key "$public_key"
            echo "App signing key pair already exists; reusing: $private_key / $public_key"
            return
        fi

        echo "Refusing to overwrite partial app signing key material. Remove both files or choose a new path." >&2
        exit 1
    fi

    mkdir -p "$(dirname "$private_key")" "$(dirname "$public_key")"
    openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:2048 -out "$private_key" >/dev/null 2>&1
    openssl pkey -in "$private_key" -pubout -out "$public_key" >/dev/null 2>&1
    echo "Wrote app signing private key: $private_key"
    echo "Wrote app signing public key : $public_key"
}

sign_app_package()
{
    local app_package=${1:-}
    local private_key=${2:-}
    local signature=${3:-}
    local receipt=${4:-}
    if [[ -z "$app_package" || ! -f "$app_package" ]]; then
        echo "TAPP package not found: $app_package" >&2
        exit 1
    fi
    if [[ "$app_package" != *.tapp ]]; then
        echo "Not a .tapp package: $app_package" >&2
        exit 1
    fi
    if [[ -z "$private_key" || ! -f "$private_key" ]]; then
        echo "Private key not found: $private_key" >&2
        exit 1
    fi
    validate_private_key "$private_key"
    if [[ -z "$signature" ]]; then
        signature="$app_package.sig"
    fi
    if [[ -z "$receipt" ]]; then
        receipt="$signature.receipt"
    fi

    check_app_manifest "$app_package" >/dev/null
    need_tool openssl
    mkdir -p "$(dirname "$signature")" "$(dirname "$receipt")"
    openssl dgst -sha256 -sign "$private_key" -out "$signature" "$app_package"

    local package_hash
    local signature_hash
    package_hash=$(sha256_value "$app_package")
    signature_hash=$(sha256_value "$signature")
    {
        echo "tinyos.tapp.signature.version=0"
        echo "tapp.path=$app_package"
        echo "tapp.sha256=$package_hash"
        echo "signature.path=$signature"
        echo "signature.sha256=$signature_hash"
        echo "signature.algorithm=rsa-sha256"
        echo "signature.state=created"
        echo "generated.utc=$(timestamp_utc)"
    } > "$receipt"

    echo "Wrote TAPP signature: $signature"
    echo "Wrote TAPP signature receipt: $receipt"
    echo "TAPP SHA256: $package_hash"
}

trust_app_key()
{
    local public_key=${1:-build/keys/tapp-dev-public.pem}
    local trust_manifest=${2:-}
    if [[ ! -f "$public_key" ]]; then
        echo "Public key not found: $public_key" >&2
        exit 1
    fi
    if [[ -z "$trust_manifest" ]]; then
        trust_manifest="$public_key.trust"
    fi

    local key_hash
    key_hash=$(public_key_fingerprint "$public_key")
    mkdir -p "$(dirname "$trust_manifest")"
    {
        echo "tinyos.tapp.trust.version=0"
        echo "public_key.path=$public_key"
        echo "public_key.der_sha256=$key_hash"
        echo "algorithm=rsa-sha256"
        echo "scope=app-package"
        echo "trust.state=development-only"
        echo "kernel.anchor=tinyos-dev-app-signing"
        echo "generated.utc=$(timestamp_utc)"
    } > "$trust_manifest"

    echo "Wrote app trust manifest: $trust_manifest"
    echo "Public key DER SHA256: $key_hash"
}

verify_app_package()
{
    local app_package=${1:-examples/example-system-tool.tapp}
    local public_key=${2:-}
    local signature=${3:-}
    if [[ "$app_package" != *.tapp ]]; then
        echo "Not a .tapp package: $app_package" >&2
        exit 1
    fi

    check_app_manifest "$app_package" >/dev/null

    local signature_policy
    local payload_policy
    local source_hash
    signature_policy=$(manifest_value tinyos.tapp.signature_policy "$app_package")
    payload_policy=$(manifest_value tinyos.tapp.payload_policy "$app_package")
    source_hash=$(manifest_value tinyos.tapp.source_sha256 "$app_package")

    if [[ "$signature_policy" != "required" ]]; then
        echo "TAPP verify failed: signature_policy must be required" >&2
        exit 1
    fi

    if [[ "$payload_policy" != "hash-required" ]]; then
        echo "TAPP verify failed: payload_policy must be hash-required" >&2
        exit 1
    fi

    if [[ -z "$source_hash" ]]; then
        echo "TAPP verify failed: source hash is empty" >&2
        exit 1
    fi

    echo "TAPP verify policy passed: $app_package"
    echo "Signature policy: $signature_policy"
    echo "Payload policy  : $payload_policy"
    echo "Source SHA256   : $source_hash"

    if [[ -n "$public_key" ]]; then
        if [[ ! -f "$public_key" ]]; then
            echo "Public key not found: $public_key" >&2
            exit 1
        fi
        validate_public_key "$public_key"
        if [[ -z "$signature" ]]; then
            signature="$app_package.sig"
        fi
        if [[ ! -f "$signature" ]]; then
            echo "Signature not found: $signature" >&2
            exit 1
        fi

        need_tool openssl
        openssl dgst -sha256 -verify "$public_key" -signature "$signature" "$app_package" >/dev/null
        echo "Signature verify: ok"
        echo "Signature file : $signature"
    fi
}

build_image()
{
    local output=${1:-build/images/tinyos-$(timestamp_utc).iso}
    mkdir -p "$(dirname "$output")"
    make iso
    cp build/tinyos.iso "$output"
    sha256_value "$output" > "$output.sha256"
    echo "Built image: $output"
    echo "SHA256: $(cat "$output.sha256")"
}

write_manifest()
{
    local image=${1:-}
    local manifest=${2:-}
    if [[ -z "$image" || ! -f "$image" ]]; then
        echo "Image not found: $image" >&2
        exit 1
    fi

    if [[ -z "$manifest" ]]; then
        manifest="$image.manifest"
    fi

    mkdir -p "$(dirname "$manifest")"
    local hash
    hash=$(sha256_value "$image")
    {
        echo "tinyos.image.manifest.version=0"
        echo "image.path=$image"
        echo "image.sha256=$hash"
        echo "generated.utc=$(timestamp_utc)"
        echo "profile.path=examples/system.profile"
        echo "app.package.extension=.tapp"
        echo "app.signing.required=true"
        echo "signing.required=true"
        echo "encryption.recommended=true"
        echo "transport=ssh-scp-now,tinylink-later"
        echo "target.verify=planned"
        echo "rollback=planned"
    } > "$manifest"

    echo "Wrote manifest: $manifest"
}

keygen()
{
    local output=${1:-build/images/tinyos-deploy.agekey}
    need_tool age-keygen
    mkdir -p "$(dirname "$output")"
    age-keygen -o "$output"
    awk '/^# public key: / { print $4 }' "$output" > "$output.pub"
    echo "Wrote secret key: $output"
    echo "Wrote recipient : $output.pub"
}

sign_image()
{
    local image=${1:-}
    local private_key=${2:-}
    local signature=${3:-}
    if [[ -z "$image" || ! -f "$image" ]]; then
        echo "Image not found: $image" >&2
        exit 1
    fi
    if [[ -z "$private_key" || ! -f "$private_key" ]]; then
        echo "Private key not found: $private_key" >&2
        exit 1
    fi
    if [[ -z "$signature" ]]; then
        signature="$image.sig"
    fi

    need_tool openssl
    openssl dgst -sha256 -sign "$private_key" -out "$signature" "$image"
    echo "Wrote signature: $signature"
}

encrypt_image()
{
    local image=${1:-}
    local recipient_file=${2:-}
    local output=${3:-}
    if [[ -z "$image" || ! -f "$image" ]]; then
        echo "Image not found: $image" >&2
        exit 1
    fi
    if [[ -z "$recipient_file" || ! -f "$recipient_file" ]]; then
        echo "Recipient file not found: $recipient_file" >&2
        exit 1
    fi
    if [[ -z "$output" ]]; then
        output="$image.age"
    fi

    need_tool age
    age -R "$recipient_file" -o "$output" "$image"
    echo "Wrote encrypted image: $output"
}

deploy_signature_path()
{
    local image=$1
    if [[ -f "$image.sig" ]]; then
        echo "$image.sig"
        return
    fi

    if [[ "$image" == *.age && -f "${image%.age}.sig" ]]; then
        echo "${image%.age}.sig"
        return
    fi

    return 1
}

deploy_check()
{
    local image=${1:-}
    if [[ -z "$image" || ! -f "$image" ]]; then
        echo "Image not found: $image" >&2
        exit 1
    fi

    local encrypted=false
    local signed=false
    local signature=""
    if [[ "$image" == *.age ]]; then
        encrypted=true
    fi

    if signature=$(deploy_signature_path "$image"); then
        signed=true
    fi

    if [[ "$encrypted" != true && "${TINYOS_ALLOW_PLAINTEXT_DEPLOY:-0}" != "1" ]]; then
        echo "Deploy check failed: image must be encrypted (.age) before remote transport." >&2
        exit 1
    fi

    if [[ "$signed" != true && "${TINYOS_ALLOW_UNSIGNED_DEPLOY:-0}" != "1" ]]; then
        echo "Deploy check failed: missing adjacent signature for $image." >&2
        exit 1
    fi

    echo "Deploy check passed: $image"
    if [[ -n "$signature" ]]; then
        echo "Signature evidence: $signature"
    fi
    if [[ "$encrypted" != true ]]; then
        echo "Plaintext deploy override: enabled"
    fi
    if [[ "$signed" != true ]]; then
        echo "Unsigned deploy override: enabled"
    fi
}

deploy_image()
{
    local image=${1:-}
    local target=${2:-}
    if [[ -z "$image" || ! -f "$image" ]]; then
        echo "Image not found: $image" >&2
        exit 1
    fi
    if [[ -z "$target" ]]; then
        echo "Missing deploy target. Use user@host:path" >&2
        exit 1
    fi

    deploy_check "$image"
    need_tool scp
    scp "$image" "$target"
    echo "Deployed image to: $target"
}

command_name=${1:-plan}
if [[ $# -gt 0 ]]; then
    shift
fi

case "$command_name" in
    plan)
        print_plan
        ;;
    provision-plan)
        print_provision_plan
        ;;
    install-plan)
        print_install_plan
        ;;
    check-profile)
        check_profile "$@"
        ;;
    check-install-profile)
        check_install_profile "$@"
        ;;
    check-app)
        check_app_manifest "$@"
        ;;
    pack-app)
        pack_app "$@"
        ;;
    keygen-app)
        keygen_app "$@"
        ;;
    sign-app)
        sign_app_package "$@"
        ;;
    verify-app)
        verify_app_package "$@"
        ;;
    trust-app)
        trust_app_key "$@"
        ;;
    build)
        build_image "$@"
        ;;
    manifest)
        write_manifest "$@"
        ;;
    keygen)
        keygen "$@"
        ;;
    sign)
        sign_image "$@"
        ;;
    encrypt)
        encrypt_image "$@"
        ;;
    deploy-check)
        deploy_check "$@"
        ;;
    deploy)
        deploy_image "$@"
        ;;
    help|--help|-h)
        usage
        ;;
    *)
        usage
        echo "Unknown command: $command_name" >&2
        exit 1
        ;;
esac