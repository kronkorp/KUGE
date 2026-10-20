#include "cinematic/Cinematic.hpp"
#include "Logger.hpp"
#include "Random.hpp"
#include <algorithm>
#include <cmath>
#include <format>

namespace
{
    using kuge::Json;

    float mix(float a, float b, double p) { return a + (b - a) * static_cast<float>(p); }

    // Numbers, and arrays of numbers, go through the curve; the rest jumps to its value when the tween ends
    Json lerpJson(const Json& from, const Json& to, double p)
    {
        if (p >= 1.0) {
            return to;
        }
        if (from.isNumber() && to.isNumber()) {
            return Json(from.number() + (to.number() - from.number()) * p);
        }
        if (from.isArray() && to.isArray() && from.size() == to.size()) {
            Json out = Json::array();

            for (std::size_t i = 0; i < to.size(); ++i) {
                out.push(lerpJson(from.at(i), to.at(i), p));
            }
            return out;
        }
        return from;
    }

    std::array<float, 4> colorOf(const Json& array)
    {
        return {static_cast<float>(array.at(0).number()), static_cast<float>(array.at(1).number()), static_cast<float>(array.at(2).number()),
            static_cast<float>(array.at(3).number())};
    }
}

kuge::Cinematic::Cinematic(std::shared_ptr<const Timeline> timeline, Services services)
    : m_timeline(timeline), m_services(std::move(services)), m_player(timeline, [this](const TimelinePlayer::Step& s) { step(s); })
{
}

void kuge::Cinematic::warn(const std::string& why) const
{
    Logger::logger().category("client").warn("cinematic '{}': {}", m_timeline->id, why);
}

kuge::Color kuge::Cinematic::fade(void) const noexcept
{
    const auto byte = [](float v) { return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };

    return {byte(m_fade[0]), byte(m_fade[1]), byte(m_fade[2]), byte(m_fade[3])};
}

// The shake of the last update is taken off first: the keys work on the camera as it was
void kuge::Cinematic::removeShake(void)
{
    if (m_services.camera) {   // (put back as it was, not subtracted: floats would drift a little each frame)
        const bool untouched = m_services.camera->position == m_shaken;

        m_services.camera->position = untouched ? m_unshaken
            : Vec2{m_services.camera->position.x - m_shake.x, m_services.camera->position.y - m_shake.y};
    }
    m_shake = {};
    m_nextShake = {};
}

void kuge::Cinematic::update(double dt)
{
    removeShake();
    m_player.update(dt);
    m_shake = m_nextShake;
    if (m_services.camera) {
        m_unshaken = m_services.camera->position;
        m_services.camera->position = {m_unshaken.x + m_shake.x, m_unshaken.y + m_shake.y};
        m_shaken = m_services.camera->position;
    }
}

void kuge::Cinematic::skip(void)
{
    if (m_player.finished()) {
        return;
    }
    removeShake();
    m_skipping = true;
    m_player.skip();
    m_skipping = false;
}

void kuge::Cinematic::step(const TimelinePlayer::Step& s)
{
    const Id id{s.trackIndex, s.keyIndex};

    if (s.phase == TimelinePlayer::Step::Phase::Start) {
        begin(s, m_started[id]);
    }
    Start& start = m_started[id];

    if (!start.failed) {
        apply(s, start);
    }
    if (s.phase == TimelinePlayer::Step::Phase::End) {
        if (m_subtitleKey == id) {
            m_subtitle.reset();
            m_subtitleKey.reset();
        }
        m_started.erase(id);
    }
}

// What a key needs to know when it starts: which entity, and where the values were
void kuge::Cinematic::begin(const TimelinePlayer::Step& s, Start& start)
{
    const Json& data = s.key.data;

    switch (s.track.kind) {
        case TrackKind::Transform:
        case TrackKind::Animation: {
            const std::string& target = data["target"].string();

            start.entity = m_services.find ? m_services.find(target) : std::nullopt;
            if (!start.entity || !m_services.world) {
                warn(std::format("'{}' is not an entity of this scene", target));
                start.failed = true;
                return;
            }
            if (s.track.kind == TrackKind::Animation) {
                return;
            }
            const std::string component = data.find("component") ? data["component"].string() : "Transform2D";

            if (!m_services.registry || !m_services.registry->byName(component)) {
                warn(std::format("no component '{}' is described", component));
                start.failed = true;
                return;
            }
            const Json now = m_services.registry->toJson(*m_services.world, *start.entity, component);

            start.from = Json::object();
            for (const auto& [field, value] : data["to"].members()) {
                if (const Json* was = now.find(field)) {
                    start.from.set(field, *was);
                } else {   // reported by the registry when it is set
                    start.from.set(field, value);
                }
            }
            break;
        }
        case TrackKind::Camera:
            if (m_services.camera) {
                start.position = m_services.camera->position;
                start.zoom = m_services.camera->zoom;
            }
            break;
        case TrackKind::Fade:
            start.color = m_fade;
            break;
        default:
            break;
    }
}

