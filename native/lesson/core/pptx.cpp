#include "prep_sheet.hpp"
#include "zip.hpp"

// PowerPoint package writer for the prep sheet. The package is the minimum PowerPoint accepts:
// one theme, one master, one blank layout, and slides drawn with explicit shapes (no placeholders),
// so the look does not depend on a template file being present at runtime.
namespace odeum::lesson {
namespace {
constexpr long slide_w = 12192000, slide_h = 6858000;   // 16:9 in EMU
constexpr long margin = 640080;                          // 0.7 in
constexpr long content_w = slide_w - 2 * margin;
constexpr const char* ink = "1B2A41";       // text
constexpr const char* muted = "5E6B7D";     // secondary text
constexpr const char* accent = "0B8F83";    // rules, tags
constexpr const char* tint = "E8F4F2";      // question panel
constexpr const char* rule = "C9D2DC";      // hairlines, writing box
constexpr const char* white = "FFFFFF";
constexpr const char* font = "Yu Gothic";

std::string escape(std::string_view text) {
    std::string out;
    for (const char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: if (static_cast<unsigned char>(c) >= 0x20 || c == '\n' || c == '\t') out.push_back(c);
        }
    }
    return out;
}
struct Run { std::string text; int size; bool bold = false; const char* color = ink; };
struct Paragraph { std::vector<Run> runs; const char* align = "l"; int space_after = 0; };

std::string paragraph_xml(const Paragraph& paragraph) {
    std::string out = "<a:p><a:pPr algn=\"" + std::string(paragraph.align) + "\"><a:lnSpc><a:spcPct val=\"115000\"/></a:lnSpc><a:spcAft><a:spcPts val=\""
        + std::to_string(paragraph.space_after * 100) + "\"/></a:spcAft><a:buNone/></a:pPr>";
    for (const auto& run : paragraph.runs) {
        out += "<a:r><a:rPr lang=\"ja-JP\" altLang=\"en-US\" sz=\"" + std::to_string(run.size * 100) + "\" b=\"" + (run.bold ? "1" : "0")
            + "\" dirty=\"0\"><a:solidFill><a:srgbClr val=\"" + run.color + "\"/></a:solidFill><a:latin typeface=\"" + font
            + "\"/><a:ea typeface=\"" + font + "\"/></a:rPr><a:t>" + escape(run.text) + "</a:t></a:r>";
    }
    return out + "</a:p>";
}
// Collects one slide's shapes; ids only need to be unique within the slide.
class Canvas {
public:
    void box(long x, long y, long w, long h, const char* fill, const char* line = nullptr, bool dashed = false, bool rounded = false) {
        shapes_ += open("Shape", x, y, w, h, false) + geometry(rounded);
        shapes_ += fill ? "<a:solidFill><a:srgbClr val=\"" + std::string(fill) + "\"/></a:solidFill>" : std::string("<a:noFill/>");
        if (line) {
            shapes_ += "<a:ln w=\"12700\"><a:solidFill><a:srgbClr val=\"" + std::string(line) + "\"/></a:solidFill>";
            if (dashed) shapes_ += "<a:prstDash val=\"dash\"/>";
            shapes_ += "</a:ln>";
        } else shapes_ += "<a:ln><a:noFill/></a:ln>";
        shapes_ += "</p:spPr><p:txBody><a:bodyPr/><a:lstStyle/><a:p><a:endParaRPr lang=\"ja-JP\"/></a:p></p:txBody></p:sp>";
    }
    void text(long x, long y, long w, long h, const std::vector<Paragraph>& paragraphs, const char* anchor = "t") {
        shapes_ += open("Text", x, y, w, h, true) + geometry(false) + "<a:noFill/></p:spPr><p:txBody><a:bodyPr wrap=\"square\" lIns=\"0\" tIns=\"0\" rIns=\"0\" bIns=\"0\" anchor=\""
            + std::string(anchor) + "\"><a:noAutofit/></a:bodyPr><a:lstStyle/>";
        for (const auto& paragraph : paragraphs) shapes_ += paragraph_xml(paragraph);
        shapes_ += "</p:txBody></p:sp>";
    }
    std::string slide(const char* background) const {
        return std::string("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<p:sld xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
            "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" xmlns:p=\"http://schemas.openxmlformats.org/presentationml/2006/main\">"
            "<p:cSld><p:bg><p:bgPr><a:solidFill><a:srgbClr val=\"") + background + "\"/></a:solidFill><a:effectLst/></p:bgPr></p:bg><p:spTree>"
            "<p:nvGrpSpPr><p:cNvPr id=\"1\" name=\"\"/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr><p:grpSpPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"0\" cy=\"0\"/>"
            "<a:chOff x=\"0\" y=\"0\"/><a:chExt cx=\"0\" cy=\"0\"/></a:xfrm></p:grpSpPr>" + shapes_
            + "</p:spTree></p:cSld><p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr></p:sld>";
    }
private:
    std::string shapes_;
    int next_ = 2;
    std::string open(const char* kind, long x, long y, long w, long h, bool text_box) {
        const auto id = std::to_string(next_++);
        return "<p:sp><p:nvSpPr><p:cNvPr id=\"" + id + "\" name=\"" + kind + " " + id + "\"/><p:cNvSpPr" + (text_box ? " txBox=\"1\"" : "")
            + "/><p:nvPr/></p:nvSpPr><p:spPr><a:xfrm><a:off x=\"" + std::to_string(x) + "\" y=\"" + std::to_string(y) + "\"/><a:ext cx=\""
            + std::to_string(w) + "\" cy=\"" + std::to_string(h) + "\"/></a:xfrm>";
    }
    static std::string geometry(bool rounded) {
        return rounded ? "<a:prstGeom prst=\"roundRect\"><a:avLst><a:gd name=\"adj\" fmla=\"val 6000\"/></a:avLst></a:prstGeom>"
            : "<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom>";
    }
};

void heading(Canvas& canvas, const std::string& title) {
    canvas.text(margin, 502920, content_w, 640080, {{{{title, 28, true}}}}, "b");
    canvas.box(margin, 1234440, 1097280, 45720, accent);
}
void footer(Canvas& canvas, const PrepSheet& sheet, std::size_t page) {
    canvas.box(margin, slide_h - 594360, content_w, 9525, rule);
    canvas.text(margin, slide_h - 502920, content_w - 1371600, 274320, {{{{sheet.title + " — 予習シート", 10, false, muted}}}});
    canvas.text(slide_w - margin - 1371600, slide_h - 502920, 1371600, 274320,
        {{{{std::to_string(page) + " / " + std::to_string(sheet.slides.size()), 10, false, muted}}, "r"}});
}
std::string cover(const SheetSlide& slide) {
    Canvas canvas;
    canvas.box(margin, 1463040, 731520, 73152, accent);
    canvas.text(margin, 1691640, content_w, 1554480, {{{{slide.title, 40, true, white}}}}, "t");
    if (!slide.lead.empty()) canvas.text(margin, 3200400, content_w, 502920, {{{{slide.lead, 20, false, "BFE3DE"}}}});
    std::vector<Paragraph> facts;
    for (const auto& line : slide.lines) facts.push_back({{{line, 14, false, "D5DCE6"}}, "l", 4});
    canvas.text(margin, 3931920, content_w, 1188720, facts);
    canvas.text(margin, 5532120, 5486400, 320040, {{{{"予習シート  ／  名前:", 14, true, white}}}}, "ctr");
    canvas.box(margin + 2286000, 5852160 - 9525, 3657600, 9525, "8FA3BA");
    return canvas.slide(ink);
}
std::string bullet_slide(const SheetSlide& slide, const PrepSheet& sheet, std::size_t page) {
    Canvas canvas;
    heading(canvas, slide.title);
    // One row per item with a hanging accent mark: wrapped lines stay aligned under the text.
    const long top = 1600200, row = (slide_h - 731520 - top) / 6;
    for (std::size_t i = 0; i < slide.lines.size(); ++i) {
        const long y = top + row * static_cast<long>(i);
        canvas.box(margin, y + 100584, 91440, 91440, accent);
        canvas.text(margin + 320040, y, content_w - 320040, row - 45720, {{{{slide.lines[i], 18}}}});
    }
    footer(canvas, sheet, page);
    return canvas.slide(white);
}
std::string rows_slide(const SheetSlide& slide, const PrepSheet& sheet, std::size_t page, long left_w, std::size_t capacity, int size) {
    Canvas canvas;
    heading(canvas, slide.title);
    const long top = 1554480, row = (slide_h - 731520 - top) / static_cast<long>(capacity);
    for (std::size_t i = 0; i < slide.rows.size(); ++i) {
        const long y = top + row * static_cast<long>(i);
        canvas.text(margin, y, left_w - 182880, row - 91440, {{{{slide.rows[i].first, size, true, accent}}}}, "ctr");
        canvas.text(margin + left_w, y, content_w - left_w, row - 91440, {{{{slide.rows[i].second, size}}}}, "ctr");
        canvas.box(margin, y + row - 45720, content_w, 9525, rule);
    }
    footer(canvas, sheet, page);
    return canvas.slide(white);
}
std::string question_slide(const SheetSlide& slide, const PrepSheet& sheet, std::size_t page) {
    Canvas canvas;
    canvas.text(margin, 502920, content_w, 274320, {{{{slide.lead + "  ｜  " + slide.title, 12, true, accent}}}});
    canvas.box(margin, 914400, content_w, 1463040, tint, nullptr, false, true);
    canvas.text(margin + 320040, 914400, content_w - 640080, 1463040, {{{{slide.lines.at(0), 24, true}}}}, "ctr");
    long y = 2560320;
    if (!slide.lines.at(1).empty()) {
        canvas.text(margin, y, content_w, 548640, {{{{"ヒント  ", 13, true, accent}, {slide.lines.at(1), 13, false, muted}}}});
        y += 640080;
    }
    canvas.text(margin, y, content_w, 274320, {{{{"自分の考え (箇条書きで構いません。正解かどうかは気にしない)", 12, false, muted}}}});
    y += 365760;
    canvas.box(margin, y, content_w, slide_h - 731520 - y, nullptr, rule, true, true);
    footer(canvas, sheet, page);
    return canvas.slide(white);
}

const char* const content_types_head = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
    "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
    "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
    "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
    "<Override PartName=\"/ppt/presentation.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.presentation.main+xml\"/>"
    "<Override PartName=\"/ppt/slideMasters/slideMaster1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.slideMaster+xml\"/>"
    "<Override PartName=\"/ppt/slideLayouts/slideLayout1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.slideLayout+xml\"/>"
    "<Override PartName=\"/ppt/theme/theme1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.theme+xml\"/>";
const char* const xml_head = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";
const char* const rels_ns = "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">";
const char* const rel_type = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/";
const char* const main_ns = "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
    "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" xmlns:p=\"http://schemas.openxmlformats.org/presentationml/2006/main\"";
const char* const empty_tree = "<p:cSld><p:spTree><p:nvGrpSpPr><p:cNvPr id=\"1\" name=\"\"/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr><p:grpSpPr><a:xfrm>"
    "<a:off x=\"0\" y=\"0\"/><a:ext cx=\"0\" cy=\"0\"/><a:chOff x=\"0\" y=\"0\"/><a:chExt cx=\"0\" cy=\"0\"/></a:xfrm></p:grpSpPr></p:spTree></p:cSld>";

std::string relationship(int id, const std::string& type, const std::string& target) {
    return "<Relationship Id=\"rId" + std::to_string(id) + "\" Type=\"" + rel_type + type + "\" Target=\"" + target + "\"/>";
}
std::string theme() {
    const auto fonts = [](const char* tag) {
        return std::string("<a:") + tag + "><a:latin typeface=\"" + font + "\"/><a:ea typeface=\"" + font + "\"/><a:cs typeface=\"\"/></a:" + tag + ">";
    };
    const auto color = [](const char* name, const char* value) {
        return std::string("<a:") + name + "><a:srgbClr val=\"" + value + "\"/></a:" + name + ">";
    };
    const std::string fill = "<a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill>";
    const std::string line = "<a:ln w=\"9525\"><a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill></a:ln>";
    const std::string effect = "<a:effectStyle><a:effectLst/></a:effectStyle>";
    return std::string(xml_head) + "<a:theme xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" name=\"Odeum Lesson\"><a:themeElements>"
        "<a:clrScheme name=\"Odeum Lesson\">" + color("dk1", ink) + color("lt1", white) + color("dk2", muted) + color("lt2", tint)
        + color("accent1", accent) + color("accent2", "2F6FED") + color("accent3", "E0A100") + color("accent4", "C2410C")
        + color("accent5", "7C3AED") + color("accent6", "475569") + color("hlink", "2F6FED") + color("folHlink", "7C3AED")
        + "</a:clrScheme><a:fontScheme name=\"Odeum Lesson\">" + fonts("majorFont") + fonts("minorFont") + "</a:fontScheme>"
        "<a:fmtScheme name=\"Odeum Lesson\"><a:fillStyleLst>" + fill + fill + fill + "</a:fillStyleLst><a:lnStyleLst>" + line + line + line
        + "</a:lnStyleLst><a:effectStyleLst>" + effect + effect + effect + "</a:effectStyleLst><a:bgFillStyleLst>" + fill + fill + fill
        + "</a:bgFillStyleLst></a:fmtScheme></a:themeElements></a:theme>";
}
}

