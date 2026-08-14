#pragma once

#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include <cmath>
#include <utility>

namespace agplayer::voice_clone {

class CapabilityValidationResult {
public:
    bool isValid() const { return error_.isEmpty(); }
    QString errorString() const { return error_; }
    QStringList groups() const { return groups_; }
    QStringList controlTypes() const { return controlTypes_; }

    static CapabilityValidationResult failure(QString error)
    {
        CapabilityValidationResult result;
        result.error_ = std::move(error);
        return result;
    }

    void addGroup(const QString& group) { groups_.append(group); }
    void addControlType(const QString& type)
    {
        if (!controlTypes_.contains(type)) controlTypes_.append(type);
    }

private:
    QString error_;
    QStringList groups_;
    QStringList controlTypes_;
};

namespace detail {

inline bool hasOnlyKeys(const QJsonObject& object,
                        const QSet<QString>& allowed,
                        QString* unknownKey)
{
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (!allowed.contains(it.key())) {
            if (unknownKey) *unknownKey = it.key();
            return false;
        }
    }
    return true;
}

inline bool isInteger(const QJsonValue& value)
{
    return value.isDouble() && std::isfinite(value.toDouble())
           && std::floor(value.toDouble()) == value.toDouble();
}

inline QString validateValue(const QString& key,
                             const QString& type,
                             const QJsonObject& control,
                             const QJsonValue& value,
                             const bool validateFile)
{
    if (type == QStringLiteral("bool")) {
        if (!value.isBool()) return QStringLiteral("%1 must be a bool").arg(key);
        return {};
    }

    if (type == QStringLiteral("enum")) {
        if (!value.isString()) return QStringLiteral("%1 must be an enum string").arg(key);
        const QJsonArray options = control.value(QStringLiteral("options")).toArray();
        if (!options.contains(value)) return QStringLiteral("%1 is not an allowed option").arg(key);
        return {};
    }

    if (type == QStringLiteral("int") || type == QStringLiteral("double")) {
        if (!value.isDouble() || !std::isfinite(value.toDouble())
            || (type == QStringLiteral("int") && !isInteger(value))) {
            return QStringLiteral("%1 has the wrong numeric type").arg(key);
        }
        const double number = value.toDouble();
        if (control.contains(QStringLiteral("minimum"))
            && number < control.value(QStringLiteral("minimum")).toDouble()) {
            return QStringLiteral("%1 is below minimum").arg(key);
        }
        if (control.contains(QStringLiteral("maximum"))
            && number > control.value(QStringLiteral("maximum")).toDouble()) {
            return QStringLiteral("%1 is above maximum").arg(key);
        }
        return {};
    }

    if (type == QStringLiteral("string") || type == QStringLiteral("file")) {
        if (!value.isString()) return QStringLiteral("%1 must be a string").arg(key);
        const QString text = value.toString();
        if (control.contains(QStringLiteral("maximumLength"))
            && text.size() > control.value(QStringLiteral("maximumLength")).toInt()) {
            return QStringLiteral("%1 exceeds maximum length").arg(key);
        }
        if (type == QStringLiteral("file") && validateFile && !text.isEmpty()) {
            const QFileInfo file(text);
            if (!file.exists() || !file.isFile()) {
                return QStringLiteral("%1 file does not exist").arg(key);
            }
            const QJsonArray extensions = control.value(QStringLiteral("extensions")).toArray();
            bool supported = extensions.isEmpty();
            for (const QJsonValue& extension : extensions) {
                if (file.suffix().compare(extension.toString(), Qt::CaseInsensitive) == 0) {
                    supported = true;
                    break;
                }
            }
            if (!supported) return QStringLiteral("%1 has an unsupported extension").arg(key);
        }
        return {};
    }

    return QStringLiteral("%1 uses unsupported control type %2").arg(key, type);
}

} // namespace detail