void kuge::Cinematic::apply(const TimelinePlayer::Step& s, Start& start)
{
    const Json& data = s.key.data;
    const bool starting = s.phase == TimelinePlayer::Step::Phase::Start;
    // An instant key has only its Start: it is at its value at once
    const double p = s.key.duration > 0.0 ? s.eased : 1.0;

    switch (s.track.kind) {
        case TrackKind::Transform:
            transform(s.key, start, p);
            break;
        case TrackKind::Animation:
            if (starting) {
                kw::World& world = *m_services.world;

                if (!world.has<Animator>(*start.entity) || !world.get<Animator>(*start.entity).play(data["clip"].string(), true)) {
                    warn(std::format("'{}' has no animation '{}'", data["target"].string(), data["clip"].string()));
                }
            }
            break;
        case TrackKind::Camera:
            if (!m_services.camera) {
                break;
            }
            if (const Json* position = data.find("position")) {
                const Vec2 to{static_cast<float>(position->at(0).number()), static_cast<float>(position->at(1).number())};

                m_services.camera->position = {mix(start.position.x, to.x, p), mix(start.position.y, to.y, p)};
            }
            if (const Json* zoom = data.find("zoom")) {
                m_services.camera->zoom = mix(start.zoom, static_cast<float>(zoom->number()), p);
            }
            if (const Json* shake = data.find("shake"); shake && s.phase == TimelinePlayer::Step::Phase::Tween) {
                const auto strength = static_cast<float>((*shake)["amplitude"].number() * (1.0 - s.progress));

                m_nextShake = {m_nextShake.x + m_random.range(-1.0f, 1.0f) * strength, m_nextShake.y + m_random.range(-1.0f, 1.0f) * strength};
            }
            break;
        case TrackKind::Fade: {
            const auto to = colorOf(data["color"]);

            for (std::size_t i = 0; i < 4; ++i) {
                m_fade[i] = mix(start.color[i], to[i], p);
            }
            break;
        }
        case TrackKind::Audio:
            if (!starting) {
                break;
            }
            if (!m_services.audio) {
                warn("an audio key was ignored: this scene has no audio");
            } else if (const Json* file = data.find("sound"); file && !m_skipping) {
                PlayOptions options;

                options.volume = data.find("volume") ? static_cast<float>(data["volume"].number()) : 1.0f;
                options.bus = data.find("bus") && data["bus"].isString() && data["bus"].string() == "dialogue" ? Bus::Dialogue : Bus::Effects;
                if (auto sound = m_services.sound ? m_services.sound(file->string()) : nullptr) {
                    m_sounds.push_back(sound);
                    m_services.audio->play(*sound, options);
                } else {
                    warn(std::format("sound '{}' could not be loaded", file->string()));
                }
            } else if (const Json* music = data.find("music")) {
                MusicOptions options;

                options.volume = data.find("volume") ? static_cast<float>(data["volume"].number()) : 1.0f;
                options.loop = data.find("loop") ? data["loop"].boolean() : true;
                options.fadeIn = data.find("fadeIn") ? static_cast<float>(data["fadeIn"].number()) : 0.0f;
                if (auto loaded = m_services.music ? m_services.music(music->string()) : nullptr) {
                    m_musics.push_back(loaded);
                    m_services.audio->playMusic(*loaded, options);
                } else {
                    warn(std::format("music '{}' could not be loaded", music->string()));
                }
            } else if (const Json* fadeOut = data.find("stopMusic")) {
                m_services.audio->stopMusic(static_cast<float>(fadeOut->number()));
            }
            break;
        case TrackKind::Text:
            if (starting) {
                if (data["text"].string().empty()) {
                    m_subtitle.reset();
                    m_subtitleKey.reset();
                } else {
                    m_subtitle = Subtitle{data.find("speaker") ? data["speaker"].string() : std::string(), data["text"].string()};
                    m_subtitleKey = s.key.duration > 0.0 ? std::optional<Id>(Id{s.trackIndex, s.keyIndex}) : std::nullopt;
                }
            }
            break;
        case TrackKind::Event:
            if (starting && m_services.event) {
                m_services.event(data["name"].string(), data);
            }
            break;
    }
}

void kuge::Cinematic::transform(const TimelineKey& key, Start& start, double p)
{
    const std::string component = key.data.find("component") ? key.data["component"].string() : "Transform2D";
    const ComponentRegistry::Component* described = m_services.registry->byName(component);
    Json patch = Json::object();

    for (const auto& [field, to] : key.data["to"].members()) {
        Json value = lerpJson(start.from[field], to, p);
        const auto info = std::find_if(described->fields.begin(), described->fields.end(), [&](const FieldInfo& f) { return f.name == field; });

        if (info != described->fields.end() && info->type == FieldType::Int && value.isNumber()) {
            value = Json(std::round(value.number()));   // (half way between 0 and 3 frames is the frame 2, not 1.5)
        }
        patch.set(field, std::move(value));
    }
    const auto result = m_services.registry->fromJson(*m_services.world, *start.entity, component, patch);

    if (!result.ok) {
        warn(result.message);
        start.failed = true;   // (once: the same key would say it every frame)
    }
}
