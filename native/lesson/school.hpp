#pragma once
#include "classroom.hpp"
#include "tool_desk.hpp"
#include <deque>

namespace odeum::lesson {
// The classes this service is running, and the join codes that lead to them.
class School {
public:
    static constexpr std::size_t max_classes = 8, max_failures_per_minute = 30;
    struct Opened { std::string id, code; };
    School(std::vector<Plot> catalog, std::shared_ptr<const LessonModel> model, std::size_t max_students);
    Opened open(const std::string& plot_id);          // throws unknown_plot / capacity
    void close(const std::string& class_id);
    Classroom& classroom(const std::string& class_id); // throws unknown_class
    // Join code as typed by a student -> class id. Wrong codes are throttled across the service
    // so the code cannot be guessed by trying; throws class_not_found / rate_limited.
    std::string resolve(std::string_view code, Millis now);
    const Plot* plot(std::string_view id) const;
    ToolDesk& tools() { return tools_; }      // closing a class takes its tools' access with it
    Json catalog() const;   // console: available plots and running classes with their join codes
    std::vector<std::pair<std::string, Outbox>> tick(Millis now);
private:
    struct Running { std::string code; std::unique_ptr<Classroom> classroom; };
    std::vector<Plot> catalog_;
    std::shared_ptr<const LessonModel> model_;
    std::size_t max_students_;
    std::map<std::string, Running> classes_;
    std::map<std::string, std::string> codes_;  // normalised join code -> class id
    std::deque<Millis> failures_;
    std::size_t next_ = 0;
    ToolDesk tools_;
};
// 10 Crockford base32 symbols (50 bits), the same shape as the relay's guest join codes.
std::string random_join_code();
std::string random_token();
}
