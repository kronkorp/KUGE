#include "Scene.hpp"
#include "Engine.hpp"

namespace
{
    // What the scheduler runs in place of a system. The scheduler is C code: an exception
    // that went through it would leave it (and the system) in a half-done state, so the
    // exception is caught here, kept by the scene, and thrown again once the scheduler is
    // out of the way (see Scene::fixedTick). The systems that follow in that pass are skipped.
    class GuardedSystem final : public kw::ISystem
    {
        public:
            GuardedSystem(std::unique_ptr<kw::ISystem> system, std::exception_ptr& error)
                : m_system(std::move(system)), m_error(error) {}

            bool handle(kw::World& world) override
            {
                if (m_error) {
                    return true;
                }
                try {
                    const bool again = m_system->handle(world);

                    if (m_system->isDone()) {
                        markAsDone();
                    }
                    return again;
                } catch (...) {
                    m_error = std::current_exception();
                    return true;
                }
            }

        private:
            std::unique_ptr<kw::ISystem>  m_system;
            std::exception_ptr&           m_error;
    };
}

kuge::Scene::Scene(void) : m_mailbox(std::make_shared<Mailbox>()), m_world(std::make_unique<kw::World>())
{
    m_world->addResource<Time>();
}

kuge::Scene::~Scene(void)
{
    m_mailbox->close();   // even if it never ran: nobody waits for it
}

kw::World& kuge::Scene::world(void) noexcept
{
    return *m_world;
}

kuge::SceneContext& kuge::Scene::ctx(void) noexcept
{
    return m_ctx;
}

kw::SystemHandle kuge::Scene::addSystem(
    kw::Schedule schedule,
    kw::StageId stage,
    std::unique_ptr<kw::ISystem> system,
    std::size_t delay,
    std::size_t interval
)
{
    auto handle = m_world->scheduleSystem(schedule, stage, std::make_unique<GuardedSystem>(std::move(system), m_error), delay, interval);

    m_systems.push_back(handle);
    return handle;
}

bool kuge::Scene::removeSystem(const kw::SystemHandle& handle)
{
    return m_world->removeSystem(handle);
}

void kuge::Scene::attach(const SceneContext& context, bool mainThread)
{
    m_ctx = context;
    m_ctx.engine().inject(*m_world, mainThread);
}

void kuge::Scene::deliverMessages(void)
{
    bool stopping = false;

    for (const Message& message : m_mailbox->drain()) {
        if (message.is<StopRequest>()) {
            // Once is enough: a second pop would end the scene under this one
            if (!stopping) {
                m_ctx.scenes().pop();
                stopping = true;
            }
        } else {
            onMessage(message);
        }
    }
}

void kuge::Scene::fixedTick(const Time& time)
{
    m_world->getResource<Time>() = time;
    m_world->runOnce(kw::Schedule::Fixed);
    rethrowError();
}

void kuge::Scene::frame(const Time& time)
{
    m_world->getResource<Time>() = time;
    m_world->runOnce(kw::Schedule::Frame);
    rethrowError();
}

void kuge::Scene::rethrowError(void)
{
    if (m_error) {
        std::exception_ptr error = std::move(m_error);

        m_error = nullptr;
        std::rethrow_exception(error);
    }
}

// Removing the systems while the resources are still there lets a system
// release what it holds before the World goes away
void kuge::Scene::shutdown(void)
{
    m_mailbox->close();
    for (auto it = m_systems.rbegin(); it != m_systems.rend(); ++it) {
        m_world->removeSystem(*it);
    }
    m_systems.clear();
}

kw::World& kuge::SceneSetup::world(void) noexcept
{
    return m_scene.world();
}

kuge::SceneContext& kuge::SceneSetup::ctx(void) noexcept
{
    return m_scene.ctx();
}

kw::SystemHandle kuge::SceneSetup::addSystem(
    kw::Schedule schedule,
    kw::StageId stage,
    std::unique_ptr<kw::ISystem> system,
    std::size_t delay,
    std::size_t interval
)
{
    return m_scene.addSystem(schedule, stage, std::move(system), delay, interval);
}
