#include "TrendFileReader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>

namespace TrendHandler {

// Convenience aliases for writer constants
static constexpr qint64 kHdr    = TrendFileWriter::kHeaderSize;
static constexpr qint64 kBlk    = TrendFileWriter::kBlockSize;
static constexpr qint64 kBlkHdr = TrendFileWriter::kBlockHeaderSize;
static constexpr qint64 kRec    = TrendFileWriter::kRecordSize;
static constexpr qint64 kRpB    = TrendFileWriter::kRecordsPerBlock;

// ---------------------------------------------------------------------------
// stimeToUnix — STIME[6] = {yy,mm,dd,hh,min,ss} → unix seconds
// ---------------------------------------------------------------------------
quint32 TrendFileReader::stimeToUnix(const quint8 stime[6])
{
    const QDateTime dt(QDate(2000 + stime[0], stime[1], stime[2]),
                       QTime(stime[3], stime[4], stime[5]));
    return dt.isValid() ? static_cast<quint32>(dt.toSecsSinceEpoch()) : 0;
}

// ---------------------------------------------------------------------------
// listFiles
// ---------------------------------------------------------------------------
QList<TrendFileReader::FileInfo> TrendFileReader::listFiles(const QString &dirPath, QString &error)
{
    QList<FileInfo> result;

    QDir dir(dirPath);
    if (!dir.exists()) {
        error = QStringLiteral("Directory not found: %1").arg(dirPath);
        return result;
    }

    const QStringList entries = dir.entryList(QStringList() << QStringLiteral("*.tnd"),
                                              QDir::Files, QDir::Name);
    for (const QString &name : entries) {
        FileInfo info;
        QString parseErr;
        if (!parseHeader(dir.filePath(name), info, parseErr))
            continue;
        result.append(info);
    }

    return result;
}

// ---------------------------------------------------------------------------
// readHeader — raw 512 bytes
// ---------------------------------------------------------------------------
QByteArray TrendFileReader::readHeader(const QString &path, QString &error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { error = f.errorString(); return {}; }

    const QByteArray data = f.read(kHdr);
    if (data.size() < kHdr) { error = QStringLiteral("File too short"); return {}; }
    return data;
}

// ---------------------------------------------------------------------------
// readLatest — 32 bytes of the last written record
// ---------------------------------------------------------------------------
QByteArray TrendFileReader::readLatest(const QString &path, QString &error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { error = f.errorString(); return {}; }

    const qint64 fileSize = f.size();

    // Walk backwards from last possible block until we find one with data
    qint64 blkIdx = (fileSize - kHdr) / kBlk;
    while (blkIdx >= 0) {
        const qint64 blkOff = kHdr + blkIdx * kBlk;
        if (blkOff + kBlkHdr > fileSize) { --blkIdx; continue; }

        f.seek(blkOff);
        const QByteArray blkHdrData = f.read(kBlkHdr);
        if (blkHdrData.size() < kBlkHdr) { --blkIdx; continue; }

        const auto *bh = reinterpret_cast<const TndBlockHeader *>(blkHdrData.constData());
        if (bh->dataCnt == 0) { --blkIdx; continue; }

        const qint64 recOff = blkOff + kBlkHdr + (bh->dataCnt - 1) * kRec;
        if (recOff + kRec > fileSize) { --blkIdx; continue; }

        f.seek(recOff);
        const QByteArray rec = f.read(kRec);
        if (rec.size() < kRec) { --blkIdx; continue; }
        return rec;
    }

    error = QStringLiteral("No records found");
    return {};
}

// ---------------------------------------------------------------------------
// readSample — binary record array, step-sampled between from and to (unix sec)
// ---------------------------------------------------------------------------
QByteArray TrendFileReader::readSample(const QString &path,
                                       quint32 from, quint32 to,
                                       quint16 step, QString &error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { error = f.errorString(); return {}; }

    const qint64 fileSize = f.size();
    if (fileSize < kHdr + kBlkHdr) { error = QStringLiteral("File too short"); return {}; }

    // Parse fixed header
    const QByteArray hdrData = f.read(kHdr);
    if (hdrData.size() < kHdr) { error = QStringLiteral("Header read failed"); return {}; }
    const auto *fh = reinterpret_cast<const TndFixedHeader *>(hdrData.constData());

    if (fh->freq == 0) { error = QStringLiteral("Invalid FREQ=0 in header"); return {}; }
    if (step == 0) step = 1;

    const quint32 startTime = stimeToUnix(fh->stime);
    const quint16 freq      = fh->freq;

    // Convert time range → record indices
    const qint64 riStart = (from >= startTime) ? static_cast<qint64>((from - startTime) / freq) : 0;
    const qint64 riEnd   = (to   >= startTime) ? static_cast<qint64>((to   - startTime) / freq) : -1;

    if (riEnd < riStart) return {};

    QByteArray result;
    // Cap reserve to avoid OOM on huge time ranges
    const qint64 estRecords = qMin((riEnd - riStart) / step + 1, static_cast<qint64>(50000));
    result.reserve(static_cast<int>(estRecords * kRec));

    qint64  curBlkIdx  = -1;
    quint16 curDataCnt = 0;

    for (qint64 ri = riStart; ri <= riEnd; ri += step) {
        const qint64 blkIdx   = ri / kRpB;
        const qint64 recInBlk = ri % kRpB;

        // Load block header when entering a new block
        if (blkIdx != curBlkIdx) {
            const qint64 blkOff = kHdr + blkIdx * kBlk;
            if (blkOff + kBlkHdr > fileSize) break;

            f.seek(blkOff);
            const QByteArray blkHdrData = f.read(kBlkHdr);
            if (blkHdrData.size() < kBlkHdr) break;

            const auto *bh = reinterpret_cast<const TndBlockHeader *>(blkHdrData.constData());
            curBlkIdx  = blkIdx;
            curDataCnt = bh->dataCnt;
        }

        // Past last valid record in this block → end of data
        if (recInBlk >= curDataCnt) break;

        const qint64 recOff = kHdr + blkIdx * kBlk + kBlkHdr + recInBlk * kRec;
        if (recOff + kRec > fileSize) break;

        f.seek(recOff);
        const QByteArray rec = f.read(kRec);
        if (rec.size() < kRec) break;
        result.append(rec);
    }

    return result;
}

// ---------------------------------------------------------------------------
// parseHeader (private) — fills FileInfo from 512-byte header
// ---------------------------------------------------------------------------
bool TrendFileReader::parseHeader(const QString &path, FileInfo &out, QString &error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { error = f.errorString(); return false; }

    const QByteArray hdr = f.read(kHdr);
    if (hdr.size() < kHdr) { error = QStringLiteral("Header too short"); return false; }

    const auto *fh = reinterpret_cast<const TndFixedHeader *>(hdr.constData());
    if (fh->id[0] != 'S' || fh->id[1] != 'W') {
        error = QStringLiteral("Invalid magic");
        return false;
    }

    out.startTime = stimeToUnix(fh->stime);
    out.freq      = fh->freq;
    out.chCount   = fh->chCnt;
    out.fileSize  = f.size();
    out.filename  = QFileInfo(path).fileName();

    quint32 recordCount = 0;
    const qint64 fileSize = out.fileSize;
    if (fileSize >= kHdr + kBlkHdr) {
        const qint64 lastBlkIdx = (fileSize - kHdr) / kBlk;
        const qint64 lastBlkOff = kHdr + lastBlkIdx * kBlk;

        if (lastBlkOff + kBlkHdr <= fileSize) {
            f.seek(lastBlkOff);
            const QByteArray blkHdr = f.read(kBlkHdr);
            if (blkHdr.size() == kBlkHdr) {
                const auto *bh = reinterpret_cast<const TndBlockHeader *>(blkHdr.constData());
                recordCount = static_cast<quint32>(lastBlkIdx) * kRpB + bh->dataCnt;
            }
        }
    }

    out.recordCount = recordCount;
    out.endTime     = out.freq > 0 ? out.startTime + recordCount * out.freq : out.startTime;
    return true;
}

} // namespace TrendHandler
