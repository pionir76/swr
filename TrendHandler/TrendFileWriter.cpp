#include "TrendFileWriter.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>

namespace TrendHandler {

TrendFileWriter::TrendFileWriter() = default;

TrendFileWriter::~TrendFileWriter()
{
    close();
}

// ---------------------------------------------------------------------------
// open : Create new trend file and write header
// keep file open for subsequent appendRecord() calls
// ---------------------------------------------------------------------------
bool TrendFileWriter::open(const QString &path, const TndWriterConfig &config,
                           const QDateTime &startTime, QString &error)
{
    close();

    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        error = QStringLiteral("Failed to create directory: %1").arg(QFileInfo(path).absolutePath());
        return false;
    }

    m_file.setFileName(path);
    if (!m_file.open(QIODevice::WriteOnly)) {
        error = QStringLiteral("Failed to open file: %1").arg(m_file.errorString());
        return false;
    }

    // --- Build fixed header (64 bytes) ---
    TndFixedHeader fh;
    fh.chCnt = config.chCnt;
    fh.freq  = config.freq;
    fh.zcode = config.zcode;
    fh.fver  = config.fver;

    // Use caller-supplied startTime so filename and STIME header are identical
    fh.stime[0] = static_cast<quint8>(startTime.date().year() % 100);
    fh.stime[1] = static_cast<quint8>(startTime.date().month());
    fh.stime[2] = static_cast<quint8>(startTime.date().day());
    fh.stime[3] = static_cast<quint8>(startTime.time().hour());
    fh.stime[4] = static_cast<quint8>(startTime.time().minute());
    fh.stime[5] = static_cast<quint8>(startTime.time().second());

    if (m_file.write(reinterpret_cast<const char*>(&fh), sizeof(fh)) != sizeof(fh)) {
        error = QStringLiteral("Header write failed: %1").arg(m_file.errorString());
        m_file.close();
        return false;
    }

    // --- Channel area: always write 16 slots (unused = zero) ---
    for (int i = 0; i < 16; ++i) {
        const TndChannelInfo &ch = (i < config.chCnt) ? config.channels[i] : TndChannelInfo{};
        if (m_file.write(reinterpret_cast<const char*>(&ch), sizeof(ch)) != sizeof(ch)) {
            error = QStringLiteral("Channel info write failed: %1").arg(m_file.errorString());
            m_file.close();
            return false;
        }
    }

    // --- First block header ---
    TndBlockHeader bh;
    if (m_file.write(reinterpret_cast<const char*>(&bh), sizeof(bh)) != sizeof(bh)) {
        error = QStringLiteral("Block header write failed: %1").arg(m_file.errorString());
        m_file.close();
        return false;
    }

    m_blkIndex    = 0;
    m_dataCnt     = 0;
    m_recordCount = 0;
    return true;
}

// ---------------------------------------------------------------------------
// appendRecord
// ---------------------------------------------------------------------------
bool TrendFileWriter::appendRecord(const quint16 values[16], QString &error)
{
    if (!m_file.isOpen()) {
        error = QStringLiteral("File not open");
        return false;
    }

    // At block boundary a record write also emits pad(24) + next block header(8) = +32 extra bytes
    const qint64 worstCase = (m_dataCnt == kRecordsPerBlock - 1)
                             ? kRecordSize + kBlockPadSize + kBlockHeaderSize
                             : kRecordSize;
    if (fileSize() + worstCase > kMaxFileSize) {
        error = QStringLiteral("capacity_exceeded");
        return false;
    }

    // Write record at its exact offset
    const qint64 recOff = blockOffset() + kBlockHeaderSize
                          + static_cast<qint64>(m_dataCnt) * kRecordSize;

    if (!m_file.seek(recOff)) {
        error = m_file.errorString();
        return false;
    }
    if (m_file.write(reinterpret_cast<const char*>(values), kRecordSize) != kRecordSize) {
        error = m_file.errorString();
        return false;
    }

    m_dataCnt++;
    m_recordCount++;

    // Update DATACNT in current block header
    if (!m_file.seek(blockOffset() + 4)) {
        error = m_file.errorString();
        return false;
    }
    if (m_file.write(reinterpret_cast<const char*>(&m_dataCnt), sizeof(quint16)) != sizeof(quint16)) {
        error = m_file.errorString();
        return false;
    }

    // Block full → pad remaining 24 bytes and start next block
    if (m_dataCnt == kRecordsPerBlock) {
        const qint64 padOff = blockOffset() + kBlockHeaderSize
                              + static_cast<qint64>(kRecordsPerBlock) * kRecordSize;
        if (!m_file.seek(padOff)) {
            error = m_file.errorString();
            return false;
        }
        if (m_file.write(QByteArray(kBlockPadSize, '\0')) != kBlockPadSize) {
            error = m_file.errorString();
            return false;
        }

        // Write next block header before advancing internal state so that
        // an I/O failure here leaves state consistent with what is on disk.
        const quint32 nextBlkIndex = m_blkIndex + 1;
        const qint64  nextBlkOff   = kHeaderSize + static_cast<qint64>(nextBlkIndex) * kBlockSize;

        if (!m_file.seek(nextBlkOff)) {
            error = m_file.errorString();
            return false;
        }

        TndBlockHeader bh;
        bh.blkIndex = nextBlkIndex;
        if (m_file.write(reinterpret_cast<const char*>(&bh), sizeof(bh)) != sizeof(bh)) {
            error = QStringLiteral("Block header write failed: %1").arg(m_file.errorString());
            return false;
        }

        m_blkIndex = nextBlkIndex;
        m_dataCnt  = 0;
    }

    m_file.flush();
    return true;
}

// ---------------------------------------------------------------------------
// close
// ---------------------------------------------------------------------------
void TrendFileWriter::close()
{
    if (m_file.isOpen())
        m_file.close();

    m_blkIndex    = 0;
    m_dataCnt     = 0;
    m_recordCount = 0;
}

bool TrendFileWriter::isOpen() const
{
    return m_file.isOpen();
}

qint64 TrendFileWriter::fileSize() const
{
    return m_file.size();
}

qint64 TrendFileWriter::blockOffset() const
{
    return kHeaderSize + static_cast<qint64>(m_blkIndex) * kBlockSize;
}

} // namespace TrendHandler
