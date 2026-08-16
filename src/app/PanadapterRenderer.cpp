// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/PanadapterRenderer.h"
#include "app/PanadapterView.h"
#include "app/WaterfallPalette.h"
#include "core/SpectrumFeed.h"

#include <QFile>
#include <QLoggingCategory>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <vector>

Q_LOGGING_CATEGORY(dsdrGpu, "dsdr.gpu")

namespace dsdr::app {

namespace {

/// Altezza della texture ad anello: ~20 s di storia a 25 righe/s.
constexpr int kWaterfallRows = 512;

/// Numero massimo di righe consumate in un frame: se la UI è rimasta indietro
/// recuperiamo, ma senza trasformare un frame in un caricamento massiccio.
constexpr int kMaxRowsPerFrame = 8;

/// Densità della griglia in rilievo. Non serve un vertice per bin: la
/// superficie si legge per la forma d'insieme, non per il singolo campione, e
/// una mesh più fitta costerebbe senza aggiungere informazione. Il prodotto
/// resta sotto i 65 536 vertici, così gli indici stanno in 16 bit.
constexpr int kReliefGridWidth = 192;
constexpr int kReliefGridDepth = 96;

/// Quanto è profonda la scena in rilievo, in unità di scena. Con la mesh larga
/// 1.0, questo rapporto dà una superficie che si legge senza schiacciarsi.
constexpr float kReliefDepth = 1.4f;

/// Con che frazione della differenza risale, a ogni riga, la stima del fondo di
/// un bin. A venticinque righe al secondo, un centesimo è una costante di tempo
/// di circa quattro secondi: abbastanza lenta da non seguire una trasmissione,
/// abbastanza svelta da accorgersi che il preselettore è cambiato.
constexpr float kFloorRise = 0.01f;

QShader loadShader(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(dsdrGpu) << "shader non trovato:" << path;
        return QShader();
    }
    return QShader::fromSerialized(file.readAll());
}

} // namespace

PanadapterRenderer::PanadapterRenderer() = default;
PanadapterRenderer::~PanadapterRenderer() = default;

void PanadapterRenderer::initialize(QRhiCommandBuffer *cb)
{
    Q_UNUSED(cb)

    if (m_rhi != rhi()) {
        // Cambio di contesto grafico: tutto ciò che avevamo è inutilizzabile.
        releaseSpectrumResources();
        m_waterfallPipeline.reset();
        m_tracePipeline.reset();
        m_fillPipeline.reset();
        m_traceSrb.reset();
        m_fillSrb.reset();
        m_reliefPipeline.reset();
        m_reliefSrb.reset();
        m_reliefUbuf.reset();
        m_reliefVbuf.reset();
        m_reliefIbuf.reset();
        m_reliefUnavailable = false;
        m_quadVbuf.reset();
        m_waterfallUbuf.reset();
        m_traceUbuf.reset();
        m_fillUbuf.reset();
        m_peakUbuf.reset();
        m_peakSrb.reset();
        m_linearSampler.reset();
        m_colorMapTexture.reset();
        m_uploadedPalette = -1;
        m_quadUploaded = false;
        m_rhi = rhi();
    }

    if (!m_waterfallPipeline)
        createPipelines();
}

