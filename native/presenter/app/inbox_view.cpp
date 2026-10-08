#include "inbox_view.hpp"
namespace odeum::presenter {
void inbox_view(tela::Document& doc, PresenterController& controller) {
    auto& inbox = controller.inbox();
    tela::Layout heading; heading.lines = 2; heading.padding = 6;
    doc.text("inbox-heading", "質問・感想の受信箱（このパネルは配信対象外）", heading);
    doc.text("inbox-policy", "最新100件を接続中だけ保持します。表示・非表示は投稿者が選びます。", heading);
    if (const auto* item = inbox.selected()) {
        doc.text("inbox-count", std::to_string(inbox.index() + 1) + " / " + std::to_string(inbox.size()), heading);
        doc.text("inbox-author", item->name + (item->category == "question" ? " — 質問" : " — 感想"), heading);
        doc.text("inbox-visibility", item->show_on_screen ? "画面表示を選択した投稿" : "画面に出さない投稿", heading);
        tela::Layout body; body.lines = 16; body.padding = 8;
        doc.text("inbox-body", item->text, body);
        doc.button("inbox-newer", "新しい投稿", [&controller] { controller.inbox().previous(); });
        doc.button("inbox-older", "古い投稿", [&controller] { controller.inbox().next(); });
    } else doc.text("inbox-empty", "投稿はまだありません", heading);
    doc.button("inbox-back", "配信操作へ戻る", [&controller] { controller.toggle_inbox(); });
}
}
