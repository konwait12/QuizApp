#include "services/LegacyBankImporter.h"

#include "domain/QuestionIdentity.h"

#include <QCryptographicHash>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <optional>

namespace quizapp::services {
namespace {

const QUuid kBankNamespace(QStringLiteral("d7f5f3d5-df20-59e8-a13d-b67c3f5204dd"));
const QUuid kSubjectNamespace(QStringLiteral("6df0cc6d-4cb4-5270-843a-d63269d4d20e"));

enum class BooleanValue {
    Unknown,
    True,
    False,
};

struct BooleanOptions {
    bool valid = false;
    int trueIndex = -1;
    int falseIndex = -1;
};

void addDiagnostic(
    domain::BankImportResult *result,
    domain::ImportDiagnosticSeverity severity,
    const QString &code,
    const QString &message,
    qsizetype questionIndex = -1,
    const QString &sourceQuestionId = {})
{
    result->diagnostics.append(domain::ImportDiagnostic{
        severity,
        code,
        message,
        questionIndex,
        sourceQuestionId,
    });
}

QString cleanText(QString value)
{
    for (const char16_t character : {u'\u200B', u'\u200C', u'\u200D', u'\uFEFF'}) {
        value.remove(QChar(character));
    }
    value.remove(QRegularExpression(QStringLiteral("[─━]{5,}")));
    return value.simplified().trimmed();
}

QJsonValue firstValue(const QJsonObject &object, const QStringList &keys)
{
    for (const QString &key : keys) {
        const auto found = object.constFind(key);
        if (found != object.constEnd() && !found->isNull() && !found->isUndefined()) {
            return *found;
        }
    }
    return {};
}

QString jsonText(const QJsonValue &value)
{
    if (value.isString()) {
        return value.toString();
    }
    if (value.isDouble()) {
        return QString::number(value.toDouble(), 'g', 15);
    }
    if (value.isBool()) {
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        return jsonText(firstValue(object, {
            QStringLiteral("text"),
            QStringLiteral("value"),
            QStringLiteral("label"),
            QStringLiteral("content"),
        }));
    }
    if (value.isArray()) {
        QStringList parts;
        for (const QJsonValue &entry : value.toArray()) {
            const QString part = jsonText(entry).trimmed();
            if (!part.isEmpty()) {
                parts.append(part);
            }
        }
        return parts.join(QStringLiteral(","));
    }
    return {};
}

QString stripOptionLabel(const QString &value)
{
    QString result = value.trimmed();
    result.remove(QRegularExpression(
        QStringLiteral("^\\s*[A-Ha-h](?:[.．、]|\\s+)\\s*")));
    return cleanText(result);
}

QStringList splitOptionBlock(const QString &value)
{
    QString text = value;
    text.replace(u'\r', u'\n');
    for (const char16_t character : {u'\u200B', u'\u200C', u'\u200D', u'\uFEFF'}) {
        text.remove(QChar(character));
    }
    const QRegularExpression expression(QStringLiteral(
        "(?:^|\\n)\\s*[A-Ha-h](?:[.．、]|\\s+)\\s*([\\s\\S]*?)"
        "(?=\\n\\s*[A-Ha-h](?:[.．、]|\\s+)\\s*|$)"));
    QStringList options;
    auto matches = expression.globalMatch(text);
    while (matches.hasNext()) {
        const QString option = stripOptionLabel(matches.next().captured(1));
        if (!option.isEmpty()) {
            options.append(option);
        }
    }
    return options;
}

QStringList normalizeOptions(const QJsonValue &value)
{
    QVector<QJsonValue> entries;
    if (value.isArray()) {
        for (const QJsonValue &entry : value.toArray()) {
            entries.append(entry);
        }
    } else if (value.isObject()) {
        const QJsonObject object = value.toObject();
        QStringList keys = object.keys();
        std::sort(keys.begin(), keys.end(), [](const QString &left, const QString &right) {
            return left.compare(right, Qt::CaseInsensitive) < 0;
        });
        for (const QString &key : keys) {
            entries.append(object.value(key));
        }
    }

    QStringList options;
    for (const QJsonValue &entry : entries) {
        const QString text = jsonText(entry).trimmed();
        if (text.isEmpty()) {
            continue;
        }
        const QStringList split = splitOptionBlock(text);
        if (split.size() > 1) {
            options.append(split);
        } else {
            options.append(stripOptionLabel(text));
        }
    }
    if (options.size() == 1) {
        const QStringList split = splitOptionBlock(options.constFirst());
        if (split.size() > 1) {
            return split;
        }
    }
    options.removeAll(QString());
    return options;
}

BooleanValue booleanValue(const QString &value)
{
    const QString normalized = stripOptionLabel(value).remove(u' ').toLower();
    const QSet<QString> trueValues{
        QStringLiteral("对"), QStringLiteral("正确"), QStringLiteral("是"),
        QStringLiteral("true"), QStringLiteral("t"), QStringLiteral("yes"),
        QStringLiteral("y"), QStringLiteral("1"), QStringLiteral("√"),
        QStringLiteral("✓"),
    };
    const QSet<QString> falseValues{
        QStringLiteral("错"), QStringLiteral("错误"), QStringLiteral("否"),
        QStringLiteral("false"), QStringLiteral("f"), QStringLiteral("no"),
        QStringLiteral("n"), QStringLiteral("0"), QStringLiteral("×"),
        QStringLiteral("✗"),
    };
    if (trueValues.contains(normalized)) {
        return BooleanValue::True;
    }
    if (falseValues.contains(normalized)) {
        return BooleanValue::False;
    }
    return BooleanValue::Unknown;
}

BooleanOptions inspectBooleanOptions(const QStringList &options)
{
    BooleanOptions result;
    if (options.size() != 2) {
        return result;
    }
    for (int index = 0; index < options.size(); ++index) {
        switch (booleanValue(options.at(index))) {
        case BooleanValue::True:
            result.trueIndex = index;
            break;
        case BooleanValue::False:
            result.falseIndex = index;
            break;
        case BooleanValue::Unknown:
            return {};
        }
    }
    result.valid = result.trueIndex >= 0 && result.falseIndex >= 0
        && result.trueIndex != result.falseIndex;
    return result;
}

std::optional<domain::QuestionType> declaredType(const QString &value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized.contains(QStringLiteral("主观"))
        || normalized.contains(QStringLiteral("填空"))
        || normalized.contains(QStringLiteral("解答"))
        || normalized == QStringLiteral("subjective")) {
        return domain::QuestionType::Subjective;
    }
    if (normalized.contains(QStringLiteral("多选"))
        || normalized == QStringLiteral("multi")
        || normalized == QStringLiteral("multiple")) {
        return domain::QuestionType::Multiple;
    }
    if (normalized.contains(QStringLiteral("判断"))
        || normalized == QStringLiteral("bool")
        || normalized == QStringLiteral("tf")
        || normalized == QStringLiteral("truefalse")) {
        return domain::QuestionType::Boolean;
    }
    if (normalized.contains(QStringLiteral("单选"))
        || normalized == QStringLiteral("single")) {
        return domain::QuestionType::Single;
    }
    return std::nullopt;
}

QString normalizedChoiceAnswer(QString value, bool *hadDuplicates)
{
    value = value.trimmed();
    value.remove(QRegularExpression(
        QStringLiteral("^(?:正确答案|参考答案|答案|correct\\s+answer|answer)\\s*[:：]?\\s*"),
        QRegularExpression::CaseInsensitiveOption));
    const QString upper = value.toUpper();
    QString answer;
    QSet<QChar> seen;
    *hadDuplicates = false;
    for (const QChar character : upper) {
        if (character < u'A' || character > u'Z') {
            continue;
        }
        if (seen.contains(character)) {
            *hadDuplicates = true;
            continue;
        }
        seen.insert(character);
        answer.append(character);
    }
    std::sort(answer.begin(), answer.end());
    return answer;
}

QStringList imageSources(const QJsonValue &value)
{
    if (value.isArray()) {
        QStringList result;
        for (const QJsonValue &entry : value.toArray()) {
            const QString source = jsonText(entry).trimmed();
            if (!source.isEmpty()) {
                result.append(source);
            }
        }
        return result;
    }
    const QString text = jsonText(value).trimmed();
    if (text.isEmpty()) {
        return {};
    }
    if (text.startsWith(u'[')) {
        const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8());
        if (document.isArray()) {
            return imageSources(document.array());
        }
    }
    return {text};
}