bool PanadapterRenderer::createPipelines()
{
    if (!m_rhi)
        return false;

    const QShader waterfallVert = loadShader(QStringLiteral(":/shaders/waterfall.vert.qsb"));
    const QShader waterfallFrag = loadShader(QStringLiteral(":/shaders/waterfall.frag.qsb"));
    const QShader traceVert = loadShader(QStringLiteral(":/shaders/trace.vert.qsb"));
    const QShader traceFrag = loadShader(QStringLiteral(":/shaders/trace.frag.qsb"));
    if (!waterfallVert.isValid() || !waterfallFrag.isValid() || !traceVert.isValid()
        || !traceFrag.isValid()) {
        return false;
    }

    // Gli shader del rilievo si tengono da parte: la loro pipeline nasce solo
    // se e quando l'utente sceglie quella vista, perché campionare texture nel
    // vertex shader non è dato per scontato su tutto l'hardware.
    m_reliefVertShader = loadShader(QStringLiteral(":/shaders/waterfall3d.vert.qsb"));
    m_reliefFragShader = loadShader(QStringLiteral(":/shaders/waterfall3d.frag.qsb"));

    // Quad a tutto schermo del waterfall: posizione + UV.
    static const float quad[] = {
        0.0f, 0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 1.0f,
        1.0f, 1.0f, 1.0f, 1.0f,
    };
    m_quadVbuf.reset(m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, sizeof(quad)));
    m_quadVbuf->create();
    m_quadUploaded = false;

    m_waterfallUbuf.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 96));
    m_waterfallUbuf->create();
    m_traceUbuf.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 96));
    m_traceUbuf->create();
    m_fillUbuf.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 96));
    m_fillUbuf->create();
    m_peakUbuf.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 96));
    m_peakUbuf->create();

    m_linearSampler.reset(m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear,
                                            QRhiSampler::None,
                                            QRhiSampler::ClampToEdge, QRhiSampler::Repeat));
    m_linearSampler->create();

    m_colorMapTexture.reset(m_rhi->newTexture(QRhiTexture::RGBA8, QSize(kColorMapSize, 1)));
    m_colorMapTexture->create();
    m_uploadedPalette = -1;

    // ── Pipeline del waterfall ───────────────────────────────────────────
    QRhiVertexInputLayout quadLayout;
    quadLayout.setBindings({{4 * sizeof(float)}});
    quadLayout.setAttributes({
        {0, 0, QRhiVertexInputAttribute::Float2, 0},
        {0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float)},
    });

    m_waterfallPipeline.reset(m_rhi->newGraphicsPipeline());
    m_waterfallPipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
    m_waterfallPipeline->setShaderStages({
        {QRhiShaderStage::Vertex, waterfallVert},
        {QRhiShaderStage::Fragment, waterfallFrag},
    });
    m_waterfallPipeline->setVertexInputLayout(quadLayout);
    m_waterfallPipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
    // Lo SRB definitivo arriva con la texture; qui serve solo il layout.
    // Viene assegnato in ensureSpectrumResources().

    // ── Pipeline della traccia e del riempimento ─────────────────────────
    QRhiVertexInputLayout traceLayout;
    traceLayout.setBindings({{2 * sizeof(float)}});
    traceLayout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float2, 0}});

    m_traceSrb.reset(m_rhi->newShaderResourceBindings());
    m_traceSrb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
            m_traceUbuf.get()),
    });
    m_traceSrb->create();

    m_fillSrb.reset(m_rhi->newShaderResourceBindings());
    m_fillSrb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
            m_fillUbuf.get()),
    });
    m_fillSrb->create();

    m_peakSrb.reset(m_rhi->newShaderResourceBindings());
    m_peakSrb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
            m_peakUbuf.get()),
    });
    m_peakSrb->create();

    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable = true;
    blend.srcColor = QRhiGraphicsPipeline::One; // colori pre-moltiplicati
    blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    blend.srcAlpha = QRhiGraphicsPipeline::One;
    blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;

    m_fillPipeline.reset(m_rhi->newGraphicsPipeline());
    m_fillPipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
    m_fillPipeline->setTargetBlends({blend});
    m_fillPipeline->setShaderStages({
        {QRhiShaderStage::Vertex, traceVert},
        {QRhiShaderStage::Fragment, traceFrag},
    });
    m_fillPipeline->setVertexInputLayout(traceLayout);
    m_fillPipeline->setShaderResourceBindings(m_fillSrb.get());
    m_fillPipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
    m_fillPipeline->create();

    m_tracePipeline.reset(m_rhi->newGraphicsPipeline());
    m_tracePipeline->setTopology(QRhiGraphicsPipeline::LineStrip);
    m_tracePipeline->setTargetBlends({blend});
    m_tracePipeline->setShaderStages({
        {QRhiShaderStage::Vertex, traceVert},
        {QRhiShaderStage::Fragment, traceFrag},
    });
    m_tracePipeline->setVertexInputLayout(traceLayout);
    m_tracePipeline->setShaderResourceBindings(m_traceSrb.get());
    m_tracePipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
    m_tracePipeline->create();

    return true;
}

void PanadapterRenderer::releaseSpectrumResources()
{
    m_waterfallTexture.reset();
    m_waterfallSrb.reset();
    m_reliefSrb.reset();
    m_reliefPipeline.reset();
    m_traceVbuf.reset();
    m_fillVbuf.reset();
    m_peakVbuf.reset();
    m_peakRow.clear();
    m_peakSeeded = false;
    m_peakClock.invalidate();
    m_binFloorDb.clear();
    m_binCount = 0;
    m_writeRow = 0;
}