std::string write_pptx(const PrepSheet& sheet) {
    std::vector<std::pair<std::string, std::string>> files;
    std::string types = content_types_head, slide_ids, presentation_rels = std::string(xml_head) + rels_ns
        + relationship(1, "slideMaster", "slideMasters/slideMaster1.xml") + relationship(2, "theme", "theme/theme1.xml");
    std::vector<std::pair<std::string, std::string>> slides;
    for (std::size_t i = 0; i < sheet.slides.size(); ++i) {
        const auto& slide = sheet.slides[i];
        const auto number = std::to_string(i + 1);
        std::string xml;
        switch (slide.kind) {
            case SheetSlide::Kind::cover: xml = cover(slide); break;
            case SheetSlide::Kind::bullets: xml = bullet_slide(slide, sheet, i + 1); break;
            case SheetSlide::Kind::terms: xml = rows_slide(slide, sheet, i + 1, 2926080, 5, 16); break;
            case SheetSlide::Kind::schedule: xml = rows_slide(slide, sheet, i + 1, 1280160, 9, 16); break;
            case SheetSlide::Kind::question: xml = question_slide(slide, sheet, i + 1); break;
        }
        slides.emplace_back("ppt/slides/slide" + number + ".xml", std::move(xml));
        slides.emplace_back("ppt/slides/_rels/slide" + number + ".xml.rels",
            std::string(xml_head) + rels_ns + relationship(1, "slideLayout", "../slideLayouts/slideLayout1.xml") + "</Relationships>");
        types += "<Override PartName=\"/ppt/slides/slide" + number + ".xml\" ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.slide+xml\"/>";
        slide_ids += "<p:sldId id=\"" + std::to_string(256 + i) + "\" r:id=\"rId" + std::to_string(3 + i) + "\"/>";
        presentation_rels += relationship(static_cast<int>(3 + i), "slide", "slides/slide" + number + ".xml");
    }
    files.emplace_back("[Content_Types].xml", types + "</Types>");
    files.emplace_back("_rels/.rels", std::string(xml_head) + rels_ns + relationship(1, "officeDocument", "ppt/presentation.xml") + "</Relationships>");
    files.emplace_back("ppt/presentation.xml", std::string(xml_head) + "<p:presentation " + main_ns + "><p:sldMasterIdLst><p:sldMasterId id=\"2147483648\" r:id=\"rId1\"/>"
        "</p:sldMasterIdLst><p:sldIdLst>" + slide_ids + "</p:sldIdLst><p:sldSz cx=\"" + std::to_string(slide_w) + "\" cy=\"" + std::to_string(slide_h)
        + "\"/><p:notesSz cx=\"6858000\" cy=\"9144000\"/></p:presentation>");
    files.emplace_back("ppt/_rels/presentation.xml.rels", presentation_rels + "</Relationships>");
    files.emplace_back("ppt/slideMasters/slideMaster1.xml", std::string(xml_head) + "<p:sldMaster " + main_ns + ">" + empty_tree
        + "<p:clrMap bg1=\"lt1\" tx1=\"dk1\" bg2=\"lt2\" tx2=\"dk2\" accent1=\"accent1\" accent2=\"accent2\" accent3=\"accent3\" accent4=\"accent4\" "
        "accent5=\"accent5\" accent6=\"accent6\" hlink=\"hlink\" folHlink=\"folHlink\"/><p:sldLayoutIdLst><p:sldLayoutId id=\"2147483649\" r:id=\"rId1\"/>"
        "</p:sldLayoutIdLst></p:sldMaster>");
    files.emplace_back("ppt/slideMasters/_rels/slideMaster1.xml.rels", std::string(xml_head) + rels_ns
        + relationship(1, "slideLayout", "../slideLayouts/slideLayout1.xml") + relationship(2, "theme", "../theme/theme1.xml") + "</Relationships>");
    files.emplace_back("ppt/slideLayouts/slideLayout1.xml", std::string(xml_head) + "<p:sldLayout " + main_ns + " type=\"blank\" preserve=\"1\">"
        + std::string(empty_tree).insert(7, " name=\"Blank\"") + "<p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr></p:sldLayout>");
    files.emplace_back("ppt/slideLayouts/_rels/slideLayout1.xml.rels", std::string(xml_head) + rels_ns
        + relationship(1, "slideMaster", "../slideMasters/slideMaster1.xml") + "</Relationships>");
    files.emplace_back("ppt/theme/theme1.xml", theme());
    files.insert(files.end(), std::make_move_iterator(slides.begin()), std::make_move_iterator(slides.end()));
    return zip_store(files);
}
}