QJsonArray questionArray(const QJsonDocument &document, const QJsonObject &root)
{
    if (document.isArray()) {
        return document.array();
    }
    const QJsonValue direct = firstValue(root, {
        QStringLiteral("questions"),
        QStringLiteral("data"),
        QStringLiteral("items"),
    });
    if (direct.isArray()) {
        return direct.toArray();
    }
    for (const QString &key : root.keys()) {
        if (root.value(key).isArray()) {
            return root.value(key).toArray();
        }
    }
    return {};
}

QStringList bankPath(const QJsonObject &root, const QString &fallbackName)
{
    const QJsonValue rawPath = firstValue(root, {
        QStringLiteral("path"), QStringLiteral("路径"),
    });
    QStringList path;
    if (rawPath.isArray()) {
        for (const QJsonValue &entry : rawPath.toArray()) {
            const QString segment = cleanText(jsonText(entry));
            if (!segment.isEmpty()) {
                path.append(segment);
            }
        }
    }
    if (!path.isEmpty()) {
        return path;
    }

    QString subject = cleanText(jsonText(firstValue(root, {
        QStringLiteral("subject"), QStringLiteral("科目"),
    })));
    QString chapter = cleanText(jsonText(firstValue(root, {
        QStringLiteral("chapter"), QStringLiteral("章节"),
    })));
    if (subject.isEmpty()) {
        const qsizetype separator = fallbackName.indexOf(u'-');
        if (separator > 0) {
            subject = fallbackName.left(separator).trimmed();
            if (chapter.isEmpty()) {
                chapter = fallbackName.mid(separator + 1).trimmed();
            }
        }
    }
    if (subject.isEmpty()) {
        subject = QStringLiteral("未分类");
    }
    if (chapter.isEmpty()) {
        chapter = QStringLiteral("综合练习");
    }
    return {subject, chapter};
}