void PanadapterRenderer::synchronize(QQuickRhiItem *rhiItem)
{
    auto *item = static_cast<PanadapterView *>(rhiItem);

    m_feed = item->feed();
    m_floorDb = static_cast<float>(item->floorDb());
    m_ceilingDb = static_cast<float>(item->ceilingDb());
    m_spectrumRatio = static_cast<float>(item->spectrumRatio());
    m_viewStart = static_cast<float>(item->viewStart());
    m_viewSpan = static_cast<float>(item->viewSpan());
    m_autoRangeStart = static_cast<float>(item->autoRangeStart());
    m_autoRangeSpan = static_cast<float>(item->autoRangeSpan());
    m_mirrorSideband = item->mirrorSideband();
    m_mirrorLowerSideband = item->mirrorLowerSideband();
    m_mode = item->waterfallMode();
    m_paletteIndex = item->paletteIndex();
    m_gamma = static_cast<float>(item->gamma());
    m_blackThreshold = static_cast<float>(item->blackThreshold());
    m_tilt = static_cast<float>(item->tilt());
    m_rotation = static_cast<float>(item->rotation3d());
    m_reliefScale = static_cast<float>(item->reliefScale());
    m_reliefGrid = static_cast<float>(item->reliefGrid());
    m_floorFlattening = static_cast<float>(item->floorFlattening());
    m_timeSpan = static_cast<float>(item->timeSpan());
    m_frozen = item->frozen();
    m_traceColor = item->traceColor();
    m_fillColor = item->fillColor();
    m_peakColor = item->peakColor();
    m_backgroundColor = item->backgroundColor();
    m_peakHold = item->peakHold();
    m_peakDecayDb = static_cast<float>(item->peakDecayDb());

    m_pendingRows = 0;
    if (!m_feed)
        return;

    const int bins = m_feed->binCount();
    if (bins <= 0)
        return;

    if (bins != m_binCount) {
        // La geometria è cambiata: le vecchie texture non descrivono più
        // la stessa banda, meglio ripartire da zero che mescolare.
        releaseSpectrumResources();
        m_binCount = bins;
    }

    // Le righe si consumano comunque, anche a immagine ferma: il ring ha un
    // fondo, e un consumatore che smette di leggere non «mette in pausa» il
    // DSP — lo fa scartare. Fermo vuol dire che non si scrive più sullo
    // schermo, non che la radio si ferma.
    m_pendingRows = m_feed->fetchRows(m_fetched, kMaxRowsPerFrame);

    if (m_pendingRows > 0) {
        const std::size_t offset = static_cast<std::size_t>(m_pendingRows - 1)
            * static_cast<std::size_t>(m_binCount);
        // Meter e autoscala devono restare una misura della radio, non della
        // sua copia grafica. Si salva l'ultima riga prima di riflettere le
        // righe che finiranno nella texture e nella traccia.
        m_measurementRow.assign(m_fetched.begin() + offset,
                                m_fetched.begin() + offset + m_binCount);
        mirrorFetchedRows();
    }

    // Il fondo per bin si aggiorna sempre, anche mentre l'immagine è ferma:
    // riprendendo, una stima vecchia di dieci secondi correggerebbe il
    // waterfall con la pendenza che la banda aveva prima.
    trackBinFloor();

    if (m_frozen) {
        m_pendingRows = 0;
        return;
    }

    if (m_pendingRows > 0) {
        const std::size_t offset = static_cast<std::size_t>(m_pendingRows - 1)
            * static_cast<std::size_t>(m_binCount);
        m_latestRow.assign(m_fetched.begin() + offset,
                           m_fetched.begin() + offset + m_binCount);
        m_traceDirty = true;

        // L'auto-range misura qui, dove i campioni ci sono già, e riporta i
        // livelli all'item: `synchronize()` è l'unico punto in cui il thread
        // GUI è fermo e lo scambio è sicuro in entrambe le direzioni.
        // Questa finestra viene copiata da QML nello stesso synchronize().
        // Per un SDR IQ coincide con la vista; con audio+CAT esclude invece
        // il lato opposto al VFO, che la radio non ha mai consegnato.
        item->reportMeasuredLevels(m_measurementRow, m_autoRangeStart, m_autoRangeSpan);

        // E quante righe sono passate: da qui l'item ricava quanti secondi di
        // storia stia mostrando il waterfall, che è l'unico modo onesto di
        // graduarne l'asse dei tempi.
        item->reportRowsConsumed(m_pendingRows, kWaterfallRows);
    }
}

void PanadapterRenderer::mirrorFetchedRows()
{
    if (!m_mirrorSideband || m_binCount < 2 || m_pendingRows <= 0)
        return;

    const int half = m_binCount / 2;
    for (int row = 0; row < m_pendingRows; ++row) {
        float *values = m_fetched.data()
            + static_cast<std::size_t>(row) * static_cast<std::size_t>(m_binCount);
        for (int lowerBin = 0; lowerBin < half; ++lowerBin) {
            const int upperBin = m_binCount - 1 - lowerBin;
            if (m_mirrorLowerSideband)
                values[upperBin] = values[lowerBin];
            else
                values[lowerBin] = values[upperBin];
        }
    }
}

bool PanadapterRenderer::ensureSpectrumResources(QRhiResourceUpdateBatch *batch)
{
    if (!m_rhi || m_binCount <= 0 || !m_waterfallPipeline)
        return false;

    if (!m_quadUploaded && m_quadVbuf) {
        static const float quad[] = {
            0.0f, 0.0f, 0.0f, 0.0f,
            1.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 1.0f,
            1.0f, 1.0f, 1.0f, 1.0f,
        };
        batch->uploadStaticBuffer(m_quadVbuf.get(), quad);
        m_quadUploaded = true;
    }

    if (m_uploadedPalette != m_paletteIndex && m_colorMapTexture) {
        const QByteArray map = buildWaterfallColorMap(m_paletteIndex);
        QRhiTextureSubresourceUploadDescription sub(map.constData(), map.size());
        batch->uploadTexture(m_colorMapTexture.get(),
                             QRhiTextureUploadDescription({0, 0, sub}));
        m_uploadedPalette = m_paletteIndex;
    }

    if (m_waterfallTexture)
        return true;

    if (!m_rhi->isTextureFormatSupported(QRhiTexture::R8)) {
        qCWarning(dsdrGpu) << "formato R8 non supportato: waterfall disabilitato";
        return false;
    }

    m_waterfallTexture.reset(
        m_rhi->newTexture(QRhiTexture::R8, QSize(m_binCount, kWaterfallRows)));
    if (!m_waterfallTexture->create()) {
        m_waterfallTexture.reset();
        return false;
    }

    // Texture appena creata: azzeriamola, altrimenti mostrerebbe memoria a caso.
    {
        QByteArray blank(static_cast<qsizetype>(m_binCount) * kWaterfallRows, char(0));
        QRhiTextureSubresourceUploadDescription sub(blank.constData(), blank.size());
        sub.setSourceSize(QSize(m_binCount, kWaterfallRows));
        batch->uploadTexture(m_waterfallTexture.get(), QRhiTextureUploadDescription({0, 0, sub}));
    }
    m_writeRow = 0;

    m_waterfallSrb.reset(m_rhi->newShaderResourceBindings());
    m_waterfallSrb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
            m_waterfallUbuf.get()),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                  m_waterfallTexture.get(), m_linearSampler.get()),
        QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage,
                                                  m_colorMapTexture.get(), m_linearSampler.get()),
    });
    m_waterfallSrb->create();

    m_waterfallPipeline->setShaderResourceBindings(m_waterfallSrb.get());
    if (!m_waterfallPipeline->create())
        return false;

    const quint32 traceBytes = static_cast<quint32>(m_binCount) * 2 * sizeof(float);
    m_traceVbuf.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, traceBytes));
    m_traceVbuf->create();

    m_fillVbuf.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, traceBytes * 2));
    m_fillVbuf->create();

    m_peakVbuf.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, traceBytes));
    m_peakVbuf->create();

    return true;
}

