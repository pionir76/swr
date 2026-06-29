#ifndef PCLINKETH_H
#define PCLINKETH_H

#include "Comm/pclinkdrv.h"
#include <QTcpSocket>

class PcLinkEth : public PcLinkDrv
{
    Q_OBJECT

public:
    PcLinkEth(qintptr s, Pdc* pdc, QObject* parent = 0);

protected:
    virtual void            commClose();
    virtual bool            commOpen();
    virtual bool            read(QByteArray& rcv);
    virtual eCOMM_ERROR     write(const QByteArray& req);

private:
    qintptr                 m_socketDes;
    QTcpSocket*             m_socket;
};

#endif // PCLINKETH_H
