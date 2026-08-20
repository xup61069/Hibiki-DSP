#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include <cstdint>
#include <string>
#include <vector>

namespace hibiki {

struct AudioSessionIdentityV1 {
    std::string endpoint_id;
    std::string session_instance_id;
    std::uint32_t process_id{0}; // advisory only; never the identity key
};

enum class SessionGainOwner : std::uint8_t {
    WindowsSession,
    HibikiInternal,
};

struct AudioSessionDescriptorV1 {
    std::uint32_t schema_version{1};
    AudioSessionIdentityV1 identity{};
    std::string display_name;
    std::string app_id;
    bool active{true};
    SessionGainOwner gain_owner{SessionGainOwner::WindowsSession};
    std::string lane_id;
    std::string output_group;
    double makeup_gain_db{0.0};
};

class AudioSessionRegistry final {
public:
    [[nodiscard]] bool upsert(AudioSessionDescriptorV1 descriptor);
    [[nodiscard]] bool remove(const AudioSessionIdentityV1& identity) noexcept;
    [[nodiscard]] bool bind(const AudioSessionIdentityV1& identity,
                            std::string lane_id,
                            std::string output_group);
    [[nodiscard]] bool set_gain_owner(const AudioSessionIdentityV1& identity,
                                      SessionGainOwner owner) noexcept;
    [[nodiscard]] bool set_makeup_gain_db(const AudioSessionIdentityV1& identity,
                                          double makeup_gain_db) noexcept;
    void mark_endpoint_sessions_inactive(const std::string& endpoint_id) noexcept;
    [[nodiscard]] AudioSessionDescriptorV1* find(
        const AudioSessionIdentityV1& identity) noexcept;
    [[nodiscard]] const AudioSessionDescriptorV1* find(
        const AudioSessionIdentityV1& identity) const noexcept;
    [[nodiscard]] const std::vector<AudioSessionDescriptorV1>& sessions() const noexcept {
        return sessions_;
    }

private:
    static bool valid(const AudioSessionDescriptorV1& descriptor) noexcept;
    static bool same_identity(const AudioSessionIdentityV1& left,
                              const AudioSessionIdentityV1& right) noexcept;

    std::vector<AudioSessionDescriptorV1> sessions_;
};

}  // namespace hibiki
