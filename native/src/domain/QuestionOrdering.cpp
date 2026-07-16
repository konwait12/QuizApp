#include "domain/QuestionOrdering.h"

namespace quizapp::domain {
namespace {

int chineseDigit(QChar character)
{
    const QString digits = QStringLiteral("零一二三四五六七八九");
    return digits.indexOf(character);
}

int parseChineseNumber(const QString &text)
{
    bool arabicOk = false;
    const int arabic = text.toInt(&arabicOk);
    if (arabicOk) {
        return arabic;
    }

    int total = 0;
    int current = 0;
    for (QChar character : text) {
        const int digit = chineseDigit(character);
        if (digit >= 0) {
            current = digit;
            continue;
        }
        int unit = 0;
        if (character == QChar(u'十')) {
            unit = 10;
        } else if (character == QChar(u'百')) {
            unit = 100;
        } else if (character == QChar(u'千')) {
            unit = 1000;
        } else {
            return -1;
        }
        total += (current == 0 ? 1 : current) * unit;
        current = 0;
    }
    return total + current;
}

int chapterNumber(const QString &title)
{
    if (!title.startsWith(QChar(u'第'))) {
        return -1;
    }
    const qsizetype chapterEnd = title.indexOf(QChar(u'章'));
    const qsizetype lessonEnd = title.indexOf(QChar(u'讲'));
    qsizetype end = chapterEnd;
    if (end < 0 || (lessonEnd >= 0 && lessonEnd < end)) {
        end = lessonEnd;
    }
    return end > 1 ? parseChineseNumber(title.mid(1, end - 1)) : -1;
}

int sectionRank(const QString &title)
{
    if (title.contains(QStringLiteral("选择"))) {
        return 0;
    }
    if (title.contains(QStringLiteral("填空"))) {
        return 1;
    }
    if (title.contains(QStringLiteral("判断"))) {
        return 2;
    }
    if (title.contains(QStringLiteral("解答"))) {
        return 3;
    }
    return 4;
}

} // namespace

bool naturalLibraryTitleLess(const QString &left, const QString &right)
{
    const int leftChapter = chapterNumber(left);
    const int rightChapter = chapterNumber(right);
    if (leftChapter >= 0 && rightChapter >= 0 && leftChapter != rightChapter) {
        return leftChapter < rightChapter;
    }
    const int leftRank = sectionRank(left);
    const int rightRank = sectionRank(right);
    if (leftRank != rightRank && (leftRank < 4 || rightRank < 4)) {
        return leftRank < rightRank;
    }
    return QString::localeAwareCompare(left, right) < 0;
}

bool sourceQuestionLess(const Question &left, const Question &right)
{
    const qsizetype commonSize = qMin(left.path.size(), right.path.size());
    for (qsizetype index = 0; index < commonSize; ++index) {
        if (left.path.at(index) == right.path.at(index)) {
            continue;
        }
        return naturalLibraryTitleLess(left.path.at(index), right.path.at(index));
    }
    if (left.path.size() != right.path.size()) {
        return left.path.size() < right.path.size();
    }
    if (left.sourceOrder != right.sourceOrder) {
        return left.sourceOrder < right.sourceOrder;
    }
    return left.id.toString(QUuid::WithoutBraces)
        < right.id.toString(QUuid::WithoutBraces);
}

} // namespace quizapp::domain
