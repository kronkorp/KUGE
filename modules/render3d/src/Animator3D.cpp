#include "Animator3D.hpp"
#include "Serializer.hpp"
#include "animation/Animation.hpp"
#include <algorithm>
#include <charconv>
#include <format>
#include <sstream>

kuge::ClipCues kuge::parseCues(std::string_view text)
{
    ClipCues cues;
    std::istringstream lines{std::string(text)};
    std::string line;
    std::size_t number = 0;

    while (std::getline(lines, line)) {
        ++number;
        line = line.substr(0, line.find('#'));
        std::istringstream words(line);
        std::string keyword, clip, name, seconds, extra;

        if (!(words >> keyword)) {
            continue;       // (empty, or a comment)
        }
        if (keyword != "cue" || !(words >> clip >> seconds >> name) || (words >> extra)) {
            throw AnimationError(std::format("line {}: expected 'cue <clip> <seconds> <name>'", number));
        }
        float time = 0.0f;
        const auto parsed = std::from_chars(seconds.data(), seconds.data() + seconds.size(), time);

        if (parsed.ec != std::errc() || parsed.ptr != seconds.data() + seconds.size() || time < 0.0f) {
            throw AnimationError(std::format("line {}: '{}' is not a time in seconds", number, seconds));
        }
        cues[clip].push_back({time, name});
    }
    for (auto& [clip, list] : cues) {
        std::stable_sort(list.begin(), list.end(), [](const Cue3D& a, const Cue3D& b) { return a.time < b.time; });
    }
    return cues;
}

kuge::ClipCues kuge::loadCues(const std::filesystem::path& file)
{
    try {
        const auto bytes = readFile(file);

        return parseCues(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    } catch (const SerializerError&) {
        throw AnimationError(std::format("cannot open '{}'", file.string()));
    } catch (const AnimationError& error) {
        throw AnimationError(std::format("'{}': {}", file.string(), error.what()));
    }
}

kuge::Animator3D kuge::Animator3D::of(std::shared_ptr<const k3::AnimatedModel> model, std::string_view first,
    const k3::PlayOptions& options, std::shared_ptr<const ClipCues> cues)
{
    Animator3D result{model, k3::Animator(model), std::move(cues)};

    if (!first.empty()) {
        result.play(first, options);
    }
    return result;
}

bool kuge::Animator3D::play(std::string_view clip, const k3::PlayOptions& playOptions, std::string_view then)
{
    // (k3::Animator does not start again a clip that plays: nor do the cues)
    const bool starts = animator.clip() != clip || animator.finished();

    if (!animator.play(clip, playOptions)) {
        return false;
    }
    options = playOptions;
    next = then;
    if (starts) {
        seen = -1;
    }
    return true;
}
