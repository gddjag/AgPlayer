#include "editor_action_model.hpp"

#include <algorithm>

namespace {

std::vector<EditorAction> defaultActions()
{
    return {
        {"editor.open", QStringLiteral("打开"), QStringLiteral("打开音频文件"),
            "Ctrl+O", "folder-open-line"},
        {"editor.save", QStringLiteral("保存"), QStringLiteral("保存当前音频"),
            "Ctrl+S", "save-3-line"},
        {"editor.undo", QStringLiteral("撤销"), QStringLiteral("撤销上一步"),
            "Ctrl+Z", "arrow-go-back-line"},
        {"editor.redo", QStringLiteral("重做"), QStringLiteral("重做上一步"),
            "Ctrl+Y", "arrow-go-forward-line"},
        {"editor.cut", QStringLiteral("剪切"), QStringLiteral("剪切选区"),
            "Ctrl+X", "scissors-cut-line"},
        {"editor.copy", QStringLiteral("复制"), QStringLiteral("复制选区"),
            "Ctrl+C", "file-copy-line"},
        {"editor.paste", QStringLiteral("粘贴"), QStringLiteral("粘贴到播放头"),
            "Ctrl+V", "clipboard-line"},
        {"editor.deleteSelection", QStringLiteral("删除"),
            QStringLiteral("删除选区"), "Delete", "delete-bin-line"},
        {"editor.split", QStringLiteral("分割"), QStringLiteral("在播放头分割片段"),
            "S", "scissors-cut-line"},
        {"editor.merge", QStringLiteral("合并"), QStringLiteral("合并选区覆盖的相邻片段"),
            "Ctrl+M", "links-line"},
        {"editor.cropToSelection", QStringLiteral("裁剪"),
            QStringLiteral("仅保留选区"), "Ctrl+T", "crop-line"},
        {"editor.silenceSelection", QStringLiteral("静音"),
            QStringLiteral("将选区设为静音"), "Ctrl+L", "volume-mute-line"},
        {"editor.fadeIn", QStringLiteral("淡入"), QStringLiteral("选区淡入"),
            "Ctrl+Alt+I", "chart-line"},
        {"editor.fadeOut", QStringLiteral("淡出"), QStringLiteral("选区淡出"),
            "Ctrl+Alt+O", "chart-line"},
        {"editor.export", QStringLiteral("导出"), QStringLiteral("导出音频"),
            "Ctrl+Shift+E", "upload-2-line"}
    };
}

} // namespace

EditorActionModel::EditorActionModel(QObject* parent)
    : QAbstractListModel(parent), actions_(defaultActions())
{
}

int EditorActionModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(actions_.size());
}

QVariant EditorActionModel::data(const QModelIndex& index, const int role) const
{
    if (!index.isValid() || index.row() < 0
        || static_cast<std::size_t>(index.row()) >= actions_.size()) {
        return {};
    }
    const auto& action = actions_[static_cast<std::size_t>(index.row())];
    switch (role) {
    case IdRole: return action.id;
    case TextRole: return action.text;
    case TooltipRole: return action.tooltip;
    case ShortcutRole: return action.shortcut;
    case IconRole: return action.icon;
    case EnabledRole: return action.enabled;
    case CheckedRole: return action.checked;
    case BusyRole: return action.busy;
    default: return {};
    }
}

QHash<int, QByteArray> EditorActionModel::roleNames() const
{
    return {{IdRole, "id"}, {TextRole, "text"}, {TooltipRole, "tooltip"},
            {ShortcutRole, "shortcut"}, {IconRole, "icon"},
            {EnabledRole, "enabled"}, {CheckedRole, "checked"},
            {BusyRole, "busy"}};
}

EditorAction* EditorActionModel::action(const QString& id) noexcept
{
    const auto found = std::find_if(actions_.begin(), actions_.end(),
        [&id](const EditorAction& action) { return action.id == id; });
    return found == actions_.end() ? nullptr : &*found;
}

const EditorAction* EditorActionModel::action(const QString& id) const noexcept
{
    const auto found = std::find_if(actions_.cbegin(), actions_.cend(),
        [&id](const EditorAction& action) { return action.id == id; });
    return found == actions_.cend() ? nullptr : &*found;
}

QStringList EditorActionModel::ids() const
{
    QStringList result;
    result.reserve(static_cast<qsizetype>(actions_.size()));
    for (const auto& action : actions_) {
        result.append(action.id);
    }
    return result;
}

void EditorActionModel::setEnabled(const QString& id, const bool enabled)
{
    EditorAction* const item = action(id);
    if (!item || item->enabled == enabled) {
        return;
    }
    item->enabled = enabled;
    const auto row = static_cast<int>(item - actions_.data());
    emit dataChanged(index(row), index(row), {EnabledRole});
}
