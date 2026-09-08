#include "TrendFileRecorder.h"

#include <QDir>
#include <cmath>
#include <cstdlib>
#include "../data_collection/model/UnifiedRegister.h"
#include "../utils/Logger.h"

namespace TrendHandler {

TrendFileRecorder::TrendFileRecorder(DataCollection::Store::RegisterTable *regTable,
                                     QObject *parent)
    : QObject(parent)
    , m_regTable(regTable)
{
    connect(&m_timer, &QTimer::timeout, this, &TrendFileRecorder::onTimer);
}

// ---------------------------------------------------------------------------
// start
// ---------------------------------------------------------------------------
bool TrendFileRecorder::start(const TrendConfig &config, QString &error)
{
    if (m_recording) {
        error = QStringLiteral("Already recording");
        return false;
    }

    if (config.channels.isEmpty()) {
        error = QStringLiteral("No channels configured");
        return false;
    }

    const QString dir = QStringLiteral(SR_TFILE_DIR);
    if (!QDir().mkpath(dir)) {
        error = QStringLiteral("Failed to create directory: %1").arg(dir);
        return false;
    }

    const QDateTime now = QDateTime::currentDateTime();
    m_filename = now.toString(QStringLiteral("yyMMdd_HHmmss")) + QStringLiteral(".tnd");
    const QString path = dir + QStringLiteral("/") + m_filename;

    TndWriterConfig wcfg;
    buildWriterConfig(config, wcfg);

    // open() must use the same 'now' so filename and STIME header match exactly
    if (!m_writer.open(path, wcfg, now, error))
        return false;

    m_channelIds.clear();
    const int chCnt = qMin(config.channels.size(), 16);  // guard: never exceed values[16]
    for (int i = 0; i < chCnt; ++i)
        m_channelIds.append(config.channels[i].regId);

    m_freq      = config.sampleIntervalSec;
    m_startTime = now;
    m_recording = true;

    m_timer.start(m_freq * 1000);

    Util::Logger::info(QStringLiteral("TrendFileRecorder started: %1 (freq=%2s, channels=%3)")
                           .arg(m_filename).arg(m_freq).arg(m_channelIds.size()));
    return true;
}

// ---------------------------------------------------------------------------
// stop
// ---------------------------------------------------------------------------
void TrendFileRecorder::stop()
{
    if (!m_recording)
        return;

    m_timer.stop();
    m_lastRecordCount = m_writer.recordCount();
    m_lastFileSize    = m_writer.fileSize();
    m_writer.close();
    m_recording = false;

    Util::Logger::info(QStringLiteral("TrendFileRecorder stopped: %1 (records=%2)")
                           .arg(m_filename).arg(m_lastRecordCount));
}

// ---------------------------------------------------------------------------
// status
// ---------------------------------------------------------------------------
TrendFileRecorder::Status TrendFileRecorder::status() const
{
    Status s;
    s.recording   = m_recording;
    s.filename    = m_filename;
    s.elapsedSec  = m_recording ? m_startTime.secsTo(QDateTime::currentDateTime()) : 0;
    s.fileSize    = m_recording ? m_writer.fileSize() : m_lastFileSize;
    s.recordCount = m_recording ? m_writer.recordCount() : m_lastRecordCount;
    s.freq        = m_freq;
    return s;
}

// ---------------------------------------------------------------------------
// onTimer
// ---------------------------------------------------------------------------
void TrendFileRecorder::onTimer()
{
    using RT = DataCollection::Model::RegisterType;

    quint16 values[16] = {};

    for (int i = 0; i < m_channelIds.size(); ++i) {
#ifdef SR_TREND_DEV_TESTDATA
        const qint64 ts = QDateTime::currentSecsSinceEpoch();
        const double base = 30000.0 + i * 1000.0;
        const double amp  = 25000.0 - i * 500.0;
        const double freq = 0.002   + i * 0.0003;
        values[i] = static_cast<quint16>(base + amp * std::sin(freq * ts + i));
#else
        const DataCollection::Model::RegisterState st = m_regTable->state(m_channelIds[i]);
        if (!st.isValid)
            continue;
        if (st.config.type == RT::Coil || st.config.type == RT::DiscreteInput)
            values[i] = (!st.rawCoils.isEmpty() && st.rawCoils[0]) ? 1 : 0;
        else
            values[i] = st.rawRegisters.isEmpty() ? 0 : st.rawRegisters[0];
#endif
    }

    QString err;
    if (!m_writer.appendRecord(values, err)) {
        if (err == QLatin1String("capacity_exceeded")) {
            stop();
            Util::Logger::error(QStringLiteral("TrendFileRecorder: capacity exceeded, recording stopped."));
        } else {
            Util::Logger::error(QStringLiteral("TrendFileRecorder: write error: %1").arg(err));
        }
    }
}

// ---------------------------------------------------------------------------
// buildWriterConfig
// ---------------------------------------------------------------------------
void TrendFileRecorder::buildWriterConfig(const TrendConfig &config, TndWriterConfig &out) const
{
    out.chCnt = static_cast<quint8>(qMin(config.channels.size(), 16));
    out.freq  = static_cast<quint16>(config.sampleIntervalSec);
    out.zcode = static_cast<quint8>(std::atoi(SR_ZCODE));
    out.fver  = static_cast<quint8>(std::atoi(SR_VERSION));

    for (int i = 0; i < out.chCnt; ++i) {
        const TrendChannelConfig &src = config.channels[i];
        TndChannelInfo &dst = out.channels[i];

        const QByteArray tag = src.tag.left(16).toLatin1().leftJustified(16, '\0', true);
        memcpy(dst.chtag, tag.constData(), 16);

        dst.inrh     = src.maxValue;
        dst.inrl     = src.minValue;
        dst.dotpos   = scaleToDotpos(src.scale);
        dst.isSigned = src.isSigned ? 1 : 0;

        const QByteArray unit = src.unit.toLatin1().leftJustified(4, '\0', true);
        memcpy(dst.unit, unit.constData(), 4);
    }
}

// ---------------------------------------------------------------------------
// scaleToDotpos
// scale=1.0 → 0, scale=0.1 → 1, scale=0.01 → 2
// ---------------------------------------------------------------------------
quint8 TrendFileRecorder::scaleToDotpos(double scale)
{
    if (scale <= 0.0 || scale >= 1.0)
        return 0;
    quint8 dp = 0;
    double s = scale;
    while (s < 1.0 && dp < 4) { s *= 10.0; ++dp; }
    return dp;
}

} // namespace TrendHandler
