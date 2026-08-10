// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — i modelli della riduzione neurale (IMPL-001 §6).
//
// Un modello è un file, e la promessa del progetto è che sia **sostituibile**:
// chi allena un modello sulle proprie bande lo mette nella cartella e lo trova
// nell'elenco. Perché quella promessa valga davvero servono due cose che
// sembrano dettagli e non lo sono.
//
// La prima è che l'elenco dica **dove** guarda. Una cartella nascosta dentro i
// dati applicativi è il posto giusto per starci, ed è il posto sbagliato per
// trovarla: chi ha un file da mettere deve poter leggere il percorso.
//
// La seconda è che un file rotto non diventi una voce nell'elenco. Un modello
// che non si apre va detto adesso, non quando l'operatore lo sceglie e lo
// stadio resta muto senza spiegazioni.
#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QVector>

namespace dsdr::dsp::neural {

/// Un modello trovato su disco.
struct NrModel
{
    QString name;      ///< il nome del file, senza estensione
    QString path;
    qint64 bytes = 0;
    QString engineId;  ///< a quale motore appartiene: "dfn3", "rnnoise"
    bool usable = true;
    QString problem;   ///< perché non è usabile, se non lo è
};

class ModelStore : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString directory READ directory CONSTANT)
    Q_PROPERTY(int count READ rowCount NOTIFY changed)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        PathRole,
        EngineRole,
        UsableRole,
        ProblemRole,
        SizeRole,
    };

    explicit ModelStore(QObject *parent = nullptr);

    /// La cartella in cui si cercano i modelli. Esposta perché chi ne ha uno
    /// da mettere deve sapere dove.
    QString directory() const { return m_directory; }

    /// Rilegge la cartella. Chiamabile dalla UI: chi copia un file mentre
    /// l'applicazione è aperta non deve riavviarla per vederlo.
    Q_INVOKABLE void refresh();

    /// Il percorso del modello scelto, vuoto se non ce n'è.
    Q_INVOKABLE QString pathAt(int row) const;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    /// Riconosce a quale motore appartiene un file dal nome e
    /// dall'estensione. Statica e pura: è una regola, e va potuta verificare
    /// senza un disco davanti.
    static QString engineForFile(const QString &fileName);

signals:
    void changed();

private:
    QString m_directory;
    QVector<NrModel> m_models;
};

} // namespace dsdr::dsp::neural
