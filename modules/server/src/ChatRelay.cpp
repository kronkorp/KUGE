#include "ChatRelay.hpp"
#include "Utf8.hpp"
#include <algorithm>

bool kuge::server::ChatRelay::clean(std::string_view text, std::size_t maxBytes)
{
    const std::string_view line = trimSpaces(text);

    if (line.empty() || text.size() > maxBytes || !utf8Valid(text)) {
        return false;
    }
    for (std::size_t i = 0; i < line.size(); ++i) {
        const auto byte = static_cast<unsigned char>(line[i]);

        // C0 controls and DEL, and the C1 ones (U+0080 to U+009F, which are 0xC2 0x80 to 0xC2 0x9F)
        if (byte < 0x20 || byte == 0x7F || (byte == 0xC2 && i + 1 < line.size() && static_cast<unsigned char>(line[i + 1]) < 0xA0)) {
            return false;
        }
    }
    return true;
}

std::optional<std::string> kuge::server::ChatRelay::accept(net::ConnectionId from, std::string_view text, double now)
{
    if (!clean(text, m_config.maxBytes)) {
        return std::nullopt;
    }
    const double burst = static_cast<double>(std::max(m_config.burst, 1));
    auto [it, fresh] = m_buckets.try_emplace(from, Bucket{burst, now});
    Bucket& bucket = it->second;

    if (!fresh && m_config.refillSeconds > 0.0) {
        bucket.tokens = std::min(burst, bucket.tokens + (now - bucket.at) / m_config.refillSeconds);
    }
    bucket.at = now;
    if (bucket.tokens < 1.0) {
        return std::nullopt;
    }
    bucket.tokens -= 1.0;
    return std::string(trimSpaces(text));
}