bool PanadapterRenderer::ensureReliefResources(QRhiResourceUpdateBatch *batch)
{
    if (m_reliefUnavailable || !m_rhi || !m_waterfallTexture)
        return false;
    if (m_reliefPipeline)
        return true;

    if (!m_reliefVertShader.isValid() || !m_reliefFragShader.isValid()) {
        m_reliefUnavailable = true;
        return false;
    }

    if (!m_reliefVbuf) {
        constexpr int vertexCount = kReliefGridWidth * kReliefGridDepth;
        constexpr int indexCount = (kReliefGridWidth - 1) * (kReliefGridDepth - 1) * 6;
        static_assert(vertexCount <= 65535, "gli indici a 16 bit non basterebbero");

        std::vector<float> vertices;
        vertices.reserve(static_cast<std::size_t>(vertexCount) * 2);
        for (int z = 0; z < kReliefGridDepth; ++z) {
            const float age = static_cast<float>(z) / (kReliefGridDepth - 1);
            for (int x = 0; x < kReliefGridWidth; ++x) {
                vertices.push_back(static_cast<float>(x) / (kReliefGridWidth - 1));
                vertices.push_back(age);
            }
        }

        std::vector<quint16> indices;
        indices.reserve(static_cast<std::size_t>(indexCount));
        for (int z = 0; z < kReliefGridDepth - 1; ++z) {
            for (int x = 0; x < kReliefGridWidth - 1; ++x) {
                const auto base = static_cast<quint16>(z * kReliefGridWidth + x);
                const auto next = static_cast<quint16>(base + kReliefGridWidth);
                indices.push_back(base);
                indices.push_back(next);
                indices.push_back(static_cast<quint16>(base + 1));
                indices.push_back(static_cast<quint16>(base + 1));
                indices.push_back(next);
                indices.push_back(static_cast<quint16>(next + 1));
            }
        }
        m_reliefIndexCount = static_cast<quint32>(indices.size());

        const auto vbytes = static_cast<quint32>(vertices.size() * sizeof(float));
        const auto ibytes = static_cast<quint32>(indices.size() * sizeof(quint16));

        m_reliefVbuf.reset(
            m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, vbytes));
        m_reliefIbuf.reset(
            m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::IndexBuffer, ibytes));
        if (!m_reliefVbuf->create() || !m_reliefIbuf->create()) {
            m_reliefVbuf.reset();
            m_reliefIbuf.reset();
            m_reliefUnavailable = true;
            return false;
        }

        batch->uploadStaticBuffer(m_reliefVbuf.get(), vertices.data());
        batch->uploadStaticBuffer(m_reliefIbuf.get(), indices.data());
    }

    if (!m_reliefUbuf) {
        // Più capiente degli altri: oltre alla matrice e ai livelli porta i
        // passi della griglia, che servono allo shader per ricavare la normale
        // dai vertici vicini.
        m_reliefUbuf.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 128));
        if (!m_reliefUbuf->create()) {
            m_reliefUnavailable = true;
            return false;
        }
    }

    m_reliefSrb.reset(m_rhi->newShaderResourceBindings());
    m_reliefSrb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
            m_reliefUbuf.get()),
        // Il campionamento avviene nel vertex stage: è lì che si costruisce
        // l'altezza della superficie.
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::VertexStage,
                                                  m_waterfallTexture.get(), m_linearSampler.get()),
        QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage,
                                                  m_colorMapTexture.get(), m_linearSampler.get()),
    });
    if (!m_reliefSrb->create()) {
        m_reliefSrb.reset();
        m_reliefUnavailable = true;
        return false;
    }

    QRhiVertexInputLayout gridLayout;
    gridLayout.setBindings({{2 * sizeof(float)}});
    gridLayout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float2, 0}});

    m_reliefPipeline.reset(m_rhi->newGraphicsPipeline());
    m_reliefPipeline->setTopology(QRhiGraphicsPipeline::Triangles);
    m_reliefPipeline->setShaderStages({
        {QRhiShaderStage::Vertex, m_reliefVertShader},
        {QRhiShaderStage::Fragment, m_reliefFragShader},
    });
    m_reliefPipeline->setVertexInputLayout(gridLayout);
    m_reliefPipeline->setShaderResourceBindings(m_reliefSrb.get());
    m_reliefPipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
    // Le creste vicine devono coprire quelle lontane: senza test di profondità
    // la superficie si disegnerebbe in ordine di riga, non di distanza.
    m_reliefPipeline->setDepthTest(true);
    m_reliefPipeline->setDepthWrite(true);
    m_reliefPipeline->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
    // Niente scarto di facce: guardando la superficie da sotto, con
    // inclinazioni basse, si vedrebbero buchi.
    m_reliefPipeline->setCullMode(QRhiGraphicsPipeline::None);

    if (!m_reliefPipeline->create()) {
        m_reliefPipeline.reset();
        m_reliefUnavailable = true;
        qCWarning(dsdrGpu) << "vista in rilievo non disponibile su questo hardware";
        return false;
    }

    return true;
}

