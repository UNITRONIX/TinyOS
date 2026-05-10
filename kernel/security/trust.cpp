#include <stddef.h>
#include <stdint.h>

#include <tinyos/kernel/klog.hpp>
#include <tinyos/kernel/security/trust.hpp>

namespace
{
    using tinyos::kernel::security::trust::KeyUse;
    using tinyos::kernel::security::trust::State;
    using tinyos::kernel::security::trust::TrustAnchor;

    const TrustAnchor g_anchors[] = {
        { "tinyos-dev-app-signing", "rsa-sha256", "sha256:host-build-key", "build/keys/tapp-dev-public.pem", KeyUse::AppPackage, State::DevelopmentOnly, true, false },
        { "tinyos-release-app-root", "rsa-sha256", "sha256:release-app-root-planned", "future-secure-storage", KeyUse::AppPackage, State::Planned, true, false },
        { "tinyos-image-signing-root", "rsa-sha256", "sha256:image-root-planned", "future-secure-storage", KeyUse::ImageManifest, State::Planned, false, true },
        { "tinyos-recovery-root", "rsa-sha256", "sha256:recovery-root-planned", "future-rom-or-fuse", KeyUse::Recovery, State::Planned, false, true },
        { "tinyos-revoked-test-key", "rsa-sha256", "sha256:revoked-test-key", "test-fixture", KeyUse::AppPackage, State::Revoked, false, false }
    };

    bool g_ready = false;

    bool strings_equal(const char* left, const char* right)
    {
        if (left == nullptr || right == nullptr)
        {
            return left == right;
        }

        while (*left != '\0' && *right != '\0')
        {
            if (*left != *right)
            {
                return false;
            }

            ++left;
            ++right;
        }

        return *left == '\0' && *right == '\0';
    }

    bool active_anchor_state(State state)
    {
        return state == State::Trusted || state == State::DevelopmentOnly;
    }
}

namespace tinyos::kernel::security::trust
{
    void initialize()
    {
        g_ready = true;
        tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "TAPP trust store initialized.");
    }

    bool is_ready()
    {
        return g_ready;
    }

    size_t count()
    {
        return sizeof(g_anchors) / sizeof(g_anchors[0]);
    }

    size_t trusted_count()
    {
        size_t trusted = 0;
        for (size_t index = 0; index < count(); ++index)
        {
            if (g_anchors[index].state == State::Trusted)
            {
                ++trusted;
            }
        }

        return trusted;
    }

    size_t development_count()
    {
        size_t development = 0;
        for (size_t index = 0; index < count(); ++index)
        {
            if (g_anchors[index].state == State::DevelopmentOnly)
            {
                ++development;
            }
        }

        return development;
    }

    size_t planned_count()
    {
        size_t planned = 0;
        for (size_t index = 0; index < count(); ++index)
        {
            if (g_anchors[index].state == State::Planned)
            {
                ++planned;
            }
        }

        return planned;
    }

    size_t app_package_anchor_count()
    {
        size_t anchors = 0;
        for (size_t index = 0; index < count(); ++index)
        {
            if (g_anchors[index].permits_app_packages && active_anchor_state(g_anchors[index].state))
            {
                ++anchors;
            }
        }

        return anchors;
    }

    size_t image_anchor_count()
    {
        size_t anchors = 0;
        for (size_t index = 0; index < count(); ++index)
        {
            if (g_anchors[index].permits_images && active_anchor_state(g_anchors[index].state))
            {
                ++anchors;
            }
        }

        return anchors;
    }

    const TrustAnchor* at(size_t index)
    {
        return index < count() ? &g_anchors[index] : nullptr;
    }

    const TrustAnchor* find(const char* name)
    {
        for (size_t index = 0; index < count(); ++index)
        {
            if (strings_equal(g_anchors[index].name, name))
            {
                return &g_anchors[index];
            }
        }

        return nullptr;
    }

    bool algorithm_allowed_for_apps(const char* algorithm)
    {
        for (size_t index = 0; index < count(); ++index)
        {
            const auto& anchor = g_anchors[index];
            if (anchor.permits_app_packages && active_anchor_state(anchor.state) && strings_equal(anchor.algorithm, algorithm))
            {
                return true;
            }
        }

        return false;
    }

    bool has_app_package_anchor()
    {
        return app_package_anchor_count() != 0;
    }

    bool validation_self_test()
    {
        const auto* development_anchor = find("tinyos-dev-app-signing");
        const auto* revoked_anchor = find("tinyos-revoked-test-key");
        return g_ready &&
            count() == 5 &&
            development_count() == 1 &&
            planned_count() >= 3 &&
            app_package_anchor_count() == 1 &&
            image_anchor_count() == 0 &&
            development_anchor != nullptr &&
            development_anchor->permits_app_packages &&
            algorithm_allowed_for_apps("rsa-sha256") &&
            revoked_anchor != nullptr &&
            revoked_anchor->state == State::Revoked;
    }

    const char* key_use_name(KeyUse use)
    {
        switch (use)
        {
        case KeyUse::AppPackage:
            return "app-package";
        case KeyUse::ImageManifest:
            return "image-manifest";
        case KeyUse::Recovery:
            return "recovery";
        }

        return "unknown";
    }

    const char* state_name(State state)
    {
        switch (state)
        {
        case State::Trusted:
            return "trusted";
        case State::DevelopmentOnly:
            return "development-only";
        case State::Planned:
            return "planned";
        case State::Revoked:
            return "revoked";
        }

        return "unknown";
    }
}