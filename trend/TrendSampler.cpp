#include "TrendSampler.h"

#include <QDateTime>
#include "../data_collection/model/UnifiedRegister.h"
#include "../utils/Logger.h"

#ifdef SR_TREND_DEV_TESTDATA
#include <cmath>
#endif

namespace Trend {

TrendSampler::TrendSampler(DataCollection::Store::RegisterTable *regTable,
                            TrendDatabase *trendDb,
                            QObject *parent)
    : QObject(parent)
    , m_regTable(regTable)
    , m_trendDb(trendDb)
{
    connect(&m_sampleTimer, &QTimer::timeout, this, &TrendSampler::onSampleTimer);
    connect(&m_flushTimer,  &QTimer::timeout, this, &TrendSampler::onFlushTimer);
}

// ---------------------------------------------------------------------------
// When Config Changes
// ApiServe (handlePutTrendConfig) → TrendSampler(aapplyConfig) → TrendSampler(start/stop)
// ---------------------------------------------------------------------------
void TrendSampler::applyConfig(const TrendConfig &config)
{
    m_channelIds.clear();
    for (const TrendChannelConfig &ch : config.channels)
        m_channelIds.append(ch.regId);

    if (config.sampleIntervalSec != m_intervalSec) {
        m_intervalSec = config.sampleIntervalSec;
        if (m_running)
            m_sampleTimer.setInterval(m_intervalSec * 1000);
    }

#ifdef SR_TREND_DEV_TESTDATA
    m_intervalSec = 1;
    if (m_running)
        m_sampleTimer.setInterval(1000);
#endif

    if (!m_channelIds.isEmpty() && !m_running)
        start();
    else if (m_channelIds.isEmpty() && m_running)
        stop();
}

// ---------------------------------------------------------------------------
// Start / Stop
// ---------------------------------------------------------------------------
void TrendSampler::start()
{
    if (m_running)
        return;
    m_running = true;
    m_buffer.clear();
    m_sampleTimer.start(m_intervalSec * 1000);
    m_flushTimer.start(kFlushSec * 1000);

    Util::Logger::info(QStringLiteral("TrendSampler started (interval=%1s, channels=%2)")
                           .arg(m_intervalSec).arg(m_channelIds.size()));
}

void TrendSampler::stop()
{
    if (!m_running)
        return;
    m_sampleTimer.stop();
    m_flushTimer.stop();
    flush();
    m_running = false;

    Util::Logger::info(QStringLiteral("TrendSampler stopped."));
}

// ---------------------------------------------------------------------------
// Sampling
// ---------------------------------------------------------------------------
void TrendSampler::onSampleTimer()
{
    using RT = DataCollection::Model::RegisterType;
    const qint64 ts = QDateTime::currentSecsSinceEpoch();

    for (int regId : std::as_const(m_channelIds)) {
        const DataCollection::Model::RegisterState st = m_regTable->state(regId);
        if (!st.isValid)
            continue;

        TrendRawPoint pt;
        pt.regId = regId;
        pt.ts    = ts;

#ifdef SR_TREND_DEV_TESTDATA
        // Dev mode: generate a sine wave per channel for frontend testing
        // Each channel gets a different phase via regId, amplitude varies by channel index
        const int chIdx   = m_channelIds.indexOf(regId);
        const double base = 30000.0 + chIdx * 1000.0;
        const double amp  = 25000.0 - chIdx * 500.0;
        const double freq = 0.002 + chIdx * 0.0003;
        pt.value = static_cast<quint16>(base + amp * std::sin(freq * ts + chIdx));
#else
        if (st.config.type == RT::Coil || st.config.type == RT::DiscreteInput)
            pt.value = (!st.rawCoils.isEmpty() && st.rawCoils[0]) ? 1 : 0;
        else
            pt.value = st.rawRegisters.isEmpty() ? 0 : st.rawRegisters[0];
#endif

        m_buffer.append(pt);
    }

    if (m_buffer.size() >= kMaxBuffer)
        flush();
}

void TrendSampler::onFlushTimer()
{
    flush();
}

// ---------------------------------------------------------------------------
// Flush
// ---------------------------------------------------------------------------
void TrendSampler::flush()
{
    if (m_buffer.isEmpty())
        return;

    QString err;
    if (!m_trendDb->insertBatch(m_buffer, err))
        Util::Logger::error(QStringLiteral("TrendSampler flush failed: %1").arg(err));

    m_buffer.clear();
}

} // namespace Trend