void PanadapterRenderer::trackBinFloor()
{
    if (m_pendingRows <= 0 || m_binCount <= 0)
        return;

    const auto bins = static_cast<std::size_t>(m_binCount);
    if (m_binFloorDb.size() != bins) {
        // Si parte dalla prima riga che arriva. Partire da meno infinito
        // vorrebbe dire aspettare secondi prima che la stima significhi
        // qualcosa, e in quei secondi l'appiattimento correggerebbe a caso.
        m_binFloorDb.assign(m_fetched.begin(), m_fetched.begin() + m_binCount);
        return;
    }

    for (int row = 0; row < m_pendingRows; ++row) {
        const float *source = m_fetched.data() + static_cast<std::size_t>(row) * m_binCount;
        for (std::size_t bin = 0; bin < bins; ++bin) {
            const float level = source[bin];
            if (!std::isfinite(level))
                continue;

            // Asimmetrico, e non è un dettaglio: il fondo è il livello che il
            // bin tocca quando non c'è nessuno, e una portante che si accende
            // non deve alzarlo — se lo alzasse, l'appiattimento scaverebbe una
            // buca proprio sotto il segnale che si sta guardando. Scende
            // subito, risale piano.
            float &floor = m_binFloorDb[bin];
            floor = (level < floor) ? level : floor + (level - floor) * kFloorRise;
        }
    }
}

void PanadapterRenderer::uploadRows(QRhiResourceUpdateBatch *batch)
{
    if (m_pendingRows <= 0 || !m_waterfallTexture)
        return;

    const float span = std::max(m_ceilingDb - m_floorDb, 1.0f);
    m_rowBytes.resize(static_cast<std::size_t>(m_binCount));

    // La quota media del fondo: è il riferimento rispetto a cui si toglie la
    // pendenza. Togliere il fondo *assoluto* di ogni bin porterebbe tutta
    // l'immagine a zero e con essa il significato della scala; qui si toglie
    // soltanto lo scarto dalla media, cioè la forma, e il livello complessivo
    // resta quello misurato.
    const bool flatten = m_floorFlattening > 0.001f
        && m_binFloorDb.size() == static_cast<std::size_t>(m_binCount);
    float meanFloor = 0.0f;
    if (flatten) {
        double sum = 0.0;
        for (const float value : m_binFloorDb)
            sum += value;
        meanFloor = static_cast<float>(sum / static_cast<double>(m_binFloorDb.size()));
    }

    for (int row = 0; row < m_pendingRows; ++row) {
        const float *source = m_fetched.data() + static_cast<std::size_t>(row) * m_binCount;
        for (int bin = 0; bin < m_binCount; ++bin) {
            float level = source[bin];
            if (flatten) {
                level -= (m_binFloorDb[static_cast<std::size_t>(bin)] - meanFloor)
                    * m_floorFlattening;
            }
            const float normalized = std::clamp((level - m_floorDb) / span, 0.0f, 1.0f);
            m_rowBytes[static_cast<std::size_t>(bin)] =
                static_cast<uchar>(std::lround(normalized * 255.0f));
        }

        QRhiTextureSubresourceUploadDescription sub(m_rowBytes.data(),
                                                    static_cast<int>(m_rowBytes.size()));
        sub.setSourceSize(QSize(m_binCount, 1));
        sub.setDestinationTopLeft(QPoint(0, m_writeRow));
        batch->uploadTexture(m_waterfallTexture.get(), QRhiTextureUploadDescription({0, 0, sub}));

        m_writeRow = (m_writeRow + 1) % kWaterfallRows;
    }

    m_pendingRows = 0;
}

void PanadapterRenderer::updateTraceGeometry(QRhiResourceUpdateBatch *batch)
{
    if (!m_traceDirty || m_latestRow.empty() || !m_traceVbuf || !m_fillVbuf)
        return;

    const int bins = static_cast<int>(m_latestRow.size());
    const float span = std::max(m_ceilingDb - m_floorDb, 1.0f);
    const float step = (bins > 1) ? 1.0f / static_cast<float>(bins - 1) : 1.0f;

    m_traceVertices.resize(static_cast<std::size_t>(bins) * 2);
    m_fillVertices.resize(static_cast<std::size_t>(bins) * 4);

    for (int i = 0; i < bins; ++i) {
        const float x = static_cast<float>(i) * step;
        const float y = std::clamp((m_latestRow[static_cast<std::size_t>(i)] - m_floorDb) / span,
                                   0.0f, 1.0f);

        m_traceVertices[static_cast<std::size_t>(i) * 2] = x;
        m_traceVertices[static_cast<std::size_t>(i) * 2 + 1] = y;

        // Il riempimento è una striscia di triangoli fra la base e la traccia.
        m_fillVertices[static_cast<std::size_t>(i) * 4] = x;
        m_fillVertices[static_cast<std::size_t>(i) * 4 + 1] = 0.0f;
        m_fillVertices[static_cast<std::size_t>(i) * 4 + 2] = x;
        m_fillVertices[static_cast<std::size_t>(i) * 4 + 3] = y;
    }

    batch->updateDynamicBuffer(m_traceVbuf.get(), 0,
                               static_cast<quint32>(m_traceVertices.size() * sizeof(float)),
                               m_traceVertices.data());
    batch->updateDynamicBuffer(m_fillVbuf.get(), 0,
                               static_cast<quint32>(m_fillVertices.size() * sizeof(float)),
                               m_fillVertices.data());
    m_traceDirty = false;
}

