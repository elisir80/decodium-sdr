// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/PanadapterRenderer.h"
#include "app/PanadapterView.h"
#include "core/SpectrumFeed.h"

#include <QFile>
#include <QLoggingCategory>

#include <algorithm>
#include <cmath>

Q_LOGGING_CATEGORY(dsdrGpu, "dsdr.gpu")

namespace dsdr::app {

namespace {

/// Altezza della texture ad anello: ~20 s di storia a 25 righe/s.
constexpr int kWaterfallRows = 512;

/// Numero massimo di righe consumate in un frame: se la UI è rimasta indietro
/// recuperiamo, ma senza trasformare un frame in un caricamento massiccio.
constexpr int kMaxRowsPerFrame = 8;

constexpr int kColorMapSize = 256;

QShader loadShader(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(dsdrGpu) << "shader non trovato:" << path;
        return QShader();
    }
    return QShader::fromSerialized(file.readAll());
}

/// Colormap DECODIUM: nero → indaco → ciano → verde → ambra → rosso → bianco.
/// Progressione percettivamente monotona: un segnale più forte è sempre più
/// chiaro, che è ciò che rende leggibile un waterfall affollato.
QByteArray buildColorMap()
{
    struct Stop
    {
        float position;
        float r, g, b;
    };
    static const Stop stops[] = {
        {0.00f, 0.02f, 0.03f, 0.06f},
        {0.18f, 0.05f, 0.09f, 0.35f},
        {0.36f, 0.05f, 0.42f, 0.62f},
        {0.52f, 0.10f, 0.72f, 0.60f},
        {0.68f, 0.55f, 0.85f, 0.25f},
        {0.82f, 0.98f, 0.72f, 0.15f},
        {0.93f, 1.00f, 0.35f, 0.15f},
        {1.00f, 1.00f, 0.96f, 0.92f},
    };
    constexpr int stopCount = static_cast<int>(std::size(stops));

    QByteArray data;
    data.resize(kColorMapSize * 4);
    auto *out = reinterpret_cast<uchar *>(data.data());

    for (int i = 0; i < kColorMapSize; ++i) {
        const float t = static_cast<float>(i) / (kColorMapSize - 1);

        int segment = 0;
        while (segment < stopCount - 2 && t > stops[segment + 1].position)
            ++segment;

        const Stop &a = stops[segment];
        const Stop &b = stops[segment + 1];
        const float span = std::max(b.position - a.position, 1e-6f);
        const float k = std::clamp((t - a.position) / span, 0.0f, 1.0f);

        out[i * 4 + 0] = static_cast<uchar>(std::lround(255.0f * (a.r + (b.r - a.r) * k)));
        out[i * 4 + 1] = static_cast<uchar>(std::lround(255.0f * (a.g + (b.g - a.g) * k)));
        out[i * 4 + 2] = static_cast<uchar>(std::lround(255.0f * (a.b + (b.b - a.b) * k)));
        out[i * 4 + 3] = 255;
    }
    return data;
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
        m_quadVbuf.reset();
        m_waterfallUbuf.reset();
        m_traceUbuf.reset();
        m_fillUbuf.reset();
        m_linearSampler.reset();
        m_colorMapTexture.reset();
        m_colorMapUploaded = false;
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

    m_waterfallUbuf.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 80));
    m_waterfallUbuf->create();
    m_traceUbuf.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 96));
    m_traceUbuf->create();
    m_fillUbuf.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 96));
    m_fillUbuf->create();

    m_linearSampler.reset(m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear,
                                            QRhiSampler::None,
                                            QRhiSampler::ClampToEdge, QRhiSampler::Repeat));
    m_linearSampler->create();

    m_colorMapTexture.reset(m_rhi->newTexture(QRhiTexture::RGBA8, QSize(kColorMapSize, 1)));
    m_colorMapTexture->create();
    m_colorMapUploaded = false;

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
    m_traceVbuf.reset();
    m_fillVbuf.reset();
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
    m_traceColor = item->traceColor();
    m_fillColor = item->fillColor();
    m_backgroundColor = item->backgroundColor();

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

    m_pendingRows = m_feed->fetchRows(m_fetched, kMaxRowsPerFrame);
    if (m_pendingRows > 0) {
        const std::size_t offset = static_cast<std::size_t>(m_pendingRows - 1)
            * static_cast<std::size_t>(m_binCount);
        m_latestRow.assign(m_fetched.begin() + offset,
                           m_fetched.begin() + offset + m_binCount);
        m_traceDirty = true;
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

    if (!m_colorMapUploaded && m_colorMapTexture) {
        const QByteArray map = buildColorMap();
        QRhiTextureSubresourceUploadDescription sub(map.constData(), map.size());
        batch->uploadTexture(m_colorMapTexture.get(),
                             QRhiTextureUploadDescription({0, 0, sub}));
        m_colorMapUploaded = true;
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

    return true;
}

void PanadapterRenderer::uploadRows(QRhiResourceUpdateBatch *batch)
{
    if (m_pendingRows <= 0 || !m_waterfallTexture)
        return;

    const float span = std::max(m_ceilingDb - m_floorDb, 1.0f);
    m_rowBytes.resize(static_cast<std::size_t>(m_binCount));

    for (int row = 0; row < m_pendingRows; ++row) {
        const float *source = m_fetched.data() + static_cast<std::size_t>(row) * m_binCount;
        for (int bin = 0; bin < m_binCount; ++bin) {
            const float normalized = std::clamp((source[bin] - m_floorDb) / span, 0.0f, 1.0f);
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

void PanadapterRenderer::render(QRhiCommandBuffer *cb)
{
    if (!m_rhi)
        return;

    QRhiResourceUpdateBatch *batch = m_rhi->nextResourceUpdateBatch();
    const bool ready = ensureSpectrumResources(batch);
    if (ready) {
        uploadRows(batch);
        updateTraceGeometry(batch);
    }

    const QSize outputSize = renderTarget()->pixelSize();
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
        batch->updateDynamicBuffer(m_waterfallUbuf.get(), 76, 4, &unused);

        const float traceRgba[4] = {
            static_cast<float>(m_traceColor.redF()), static_cast<float>(m_traceColor.greenF()),
            static_cast<float>(m_traceColor.blueF()), static_cast<float>(m_traceColor.alphaF())};
        const float fillRgba[4] = {
            static_cast<float>(m_fillColor.redF()), static_cast<float>(m_fillColor.greenF()),
            static_cast<float>(m_fillColor.blueF()), static_cast<float>(m_fillColor.alphaF())};
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
    }

    cb->beginPass(renderTarget(), m_backgroundColor, {1.0f, 0}, batch);

    if (ready && m_waterfallTexture) {
        const float waterfallHeight =
            static_cast<float>(outputSize.height()) * (1.0f - m_spectrumRatio);
        const float spectrumHeight = static_cast<float>(outputSize.height()) - waterfallHeight;

        // ── Waterfall: metà inferiore ────────────────────────────────────
        cb->setGraphicsPipeline(m_waterfallPipeline.get());
        cb->setViewport(QRhiViewport(0.0f, 0.0f, static_cast<float>(outputSize.width()),
                                     waterfallHeight));
        cb->setShaderResources(m_waterfallSrb.get());
        const QRhiCommandBuffer::VertexInput quadBinding(m_quadVbuf.get(), 0);
        cb->setVertexInput(0, 1, &quadBinding);
        cb->draw(4);

        // ── Spettro: fascia superiore ────────────────────────────────────
        if (!m_traceVertices.empty()) {
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
        }
    }

    cb->endPass();
}

} // namespace dsdr::app
