#include "pclinketh.h"
#include "logger.h"
#include "helper.h"

#include <QThread>

#define PCLINK_ETH_WRITE_TIMEOUT        200
#define PCLINK_ETH_READ_TIMEOUT       60000

PcLinkEth::PcLinkEth(qintptr s, Pdc* pdc, QObject* parent) : PcLinkDrv(pdc, PCLINK_ETH, parent)
{
    m_socketDes = s;
    m_socket = nullptr;
}

void PcLinkEth::commClose()
{
    if(m_socket){
        m_socket->close();

        delete m_socket;
        m_socket = nullptr;
    }

    Logger log(SYSTEM_LOG_FILE);
    log.write(">> PCLINK ETHERNET HAD CLOSED");
}

bool PcLinkEth::commOpen()
{
    if(m_socket){
        m_socket->close();

        delete m_socket;
        m_socket = nullptr;
    }
    m_socket = new QTcpSocket();

    if(!m_socket->setSocketDescriptor(m_socketDes)){
        qDebug() << m_socket->errorString();
        return false;
    }

    connect(m_socket,
            SIGNAL(disconnected()),
            this,
            SLOT(stopWork()));

    Logger log(SYSTEM_LOG_FILE);
    log.write(">> PCLINK ETHERNET HAD OPENED");

    return true;
}

bool  PcLinkEth::read(QByteArray& rcv)
{
    if(m_socket->waitForReadyRead(PCLINK_ETH_READ_TIMEOUT)) {
        rcv = m_socket->readAll();

        while(m_socket->waitForReadyRead(1)){
            rcv += m_socket->readAll();
            QThread::msleep(1);
        }
    }

    else{
        //---------------------------------------------------------------//
        // we have to do close connection since
        // there are any no request for TCPSERVER_WAIT_TIME
        // it's make sure avoid ghost connection.
        //---------------------------------------------------------------//
        m_socket->close();
    }
    return true;
}

eCOMM_ERROR  PcLinkEth::write(const QByteArray& req)
{
    // sometimes there are several casese that we don't need to reply.
    if(req.length()>0){
        if(m_socket->write(req) > 0){
            m_socket->waitForBytesWritten(PCLINK_ETH_WRITE_TIMEOUT);
        }
    }

    return eCOMM_ERROR::ECODE_NOERROR;
}