void PanadapterRenderer::updatePeakGeometry(QRhiResourceUpdateBatch *batch)
{
    if (!m_peakHold || m_binCount <= 0 || !m_peakVbuf || m_latestRow.empty()) {
        // Spenta la tenuta, il cronometro riparte da zero: riaccendendola non
        // deve arrivare tutta in una volta la discesa del tempo in cui era
        // ferma, che azzererebbe la riga al primo fotogramma.
        m_peakClock.invalidate();
        m_peakSeeded = false;
        return;
    }

    const auto bins = static_cast<std::size_t>(m_binCount);
    if (m_latestRow.size() != bins)
        return;

    // Alla prima riga i massimi *sono* la riga: partire da meno infinito
    // mostrerebbe la tenuta salire dal fondo per qualche secondo, come se lo
    // spettro stesse crescendo davvero.
    if (!m_peakSeeded || m_peakRow.size() != bins) {
        m_peakRow = m_latestRow;
        m_peakSeeded = true;
        m_peakClock.start();
    } else {
        float decayDb = 0.0f;
        if (m_peakClock.isValid()) {
            decayDb = m_peakDecayDb
                * static_cast<float>(m_peakClock.nsecsElapsed()) * 1e-9f;
            m_peakClock.restart();
        } else {
            m_peakClock.start();
        }

        // La riga non scende mai sotto la traccia istantanea: sotto di essa non
        // sarebbe più una tenuta, sarebbe una traccia in ritardo.
        for (std::size_t i = 0; i < bins; ++i)
            m_peakRow[i] = std::max(m_peakRow[i] - decayDb, m_latestRow[i]);
    }

    const float span = std::max(m_ceilingDb - m_floorDb, 1.0f);
    const float step = (m_binCount > 1) ? 1.0f / static_cast<float>(m_binCount - 1) : 1.0f;

    m_peakVertices.resize(bins * 2);
    for (std::size_t i = 0; i < bins; ++i) {
        m_peakVertices[i * 2] = static_cast<float>(i) * step;
        m_peakVertices[i * 2 + 1] = std::clamp((m_peakRow[i] - m_floorDb) / span, 0.0f, 1.0f);
    }

    batch->updateDynamicBuffer(m_peakVbuf.get(), 0,
                               static_cast<quint32>(m_peakVertices.size() * sizeof(float)),
                               m_peakVertices.data());
}

