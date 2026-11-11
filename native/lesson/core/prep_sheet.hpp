#pragma once
#include "timeline.hpp"

namespace odeum::lesson {
// The sheet students read before class: goals, what to prepare, vocabulary, one thinking
// question per section, and the lesson schedule. Built from the plot alone, so the sheet and
// the lesson cannot drift apart. It never contains activity answers.
struct SheetSlide {
    enum class Kind { cover, bullets, terms, question, schedule };
    Kind kind = Kind::bullets;
    std::string title, lead;                                // lead: subtitle (cover), tag (question)
    std::vector<std::string> lines;                         // bullets, cover facts, or [question, hint]
    std::vector<std::pair<std::string, std::string>> rows;  // terms: term/meaning, schedule: minutes/title
};
struct PrepSheet { std::string title; std::vector<SheetSlide> slides; };
PrepSheet build_prep_sheet(const Plot& plot, const Timeline& timeline);
// Serialises the sheet as a PowerPoint (.pptx) package.
std::string write_pptx(const PrepSheet& sheet);
}
