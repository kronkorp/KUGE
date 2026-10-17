#include "Animator3D.hpp"

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