QVector<domain::SubjectNode> subjectNodes(const QStringList &path)
{
    QVector<domain::SubjectNode> nodes;
    QString parentId;
    QStringList prefix;
    for (qsizetype index = 0; index < path.size(); ++index) {
        prefix.append(path.at(index));
        const QString id = QUuid::createUuidV5(
            kSubjectNamespace,
            prefix.join(u'/').normalized(QString::NormalizationForm_KC).toUtf8())
                               .toString(QUuid::WithoutBraces);
        nodes.append(domain::SubjectNode{
            id,
            parentId,
            path.at(index),
            QString(),
            static_cast<int>(index),
        });
        parentId = id;
    }
    return nodes;
}

domain::BuiltinExplanation builtinExplanation(const QJsonObject &question)
{
    domain::BuiltinExplanation explanation;
    const QJsonValue explanations = question.value(QStringLiteral("explanations"));
    QJsonObject structured;
    if (explanations.isObject()) {
        structured = explanations.toObject().value(QStringLiteral("builtin")).toObject();
    }
    if (structured.isEmpty()) {
        structured = question.value(QStringLiteral("builtinExplanation")).toObject();
    }
    explanation.text = cleanText(jsonText(firstValue(structured, {
        QStringLiteral("text"), QStringLiteral("content"),
    })));
    if (explanation.text.isEmpty()) {
        explanation.text = cleanText(jsonText(firstValue(question, {
            QStringLiteral("exp"), QStringLiteral("explanation"),
            QStringLiteral("解析"), QStringLiteral("note"),
        })));
    }
    explanation.videoUrl = jsonText(firstValue(structured, {
        QStringLiteral("videoUrl"), QStringLiteral("video_url"),
    })).trimmed();
    if (explanation.videoUrl.isEmpty()) {
        explanation.videoUrl = jsonText(firstValue(question, {
            QStringLiteral("videoUrl"), QStringLiteral("video_url"),
        })).trimmed();
    }
    const QJsonObject source = structured.value(QStringLiteral("source")).toObject();
    explanation.provider = jsonText(firstValue(source, {
        QStringLiteral("provider"), QStringLiteral("name"),
    })).trimmed();
    explanation.sourceId = jsonText(firstValue(source, {
        QStringLiteral("id"), QStringLiteral("sourceId"),
    })).trimmed();
    return explanation;
}