void PanadapterRenderer::render(QRhiCommandBuffer *cb)
{
    if (!m_rhi)
        return;

    QRhiResourceUpdateBatch *batch = m_rhi->nextResourceUpdateBatch();
    const bool ready = ensureSpectrumResources(batch);
    if (ready) {
        uploadRows(batch);
        updateTraceGeometry(batch);
        updatePeakGeometry(batch);
    }

    const QSize outputSize = renderTarget()->pixelSize();

    const bool relief = ready && m_mode == PanadapterView::Relief
        && ensureReliefResources(batch);

    // In rilievo la superficie prende tutta l'area: il suo bordo vicino è già
    // lo spettro istantaneo, e disegnarne una seconda copia sopra toglierebbe
    // spazio proprio alla dimensione che questa vista serve a mostrare.
    const float waterfallHeight = relief
        ? static_cast<float>(outputSize.height())
        : static_cast<float>(outputSize.height()) * (1.0f - m_spectrumRatio);
    const float spectrumHeight = static_cast<float>(outputSize.height()) - waterfallHeight;

    QMatrix4x4 mvp = m_rhi->clipSpaceCorrMatrix();
    mvp.ortho(0.0f, 1.0f, 0.0f, 1.0f, -1.0f, 1.0f);

    if (ready) {
        const float rowOffset = static_cast<float>(m_writeRow) / kWaterfallRows;
        // Zoom: waterfall e traccia mostrano la stessa porzione di banda.
        const float uMin = m_viewStart;
        const float uMax = m_viewStart + m_viewSpan;
        const float unused = 0.0f;
        batch->updateDynamicBuffer(m_waterfallUbuf.get(), 0, 64, mvp.constData());
        batch->updateDynamicBuffer(m_waterfallUbuf.get(), 64, 4, &rowOffset);
        batch->updateDynamicBuffer(m_waterfallUbuf.get(), 68, 4, &uMin);
        batch->updateDynamicBuffer(m_waterfallUbuf.get(), 72, 4, &uMax);
        batch->updateDynamicBuffer(m_waterfallUbuf.get(), 76, 4, &m_blackThreshold);
        batch->updateDynamicBuffer(m_waterfallUbuf.get(), 80, 4, &m_gamma);
        batch->updateDynamicBuffer(m_waterfallUbuf.get(), 84, 4, &m_timeSpan);
        batch->updateDynamicBuffer(m_waterfallUbuf.get(), 88, 4, &unused);

        if (relief) {
            const float aspect = (waterfallHeight > 1.0f)
                ? static_cast<float>(outputSize.width()) / waterfallHeight
                : 1.0f;

            // `tilt` è l'elevazione del punto di vista: 90° guarda la
            // superficie dallo zenit — cioè il waterfall piatto — e scendendo
            // si passa davanti alle creste.
            QMatrix4x4 model;
            model.rotate(m_tilt, 1.0f, 0.0f, 0.0f);
            model.rotate(m_rotation, 0.0f, 1.0f, 0.0f);
            // La mesh nasce con z da 0 a -depth: la si ricentra qui, così
            // ruotando si gira attorno al centro della superficie e non
            // attorno al suo bordo vicino.
            model.translate(0.0f, 0.0f, kReliefDepth * 0.5f);

            // Inclinazione e rotazione cambiano l'ingombro sullo schermo. Un
            // riquadro fisso funzionerebbe per una sola combinazione: qui si
            // misura la sagoma effettiva e si adatta la scena al riquadro
            // disponibile, che è largo e basso quanto l'area dello spettro.
            float minX = 0.0f, maxX = 0.0f, minY = 0.0f, maxY = 0.0f;
            bool first = true;
            for (const float dx : {-0.5f, 0.5f}) {
                for (const float dy : {0.0f, m_reliefScale}) {
                    for (const float dz : {-kReliefDepth, 0.0f}) {
                        const QVector3D corner = model.map(QVector3D(dx, dy, dz));
                        if (first) {
                            minX = maxX = corner.x();
                            minY = maxY = corner.y();
                            first = false;
                            continue;
                        }
                        minX = std::min(minX, corner.x());
                        maxX = std::max(maxX, corner.x());
                        minY = std::min(minY, corner.y());
                        maxY = std::max(maxY, corner.y());
                    }
                }
            }

            // Prospettiva dolce: camera lontana e campo stretto. Una
            // prospettiva marcata su un riquadro così schiacciato deformerebbe
            // le creste ai bordi al punto da falsare le ampiezze.
            constexpr float kFovY = 22.0f;
            constexpr float kDistance = 3.6f;
            const float halfH = kDistance * std::tan(qDegreesToRadians(kFovY * 0.5f));
            const float halfW = halfH * aspect;

            const float extentX = std::max(maxX - minX, 1e-3f);
            const float extentY = std::max(maxY - minY, 1e-3f);
            // Adattamento separato sui due assi. Una scala uniforme lascerebbe
            // metà riquadro vuoto: l'area dello spettro è larga e bassa, la
            // sagoma della superficie no. Lo stiramento non falsa le altezze —
            // la scala verticale in dB resta quella della vista piatta — e
            // guadagna risoluzione proprio sull'asse delle frequenze.
            // Il margine tiene conto di quanto la prospettiva ingrandisce la
            // parte vicina rispetto alla stima piana fatta qui sopra.
            const float fitX = 2.0f * halfW / extentX * 0.94f;
            const float fitY = 2.0f * halfH / extentY * 0.86f;

            QMatrix4x4 scene = m_rhi->clipSpaceCorrMatrix();
            scene.perspective(kFovY, aspect, 0.05f, 100.0f);
            scene.translate(0.0f, 0.0f, -kDistance);
            scene.scale(fitX, fitY, std::min(fitX, fitY));
            scene.translate(-(minX + maxX) * 0.5f, -(minY + maxY) * 0.5f, 0.0f);
            scene *= model;

            const float depth = kReliefDepth;
            // I passi della griglia: lo shader li usa per campionare i vertici
            // vicini e ricavarne la normale. Li conosce solo chi la griglia
            // l'ha costruita, cioè questo file.
            constexpr float stepX = 1.0f / (kReliefGridWidth - 1);
            constexpr float stepZ = 1.0f / (kReliefGridDepth - 1);
            batch->updateDynamicBuffer(m_reliefUbuf.get(), 0, 64, scene.constData());
            batch->updateDynamicBuffer(m_reliefUbuf.get(), 64, 4, &rowOffset);
            batch->updateDynamicBuffer(m_reliefUbuf.get(), 68, 4, &uMin);
            batch->updateDynamicBuffer(m_reliefUbuf.get(), 72, 4, &uMax);
            batch->updateDynamicBuffer(m_reliefUbuf.get(), 76, 4, &m_reliefScale);
            batch->updateDynamicBuffer(m_reliefUbuf.get(), 80, 4, &depth);
            batch->updateDynamicBuffer(m_reliefUbuf.get(), 84, 4, &m_blackThreshold);
            batch->updateDynamicBuffer(m_reliefUbuf.get(), 88, 4, &m_gamma);
            batch->updateDynamicBuffer(m_reliefUbuf.get(), 92, 4, &stepX);
            batch->updateDynamicBuffer(m_reliefUbuf.get(), 96, 4, &stepZ);
            batch->updateDynamicBuffer(m_reliefUbuf.get(), 100, 4, &m_timeSpan);
            batch->updateDynamicBuffer(m_reliefUbuf.get(), 104, 4, &m_reliefGrid);
        }

        const float traceRgba[4] = {
            static_cast<float>(m_traceColor.redF()), static_cast<float>(m_traceColor.greenF()),
            static_cast<float>(m_traceColor.blueF()), static_cast<float>(m_traceColor.alphaF())};
        const float fillRgba[4] = {
            static_cast<float>(m_fillColor.redF()), static_cast<float>(m_fillColor.greenF()),
            static_cast<float>(m_fillColor.blueF()), static_cast<float>(m_fillColor.alphaF())};
        const float peakRgba[4] = {
            static_cast<float>(m_peakColor.redF()), static_cast<float>(m_peakColor.greenF()),
            static_cast<float>(m_peakColor.blueF()), static_cast<float>(m_peakColor.alphaF())};
        const float noGradient = 0.0f;
        const float withGradient = 1.0f;

        batch->updateDynamicBuffer(m_traceUbuf.get(), 0, 64, mvp.constData());
        batch->updateDynamicBuffer(m_traceUbuf.get(), 64, 16, traceRgba);
        batch->updateDynamicBuffer(m_traceUbuf.get(), 80, 4, &noGradient);
        batch->updateDynamicBuffer(m_traceUbuf.get(), 84, 4, &m_viewStart);
        batch->updateDynamicBuffer(m_traceUbuf.get(), 88, 4, &m_viewSpan);

        batch->updateDynamicBuffer(m_fillUbuf.get(), 0, 64, mvp.constData());
        batch->updateDynamicBuffer(m_fillUbuf.get(), 64, 16, fillRgba);
        batch->updateDynamicBuffer(m_fillUbuf.get(), 80, 4, &withGradient);
        batch->updateDynamicBuffer(m_fillUbuf.get(), 84, 4, &m_viewStart);
        batch->updateDynamicBuffer(m_fillUbuf.get(), 88, 4, &m_viewSpan);

        batch->updateDynamicBuffer(m_peakUbuf.get(), 0, 64, mvp.constData());
        batch->updateDynamicBuffer(m_peakUbuf.get(), 64, 16, peakRgba);
        batch->updateDynamicBuffer(m_peakUbuf.get(), 80, 4, &noGradient);
        batch->updateDynamicBuffer(m_peakUbuf.get(), 84, 4, &m_viewStart);
        batch->updateDynamicBuffer(m_peakUbuf.get(), 88, 4, &m_viewSpan);
    }

    cb->beginPass(renderTarget(), m_backgroundColor, {1.0f, 0}, batch);

    if (ready && m_waterfallTexture) {
        // ── Storia dello spettro: metà inferiore ─────────────────────────
        cb->setViewport(QRhiViewport(0.0f, 0.0f, static_cast<float>(outputSize.width()),
                                     waterfallHeight));

        if (relief) {
            cb->setGraphicsPipeline(m_reliefPipeline.get());
            cb->setShaderResources(m_reliefSrb.get());
            const QRhiCommandBuffer::VertexInput gridBinding(m_reliefVbuf.get(), 0);
            cb->setVertexInput(0, 1, &gridBinding, m_reliefIbuf.get(), 0,
                               QRhiCommandBuffer::IndexUInt16);
            cb->drawIndexed(m_reliefIndexCount);
        } else {
            cb->setGraphicsPipeline(m_waterfallPipeline.get());
            cb->setShaderResources(m_waterfallSrb.get());
            const QRhiCommandBuffer::VertexInput quadBinding(m_quadVbuf.get(), 0);
            cb->setVertexInput(0, 1, &quadBinding);
            cb->draw(4);
        }

        // ── Spettro: fascia superiore ────────────────────────────────────
        if (!relief && !m_traceVertices.empty()) {
            const QRhiViewport spectrumViewport(0.0f, waterfallHeight,
                                                static_cast<float>(outputSize.width()),
                                                spectrumHeight);

            cb->setGraphicsPipeline(m_fillPipeline.get());
            cb->setViewport(spectrumViewport);
            cb->setShaderResources(m_fillSrb.get());
            const QRhiCommandBuffer::VertexInput fillBinding(m_fillVbuf.get(), 0);
            cb->setVertexInput(0, 1, &fillBinding);
            cb->draw(static_cast<quint32>(m_binCount) * 2);

            cb->setGraphicsPipeline(m_tracePipeline.get());
            cb->setViewport(spectrumViewport);
            cb->setShaderResources(m_traceSrb.get());
            const QRhiCommandBuffer::VertexInput traceBinding(m_traceVbuf.get(), 0);
            cb->setVertexInput(0, 1, &traceBinding);
            cb->draw(static_cast<quint32>(m_binCount));

            // I massimi sopra la traccia: la pipeline è la stessa, cambia solo
            // il gruppo di risorse — stesso layout, altro colore.
            if (m_peakHold && !m_peakVertices.empty()) {
                cb->setShaderResources(m_peakSrb.get());
                const QRhiCommandBuffer::VertexInput peakBinding(m_peakVbuf.get(), 0);
                cb->setVertexInput(0, 1, &peakBinding);
                cb->draw(static_cast<quint32>(m_binCount));
            }
        }
    }

    cb->endPass();
}

} // namespace dsdr::app
