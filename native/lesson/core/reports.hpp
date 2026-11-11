#pragma once
#include "types.hpp"
#include <map>
#include <set>

namespace odeum::lesson {
// Reports that the class itself could not be followed (the demo too small to read, the voice dropping).
// They are not questions: nobody else votes on them and they are never shared. Each one stays in front
// of the human teacher until it is acknowledged or answered, and its reporter can see which.
class TroubleReports {
public:
    enum class State { reported, acknowledged, replied };
    static constexpr std::size_t max_reports = 500;

    // Returns the new report's id. Throws too_many_reports past max_reports.
    std::string add(const std::string& student, const std::string& text);
    // The teacher has seen it. A report already replied to stays replied. Throws unknown_report.
    void acknowledge(const std::string& id);
    // The teacher answers it; the reply goes to the reporter. Throws unknown_report.
    void reply(const std::string& id, const std::string& text);
    const std::string& reporter(const std::string& id) const { return find(id).student; }
    // {id, text, state, reply}: state is reported (知らせました) / acknowledged (先生が確認しました) / replied (先生から返事).
    Json status(const std::string& id) const;
    Json mine(const std::string& student) const;
    Json console_list() const;                 // every report, with the reporter's id
    std::size_t open() const;                  // reports nobody has acknowledged or answered yet
    // Reports by `student` nobody acknowledged and that the student has not been told about; they are marked as told.
    std::vector<std::string> take_unacknowledged(const std::string& student);
    const std::string& text(const std::string& id) const { return find(id).text; }
    void withdraw(const std::string& student);  // the student was ejected
private:
    struct Report { std::string id, student, text, reply; State state = State::reported; bool told_at_end = false; };
    std::map<std::string, Report> reports_;
    std::vector<std::string> order_;
    std::size_t next_ = 0;
    const Report& find(const std::string& id) const;
    Report& find(const std::string& id);
};
}
