#include "cinematic/CinematicScene.hpp"
#include "AssetManager.hpp"
#include "Ref.hpp"
#include "SceneManager.hpp"
#include "Stage.hpp"
#include "ClientReflect.hpp"
#include "backend/IRenderer2D.hpp"
#include "input/ActionState.hpp"
#include "input/InputMap.hpp"
#include "ui/TextRenderer.hpp"
#include <algorithm>

// Fixed, Simulation: the time of the cinematic is the fixed tick, so a recorded run plays the same
class kuge::CinematicScene::Step final : public kw::ISystem
{
    public:
        explicit Step(CinematicScene& scene) : m_scene(scene) {}

        bool handle(kw::World& world) override
        {
            if (m_scene.m_done) {
                return true;
            }
            const std::vector<Key> keys = world.getResource<Ref<InputMap>>()->editKeys();
            const int action = m_scene.m_config.skipAction;
            const bool skip = std::find(keys.begin(), keys.end(), Key::Escape) != keys.end()
                || (action >= 0 && world.getResource<ActionState>().wasPressed(action));
            Cinematic& cinematic = *m_scene.m_cinematic;

            if (skip) {
                cinematic.skip();
            } else {
                cinematic.update(world.getResource<Time>().dt);
            }
            if (cinematic.finished()) {
                m_scene.m_done = true;
                if (m_scene.m_config.onDone) {
                    m_scene.m_config.onDone(m_scene.ctx(), skip);
                }
            }
            return true;
        }

    private:
        CinematicScene& m_scene;
};

// Frame, Render (after the sprites): the fade over everything, then the subtitle over the fade
class kuge::CinematicScene::Overlay final : public kw::ISystem
{
    public:
        explicit Overlay(CinematicScene& scene) : m_scene(scene) {}

        bool handle(kw::World& world) override
        {
            IRenderer2D& renderer = *world.getResource<Ref<IRenderer2D>>();
            const Vec2 screen = renderer.outputSize();
            const Cinematic& cinematic = *m_scene.m_cinematic;

            renderer.setClip(std::nullopt);
            if (const Color fade = cinematic.fade(); fade.a > 0) {
                renderer.fillRect(Rect{0.0f, 0.0f, screen.x, screen.y}, fade);
            }
            if (cinematic.subtitle() && m_scene.m_config.font) {
                const float band = std::max(60.0f, screen.y * 0.14f);

                renderer.fillRect(Rect{0.0f, screen.y - band, screen.x, band}, Color{0, 0, 0, 170});
                const auto& line = *cinematic.subtitle();
                const std::string text = line.speaker.empty() ? line.text : line.speaker + ": " + line.text;   // (the speaker is written, not only coloured)

                world.getResource<Ref<TextRenderer>>()->draw(*m_scene.m_config.font, text, Vec2{24.0f, screen.y - band + 12.0f}, Color{255, 255, 255, 255},
                    static_cast<int>(screen.x - 48.0f));
            }
            return true;
        }

    private:
        CinematicScene& m_scene;
};

kuge::CinematicScene::CinematicScene(Config config) : m_config(std::move(config))
{
}

void kuge::CinematicScene::onEnter(void)
{
    installClientSystems();
    if (!m_config.registry) {
        auto registry = std::make_shared<ComponentRegistry>();

        describeCore(*registry);
        describeClient(*registry);
        m_config.registry = registry;
    }
    if (m_config.setup) {
        m_config.setup(*this);
    }
    kw::World& stage = world();
    Cinematic::Services services;

    services.world = &stage;
    services.registry = m_config.registry.get();
    services.camera = &stage.getResource<Camera2D>();
    services.audio = &*stage.getResource<Ref<Audio>>();
    services.find = [this](const std::string& name) -> std::optional<kw::Entity> {
        const auto found = m_names.find(name);

        return found == m_names.end() ? std::nullopt : std::optional<kw::Entity>(found->second);
    };
    services.sound = [&stage](const std::string& file) { return stage.getResource<Ref<AssetManager<Sound>>>()->load(file); };
    services.music = [&stage](const std::string& file) { return stage.getResource<Ref<AssetManager<Music>>>()->load(file); };
    services.event = m_config.onEvent;
    m_cinematic = std::make_unique<Cinematic>(m_config.timeline, std::move(services));
    addSystem(kw::Schedule::Fixed, stage::Simulation, std::make_unique<Step>(*this));
    addSystem(kw::Schedule::Frame, stage::Render, std::make_unique<Overlay>(*this));
}
