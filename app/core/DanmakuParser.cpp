#include "core/DanmakuParser.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QXmlStreamReader>
#include <algorithm>

namespace {
// C# DanmakuMode 枚举:Unknown=0 / Rolling=1 / Bottom=4 / Top=5 /
// ReverseRolling=6 / Advanced=7(Code=8、Subtitle=9 不入 XML 存档解析路径,
// 其余值 C# 即丢弃)
constexpr int kModeRolling = 1;
constexpr int kModeBottom = 4;
constexpr int kModeTop = 5;
constexpr int kModeReverseRolling = 6;
constexpr int kModeAdvanced = 7;

// 20s 重复合并只作用于 滚动/顶部/底部 三类(C# 同)
bool isMergeableMode(int mode) {
    return mode == kModeRolling || mode == kModeTop || mode == kModeBottom;
}

// C# DuplicatedDanmakuItem
struct DuplicatedDanmakuItem {
    quint32 startMs = 0;
    quint32 count = 1;
};

// 解析中间态:startMs / id 按 C# StartMs(uint 截断)/ Id(ulong)语义持有,
// 排序与 ×N 匹配都建立在它们之上(与 C# 完全同序)。
struct ParsedItem {
    DanmakuParser::DanmakuEntry entry;
    quint32 startMs = 0;
    quint64 id = 0;  // C# Id:新格式取 p[0],旧格式恒 0
};
}  // namespace

