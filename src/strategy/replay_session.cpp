#include "strategy/replay_session.hpp"

#include <algorithm>
#include <iterator>
#include <utility>

namespace tvchart {

QString ReplaySession::reset(
    Bars bars,
    const std::size_t initiallyVisibleBars) {
    if (const auto error = validateBars(bars)) {
        return QStringLiteral("Replay bars are invalid: %1")
            .arg(QString::fromStdString(*error));
    }
    if (initiallyVisibleBars == 0 || initiallyVisibleBars > bars.size()) {
        return QStringLiteral(
            "Replay start must reveal between one bar and the full series.");
    }
    bars_ = std::move(bars);
    visibleCount_ = initiallyVisibleBars;
    return {};
}

bool ReplaySession::step(const std::size_t count) {
    if (count == 0 || finished()) {
        return false;
    }
    visibleCount_ = std::min(bars_.size(), visibleCount_ + count);
    return true;
}

Bars ReplaySession::visibleBars() const {
    return {
        bars_.begin(),
        std::next(
            bars_.begin(),
            static_cast<std::ptrdiff_t>(visibleCount_)),
    };
}

const Bars& ReplaySession::sourceBars() const noexcept {
    return bars_;
}

std::size_t ReplaySession::visibleCount() const noexcept {
    return visibleCount_;
}

std::size_t ReplaySession::totalCount() const noexcept {
    return bars_.size();
}

double ReplaySession::progressPercent() const noexcept {
    return bars_.empty()
               ? 0.0
               : static_cast<double>(visibleCount_) /
                     static_cast<double>(bars_.size()) * 100.0;
}

bool ReplaySession::finished() const noexcept {
    return bars_.empty() || visibleCount_ >= bars_.size();
}

std::int64_t ReplaySession::currentTimestamp() const noexcept {
    return visibleCount_ == 0 || bars_.empty()
               ? 0
               : bars_[visibleCount_ - 1].timestamp;
}

void ReplaySession::clear() {
    bars_.clear();
    visibleCount_ = 0;
}

} // namespace tvchart
