import QtQuick

DropArea {
    id: root
    // NativeDropRouter consumes WM_DROPFILES when Windows delivers it.  Keep
    // the Qt path active as a fallback for shell extensions and Windows builds
    // that route the drop through Qt instead of the raw native message.
    enabled: true

    signal urlsDropped(var urls)

    function submitUrls(urls) {
        if (!urls || urls.length === 0)
            return false

        var acceptedUrls = []
        for (var index = 0; index < urls.length; ++index)
            acceptedUrls.push(urls[index])

        root.urlsDropped(acceptedUrls)
        return true
    }

    onDropped: function(drop) {
        if (root.submitUrls(drop.urls))
            drop.acceptProposedAction()
    }
}
