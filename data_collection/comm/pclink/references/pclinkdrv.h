#ifndef PCLINKDRV_H
#define PCLINKDRV_H

#include <QDataStream>
#include <QElapsedTimer>
#include <QtSerialPort/QSerialPort>

#include "Comm/threadworker.h"
#include "Comm/pclinkdef.h"
#include "pdc.h"


class PcLinkDrv : public ThreadWorker
{      
    Q_OBJECT

public:
    PcLinkDrv(Pdc* pdc, const ePCLINK_DEV& type, QObject* parent = 0);

protected:
    virtual bool        onStart();
    virtual void        onIdle();
    virtual void        onStop();

    // must have been defined on child.
    virtual void        commClose() = 0;
    virtual bool        commOpen()  = 0;

    virtual bool        read(QByteArray& rcv) = 0;
    virtual eCOMM_ERROR write(const QByteArray& req) = 0;

private:
    bool                pcCommAddressCheck(PcLinkCmd& cmd, const QByteArray& rcv);
    bool                commandCheck(PcLinkCmd& cmd, const QByteArray& rcv);
    eCOMM_ERROR         pcRxFrameCheck(const QByteArray& rcv);
    eCOMM_ERROR         dataCheck(PcLinkCmd& cmd, const QByteArray& rcv);
    void                doResponse(PcLinkCmd& cmd, const QByteArray& rcv);
    void                errResponse(const eCOMM_ERROR& err, const QByteArray& rcv);

    bool                prepareNextFulData(PcLinkCmd& cmd, QByteArray& res);

protected:
    Pdc*                m_pdc;
    qint16              m_prs;
    QElapsedTimer       m_tmr;
    int                 m_sn;
};

#endif // PCLINKDRV_H
