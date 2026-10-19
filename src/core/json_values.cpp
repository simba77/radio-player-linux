#include "json_values.hpp"

#include <QJsonArray>
#include <QJsonObject>

#include <cmath>
#include <limits>

namespace radio::json {

std::optional<qint64> integer(const QJsonValue &value)
{
    if (!value.isDouble())
        return std::nullopt;
    const double number = value.toDouble();
    // За пределами qint64 приведение — неопределённое поведение, а не число.
    constexpr double limit = 9.2e18;
    if (!std::isfinite(number) || std::trunc(number) != number || std::abs(number) > limit)
        return std::nullopt;
    return static_cast<qint64>(number);
}

QString text(const QJsonValue &value)
{
    if (value.isString())
        return value.toString();
    if (const auto number = integer(value))
        return *number == 0 ? QString() : QString::number(*number);
    if (value.isDouble())
        return value.toDouble() == 0.0 ? QString() : QString::number(value.toDouble());
    return {};
}

bool truthy(const QJsonValue &value)
{
    switch (value.type()) {
    case QJsonValue::Bool:
        return value.toBool();
    case QJsonValue::Double:
        return value.toDouble() != 0.0;
    case QJsonValue::String:
        return !value.toString().isEmpty();
    case QJsonValue::Array:
        return !value.toArray().isEmpty();
    case QJsonValue::Object:
        return !value.toObject().isEmpty();
    case QJsonValue::Null:
    case QJsonValue::Undefined:
        return false;
    }
    return false;
}

}  // namespace radio::json