inline CapabilityValidationResult validateCapabilitySchema(const QJsonObject& schema)
{
    QString unknown;
    if (!detail::hasOnlyKeys(schema,
                             {QStringLiteral("protocolVersion"),
                              QStringLiteral("groups"),
                              QStringLiteral("parameters")},
                             &unknown)) {
        return CapabilityValidationResult::failure(
            QStringLiteral("unknown schema field: %1").arg(unknown));
    }
    if (schema.value(QStringLiteral("protocolVersion")).toInt(-1) != 1) {
        return CapabilityValidationResult::failure(QStringLiteral("unsupported protocol version"));
    }
    if (!schema.value(QStringLiteral("groups")).isArray()
        || !schema.value(QStringLiteral("parameters")).isArray()) {
        return CapabilityValidationResult::failure(QStringLiteral("groups and parameters must be arrays"));
    }

    CapabilityValidationResult result;
    QSet<QString> groups;
    for (const QJsonValue& value : schema.value(QStringLiteral("groups")).toArray()) {
        if (!value.isObject()) {
            return CapabilityValidationResult::failure(QStringLiteral("group must be an object"));
        }
        const QJsonObject group = value.toObject();
        if (!detail::hasOnlyKeys(group,
                                 {QStringLiteral("id"), QStringLiteral("label")},
                                 &unknown)) {
            return CapabilityValidationResult::failure(
                QStringLiteral("unknown group field: %1").arg(unknown));
        }
        const QString id = group.value(QStringLiteral("id")).toString();
        if (id.isEmpty() || group.value(QStringLiteral("label")).toString().isEmpty()
            || groups.contains(id)) {
            return CapabilityValidationResult::failure(QStringLiteral("invalid or duplicate group: %1").arg(id));
        }
        groups.insert(id);
        result.addGroup(id);
    }

    const QJsonArray parameters = schema.value(QStringLiteral("parameters")).toArray();
    QSet<QString> keys;
    QHash<QString, QJsonObject> controls;
    const QSet<QString> supportedTypes = {QStringLiteral("bool"),
                                          QStringLiteral("enum"),
                                          QStringLiteral("int"),
                                          QStringLiteral("double"),
                                          QStringLiteral("string"),
                                          QStringLiteral("file")};
    const QSet<QString> allowedControlKeys = {QStringLiteral("key"),
                                              QStringLiteral("label"),
                                              QStringLiteral("description"),
                                              QStringLiteral("type"),
                                              QStringLiteral("group"),
                                              QStringLiteral("default"),
                                              QStringLiteral("required"),
                                              QStringLiteral("options"),
                                              QStringLiteral("minimum"),
                                              QStringLiteral("maximum"),
                                              QStringLiteral("step"),
                                              QStringLiteral("maximumLength"),
                                              QStringLiteral("extensions"),
                                              QStringLiteral("visibleWhen")};

    for (const QJsonValue& value : parameters) {
        if (!value.isObject()) {
            return CapabilityValidationResult::failure(QStringLiteral("parameter must be an object"));
        }
        const QJsonObject control = value.toObject();
        if (!detail::hasOnlyKeys(control, allowedControlKeys, &unknown)) {
            return CapabilityValidationResult::failure(
                QStringLiteral("unknown parameter field: %1").arg(unknown));
        }
        const QString key = control.value(QStringLiteral("key")).toString();
        const QString type = control.value(QStringLiteral("type")).toString();
        const QString group = control.value(QStringLiteral("group")).toString();
        if (key.isEmpty() || keys.contains(key)) {
            return CapabilityValidationResult::failure(QStringLiteral("invalid or duplicate parameter: %1").arg(key));
        }
        if (!supportedTypes.contains(type)) {
            return CapabilityValidationResult::failure(QStringLiteral("unsupported type for %1").arg(key));
        }
        if (!groups.contains(group)) {
            return CapabilityValidationResult::failure(QStringLiteral("unknown group for %1").arg(key));
        }
        if (!control.contains(QStringLiteral("default"))) {
            return CapabilityValidationResult::failure(QStringLiteral("missing default for %1").arg(key));
        }
        if (control.contains(QStringLiteral("required"))
            && !control.value(QStringLiteral("required")).isBool()) {
            return CapabilityValidationResult::failure(QStringLiteral("required must be bool for %1").arg(key));
        }
        if (type == QStringLiteral("enum")) {
            const QJsonArray options = control.value(QStringLiteral("options")).toArray();
            if (options.isEmpty()) {
                return CapabilityValidationResult::failure(QStringLiteral("missing options for %1").arg(key));
            }
            for (const QJsonValue& option : options) {
                if (!option.isString()) {
                    return CapabilityValidationResult::failure(QStringLiteral("invalid option for %1").arg(key));
                }
            }
        }
        if ((type == QStringLiteral("int") || type == QStringLiteral("double"))) {
            for (const QString& bound : {QStringLiteral("minimum"),
                                         QStringLiteral("maximum"),
                                         QStringLiteral("step")}) {
                if (control.contains(bound)
                    && (!control.value(bound).isDouble()
                        || !std::isfinite(control.value(bound).toDouble()))) {
                    return CapabilityValidationResult::failure(
                        QStringLiteral("invalid %1 for %2").arg(bound, key));
                }
            }
            if (control.contains(QStringLiteral("minimum"))
                && control.contains(QStringLiteral("maximum"))
                && control.value(QStringLiteral("minimum")).toDouble()
                       > control.value(QStringLiteral("maximum")).toDouble()) {
                return CapabilityValidationResult::failure(QStringLiteral("invalid range for %1").arg(key));
            }
            if (control.contains(QStringLiteral("step"))
                && control.value(QStringLiteral("step")).toDouble() <= 0.0) {
                return CapabilityValidationResult::failure(QStringLiteral("invalid step for %1").arg(key));
            }
        }
        if (control.contains(QStringLiteral("maximumLength"))
            && (!detail::isInteger(control.value(QStringLiteral("maximumLength")))
                || control.value(QStringLiteral("maximumLength")).toInt() < 0)) {
            return CapabilityValidationResult::failure(QStringLiteral("invalid maximumLength for %1").arg(key));
        }
        const QString valueError = detail::validateValue(key,
                                                         type,
                                                         control,
                                                         control.value(QStringLiteral("default")),
                                                         false);
        if (!valueError.isEmpty()) {
            return CapabilityValidationResult::failure(
                QStringLiteral("invalid default: %1").arg(valueError));
        }
        keys.insert(key);
        controls.insert(key, control);
        result.addControlType(type);
    }

    for (const QJsonValue& value : parameters) {
        const QJsonObject control = value.toObject();
        if (!control.contains(QStringLiteral("visibleWhen"))) continue;
        const QJsonObject condition = control.value(QStringLiteral("visibleWhen")).toObject();
        if (!detail::hasOnlyKeys(condition,
                                 {QStringLiteral("key"), QStringLiteral("equals")},
                                 &unknown)
            || condition.value(QStringLiteral("key")).toString().isEmpty()
            || !condition.contains(QStringLiteral("equals"))) {
            return CapabilityValidationResult::failure(QStringLiteral("invalid visibleWhen"));
        }
        const QString dependency = condition.value(QStringLiteral("key")).toString();
        if (!controls.contains(dependency)
            || !detail::validateValue(dependency,
                                      controls.value(dependency).value(QStringLiteral("type")).toString(),
                                      controls.value(dependency),
                                      condition.value(QStringLiteral("equals")),
                                      false)
                    .isEmpty()) {
            return CapabilityValidationResult::failure(
                QStringLiteral("invalid visibleWhen dependency: %1").arg(dependency));
        }
    }
    return result;
}

