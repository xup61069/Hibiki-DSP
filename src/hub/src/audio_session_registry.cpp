#include "hibiki/audio_session_registry.hpp"

#include "hibiki/control_payloads.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace hibiki {
namespace {

constexpr std::size_t kMaxSessions = 256;
constexpr std::size_t kMaxIdentityLength = 512;
constexpr std::size_t kMaxLabelLength = 256;
constexpr std::size_t kMaxOutputGroupLength = 64;

}  // namespace

bool AudioSessionRegistry::same_identity(const AudioSessionIdentityV1& left,
                                          const AudioSessionIdentityV1& right) noexcept {
    return left.endpoint_id == right.endpoint_id &&
           left.session_instance_id == right.session_instance_id;
}

bool AudioSessionRegistry::valid(const AudioSessionDescriptorV1& descriptor) noexcept {
    return descriptor.schema_version == 1 && !descriptor.identity.endpoint_id.empty() &&
           descriptor.identity.endpoint_id.size() <= kMaxIdentityLength &&
           is_printable_utf8_v1(descriptor.identity.endpoint_id) &&
           !descriptor.identity.session_instance_id.empty() &&
           descriptor.identity.session_instance_id.size() <= kMaxIdentityLength &&
           is_printable_utf8_v1(descriptor.identity.session_instance_id) &&
           descriptor.display_name.size() <= kMaxLabelLength &&
           is_printable_utf8_v1(descriptor.display_name) &&
           descriptor.app_id.size() <= kMaxLabelLength &&
           is_printable_utf8_v1(descriptor.app_id) &&
           descriptor.lane_id.size() <= kMaxLabelLength &&
           is_printable_utf8_v1(descriptor.lane_id) &&
           descriptor.output_group.size() <= kMaxOutputGroupLength &&
           is_printable_utf8_v1(descriptor.output_group) &&
           std::isfinite(descriptor.makeup_gain_db) && descriptor.makeup_gain_db >= -144.0 &&
           descriptor.makeup_gain_db <= 12.0;
}

bool AudioSessionRegistry::upsert(AudioSessionDescriptorV1 descriptor) {
    if (!valid(descriptor)) {
        return false;
    }
    const auto existing = std::find_if(
        sessions_.begin(), sessions_.end(), [&](const auto& item) {
            return same_identity(item.identity, descriptor.identity);
        });
    if (existing != sessions_.end()) {
        descriptor.lane_id = existing->lane_id;
        descriptor.output_group = existing->output_group;
        descriptor.gain_owner = existing->gain_owner;
        descriptor.makeup_gain_db = existing->makeup_gain_db;
        *existing = std::move(descriptor);
        return true;
    }
    if (sessions_.size() >= kMaxSessions) {
        return false;
    }
    sessions_.push_back(std::move(descriptor));
    return true;
}

bool AudioSessionRegistry::remove(const AudioSessionIdentityV1& identity) noexcept {
    const auto existing = std::find_if(
        sessions_.begin(), sessions_.end(), [&](const auto& item) {
            return same_identity(item.identity, identity);
        });
    if (existing == sessions_.end()) {
        return false;
    }
    sessions_.erase(existing);
    return true;
}

bool AudioSessionRegistry::bind(const AudioSessionIdentityV1& identity,
                                std::string lane_id,
                                std::string output_group) {
    if (lane_id.empty() || lane_id.size() > kMaxLabelLength || output_group.empty() ||
        output_group.size() > kMaxOutputGroupLength) {
        return false;
    }
    auto* session = find(identity);
    if (session == nullptr) {
        return false;
    }
    session->lane_id = std::move(lane_id);
    session->output_group = std::move(output_group);
    return true;
}

bool AudioSessionRegistry::set_gain_owner(const AudioSessionIdentityV1& identity,
                                          const SessionGainOwner owner) noexcept {
    auto* session = find(identity);
    if (session == nullptr) {
        return false;
    }
    session->gain_owner = owner;
    return true;
}

bool AudioSessionRegistry::set_makeup_gain_db(const AudioSessionIdentityV1& identity,
                                              const double makeup_gain_db) noexcept {
    if (!std::isfinite(makeup_gain_db) || makeup_gain_db < -144.0 || makeup_gain_db > 12.0) {
        return false;
    }
    auto* session = find(identity);
    if (session == nullptr) return false;
    session->makeup_gain_db = makeup_gain_db;
    return true;
}

void AudioSessionRegistry::mark_endpoint_sessions_inactive(
    const std::string& endpoint_id) noexcept {
    for (auto& session : sessions_) {
        if (session.identity.endpoint_id == endpoint_id) {
            session.active = false;
        }
    }
}

AudioSessionDescriptorV1* AudioSessionRegistry::find(
    const AudioSessionIdentityV1& identity) noexcept {
    const auto existing = std::find_if(
        sessions_.begin(), sessions_.end(), [&](const auto& item) {
            return same_identity(item.identity, identity);
        });
    return existing == sessions_.end() ? nullptr : &*existing;
}

const AudioSessionDescriptorV1* AudioSessionRegistry::find(
    const AudioSessionIdentityV1& identity) const noexcept {
    const auto existing = std::find_if(
        sessions_.begin(), sessions_.end(), [&](const auto& item) {
            return same_identity(item.identity, identity);
        });
    return existing == sessions_.end() ? nullptr : &*existing;
}

}  // namespace hibiki
