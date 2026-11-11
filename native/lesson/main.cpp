#include "catalog.hpp"
#include "console_guard.hpp"
#include "prep_sheet.hpp"
#include "server.hpp"
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <csignal>
#include <fstream>
#include <iostream>

namespace {
using namespace odeum::lesson;
constexpr const char* usage =
    "usage:\n"
    "  odeum-lesson                               run the class service (ODEUM_LESSON_* environment)\n"
    "  odeum-lesson timeline <course|file.json>   print the timeline generated from a plot as JSON\n"
    "  odeum-lesson prep-sheet <course|file.json> <out.pptx>   write the prep sheet\n"
    "  odeum-lesson courses                       list built-in courses\n";

Plot find_plot(const std::string& name) {
    for (auto& plot : builtin_plots()) if (plot.id == name) return plot;
    return load_plot_file(name);
}
int serve() {
    const auto config = load_config();
    auto plots = builtin_plots();
    if (!config.plot_file.empty()) plots.push_back(load_plot_file(config.plot_file));
    School school(std::move(plots), make_provisional_model(), config.max_students);
    boost::asio::io_context io(1);
    Server server(io, config, school, write_console_token(config.console_token_file));
    boost::asio::signal_set signals(io, SIGINT, SIGTERM);
    signals.async_wait([&io](const boost::system::error_code&, int) { io.stop(); });
    server.start();
    std::clog << "odeum-lesson: students on port " << config.port << ", console on 127.0.0.1:" << config.console_port << '\n';
    io.run();
    return 0;
}
}

int main(int argc, char** argv) {
    try {
        const std::vector<std::string> args(argv + 1, argv + argc);
        if (args.empty()) return serve();
        if (args[0] == "courses" && args.size() == 1) {
            for (const auto& plot : builtin_plots()) std::cout << plot.id << '\t' << plot.title << '\n';
            return 0;
        }
        const auto model = make_provisional_model();
        if (args[0] == "timeline" && args.size() == 2) {
            const auto plot = find_plot(args[1]);
            std::cout << timeline_json(build_timeline(plot, *model)).dump(2) << '\n';
            return 0;
        }
        if (args[0] == "prep-sheet" && args.size() == 3) {
            const auto plot = find_plot(args[1]);
            std::ofstream file(args[2], std::ios::binary);
            file << write_pptx(build_prep_sheet(plot, build_timeline(plot, *model)));
            if (!file.flush()) throw std::runtime_error("Cannot write " + args[2]);
            return 0;
        }
        std::cerr << usage;
        return 2;
    } catch (const odeum::ProtocolError& error) {
        std::cerr << "odeum-lesson: " << error.code << ": " << error.what() << '\n';
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "odeum-lesson: " << error.what() << '\n';
        return 1;
    }
}
