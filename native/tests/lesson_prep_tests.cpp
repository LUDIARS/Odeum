#include "lesson_fixture.hpp"
#include "prep_sheet.hpp"
#include "zip.hpp"
#include <map>
using namespace odeum;
using namespace odeum::lesson;

namespace {
std::uint32_t u32(const std::string& data, std::size_t at) {
    return static_cast<unsigned char>(data[at]) | (static_cast<unsigned char>(data[at + 1]) << 8)
        | (static_cast<unsigned char>(data[at + 2]) << 16) | (static_cast<std::uint32_t>(static_cast<unsigned char>(data[at + 3])) << 24);
}
std::uint16_t u16(const std::string& data, std::size_t at) {
    return static_cast<std::uint16_t>(static_cast<unsigned char>(data[at]) | (static_cast<unsigned char>(data[at + 1]) << 8));
}
// Reads the archive back through its central directory, the way an Office reader does.
std::map<std::string, std::string> unzip(const std::string& archive) {
    std::map<std::string, std::string> files;
    const auto end = archive.size() - 22;
    check(u32(archive, end) == 0x06054b50, "End of central directory");
    auto at = u32(archive, end + 16);
    for (std::uint16_t i = 0; i < u16(archive, end + 10); ++i) {
        check(u32(archive, at) == 0x02014b50 && u16(archive, at + 10) == 0, "Central entry, stored");
        const auto crc = u32(archive, at + 16), size = u32(archive, at + 24), local = u32(archive, at + 42);
        const auto name = archive.substr(at + 46, u16(archive, at + 28));
        check(u32(archive, local) == 0x04034b50 && archive.substr(local + 30, name.size()) == name, "Local header matches");
        auto data = archive.substr(local + 30 + name.size(), size);
        check(crc32(data) == crc, "CRC matches content");
        files.emplace(name, std::move(data));
        at += 46 + name.size();
    }
    return files;
}
bool has(const std::string& text, std::string_view part) { return text.find(part) != std::string::npos; }
}

int main() { return run([] {
    check(crc32("123456789") == 0xcbf43926u && crc32("") == 0, "CRC-32 check value");

    const auto plot = sample_plot();
    const auto model = make_provisional_model();
    const auto sheet = build_prep_sheet(plot, build_timeline(plot, *model));
    check(sheet.slides.front().kind == SheetSlide::Kind::cover && sheet.slides.front().title == plot.title, "Cover first");
    std::size_t questions = 0, schedule_rows = 0;
    for (const auto& slide : sheet.slides) {
        questions += slide.kind == SheetSlide::Kind::question;
        if (slide.kind == SheetSlide::Kind::schedule) schedule_rows += slide.rows.size();
        check(slide.lines.size() <= 6 || slide.kind != SheetSlide::Kind::bullets, "Long lists continue on another slide");
        check(slide.rows.size() <= 9, "Row slides are paginated");
    }
    check(questions == 6, "One thinking question per section that has one");
    check(schedule_rows == plot.sections.size(), "Schedule lists every section");

    const auto archive = write_pptx(sheet);
    check(write_pptx(sheet) == archive, "Output is reproducible");
    const auto files = unzip(archive);
    for (const auto* part : {"[Content_Types].xml", "_rels/.rels", "ppt/presentation.xml", "ppt/_rels/presentation.xml.rels",
        "ppt/slideMasters/slideMaster1.xml", "ppt/slideLayouts/slideLayout1.xml", "ppt/theme/theme1.xml"})
        check(files.contains(part), "Required package part");
    std::string everything;
    for (std::size_t i = 1; i <= sheet.slides.size(); ++i) {
        const auto name = "ppt/slides/slide" + std::to_string(i) + ".xml";
        check(files.contains(name) && files.contains("ppt/slides/_rels/slide" + std::to_string(i) + ".xml.rels"), "Slide and its relationships");
        check(has(files.at("[Content_Types].xml"), "/" + name) && has(files.at("ppt/_rels/presentation.xml.rels"), "slides/slide" + std::to_string(i) + ".xml"), "Slide is registered");
        everything += files.at(name);
    }
    check(!files.contains("ppt/slides/slide" + std::to_string(sheet.slides.size() + 1) + ".xml"), "No extra slides");
    check(has(everything, "アクションゲームの作り方") && has(everything, "今日のゴール") && has(everything, "予習の問い 1"), "Titles are present");
    check(has(everything, "ヒットストップを作るとしたら") && has(everything, "カメラが良い"), "Thinking questions are present");
    check(has(everything, "Ergo Studio") && has(everything, "https://github.com/vtn-team/ActionGameSample"), "Tools and reference are present");
    // The sheet must not give away what the class is asked to find.
    check(!has(everything, find_activity(plot, "hitstop-design")->model_answer) && !has(everything, "威力 ÷ 24 秒で、威力 3 なら"), "No answers on the prep sheet");

    // Text that needs escaping survives as text.
    PrepSheet tricky{"A & B <tag>", {{SheetSlide::Kind::bullets, "x < y & \"z\"", {}, {"1 > 0"}, {}}}};
    const auto tricky_files = unzip(write_pptx(tricky));
    const auto& slide = tricky_files.at("ppt/slides/slide1.xml");
    check(has(slide, "x &lt; y &amp; &quot;z&quot;") && has(slide, "1 &gt; 0") && has(slide, "A &amp; B &lt;tag&gt;") && !has(slide, "<tag>"), "XML special characters are escaped");
}); }
