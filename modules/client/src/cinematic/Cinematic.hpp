#pragma once

#include "Random.hpp"
#include "Reflect.hpp"
#include "Timeline.hpp"
#include "animation/Animation.hpp"
#include "audio/Audio.hpp"
#include "render/Color.hpp"
#include "render/Components.hpp"
#include <array>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace kuge
{

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  Plays a Timeline on a World: what each kind of track does (see Timeline for the file)
     *
     * - transform: the fields of `to` of a component (Transform2D by default) of the entity `find(target)` go from what they are when the key
     *   starts to their value, through the registry, so any described component can be tweened (a Sprite's frame, a Body's velocity...). A
     *   number, or an array of numbers, goes through the curve; a text, a bool or an enum jumps to its value at the end.
     * - animation: `Animator::play(clip)` of the entity.
     * - camera: position and zoom of the Camera2D go to the value; `shake` moves it randomly, less and less, for the `duration` of the key.
     * - fade: the colour of the screen (fade()) goes to `color`; the scene draws it over everything.
     * - audio: a sound or a music of a file (`sound`/`music` loaders), a stop of the music. `bus: "dialogue"` plays on the Dialogue bus.
     * - text: the subtitle (subtitle()) is set for the `duration` of the key (for ever with none, until another text key).
     * - event: `Services::event(name, key)`: the game does what it wants (open a door, spawn a boss).
     *
     * Time comes from update(dt) only. skip() starts every key that was left and ends every tween, so the world is as if it had played
     * to the end; the sounds of the keys that were skipped are not played (a burst of every sound at once is not what anyone wants).
     * A key that cannot work (an entity or a clip that is not there, a field that is not described) is logged with the name of the
     * timeline and ignored: the cinematic goes on.
     */
    ////////////////////////////////////////////////////////////////////////////
    class Cinematic
    {
        public:
            struct Subtitle
            {
                std::string speaker;
                std::string text;
            };

            struct Services
            {
                kw::World*                world = nullptr;     //!< Required
                const ComponentRegistry*  registry = nullptr;  //!< Required for transform keys
                Camera2D*                 camera = nullptr;    //!< None: camera keys do nothing
                Audio*                    audio = nullptr;     //!< None: audio keys do nothing
                //! The entity that a scene calls by that name, which must exist (none: transform and animation keys on it do nothing)
                std::function<std::optional<kw::Entity>(const std::string&)>        find;
                std::function<std::shared_ptr<Sound>(const std::string& file)>      sound;
                std::function<std::shared_ptr<Music>(const std::string& file)>      music;
                std::function<void(const std::string& name, const Json& key)>       event;
            };

            Cinematic(std::shared_ptr<const Timeline> timeline, Services services);

            void update(double dt);
            //! See the class. Does nothing when it is finished.
            void skip(void);

            TimelinePlayer&       player(void) noexcept { return m_player; }
            bool                  finished(void) const noexcept { return m_player.finished(); }
            const std::optional<Subtitle>& subtitle(void) const noexcept { return m_subtitle; }
            //! The colour over the screen: transparent black until a fade key says otherwise
            Color                 fade(void) const noexcept;

        private:
            struct Start
            {
                std::optional<kw::Entity>  entity;
                Json                       from;                  //!< transform: the fields of `to`, as they were
                Vec2                       position{};            //!< camera
                float                      zoom = 1.0f;
                std::array<float, 4>       color{};               //!< fade
                bool                       failed = false;
            };
            using Id = std::pair<std::size_t, std::size_t>;

            void step(const TimelinePlayer::Step& step);
            void begin(const TimelinePlayer::Step& step, Start& start);
            void apply(const TimelinePlayer::Step& step, Start& start);
            void transform(const TimelineKey& key, Start& start, double progress);
            void removeShake(void);
            void warn(const std::string& why) const;

            std::shared_ptr<const Timeline>  m_timeline;
            Services                         m_services;
            TimelinePlayer                   m_player;
            std::map<Id, Start>              m_started;
            std::optional<Subtitle>          m_subtitle;
            std::optional<Id>                m_subtitleKey;     //!< Which key set it, to clear it when that key ends
            std::array<float, 4>             m_fade{0.0f, 0.0f, 0.0f, 0.0f};
            Vec2                             m_shake{};         //!< What the shake added to the camera at the last update
            Vec2                             m_nextShake{};
            Vec2                             m_unshaken{};      //!< The camera before the shake was added, and as it was after (to put it back exactly)
            Vec2                             m_shaken{};
            bool                             m_skipping = false;
            std::vector<std::shared_ptr<Sound>> m_sounds;       //!< They must outlive the voices that play them
            std::vector<std::shared_ptr<Music>> m_musics;
            Random                           m_random{1};
    };

}