QStringList explanationImageSources(const QJsonObject &question)
{
    QJsonObject structured;
    if (question.value(QStringLiteral("explanations")).isObject()) {
        structured = question.value(QStringLiteral("explanations"))
                         .toObject().value(QStringLiteral("builtin")).toObject();
    }
    if (structured.isEmpty()) {
        structured = question.value(QStringLiteral("builtinExplanation")).toObject();
    }
    QJsonValue images = firstValue(structured, {
        QStringLiteral("images"), QStringLiteral("imageUrls"),
    });
    if (images.isUndefined() || images.isNull()) {
        images = firstValue(question, {
            QStringLiteral("answerImages"), QStringLiteral("answer_image_url"),
        });
    }
    return imageSources(images);
}

} // namespace

domain::BankImportResult LegacyBankImporter::importJson(
    const QByteArray &json,
    const QString &sourceKey,
    const QStringList &pathOverride) const
{
    domain::BankImportResult result;
    const QString canonicalSourceKey = QString(sourceKey).replace(u'\\', u'/').trimmed();
    if (canonicalSourceKey.isEmpty()) {
        addDiagnostic(
            &result, domain::ImportDiagnosticSeverity::Error,
            QStringLiteral("bank.source_key_missing"),
            QStringLiteral("题库来源路径不能为空"));
        result.rejectedQuestionCount = 1;
        return result;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError
        || (!document.isObject() && !document.isArray())) {
        addDiagnostic(
            &result, domain::ImportDiagnosticSeverity::Error,
            QStringLiteral("bank.json_invalid"),
            QStringLiteral("JSON 无法解析：%1").arg(parseError.errorString()));
        result.rejectedQuestionCount = 1;
        return result;
    }

    const QJsonObject root = document.isObject() ? document.object() : QJsonObject();
    const QString fallbackName = QFileInfo(canonicalSourceKey).completeBaseName();
    QString title = cleanText(jsonText(firstValue(root, {
        QStringLiteral("name"), QStringLiteral("title"), QStringLiteral("科目"),
    })));
    if (title.isEmpty()) {
        title = fallbackName;
    }
    const QStringList path = pathOverride.isEmpty() ? bankPath(root, title) : pathOverride;
    const QJsonArray rawQuestions = questionArray(document, root);
    result.sourceQuestionCount = rawQuestions.size();
    if (rawQuestions.isEmpty()) {
        addDiagnostic(
            &result, domain::ImportDiagnosticSeverity::Error,
            QStringLiteral("bank.questions_missing"),
            QStringLiteral("题库没有可读取的题目数组"));
        result.rejectedQuestionCount = 1;
        return result;
    }

    domain::BankImportPackage package;
    package.subjects = subjectNodes(path);
    package.bank.id = QUuid::createUuidV5(
                          kBankNamespace,
                          canonicalSourceKey.normalized(QString::NormalizationForm_KC).toUtf8())
                          .toString(QUuid::WithoutBraces);
    package.bank.subjectId = package.subjects.constLast().id;
    package.bank.title = title;
    const QJsonValue bankSource = root.value(QStringLiteral("source"));
    package.bank.sourceProvider = bankSource.isObject()
        ? cleanText(jsonText(firstValue(bankSource.toObject(), {
              QStringLiteral("provider"), QStringLiteral("name"),
          })))
        : cleanText(jsonText(bankSource));
    if (package.bank.sourceProvider.isEmpty()) {
        package.bank.sourceProvider = QStringLiteral("legacy-json");
    }
    package.bank.sourceId = canonicalSourceKey;
    package.bank.distributionVersion = cleanText(jsonText(firstValue(root, {
        QStringLiteral("releaseVersion"), QStringLiteral("version"),
    })));

    QSet<QUuid> questionIds;
    for (qsizetype index = 0; index < rawQuestions.size(); ++index) {
        if (!rawQuestions.at(index).isObject()) {
            addDiagnostic(
                &result, domain::ImportDiagnosticSeverity::Error,
                QStringLiteral("question.not_object"),
                QStringLiteral("题目不是 JSON 对象"), index);
            ++result.rejectedQuestionCount;
            continue;
        }
        const QJsonObject raw = rawQuestions.at(index).toObject();
        const QString rawId = cleanText(jsonText(firstValue(raw, {
            QStringLiteral("id"), QStringLiteral("sourceId"),
        })));
        const QString prompt = cleanText(jsonText(firstValue(raw, {
            QStringLiteral("q"), QStringLiteral("question"),
            QStringLiteral("题目"), QStringLiteral("title"),
            QStringLiteral("stem"),
        })));
        if (prompt.isEmpty()) {
            addDiagnostic(
                &result, domain::ImportDiagnosticSeverity::Error,
                QStringLiteral("question.prompt_missing"),
                QStringLiteral("题干为空"), index, rawId);
            ++result.rejectedQuestionCount;
            continue;
        }

        const QStringList options = normalizeOptions(firstValue(raw, {
            QStringLiteral("options"), QStringLiteral("选项"),
        }));
        const QString rawAnswer = jsonText(firstValue(raw, {
            QStringLiteral("ans"), QStringLiteral("answer"),
            QStringLiteral("答案"), QStringLiteral("correct"),
            QStringLiteral("rawAns"),
        })).trimmed();
        const QString declaredText = jsonText(firstValue(raw, {
            QStringLiteral("type"), QStringLiteral("question_type"),
            QStringLiteral("题型"),
        }));
        const auto declared = declaredType(declaredText);
        bool duplicateAnswerLetters = false;
        QString answer = normalizedChoiceAnswer(rawAnswer, &duplicateAnswerLetters);
        const BooleanOptions booleanOptions = inspectBooleanOptions(options);

        domain::QuestionType type = domain::QuestionType::Single;
        bool rejected = false;
        if (declared == domain::QuestionType::Subjective) {
            type = domain::QuestionType::Subjective;
            answer = cleanText(rawAnswer);
        } else if (answer.size() > 1) {
            type = domain::QuestionType::Multiple;
        } else if (booleanOptions.valid) {
            type = domain::QuestionType::Boolean;
        } else if (declared == domain::QuestionType::Boolean) {
            addDiagnostic(
                &result, domain::ImportDiagnosticSeverity::Error,
                QStringLiteral("question.boolean_options_invalid"),
                QStringLiteral("判断题必须有一真一假两个可识别选项"), index, rawId);
            rejected = true;
        } else if (declared == domain::QuestionType::Multiple) {
            type = domain::QuestionType::Multiple;
        } else {
            type = domain::QuestionType::Single;
        }

        QStringList normalizedOptionList = options;
        if (!rejected && type == domain::QuestionType::Boolean) {
            BooleanValue rawBoolean = booleanValue(rawAnswer);
            if (rawBoolean == BooleanValue::Unknown && answer.size() == 1) {
                const int optionIndex = answer.at(0).unicode() - u'A';
                if (optionIndex == booleanOptions.trueIndex) {
                    rawBoolean = BooleanValue::True;
                } else if (optionIndex == booleanOptions.falseIndex) {
                    rawBoolean = BooleanValue::False;
                }
            }
            if (rawBoolean == BooleanValue::Unknown) {
                addDiagnostic(
                    &result, domain::ImportDiagnosticSeverity::Error,
                    QStringLiteral("question.boolean_answer_invalid"),
                    QStringLiteral("判断题答案无法映射为对或错"), index, rawId);
                rejected = true;
            } else {
                answer = rawBoolean == BooleanValue::True
                    ? QStringLiteral("A") : QStringLiteral("B");
                normalizedOptionList = {QStringLiteral("对"), QStringLiteral("错")};
            }
        }

        if (!rejected && type != domain::QuestionType::Subjective) {
            if (normalizedOptionList.size() < 2) {
                addDiagnostic(
                    &result, domain::ImportDiagnosticSeverity::Error,
                    QStringLiteral("question.options_missing"),
                    QStringLiteral("客观题至少需要两个选项"), index, rawId);
                rejected = true;
            } else if (answer.isEmpty()) {
                const bool xiaoyiExplanationAvailable =
                    package.bank.sourceProvider == QStringLiteral("xiaoyivip")
                    && !explanationImageSources(raw).isEmpty();
                if (xiaoyiExplanationAvailable) {
                    addDiagnostic(
                        &result, domain::ImportDiagnosticSeverity::Warning,
                        QStringLiteral("question.answer_unavailable"),
                        QStringLiteral("来源未提供可机器判分答案，已保留题型并提供内置解析"),
                        index, rawId);
                } else {
                    addDiagnostic(
                        &result, domain::ImportDiagnosticSeverity::Error,
                        QStringLiteral("question.answer_missing"),
                        QStringLiteral("客观题答案为空"), index, rawId);
                    rejected = true;
                }
            } else {
                for (const QChar character : answer) {
                    if (character < u'A'
                        || character.unicode() - u'A' >= normalizedOptionList.size()) {
                        addDiagnostic(
                            &result, domain::ImportDiagnosticSeverity::Error,
                            QStringLiteral("question.answer_out_of_range"),
                            QStringLiteral("答案字母超出选项范围"), index, rawId);
                        rejected = true;
                        break;
                    }
                }
            }
        }
        QSet<QString> uniqueOptions;
        for (const QString &option : normalizedOptionList) {
            const QString key = option.simplified().toCaseFolded();
            if (!key.isEmpty() && uniqueOptions.contains(key)) {
                addDiagnostic(
                    &result, domain::ImportDiagnosticSeverity::Error,
                    QStringLiteral("question.options_duplicate"),
                    QStringLiteral("题目包含重复选项"), index, rawId);
                rejected = true;
                break;
            }
            uniqueOptions.insert(key);
        }
        if (rejected) {
            ++result.rejectedQuestionCount;
            continue;
        }

        if (duplicateAnswerLetters) {
            addDiagnostic(
                &result, domain::ImportDiagnosticSeverity::Warning,
                QStringLiteral("question.answer_deduplicated"),
                QStringLiteral("重复答案字母已去重"), index, rawId);
        }
        if (declared.has_value() && declared.value() != type) {
            addDiagnostic(
                &result, domain::ImportDiagnosticSeverity::Warning,
                QStringLiteral("question.type_repaired"),
                QStringLiteral("声明题型与答案或选项冲突，已按高置信度规则修正"),
                index, rawId);
            ++result.repairedQuestionCount;
        } else if (!declared.has_value()) {
            addDiagnostic(
                &result, domain::ImportDiagnosticSeverity::Warning,
                QStringLiteral("question.type_inferred"),
                QStringLiteral("题型缺失，已根据答案和选项推断"), index, rawId);
            ++result.repairedQuestionCount;
        } else if (type == domain::QuestionType::Multiple && answer.size() == 1) {
            addDiagnostic(
                &result, domain::ImportDiagnosticSeverity::Warning,
                QStringLiteral("question.multiple_single_answer"),
                QStringLiteral("多选题目前只有一个答案字母，请复核题库来源"), index, rawId);
        }

        domain::Question question;
        question.bankId = package.bank.id;
        question.sourceProvider = package.bank.sourceProvider;
        question.sourceId = rawId.isEmpty()
            ? QString()
            : canonicalSourceKey + u'/' + rawId;
        question.path = path;
        question.type = type;
        question.prompt = prompt;
        question.options = normalizedOptionList;
        question.correctAnswer = answer;
        question.sourceOrder = static_cast<int>(index);
        question.builtinExplanation = builtinExplanation(raw);
        question.updatedAt = QDateTime::currentDateTimeUtc();
        question.id = domain::QuestionIdentity::create(
            question.sourceProvider,
            question.sourceId,
            question.path,
            question.prompt,
            question.options);
        if (questionIds.contains(question.id)) {
            addDiagnostic(
                &result, domain::ImportDiagnosticSeverity::Error,
                QStringLiteral("question.identity_duplicate"),
                QStringLiteral("题目稳定 ID 重复，请检查来源题号"), index, rawId);
            ++result.rejectedQuestionCount;
            continue;
        }
        questionIds.insert(question.id);

        const QStringList questionMedia = imageSources(firstValue(raw, {
            QStringLiteral("questionImages"), QStringLiteral("questionImageUrls"),
            QStringLiteral("question_image_url"),
        }));
        const QStringList explanationMedia = explanationImageSources(raw);
        QCryptographicHash contentHash(QCryptographicHash::Sha256);
        contentHash.addData(domain::QuestionIdentity::contentHash(question));
        for (const QString &source : questionMedia) {
            contentHash.addData(source.toUtf8());
        }
        for (const QString &source : explanationMedia) {
            contentHash.addData(source.toUtf8());
        }
        question.contentHash = contentHash.result();

        for (qsizetype mediaIndex = 0; mediaIndex < questionMedia.size(); ++mediaIndex) {
            result.pendingMedia.append(domain::PendingMediaReference{
                question.id,
                domain::ImportedMediaRole::Question,
                questionMedia.at(mediaIndex),
                static_cast<int>(mediaIndex),
            });
        }
        for (qsizetype mediaIndex = 0; mediaIndex < explanationMedia.size(); ++mediaIndex) {
            result.pendingMedia.append(domain::PendingMediaReference{
                question.id,
                domain::ImportedMediaRole::Explanation,
                explanationMedia.at(mediaIndex),
                static_cast<int>(mediaIndex),
            });
        }

        package.bank.questions.append(question);
        ++result.acceptedQuestionCount;
    }

    if (result.rejectedQuestionCount > 0) {
        addDiagnostic(
            &result, domain::ImportDiagnosticSeverity::Error,
            QStringLiteral("bank.rejected_questions"),
            QStringLiteral("题库包含 %1 道无法安全导入的题目，整库未提交")
                .arg(result.rejectedQuestionCount));
        return result;
    }

    QCryptographicHash bankHash(QCryptographicHash::Sha256);
    bankHash.addData(package.bank.id.toUtf8());
    bankHash.addData(package.bank.title.toUtf8());
    for (const domain::Question &question : package.bank.questions) {
        bankHash.addData(question.id.toRfc4122());
        bankHash.addData(question.contentHash);
    }
    package.bank.contentHash = bankHash.result();
    result.package = std::move(package);
    return result;
}

} // namespace quizapp::services
