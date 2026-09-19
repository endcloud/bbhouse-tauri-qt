#ifndef DANMAKU_PARSER_H
#define DANMAKU_PARSER_H

#include <QList>
#include <QString>

// 弹幕 XML 存档解析(comment.bilibili.com/{cid}.xml,PlayerApi::getDanmakuXml 的
// 解压产物)。移植自 C# BilibiliDanmakuXmlParser(原 ported from
// DanmakuFrostMaster UWP):旧/新 p 属性列位双形态、20s 窗口重复合并 ×N、
// 按出现时间稳定排序 —— 语义照抄。
class DanmakuParser {
   public:
    // 单条弹幕。p 属性列位映射:旧格式(8 列)
    // [0]时间(秒) [1]模式 [2]字号 [3]颜色 [4]发送时间 [5]级别 [6]hash [7]dmid;
    // 新格式(9 列)[0]dmid [1]- [2]时间(毫秒) [3]模式 [4]字号 [5]颜色
    // [6]发送时间 [7]级别 [8]hash。hash/row 是 C# DanmakuItem 未承载的原始列,
    // 按移植要求一并保留。
    struct DanmakuEntry {
        double time = 0.0;   // 出现时间(秒;新格式毫秒已归一)
        int type = 0;        // 模式:1 滚动 4 底部 5 顶部 6 逆向 7 高级(其余丢弃)
        int fontSize = 25;   // XML 原始字号(不做渲染侧增减,见 .cpp 注释)
        int fontColor = 0;   // 0xRRGGBB(仅掩 alpha,C# ParseColor 同口径)
        qint64 level = 0;    // 级别列(旧 p[5] / 新 p[7])
        QString message;     // 正文(已反转义、/n 与 \n 换行归一、trim;重复合并后带 ×N)
        QString hash;        // 用户 hash(旧 p[6] / 新 p[8])
        qint64 row = 0;      // 弹幕 id/dmid(旧 p[7] / 新 p[0])
    };

    // 全量解析。C# 的 regexFilterList 与 mergeDuplicate 参数未随移植:关键词过滤
    // 属调用方关注点,重复合并恒开(即 C# mergeDuplicate=true 路径);
    // totalCount / filteredCount / mergedCount 出参未随移植。
    static QList<DanmakuEntry> parseXml(const QString &xml);
};

#endif  // DANMAKU_PARSER_H
