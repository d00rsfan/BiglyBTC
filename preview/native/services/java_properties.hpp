// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QMap>
#include <QString>
#include <expected>

namespace biglybtc::services {

using JavaProperties = QMap<QString, QString>;
enum class JavaPropertiesError { InvalidEscape, TooManyEntries, Duplicate };
enum class JavaPropertiesDuplicates { LastWins, Reject };

QStringView nextJavaPropertyLine(QStringView&);
std::expected<JavaProperties, JavaPropertiesError> readJavaProperties(
    QStringView, qsizetype maximumEntries, JavaPropertiesDuplicates);

} // namespace biglybtc::services
