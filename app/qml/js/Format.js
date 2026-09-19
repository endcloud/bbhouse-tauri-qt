// 历史浏览域纯函数(无 UI 依赖),口径对齐原 WinUI Models.cs:
// - viewAt 为 Unix 秒,展示格式 yyyy-MM-dd HH:mm(本地时区)
// - 时长/进度格式 h:mm:ss / m:ss
// - 封面 URL 的 CDN 转码后缀以第一个 '@' 起始(去后缀 = 截断)
.pragma library

// 去掉封面 URL 的 '@…' 转码后缀,得库中原始地址(预览/下载原图/缩略图基底均用它)
function stripImageTranscode(url) {
    if (!url) return ""
    var index = url.indexOf("@")
    return index > 0 ? url.substring(0, index) : url
}

// 视频卡片统一请求 400×225 WebP；不修改模型中的原始地址或原图预览。
// 仅 B 站图片 CDN 支持此语法；本地/qrc/data/其他站点 URL 保持原样。
// 已有转码只替换路径后缀，保留查询参数与片段，避免重复追加。
function cardCoverThumbnailUrl(url) {
    return bilibiliImageTranscodeUrl(url, "@400w_225h_1c.webp")
}

// 番剧竖版海报只转换格式，保留原图尺寸与构图。
function seasonCoverUrl(url) {
    return bilibiliImageTranscodeUrl(url, "@.webp")
}

function bilibiliImageTranscodeUrl(url, suffix) {
    if (!url) return ""
    var value = String(url).trim()
    var match = value.match(/^(https?:)?\/\/([^/?#]+)([^?#]*)([?#][\s\S]*)?$/i)
    if (!match || !/(^|\.)(hdslb\.com|biliimg\.com|bilibili\.com)$/i.test(match[2])) {
        return value
    }
    var path = match[3]
    var index = path.indexOf("@")
    if (index >= 0) path = path.substring(0, index)
    return (match[1] || "https:") + "//" + match[2] + path +
            suffix + (match[4] || "")
}

// 卡片缩略图:转码后缀 @{物理宽}w_{物理高}h_1c{格式后缀}(1c 居中裁剪)。
// 宽高由调用方传容器 DIP 尺寸 × 设备像素比(向上取整在此处统一做);
// formatSuffix 由 AppController.imageTranscodeSuffix 按本机 Qt 图像插件能力
// 给定(.avif/.webp/空),缺省 .avif 保持原 Windows 口径;
// suffix 为空且源容器本身不可解码(如 .webp 头像/封面)时显式转 .jpg
function buildThumbnailUrl(coverUrl, physicalWidth, physicalHeight, formatSuffix,
                           supportedFormats) {
    var baseUrl = stripImageTranscode(coverUrl)
    if (baseUrl === "" || !(physicalWidth > 0) || !(physicalHeight > 0)) {
        return baseUrl
    }
    var suffix = (formatSuffix === undefined || formatSuffix === null)
            ? ".avif" : String(formatSuffix)
    if (suffix === "" &&
            ensureDecodableImageUrl(baseUrl, supportedFormats) !== baseUrl) {
        suffix = ".jpg"
    }
    return baseUrl + "@" + Math.ceil(physicalWidth) + "w_" +
            Math.ceil(physicalHeight) + "h_1c" + suffix
}

// 将 URL 转为本机 Qt 可解码形式:URL 最终扩展名(转码后缀后的格式优先)
// 不在 supportedFormats(AppController.decodableImageFormats)时,追加 CDN
// 纯转格式参数(无 '@' 追加 "@.jpg",已有 '@' 追加 ".jpg"),保持原尺寸。
// supportedFormats 缺省视为仅 jpeg/png/gif/svg/ico(Qt 基座必有)。
function ensureDecodableImageUrl(url, supportedFormats) {
    if (!url) return ""
    var u = String(url).trim()
    if (u === "") return ""
    var match = u.match(/\.([a-zA-Z0-9]+)([@?][^]*)?$/)
    if (match) {
        var ext = match[1].toLowerCase()
        var formats = supportedFormats ||
                ["jpg", "jpeg", "png", "gif", "svg", "ico", "bmp"]
        if (formats.indexOf(ext) !== -1 || formats.indexOf(
                    ext === "jpg" ? "jpeg" : ext) !== -1) {
            return u
        }
    }
    // 无扩展名或本地不可解码:CDN 转格式为 jpeg
    return u.indexOf("@") > 0 ? u + ".jpg" : u + "@.jpg"
}

// viewAt(Unix 秒)→ "yyyy-MM-dd HH:mm";无效值返回空串(空态文案由 UI 层出)
function formatTimestamp(viewAtSeconds) {
    if (!viewAtSeconds || viewAtSeconds <= 0) return ""
    return Qt.formatDateTime(new Date(viewAtSeconds * 1000), "yyyy-MM-dd HH:mm")
}

// 秒 → h:mm:ss(≥1h)/ m:ss(<1h)
function formatSeconds(totalSeconds) {
    var seconds = Math.max(0, Math.floor(totalSeconds))
    var hours = Math.floor(seconds / 3600)
    var minutes = Math.floor((seconds % 3600) / 60)
    var rest = seconds % 60
    function pad(value) { return value < 10 ? "0" + value : "" + value }
    return hours > 0 ? hours + ":" + pad(minutes) + ":" + pad(rest)
                     : minutes + ":" + pad(rest)
}
