#include "Timeline.hpp"
#include <algorithm>
#include <format>

namespace
{
    using kuge::Json;

    const char* const KIND_NAMES[] = {"transform", "animation", "camera", "fade", "audio", "text", "event"};
    const char* const EASE_NAMES[] = {"linear", "inQuad", "outQuad", "inOutQuad"};

    // Where in the file we are, for the message
    struct Where
    {
        const std::string& file;
        std::string        place;

        [[noreturn]] void fail(const std::string& why) const
        {
            throw kuge::TimelineError(std::format("{}: {}{}{}", file, place, place.empty() ? "" : ": ", why));
        }

        const Json& member(const Json& object, const char* key, Json::Type type) const
        {
            const Json* value = object.find(key);

            if (!value) {
                fail(std::format("'{}' is missing", key));
            }
            if (value->type() != type) {
                fail(std::format("'{}' must be {} (it is {})", key, Json::name(type), Json::name(value->type())));
            }
            return *value;
        }

        // An optional member of this type
        const Json* maybe(const Json& object, const char* key, Json::Type type) const
        {
            return object.find(key) ? &member(object, key, type) : nullptr;
        }

        void numbers(const Json& array, const char* key, std::size_t count) const
        {
            if (array.size() != count) {
                fail(std::format("'{}' must be {} numbers", key, count));
            }
            for (const Json& item : array.items()) {
                if (!item.isNumber()) {
                    fail(std::format("'{}' must be {} numbers", key, count));
                }
            }
        }
    };

    void checkKey(const Where& at, kuge::TrackKind kind, const Json& key)
    {
        using Type = Json::Type;

        switch (kind) {
            case kuge::TrackKind::Transform:
                at.member(key, "target", Type::String);
                if (at.member(key, "to", Type::Object).size() == 0) {
                    at.fail("'to' has nothing to go to");
                }
                at.maybe(key, "component", Type::String);
                break;
            case kuge::TrackKind::Animation:
                at.member(key, "target", Type::String);
                at.member(key, "clip", Type::String);
                break;
            case kuge::TrackKind::Camera: {
                const Json* position = at.maybe(key, "position", Type::Array);
                const Json* zoom = at.maybe(key, "zoom", Type::Number);
                const Json* shake = at.maybe(key, "shake", Type::Object);

                if (!position && !zoom && !shake) {
                    at.fail("a camera key needs 'position', 'zoom' or 'shake'");
                }
                if (position) {
                    at.numbers(*position, "position", 2);
                }
                if (shake && at.member(*shake, "amplitude", Type::Number).number() <= 0.0) {
                    at.fail("'shake.amplitude' must be above 0");
                }
                at.maybe(key, "target", Type::String);
                break;
            }
            case kuge::TrackKind::Fade:
                at.numbers(at.member(key, "color", Type::Array), "color", 4);
                break;
            case kuge::TrackKind::Audio:
                if (!key.find("sound") && !key.find("music") && !key.find("stopMusic")) {
                    at.fail("an audio key needs 'sound', 'music' or 'stopMusic'");
                }
                at.maybe(key, "sound", Type::String);
                at.maybe(key, "music", Type::String);
                at.maybe(key, "stopMusic", Type::Number);
                break;
            case kuge::TrackKind::Text:
                at.member(key, "text", Type::String);
                at.maybe(key, "speaker", Type::String);
                break;
            case kuge::TrackKind::Event:
                at.member(key, "name", Type::String);
                break;
        }
    }
}

double kuge::ease(Ease curve, double x) noexcept
{
    x = std::clamp(x, 0.0, 1.0);
    switch (curve) {
        case Ease::InQuad:    return x * x;
        case Ease::OutQuad:   return 1.0 - (1.0 - x) * (1.0 - x);
        case Ease::InOutQuad: return x < 0.5 ? 2.0 * x * x : 1.0 - 2.0 * (1.0 - x) * (1.0 - x);
        default:              return x;
    }
}

const char* kuge::trackKindName(TrackKind kind) noexcept
{
    return KIND_NAMES[static_cast<std::size_t>(kind)];
}