QList<DanmakuParser::DanmakuEntry> DanmakuParser::parseXml(const QString &xml) {
    QList<ParsedItem> items;
    if (xml.trimmed().isEmpty()) return {};

    // 新格式判别与 C# 同:存档根带 <oid> 节点。
    // 旧格式:<d p="23.826000213623,1,25,16777215,1422201084,0,057075e9,757076900">我从未见过如此厚颜无耻之猴</d>
    // 新格式:<d p="30845767597424709,0,59065,1,25,16777215,1585975807,0,8317333c">啊啊啊啊啊啊啊啊</d>
    // 模式 7:<d p="3782563342,0,274462,7,20,10027263,1504375326,0,a77032eb">[0,0.5,"1-1",3,"为了那个傻傻的放电妹",0,0,0.99,0.5,3000,0,true,"幼圆",1]</d>
    const bool isNewFormat = xml.contains("<oid>");

    QHash<QString, QList<DuplicatedDanmakuItem>> duplicatedDanmakuDict;

    QXmlStreamReader reader(xml);
    while (!reader.atEnd() && !reader.hasError()) {
        if (!reader.readNextStartElement()) continue;
        if (reader.name() != QLatin1String("d")) continue;

        const QString tagStr = reader.attributes().value("p").toString();
        // readElementText 已完成实体反转义(对应 C# 正则捕获后的 HtmlDecode)
        QString contentStr = reader.readElementText();

        // 空 p 标签或空正文直接丢弃(C#:计入 filteredCount)
        if (tagStr.trimmed().isEmpty() || contentStr.trimmed().isEmpty()) continue;

        // /n 与字面 \n 统一成换行后 trim(C# 同)
        contentStr.replace("/n", "\n").replace("\\n", "\n");
        contentStr = contentStr.trimmed();

        const QStringList pArray = tagStr.split(',');

        // ---- 20s 窗口重复合并(×N 后缀在收集完成后统一追加) ----
        if (pArray.size() >= 4) {
            bool modeOk = false;
            bool timeOk = false;
            const int mode = pArray.value(isNewFormat ? 3 : 1).toInt(&modeOk);
            const double time = pArray.value(isNewFormat ? 2 : 0).toDouble(&timeOk);
            if (modeOk && timeOk && isMergeableMode(mode) && time >= 0) {
                // C#:(uint)(isNewFormat ? time : time * 1000) —— 新格式本就是毫秒
                const quint32 startMs =
                        static_cast<quint32>(isNewFormat ? time : time * 1000.0);
                const auto dictIt = duplicatedDanmakuDict.find(contentStr);
                if (dictIt == duplicatedDanmakuDict.end()) {
                    duplicatedDanmakuDict.insert(contentStr,
                                                 {DuplicatedDanmakuItem{startMs, 1}});
                } else {
                    bool merged = false;
                    for (DuplicatedDanmakuItem &duplicated : dictIt.value()) {
                        // 合并 20s 窗口内的重发弹幕(C#:uint 差值截断成 int 后取
                        // 绝对值;常见量级下与带符号差值等价)
                        if (qAbs(static_cast<qint64>(startMs) -
                                 static_cast<qint64>(duplicated.startMs)) <= 20000) {
                            merged = true;
                            duplicated.count++;
                            break;
                        }
                    }
                    if (merged) continue;  // C#:mergedCount++,该条不入列
                    dictIt->append(DuplicatedDanmakuItem{startMs, 1});
                }
            }
        }

        // ---- 单条解析(对应 C# ParseDanmakuItem,失败即整条丢弃) ----
        if (pArray.size() < 8) continue;  // C#:列数不足返回 null

        bool okMode = false;
        bool okFontSize = false;
        bool okColor = false;
        bool okTime = false;
        const int mode = pArray.value(isNewFormat ? 3 : 1).toInt(&okMode);
        const int fontSize = pArray.value(isNewFormat ? 4 : 2).toInt(&okFontSize);
        const uint colorValue = pArray.value(isNewFormat ? 5 : 3).toUInt(&okColor);
        double startMs = isNewFormat ? pArray.value(2).toDouble(&okTime)
                                     : pArray.value(0).toDouble(&okTime) * 1000.0;
        // C# int/uint/double.Parse 解析失败抛异常 → 返回 null → 丢弃
        if (!okMode || !okFontSize || !okColor || !okTime) continue;

        switch (mode) {
            case kModeRolling:
            case kModeBottom:
            case kModeTop:
            case kModeReverseRolling:
            case kModeAdvanced:
                break;
            default:
                continue;  // C#:Skip unknown danmaku type → 返回 null
        }

        ParsedItem item;
        DanmakuEntry &entry = item.entry;
        if (startMs < 0) startMs = 0;
        item.startMs = static_cast<quint32>(startMs);
        entry.time = startMs / 1000.0;  // 归一到秒
        entry.type = mode;
        // 字号保留 XML 原值:C# 的 偶-2/奇-3、advanced +4(试验性)属 DanmakuFrostMaster
        // 渲染侧字体管线调整,Qt 播放器自定渲染,不在解析层做
        entry.fontSize = fontSize;
        // C# ParseColor:掩掉 alpha,余下按 0xRRGGBB 原样(位分解后重组,无实际字节交换)
        entry.fontColor = static_cast<int>(colorValue & 0xFFFFFFu);
        entry.level = pArray.value(isNewFormat ? 7 : 5).toLongLong();
        entry.hash = pArray.value(isNewFormat ? 8 : 6);
        entry.row = pArray.value(isNewFormat ? 0 : 7).toLongLong();  // dmid
        item.id = isNewFormat ? pArray.value(0).toULongLong() : 0;   // C# Id 语义

        if (entry.type == kModeAdvanced) {
            // 高级弹幕:正文取 payload 第 5 项(C# valueArray[4]);几何/透明度/
            // 时长等字段在 DanmakuEntry 中无对应,不随移植
            if (!contentStr.startsWith(QLatin1Char('[')) ||
                !contentStr.endsWith(QLatin1Char(']'))) {
                continue;  // C#:非 [..] 形态返回 null
            }
            const QJsonDocument payload = QJsonDocument::fromJson(contentStr.toUtf8());
            if (!payload.isArray()) continue;  // C#:JSON 解析异常 → 返回 null
            const QJsonArray values = payload.array();
            if (values.size() < 5) continue;  // C#:同
            // C#:ToString 后再 HtmlDecode —— 外层 XML 反转义在读取时已完成,
            // 此处不再二次解码(仅载荷含字面 &amp; 实体时有差异,极罕见)
            QString advancedText = values.at(4).toVariant().toString();
            advancedText.replace("/n", "\n").replace("\\n", "\n");
            if (advancedText.trimmed().isEmpty()) continue;
            entry.message = advancedText;
        } else {
            entry.message = contentStr;
        }

        items.append(item);
    }

    // ---- ×N 重复合并后缀(第二遍,与 C# 同) ----
    for (ParsedItem &item : items) {
        if (!isMergeableMode(item.entry.type)) continue;
        const auto dictIt = duplicatedDanmakuDict.find(item.entry.message);
        if (dictIt == duplicatedDanmakuDict.end()) continue;
        for (const DuplicatedDanmakuItem &duplicated : dictIt.value()) {
            if (duplicated.count > 1 && item.startMs == duplicated.startMs) {
                item.entry.message = item.entry.message + QStringLiteral("×") +
                                     QString::number(duplicated.count);
                break;
            }
        }
    }

    // ---- 按出现时间稳定排序(C# 归并排序:同毫秒时 advanced 段按 Id 比较,
    //      其余保持原相对次序;C# 的非对称比较分支此处取可传递的等价形式) ----
    std::stable_sort(items.begin(), items.end(), [](const ParsedItem &a, const ParsedItem &b) {
        if (a.startMs != b.startMs) return a.startMs < b.startMs;
        const bool aAdvanced = a.entry.type == kModeAdvanced;
        const bool bAdvanced = b.entry.type == kModeAdvanced;
        if (aAdvanced && bAdvanced) return a.id < b.id;
        return false;
    });

    QList<DanmakuEntry> result;
    result.reserve(items.size());
    for (const ParsedItem &item : items) {
        result.append(item.entry);
    }
    return result;
}
