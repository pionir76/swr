#pragma once

#include <QFile>
#include <QString>
#include <QtGlobal>

namespace TrendHandler {

// ---------------------------------------------------------------------------
// Binary layout structs — packed to match .tnd file format exactly
// ---------------------------------------------------------------------------
#pragma pack(push, 1)

struct TndFixedHeader {           // 64 bytes
    char    id[2]        = {'S', 'W'};
    quint8  mtype        = 0x30;
    quint8  ver          = 1;
    quint8  rev          = 0;
    quint8  chCnt        = 0;     // actual channel count (1~16)
    quint8  stime[6]     = {};    // yymmddhhmmss (binary, not BCD)
    quint16 freq         = 10;    // sample interval (sec)
    quint8  zcode        = 0;
    quint8  fver         = 0;
    quint8  reserved[48] = {};
};
static_assert(sizeof(TndFixedHeader) == 64, "TndFixedHeader size mismatch");

struct TndChannelInfo {           // 28 bytes per channel
    char    chtag[16]    = {};    // tag name (ASCII, null-padded, max 16 chars)
    quint16 inrh         = 65535; // upper raw limit (uint16)
    quint16 inrl         = 0;     // lower raw limit (uint16)
    quint8  dotpos       = 0;     // decimal places (0=int, 1→×0.1, 2→×0.01)
    quint8  isSigned     = 0;     // 0: unsigned, 1: signed
    char    unit[4]      = {};    // unit string (ASCII, null-padded)
    quint8  reserved[2]  = {};
};
static_assert(sizeof(TndChannelInfo) == 28, "TndChannelInfo size mismatch");

struct TndBlockHeader {           // 8 bytes
    quint32 blkIndex     = 0;
    quint16 dataCnt      = 0;     // valid record count in this block
    quint8  reserved[2]  = {};
};
static_assert(sizeof(TndBlockHeader) == 8, "TndBlockHeader size mismatch");

#pragma pack(pop)

// ---------------------------------------------------------------------------
// Config passed to open()
// ---------------------------------------------------------------------------
struct TndWriterConfig {
    quint8        chCnt   = 0;
    quint16       freq    = 10;
    quint8        zcode   = 0;
    quint8        fver    = 0;
    TndChannelInfo channels[16] = {};
};

// ---------------------------------------------------------------------------
// TrendFileWriter
// Writes .tnd binary trend files.
// Call open() → appendRecord() repeatedly → close().
// ---------------------------------------------------------------------------
class TrendFileWriter
{
public:
    TrendFileWriter();
    ~TrendFileWriter();

    bool open(const QString &path, const TndWriterConfig &config, QString &error);
    bool appendRecord(const quint16 values[16], QString &error);
    void close();

    bool    isOpen()      const;
    qint64  fileSize()    const;
    quint32 recordCount() const { return m_recordCount; }

    static constexpr qint64 kMaxFileSize     = 4LL * 1024 * 1024 * 1024;
    static constexpr int    kHeaderSize      = 512;
    static constexpr int    kBlockSize       = 1024;
    static constexpr int    kBlockHeaderSize = 8;
    static constexpr int    kRecordSize      = 32;   // 16 × uint16
    static constexpr int    kRecordsPerBlock = 31;
    static constexpr int    kBlockPadSize    = kBlockSize - kBlockHeaderSize
                                               - kRecordsPerBlock * kRecordSize; // 24

private:
    qint64 blockOffset() const;

    QFile   m_file;
    quint32 m_blkIndex    = 0;
    quint16 m_dataCnt     = 0;
    quint32 m_recordCount = 0;
};

} // namespace TrendHandler
