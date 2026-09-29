#include "animation/Animation.hpp"
#include "animation/AnimateSprites.hpp"
#include "Time.hpp"
#include "render/Components.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <fstream>
#include <map>
#include <numeric>
#include <sstream>

namespace
{
    std::vector<std::string_view> split(std::string_view text, std::string_view separators)
    {
        std::vector<std::string_view> parts;

        while (true) {
            const auto begin = text.find_first_not_of(separators);

            if (begin == std::string_view::npos) {
                return parts;
            }
            text.remove_prefix(begin);
            const auto end = text.find_first_of(separators);

            parts.push_back(text.substr(0, end));
            if (end == std::string_view::npos) {
                return parts;
            }
            text.remove_prefix(end);
        }
    }

    template<typename T>
    bool toNumber(std::string_view word, T& value)
    {
        const auto result = std::from_chars(word.data(), word.data() + word.size(), value);

        return result.ec == std::errc() && result.ptr == word.data() + word.size();
    }
}

int kuge::AnimationSet::find(std::string_view name) const noexcept
{
    for (std::size_t i = 0; i < clips.size(); ++i) {
        if (clips[i].name == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

kuge::AnimationSet kuge::AnimationSet::parse(std::string_view text)
{
    AnimationSet set;
    std::vector<std::size_t> lines;   // the line of each clip, to point at it
    std::size_t lineNumber = 0;

    auto fail = [&lineNumber](const std::string& what) {
        throw AnimationError(std::format("line {}: {}", lineNumber, what));
    };

    while (!text.empty()) {
        const auto newline = text.find('\n');
        std::string_view line = text.substr(0, newline);

        text = newline == std::string_view::npos ? std::string_view{} : text.substr(newline + 1);
        ++lineNumber;
        if (const auto comment = line.find('#'); comment != std::string_view::npos) {
            line = line.substr(0, comment);
        }
        const auto words = split(line, " \t\r");

        if (words.empty()) {
            continue;
        }
        if (words[0] != "clip" || words.size() < 3) {
            fail("expected 'clip <name> frames=... [fps=...]'");
        }
        AnimationClip clip;
        std::map<int, std::string> cues;
        bool hasFrames = false;

        clip.name = std::string(words[1]);
        if (set.find(clip.name) >= 0) {
            fail(std::format("there is already a clip '{}'", clip.name));
        }
        for (std::size_t i = 2; i < words.size(); ++i) {
            const auto equal = words[i].find('=');

            if (equal == std::string_view::npos || equal == 0) {
                fail(std::format("'{}' is not key=value", words[i]));
            }
            const std::string_view key = words[i].substr(0, equal);
            const std::string_view value = words[i].substr(equal + 1);

            if (key == "fps") {
                if (!toNumber(value, clip.fps) || !(clip.fps > 0.0f)) {
                    fail(std::format("fps wants a number above 0, not '{}'", value));
                }
            } else if (key == "loop") {
                if (value != "true" && value != "false") {
                    fail(std::format("loop is true or false, not '{}'", value));
                }
                clip.loop = value == "true";
            } else if (key == "next") {
                clip.next = std::string(value);
            } else if (key == "frames") {
                for (std::string_view item : split(value, ",")) {
                    const auto dash = item.find('-');
                    int first = 0;
                    int last = 0;

                    if (dash == std::string_view::npos) {
                        if (!toNumber(item, first) || first < 0) {
                            fail(std::format("'{}' is not a frame number", item));
                        }
                        last = first;
                    } else if (!toNumber(item.substr(0, dash), first) || !toNumber(item.substr(dash + 1), last)
                        || first < 0 || last < first) {
                        fail(std::format("'{}' is not a range of frames (a-b, with a <= b)", item));
                    }
                    if (clip.frames.size() + static_cast<std::size_t>(last - first + 1) > 100000) {
                        fail("too many frames");
                    }
                    for (int frame = first; frame <= last; ++frame) {
                        clip.frames.push_back(frame);
                    }
                }
                hasFrames = !clip.frames.empty();
            } else if (key.starts_with("cue.")) {
                int frame = 0;

                if (!toNumber(key.substr(4), frame) || frame < 0 || value.empty()) {
                    fail(std::format("'{}': a cue is cue.<frame>=<name>", words[i]));
                }
                cues[frame] = std::string(value);
            } else {
                fail(std::format("unknown '{}'", key));
            }
        }
        if (!hasFrames) {
            fail(std::format("the clip '{}' has no frames", clip.name));
        }
        for (const auto& [frame, name] : cues) {
            if (frame >= static_cast<int>(clip.frames.size())) {
                fail(std::format("cue.{} is out of the clip ({} frames)", frame, clip.frames.size()));
            }
            clip.cues.push_back({frame, name});
        }
        set.clips.push_back(std::move(clip));
        lines.push_back(lineNumber);
    }
    if (set.clips.empty()) {
        throw AnimationError("no clips");
    }
    // A clip can only go to one that exists: checked at the end, since it may come later
    for (std::size_t i = 0; i < set.clips.size(); ++i) {
        if (!set.clips[i].next.empty() && set.find(set.clips[i].next) < 0) {
            throw AnimationError(std::format("line {}: the clip '{}' goes to '{}', which does not exist",
                lines[i], set.clips[i].name, set.clips[i].next));
        }
    }
    return set;
}

kuge::AnimationSet kuge::AnimationSet::load(const std::filesystem::path& path)
{
    std::ifstream in(path);
    std::stringstream content;

    if (!in) {
        throw AnimationError(std::format("cannot open '{}'", path.string()));
    }
    content << in.rdbuf();
    try {
        return parse(content.str());
    } catch (const AnimationError& error) {
        throw AnimationError(std::format("'{}': {}", path.string(), error.what()));
    }
}

kuge::Animator kuge::Animator::of(
    std::shared_ptr<const Spritesheet> sheet, std::shared_ptr<const AnimationSet> clips, std::string_view first)
{
    Animator animator;

    animator.sheet = std::move(sheet);
    animator.clips = std::move(clips);
    if (!first.empty() && !animator.play(first, true)) {
        throw AnimationError(std::format("there is no clip '{}'", first));
    }
    return animator;
}

kuge::Animator kuge::Animator::loop(std::shared_ptr<const Spritesheet> sheet, float fps)
{
    const int count = sheet ? sheet->frameCount() : 0;
    auto set = std::make_shared<AnimationSet>();
    AnimationClip clip;

    if (count <= 0) {
        throw AnimationError("the sheet has no cells (no picture yet?)");
    }
    if (!(fps > 0.0f)) {
        throw AnimationError(std::format("fps wants a number above 0, not {}", fps));
    }
    clip.name = "loop";
    clip.fps = fps;
    clip.frames.resize(static_cast<std::size_t>(count));
    std::iota(clip.frames.begin(), clip.frames.end(), 0);
    set->clips.push_back(std::move(clip));
    return of(std::move(sheet), std::move(set), "loop");
}

bool kuge::Animator::play(int index, bool restart)
{
    if (!clips || index < 0 || index >= static_cast<int>(clips->clips.size())) {
        return false;
    }
    if (index == clip && !restart && !finished) {
        return true;
    }
    clip = index;
    time = 0.0f;
    seen = -1;
    finished = false;
    playing = true;
    return true;
}

bool kuge::Animator::play(std::string_view name, bool restart)
{
    return clips && play(clips->find(name), restart);
}

std::string_view kuge::Animator::current(void) const noexcept
{
    if (!clips || clip < 0 || clip >= static_cast<int>(clips->clips.size())) {
        return {};
    }
    return clips->clips[clip].name;
}

namespace
{
    void advance(kw::Entity entity, kuge::Animator& animator, kuge::Sprite& sprite, float dt,
        std::vector<kuge::AnimationEvent>& events)
    {
        using namespace kuge;

        if (!animator.sheet || !animator.clips || animator.clip < 0
            || animator.clip >= static_cast<int>(animator.clips->clips.size())) {
            return;
        }
        if (animator.playing && !animator.finished && animator.speed > 0.0f) {
            animator.time += dt * animator.speed;
        }
        const AnimationClip* clip = &animator.clips->clips[animator.clip];
        long absolute = 0;

        // A clip can end and hand over to another in the same tick
        for (int handovers = 0; handovers < 4 && !clip->frames.empty(); ++handovers) {
            const long count = static_cast<long>(clip->frames.size());

            absolute = static_cast<long>(std::floor(animator.time * clip->fps));
            const long last = clip->loop ? absolute : std::min(absolute, count - 1);
            long from = animator.seen + 1;

            if (last - from > 100000) {
                from = last - 100000;   // a huge jump: do not spin for nothing
            }
            for (long counted = from; counted <= last; ++counted) {
                for (const AnimationCue& cue : clip->cues) {
                    if (cue.frame == static_cast<int>(counted % count)) {
                        events.push_back({entity, cue.name});
                    }
                }
            }
            animator.seen = std::max(animator.seen, last);
            if (clip->loop || absolute < count) {
                break;
            }
            animator.finished = true;
            const int next = clip->next.empty() ? -1 : animator.clips->find(clip->next);

            if (next < 0) {
                break;
            }
            animator.play(next, true);
            clip = &animator.clips->clips[animator.clip];
        }
        if (clip->frames.empty()) {
            return;
        }
        const long count = static_cast<long>(clip->frames.size());
        absolute = static_cast<long>(std::floor(animator.time * clip->fps));
        const long index = clip->loop ? absolute % count : std::min(absolute, count - 1);

        sprite.sheet = animator.sheet;
        sprite.frame = clip->frames[static_cast<std::size_t>(index)];
    }
}

bool kuge::AnimateSprites::handle(kw::World& world)
{
    auto& events = world.getResource<AnimationEvents>().list;
    const float dt = static_cast<float>(world.getResource<Time>().dt);

    events.clear();
    m_entities.clear();
    auto view = world.view<Animator, Sprite>();
    for (kw::Entity entity : view) {
        m_entities.push_back(entity);
    }
    std::sort(m_entities.begin(), m_entities.end());
    for (kw::Entity entity : m_entities) {
        advance(entity, world.get<Animator>(entity), world.get<Sprite>(entity), dt, events);
    }
    return true;
}
