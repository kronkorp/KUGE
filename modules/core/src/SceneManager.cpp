#include "SceneManager.hpp"
#include "Logger.hpp"
#include <exception>

kuge::SceneManager::SceneManager(Engine& engine, const Time& time, SceneHandle parent, bool mainThread) noexcept
    : m_engine(engine), m_time(time), m_parent(std::move(parent)), m_mainThread(mainThread)
{
}

kuge::SceneManager::~SceneManager(void)
{
    try {
        clear();
    } catch (const std::exception& e) {
        Logger::logger().error("A scene threw while being left: {}", e.what());
    } catch (...) {
        Logger::logger().error("A scene threw while being left");
    }
}

void kuge::SceneManager::changeTo(std::unique_ptr<Scene> scene)
{
    enqueue(Kind::Change, std::make_unique<ReadyFactory>(std::move(scene)));
}

void kuge::SceneManager::pop(void)
{
    enqueue(Kind::Pop, nullptr);
}

void kuge::SceneManager::enqueue(Kind kind, std::unique_ptr<Factory> factory)
{
    m_pending.push_back(Op{kind, std::move(factory)});
}

void kuge::SceneManager::apply(void)
{
    // NOTE: A scene entered here can queue transitions of its own: they are
    // done in this same call, after the ones already waiting.
    while (!m_pending.empty()) {
        Op op = std::move(m_pending.front());

        m_pending.pop_front();
        switch (op.kind) {
            case Kind::Change:
                while (!m_stack.empty()) {
                    leaveTop();
                }
                enter(op.factory->make());
                break;
            case Kind::Push:
                if (!m_stack.empty()) {
                    m_stack.back()->onPause();
                }
                enter(op.factory->make());
                break;
            case Kind::Pop:
                if (m_stack.empty()) {
                    Logger::logger().warn("SceneManager::pop() with no scene to pop");
                    break;
                }
                leaveTop();
                if (!m_stack.empty()) {
                    m_stack.back()->onResume();
                }
                break;
        }
    }
}

void kuge::SceneManager::clear(void)
{
    m_pending.clear();
    while (!m_stack.empty()) {
        leaveTop();
    }
}

kuge::Scene* kuge::SceneManager::top(void) noexcept
{
    return m_stack.empty() ? nullptr : m_stack.back().get();
}

std::size_t kuge::SceneManager::size(void) const noexcept
{
    return m_stack.size();
}

bool kuge::SceneManager::empty(void) const noexcept
{
    return m_stack.empty();
}

// The scene is on the stack before onEnter(), so that top() is the right one
// from inside it
void kuge::SceneManager::enter(std::unique_ptr<Scene> scene)
{
    // The parent is the one that spawned the loop: it is the root scene's parent
    scene->attach(SceneContext(m_engine, *this, m_time, scene->handle(), m_stack.empty() ? m_parent : SceneHandle()), m_mainThread);
    m_stack.push_back(std::move(scene));
    m_stack.back()->onEnter();
}

// Off the stack before onExit(): whatever happens, the scene is not left twice
void kuge::SceneManager::leaveTop(void)
{
    std::unique_ptr<Scene> scene = std::move(m_stack.back());

    m_stack.pop_back();
    scene->onExit();
    scene->shutdown();
}
