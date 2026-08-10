// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/backends/flex/FlexProtocol.h"

namespace dsdr::hal::flex {

Line parseLine(const QString &raw)
{
    Line line;
    const QString text = raw.trimmed();
    if (text.isEmpty())
        return line;

    const QChar tag = text.at(0);
    const QString rest = text.mid(1);

    if (tag == QLatin1Char('V')) {
        line.kind = LineKind::Version;
        line.payload = rest;
        return line;
    }

    if (tag == QLatin1Char('H')) {
        line.kind = LineKind::Handle;
        line.handle = rest;
        return line;
    }

    const QStringList parts = rest.split(QLatin1Char('|'));

    if (tag == QLatin1Char('R') && parts.size() >= 2) {
        line.kind = LineKind::Response;
        line.sequence = parts.at(0).toUInt();
        // Il codice è **esadecimale**. Letto in decimale, `50000015` resta un
        // numero plausibile e il comando fallito sembrerebbe riuscito.
        line.code = parts.at(1).toUInt(nullptr, 16);
        line.payload = parts.mid(2).join(QLatin1Char('|'));
        return line;
    }

    if (tag == QLatin1Char('S') && parts.size() >= 2) {
        line.kind = LineKind::Status;
        line.handle = parts.at(0);
        line.payload = parts.mid(1).join(QLatin1Char('|'));
        return line;
    }

    if (tag == QLatin1Char('M') && parts.size() >= 2) {
        line.kind = LineKind::Message;
        line.code = parts.at(0).toUInt(nullptr, 16);
        line.payload = parts.mid(1).join(QLatin1Char('|'));
        return line;
    }

    return line;
}

QString buildCommand(quint32 sequence, const QString &command)
{
    return QStringLiteral("C%1|%2\n").arg(sequence).arg(command);
}

QHash<QString, QString> parseFields(const QString &payload)
{
    QHash<QString, QString> fields;
    const QStringList tokens = payload.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString &token : tokens) {
        const int equals = token.indexOf(QLatin1Char('='));
        if (equals <= 0)
            continue;
        fields.insert(token.left(equals), token.mid(equals + 1));
    }
    return fields;
}

QString describeRadio(const QHash<QString, QString> &fields)
{
    const QString model = fields.value(QStringLiteral("model"));
    const QString nickname = fields.value(QStringLiteral("nickname"));
    const QString callsign = fields.value(QStringLiteral("callsign"));

    QStringList parts;
    if (!model.isEmpty())
        parts << model;
    // Il soprannome e il nominativo sono quelli che l'operatore ha scritto
    // nella radio: in una stazione con due Flex sono l'unico modo di sapere
    // quale delle due si sta guardando.
    if (!nickname.isEmpty() && nickname != model)
        parts << nickname;
    if (!callsign.isEmpty())
        parts << callsign;
    return parts.join(QStringLiteral(" · "));
}

} // namespace dsdr::hal::flex
