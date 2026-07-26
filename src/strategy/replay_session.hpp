#pragma once

#include "domain/bar.hpp"

#include <QString>

#include <cstddef>
#include <cstdint>

namespace tvchart {

class ReplaySession final {
public:
    [[nodiscard]] QString reset(
        Bars bars,
        std::size_t initiallyVisibleBars);
    [[nodiscard]] bool step(std::size_t count = 1);
    [[nodiscard]] Bars visibleBars() const;
    [[nodiscard]] const Bars& sourceBars() const noexcept;
    [[nodiscard]] std::size_t visibleCount() const noexcept;
    [[nodiscard]] std::size_t totalCount() const noexcept;
    [[nodiscard]] double progressPercent() const noexcept;
    [[nodiscard]] bool finished() const noexcept;
    [[nodiscard]] std::int64_t currentTimestamp() const noexcept;
    void clear();

private:
    Bars bars_;
    std::size_t visibleCount_{};
};

} // namespace tvchart
