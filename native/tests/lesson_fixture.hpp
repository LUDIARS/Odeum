#pragma once
#include "check.hpp"
#include "catalog.hpp"
#include "model.hpp"

// The built-in sample course ("アクションゲームの作り方") is the fixture for lesson tests:
// it exercises every plot feature and is what the service ships with.
inline odeum::lesson::Plot sample_plot() {
    for (auto& plot : odeum::lesson::builtin_plots()) if (plot.id == "action-game") return plot;
    throw std::runtime_error("Sample course missing");
}
inline std::size_t count_type(const odeum::lesson::Outbox& out, std::string_view type) {
    std::size_t n = 0;
    for (const auto& message : out) n += message.body.value("type", std::string()) == type;
    return n;
}
inline const odeum::lesson::Outbound* find_type(const odeum::lesson::Outbox& out, std::string_view type, std::string_view student = {}) {
    for (const auto& message : out)
        if (message.body.value("type", std::string()) == type && (student.empty() || message.student == student)) return &message;
    return nullptr;
}