kuge::Timeline kuge::Timeline::fromJson(const Json& json, const std::string& name)
{
    Where at{name, ""};
    Timeline timeline;

    if (!json.isObject()) {
        at.fail("a timeline is an object with 'duration' and 'tracks'");
    }
    timeline.duration = at.member(json, "duration", Json::Type::Number).number();
    if (timeline.duration <= 0.0) {
        at.fail("'duration' must be above 0");
    }
    const Json& tracks = at.member(json, "tracks", Json::Type::Array);

    for (std::size_t i = 0; i < tracks.size(); ++i) {
        const Json& track = tracks.at(i);

        at.place = std::format("tracks[{}]", i);
        if (!track.isObject()) {
            at.fail("a track is an object with 'kind' and 'keys'");
        }
        const std::string& kindName = at.member(track, "kind", Json::Type::String).string();
        const auto kind = std::find_if(std::begin(KIND_NAMES), std::end(KIND_NAMES), [&](const char* known) { return kindName == known; });

        if (kind == std::end(KIND_NAMES)) {
            at.fail(std::format("unknown kind '{}' (transform, animation, camera, fade, audio, text or event)", kindName));
        }
        TimelineTrack out;

        out.kind = static_cast<TrackKind>(kind - std::begin(KIND_NAMES));
        const Json& keys = at.member(track, "keys", Json::Type::Array);

        for (std::size_t j = 0; j < keys.size(); ++j) {
            const Json& key = keys.at(j);

            at.place = std::format("tracks[{}].keys[{}]", i, j);
            if (!key.isObject()) {
                at.fail("a key is an object with 't'");
            }
            TimelineKey made;

            made.t = at.member(key, "t", Json::Type::Number).number();
            if (made.t < 0.0) {
                at.fail("'t' cannot be negative");
            }
            if (const Json* length = at.maybe(key, "duration", Json::Type::Number)) {
                made.duration = length->number();
                if (made.duration < 0.0) {
                    at.fail("'duration' cannot be negative");
                }
            }
            if (made.t + made.duration > timeline.duration) {
                at.fail(std::format("it ends at {}, after the end of the timeline ({})", made.t + made.duration, timeline.duration));
            }
            if (const Json* curve = at.maybe(key, "ease", Json::Type::String)) {
                const auto found = std::find_if(std::begin(EASE_NAMES), std::end(EASE_NAMES), [&](const char* known) { return curve->string() == known; });

                if (found == std::end(EASE_NAMES)) {
                    at.fail(std::format("unknown ease '{}' (linear, inQuad, outQuad or inOutQuad)", curve->string()));
                }
                made.curve = static_cast<Ease>(found - std::begin(EASE_NAMES));
            }
            checkKey(at, out.kind, key);
            made.data = key;
            out.keys.push_back(std::move(made));
        }
        std::stable_sort(out.keys.begin(), out.keys.end(), [](const TimelineKey& a, const TimelineKey& b) { return a.t < b.t; });
        timeline.tracks.push_back(std::move(out));
    }
    return timeline;
}

kuge::Timeline kuge::Timeline::load(const std::filesystem::path& file)
{
    const std::string name = file.filename().string();
    Json json;

    try {
        json = Json::load(file);
    } catch (const JsonError& error) {
        throw TimelineError(std::format("{}: {}", name, error.what()));
    }
    Timeline timeline = fromJson(json, name);

    timeline.id = file.stem().string();
    return timeline;
}

kuge::TimelinePlayer::TimelinePlayer(std::shared_ptr<const Timeline> timeline, Callback callback)
    : m_timeline(std::move(timeline)), m_callback(std::move(callback)), m_next(m_timeline->tracks.size(), 0)
{
}

void kuge::TimelinePlayer::fire(Step::Phase phase, std::size_t track, std::size_t key, double progress)
{
    const TimelineTrack& t = m_timeline->tracks[track];
    const TimelineKey& k = t.keys[key];

    if (m_callback) {
        m_callback(Step{phase, track, key, t, k, progress, ease(k.curve, progress)});
    }
}

void kuge::TimelinePlayer::advanceTo(double target)
{
    struct Due { double t; std::size_t track; std::size_t key; };
    std::vector<Due> due;

    m_time = target;
    for (std::size_t track = 0; track < m_next.size(); ++track) {
        const auto& keys = m_timeline->tracks[track].keys;

        for (; m_next[track] < keys.size() && keys[m_next[track]].t <= target; ++m_next[track]) {
            due.push_back({keys[m_next[track]].t, track, m_next[track]});
        }
    }
    std::sort(due.begin(), due.end(), [](const Due& a, const Due& b) {
        return a.t != b.t ? a.t < b.t : a.track != b.track ? a.track < b.track : a.key < b.key;
    });
    for (const Due& one : due) {
        fire(Step::Phase::Start, one.track, one.key, 0.0);
        if (m_timeline->tracks[one.track].keys[one.key].duration > 0.0) {
            m_running.push_back({one.track, one.key});
        }
    }
    std::vector<Running> still;
    const std::vector<Running> running = std::move(m_running);

    m_running.clear();
    for (const Running& one : running) {
        const TimelineKey& key = m_timeline->tracks[one.track].keys[one.key];
        const double progress = (m_time - key.t) / key.duration;

        if (progress >= 1.0) {
            fire(Step::Phase::End, one.track, one.key, 1.0);
        } else {
            if (progress > 0.0) {
                fire(Step::Phase::Tween, one.track, one.key, progress);
            }
            still.push_back(one);
        }
    }
    m_running.insert(m_running.begin(), still.begin(), still.end());
    m_finished = m_time >= m_timeline->duration && m_running.empty();
}

void kuge::TimelinePlayer::update(double dt)
{
    if (!m_playing || m_finished) {
        return;
    }
    advanceTo(std::min(m_time + dt * m_speed, m_timeline->duration));
}

void kuge::TimelinePlayer::restart(void)
{
    m_time = 0.0;
    m_finished = false;
    m_playing = true;
    std::fill(m_next.begin(), m_next.end(), 0);
    m_running.clear();
}

void kuge::TimelinePlayer::seek(double seconds)
{
    seconds = std::clamp(seconds, 0.0, m_timeline->duration);
    if (seconds < m_time) {
        restart();
    }
    advanceTo(seconds);
}

void kuge::TimelinePlayer::skip(void)
{
    if (!m_finished) {
        advanceTo(m_timeline->duration);
    }
}
