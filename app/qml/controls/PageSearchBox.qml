import QtQuick
import FluentUI

// 编辑中的文字留在输入框，提交结果由各自缓存的页面持有。
FluTextBox {
    id: control

    property var searchPage: null
    property bool restoringQuery: false

    function submit() {
        if (searchPage && searchPage.searchQuery !== text)
            searchPage.searchQuery = text
    }

    onSearchPageChanged: {
        restoringQuery = true
        text = searchPage ? searchPage.searchQuery : ""
        restoringQuery = false
    }
    // 内置清除按钮仅调用 clear()，已失焦时不会自行聚焦输入框。
    // 清空仍作为草稿，重新聚焦后可通过 Enter 或下一次失焦提交。
    onTextChanged: {
        if (!restoringQuery && searchPage && visible && !activeFocus)
            forceActiveFocus()
    }
    // FluTextBox 的 Keys 处理器消费 Enter/Return，须监听其 commit 信号。
    onCommit: submit()
    onEditingFinished: submit()
    onActiveFocusChanged: {
        if (!activeFocus) submit()
    }
}