inline CapabilityValidationResult validateParameters(const QJsonObject& schema,
                                                      const QJsonObject& values)
{
    const CapabilityValidationResult schemaValidation = validateCapabilitySchema(schema);
    if (!schemaValidation.isValid()) return schemaValidation;

    QHash<QString, QJsonObject> controls;
    for (const QJsonValue& value : schema.value(QStringLiteral("parameters")).toArray()) {
        const QJsonObject control = value.toObject();
        controls.insert(control.value(QStringLiteral("key")).toString(), control);
    }
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        if (!controls.contains(it.key())) {
            return CapabilityValidationResult::failure(
                QStringLiteral("unknown parameter: %1").arg(it.key()));
        }
    }

    for (auto it = controls.constBegin(); it != controls.constEnd(); ++it) {
        const QString key = it.key();
        const QJsonObject control = it.value();
        bool visible = true;
        if (control.contains(QStringLiteral("visibleWhen"))) {
            const QJsonObject condition = control.value(QStringLiteral("visibleWhen")).toObject();
            const QString dependency = condition.value(QStringLiteral("key")).toString();
            const QJsonValue actual = values.contains(dependency)
                                          ? values.value(dependency)
                                          : controls.value(dependency).value(QStringLiteral("default"));
            visible = actual == condition.value(QStringLiteral("equals"));
        }
        if (!visible && values.contains(key)) {
            return CapabilityValidationResult::failure(
                QStringLiteral("%1 is not visible for the submitted parameters").arg(key));
        }
        if (!visible) continue;
        if (!values.contains(key)) {
            if (control.value(QStringLiteral("required")).toBool(false)) {
                return CapabilityValidationResult::failure(
                    QStringLiteral("required parameter is missing: %1").arg(key));
            }
            continue;
        }
        const QJsonValue value = values.value(key);
        if (control.value(QStringLiteral("required")).toBool(false)
            && value.isString() && value.toString().isEmpty()) {
            return CapabilityValidationResult::failure(
                QStringLiteral("required parameter is empty: %1").arg(key));
        }
        const QString error = detail::validateValue(key,
                                                    control.value(QStringLiteral("type")).toString(),
                                                    control,
                                                    value,
                                                    true);
        if (!error.isEmpty()) return CapabilityValidationResult::failure(error);
    }
    return {};
}

} // namespace agplayer::voice_clone
