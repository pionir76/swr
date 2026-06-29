#include "pclinkdrv.h"
#include "logger.h"
#include "helper.h"

#include <QDirIterator>
#include <QDebug>

PcLinkDrv::PcLinkDrv(Pdc* pdc, const ePCLINK_DEV& type, QObject* parent) : ThreadWorker(parent)
{
    m_pdc  = pdc;

    if(type == PCLINK_ETH)  m_prs = MEMLC.ETH_PRS;
    else                    m_prs = MEMLC.DBP_PRS;
}

void PcLinkDrv::onStop()
{
    commClose();
}

bool PcLinkDrv::onStart()
{
    if(!commOpen()){
        return false;
    }

    m_sn = 0;
    if(m_prs == PRS_PCLINK_SUM ||
       m_prs == PRS_TCP_SUM){
        m_sn=2;
    }

    setPollingTm(50);
    return true;
}

void PcLinkDrv::onIdle()
{
    QByteArray rcv;

    if(read(rcv)){
        if(rcv.length()<(8+m_sn)){
            return;
        }

        if(m_prs == PRS_TCP_SUM){
            rcv = QString(rcv).toLocal8Bit();
        }

        eCOMM_ERROR err = ECODE_NOERROR;

        PcLinkCmd cmd;
        cmd.err   = ECODE_NOERROR;
        cmd.broad = false;
        cmd.cnt   = 0;

        if(pcCommAddressCheck(cmd, rcv)){
            err = pcRxFrameCheck(rcv);

            if(err != ECODE_NOERROR){
                errResponse(err, rcv);
                return;
            }

            if(!commandCheck(cmd, rcv)){                
                errResponse(ECODE_CMD, rcv);
                return;
            }

            if     (cmd.type ==    AMI_CMD) { if(rcv.length()!=  8+m_sn ) err = ECODE_FORMAT; } // [STX][01][AMI][CR][LF]
            else if(cmd.type ==    RSD_CMD) { if(rcv.length()!= 16+m_sn ) err = ECODE_FORMAT; } // [STX][01][RSD,01,0001][CR][LF]
            else if(cmd.type ==    RRD_CMD) { if(rcv.length() < 16+m_sn ) err = ECODE_FORMAT; } // [STX][01][RRD,01,0001][CR][LF]

#ifdef FDA_OPTION

            else if(cmd.type ==    WSD_CMD) { // [STX][01][WSD,01,0001,00A3][UVF][CR][LF]
                if(MEMLC.REMOTELINK_USE==UNUSE){
                    if(rcv.length() < 25+m_sn ) err = ECODE_FORMAT;
                }
                else{
                    if(rcv.length() < 21+m_sn ) err = ECODE_FORMAT;
                }
            }

            else if(cmd.type ==    WRD_CMD) {  // [STX][01][WRD,01,0001,00A3][UVF][CR][LF]
                if(MEMLC.REMOTELINK_USE==UNUSE){
                    if(rcv.length() < 25+m_sn ) err = ECODE_FORMAT;
                }
                else{
                    if(rcv.length() < 21+m_sn ) err = ECODE_FORMAT;
                }
            }

#else
            else if(cmd.type ==    WSD_CMD) { if(rcv.length() < 21+m_sn ) err = ECODE_FORMAT; } // [STX][01][WSD,01,0001,00A3][CR][LF]
            else if(cmd.type ==    WRD_CMD) { if(rcv.length() < 21+m_sn ) err = ECODE_FORMAT; } // [STX][01][WRD,01,0001,00A3][CR][LF]
#endif
            else if(cmd.type ==    TCM_CMD) { if(rcv.length() <  0+m_sn ) err = ECODE_FORMAT; } // CHECK !!
            else if(cmd.type ==    TNO_CMD) { if(rcv.length() <  0+m_sn ) err = ECODE_FORMAT; } // CHECK !!
            else if(cmd.type ==    SET_CMD) { if(rcv.length() <  0+m_sn ) err = ECODE_FORMAT; } // CHECK !!
            else if(cmd.type == MODA03_CMD) { if(rcv.length()!= 17 )      err = ECODE_FORMAT; } // [:][01][03][0000][0000][LRC][CR][LF]
            else if(cmd.type == MODA06_CMD) { if(rcv.length()!= 17 )      err = ECODE_FORMAT; } // [:][01][06][0000][0000][LRC][CR][LF]
            else if(cmd.type == MODA08_CMD) { if(rcv.length()!= 17 )      err = ECODE_FORMAT; } // [:][01][08][0000][0000][LRC][CR][LF]
            else if(cmd.type == MODA16_CMD) { if(rcv.length() < 23 )      err = ECODE_FORMAT; } // [:][01][10][0000][0001][02][00FF][LRC][CR][LF]
            else if(cmd.type == MODR03_CMD) { if(rcv.size()   != 8 )      err = ECODE_FORMAT; } // [01][03][0001][0002][CRC]
            else if(cmd.type == MODR06_CMD) { if(rcv.size()   != 8 )      err = ECODE_FORMAT; } // [01][06][0001][0002][CRC]
            else if(cmd.type == MODR08_CMD) { if(rcv.size()   != 8 )      err = ECODE_FORMAT; } // [01][08][0001][0002][CRC]
            else if(cmd.type == MODR16_CMD) { if(rcv.size()   < 11 )      err = ECODE_FORMAT; } // [01][10][0000][0001][02][00FF][CRC]
            else if(cmd.type == MODT03_CMD) { if(rcv.size()  != 12 )      err = ECODE_FORMAT; } // [015E 0000 0006][01][03][0004][0006]
            else if(cmd.type == MODT06_CMD) { if(rcv.size()  != 12 )      err = ECODE_FORMAT; } // [015E 0000 0006][01][06][0001][0002]
            else if(cmd.type == MODT08_CMD) { if(rcv.size()  != 12 )      err = ECODE_FORMAT; } // [015E 0000 0006][01][08][0001][0002]
            else if(cmd.type == MODT16_CMD) { if(rcv.size()   < 15 )      err = ECODE_FORMAT; } // [015E 0000 0006][01][10][0000][0001][02][00FF]

            if(err != ECODE_NOERROR){
                errResponse(err, rcv);
                return;
            }

#ifdef FDA_OPTION
            if(MEMLC.REMOTELINK_USE==UNUSE){
                //---------------------------------------------------//
                // FDA SPECIAL : FDA NOT SUPPORT WRITE ON MODBUS
                //---------------------------------------------------//
                if(cmd.type == MODA06_CMD || cmd.type == MODA16_CMD ||
                   cmd.type == MODR06_CMD || cmd.type == MODR16_CMD ||
                   cmd.type == MODT06_CMD || cmd.type == MODT16_CMD){
                    errResponse(ECODE_CMD, rcv);
                    return;
                }


                //---------------------------------------------------//
                // FDA SPECIAL : EXTRECT USR ID & PW
                //---------------------------------------------------//
                else if(cmd.type == WSD_CMD  || cmd.type == WRD_CMD){
                    // MUST HAVE TWO ";" CHARATER
                    if(rcv.count(";") != 2){
                        errResponse(ECODE_FORMAT, rcv);
                        return;
                    }

                    int sp = rcv.indexOf(";")+1;
                    int ep = rcv.size()-4;

                    QByteArray a1 = rcv.right(rcv.size()-sp);
                    QByteArray a2 = a1.left(a1.size() - 4);

                    QString usrId = a2.left(a2.indexOf(";"));
                    QString usrPw = a2.right(a2.size() - a2.indexOf(";")-1);

                    if(usrId.length()==0 || usrPw.length()==0){
                        errResponse(ECODE_FORMAT, rcv);
                        return;
                    }

                    //-----------------------------------------------------//
                    // JUST CHECK USER INFO OR CHECK AND LOGIN
                    //-----------------------------------------------------//
                    bool bLogin = false;
                    if(cmd.type == WSD_CMD || cmd.type == WRD_CMD){
                        bLogin = true;
                    }

                    //-----------------------------------------------------//
                    // USER ID AUTHORITY CHECK.
                    //-----------------------------------------------------//
                    if(!m_pdc->checkCommWriteAble(usrId, usrPw, bLogin, true)){
                        errResponse(ECODE_AUTHORITY, rcv);
                        return;
                    }

                    //-----------------------------------------------------//
                    // REMOVE USER ID/PW FORM RECIVE MSG
                    //-----------------------------------------------------//
                    rcv = rcv.remove(sp-1, ep-sp+1);
                }
            }
#endif
            err = dataCheck(cmd, rcv);
            if(err != ECODE_NOERROR){
                errResponse(err, rcv);
                return;
            }

            else{
                if(cmd.cnt > MAX_DREG_CNT){
                    errResponse(ECODE_NUMBER, rcv);
                    return;
                }
                doResponse(cmd, rcv);
            }
        }
        //qDebug() << ">> PC LINK OPERATION TOOK : " << m_tmr.elapsed() << "(ms)";
    }
}

void PcLinkDrv::errResponse(const eCOMM_ERROR& err, const QByteArray& rcv)
{
    if(err == ECODE_NORESP){
        return;
    }

    QByteArray res="";
    if(m_prs == PRS_PCLINK || m_prs == PRS_PCLINK_SUM || m_prs == PRS_TCP_SUM){
        res = QString("%1").arg(MEMLC.DBP_ADR, 2, 10, QChar('0')).toLocal8Bit() +
                  "NG" +
                  QString("%1").arg(err, 2, 10, QChar('0')).toLocal8Bit();

        if(m_prs == PRS_PCLINK_SUM ||
           m_prs == PRS_TCP_SUM){
            makePcLinkCheckSum(res);
        }
        write(STX+res+CR+LF);
    }

    else if(m_prs == PRS_MOD_ASC){
        //------------------------------------------------------//
        // STRING : [:][01][03+80][08][LRC][CR][LF] ":018308"
        //------------------------------------------------------//
        bool ok = true;
        int  fc = rcv.mid(3, 2).toInt(&ok, 16)+0x80;// .toInt(&ok, 16) + 0x08;

        res = rcv.mid(1, 2);
        res.append(QString("%1").arg( fc, 2, 16, QChar('0')).toUpper());
        res.append(QString("%1").arg(err, 2, 16, QChar('0')).toUpper());

        lrcSet(res);
        write(":"+res+CR+LF);
    }

    else if(m_prs == PRS_MOD_RTU){
        //------------------------------------------------------//
        // BINARY : [01][03][0001][0002][CRC]
        //------------------------------------------------------//
        res.append((char)rcv[0]);         // Unit ID
        res.append((char)(rcv[1]+0x80));  // Func Code + 0x80
        res.append((char)err);            // Err Code

        crcSet(res);
        write(res);
    }

    else if(m_prs == PRS_MOD_TCP){
        //------------------------------------------------------//
        // BINARY : "015E 0000 0006  01   03 [00 04 00 06] "
        //------------------------------------------------------//
        res = rcv.mid(0, 4);
        int dl = 3;

        res.append((char)(dl>>8));        // Data Length
        res.append((char)dl);             // Data Length
        res.append((char)rcv[6]);         // Unit ID
        res.append((char)(rcv[7]+0x80));  // Func Code + 0x80
        res.append((char)err);            // Err Code

        write(res);
    }
}

eCOMM_ERROR PcLinkDrv::pcRxFrameCheck(const QByteArray& rcv)
{
    if(m_prs == PRS_PCLINK){
        //------------------------------------------------------------//
        // [STX][01][RSD],[03],[0005][CR]+[LF]
        // [STX][01][AMI][CR]+[LF] : minimum lenght : 8
        //------------------------------------------------------------//
        if(rcv.length()<(8+m_sn))       return ECODE_NORESP;
        if(rcv[0] != STX)               return ECODE_FORMAT;
        if(rcv[rcv.length()-1] != LF)   return ECODE_FORMAT;
        if(rcv[rcv.length()-2] != CR)   return ECODE_FORMAT;
    }

    else if(m_prs == PRS_PCLINK_SUM || m_prs == PRS_TCP_SUM){
        //------------------------------------------------------------//
        // [STX][01][RSD],[03],[0005][SUM][CR]+[LF]
        // [STX][01][AMI][SUM][CR]+[LF] : minimum lenght : 10
        //------------------------------------------------------------//
        if(rcv.length()<(8+m_sn))       return ECODE_NORESP;
        if(rcv[0] != STX)               return ECODE_FORMAT;
        if(rcv[rcv.length()-1] != LF)   return ECODE_FORMAT;
        if(rcv[rcv.length()-2] != CR)   return ECODE_FORMAT;
        if(!checkPcLinkSum(rcv)){
            return ECODE_SUM;
        }
    }

    else if(m_prs == PRS_MOD_ASC){
        //------------------------------------------------------------//
        // STRING : [:][01][03][0001][0002][LRC][CR][LF]
        // STRING : [:][01][08][0000][0000][LRC][CR][LF] : minimum lengh : 16
        //------------------------------------------------------------//
        if(rcv.length()<16)             return ECODE_NORESP;
        if(rcv[0] != ':')               return ECODE_FORMAT;
        if(rcv[rcv.length()-1] != LF)   return ECODE_FORMAT;
        if(rcv[rcv.length()-2] != CR)   return ECODE_FORMAT;
        if(!lrcCheck(rcv))              return ECODE_SUM;
    }

    else if(m_prs == PRS_MOD_RTU){
        //------------------------------------------------------------//
        // BINARY : [01][03][0001][0002][CRC]
        //          [01][08][0000][0000][CRC] : minimum lengh : 7
        //------------------------------------------------------------//
        if(rcv.size()<8)                return ECODE_NORESP;
        if(!crcCheck(rcv))              return ECODE_SUM;
    }

    else if(m_prs == PRS_MOD_TCP){
        //------------------------------------------------------------//
        //  |Transaction Id(2) | Protocol Id(2) |   Length(2)   | Unit Id(1) | Func Code(1) | Data []|
        // BINARY : "015E 0000 0006  01   03 [00 04 00 06] "
        //           015E 0000 0006 [01] [08][0000][0000] : minimum lengh : 12
        //------------------------------------------------------------//
        bool ok = true;
        int pid = rcv.mid(2, 2).toHex().toInt(&ok, 16);  if(ok == false) return ECODE_NORESP;
        int cnt = rcv.mid(4, 2).toHex().toInt(&ok, 16);  if(ok == false) return ECODE_NORESP;

        if(pid != 0)                                 return ECODE_NORESP; // Protocol ID must be 0
        if(cnt != rcv.right(rcv.length()-6).size())  return ECODE_NORESP; // Data size must be same with (data - header)
    }

    return ECODE_NOERROR;
}

bool PcLinkDrv::pcCommAddressCheck(PcLinkCmd& cmd, const QByteArray& rcv)
{
    if(m_prs == PRS_PCLINK || m_prs == PRS_PCLINK_SUM || m_prs == PRS_TCP_SUM){
        if(rcv[0] == STX){
            int addr = rcv.mid(1, 2).toInt();
            if(addr != 0xff && addr < 100){
                if(addr == 0){
                    cmd.broad = true;
                    return true;
                }

                // We don't check address under ethernet connection.
                if(m_prs == PRS_TCP_SUM){
                    return true;
                }

                if(addr == MEMLC.DBP_ADR){
                    return true;
                }
            }
        }
    }

    else if(m_prs == PRS_MOD_ASC){
        //-------------------------------------------------------//
        // 7bit
        // ASCII : [:][01][03][0001][0002][LRC][CR][LF] - String
        //-------------------------------------------------------//
        if(rcv[0] == ':'){
            bool ok  = false;
            int addr = rcv.mid(1, 2).toInt(&ok, 16);
            if(ok == false) return false;

            if(addr != 0xff){
                if(addr == 0){
                    cmd.broad = true;
                    return true;
                }

                else if(addr == MEMLC.DBP_ADR){
                    return true;
                }
            }
        }
    }

    else if(m_prs == PRS_MOD_RTU){
        //-------------------------------------------------------//
        // 8bit
        // RTU   : [01][03][0001][0002][CRC] - Binary
        //-------------------------------------------------------//
        if(rcv.size() > 2){
            int addr = (int)rcv[0];

            if(addr == 0){
                cmd.broad = true;
                return true;
            }

            else if(addr == MEMLC.DBP_ADR){
                return true;
            }
        }
    }

    else if(m_prs == PRS_MOD_TCP){
        //-----------------------------------------------------------------------------------------------------------------//
        // Modbus TCP
        // |Transaction Id(2) | Protocol Id(2) |   Length(2)   | Unit Id(1) | Func Code(1) | Data []|
        // Example F.16> "015B 0000 001B 01 10 [00 00 00 0A 14 00 11 00 22 00 33 00 44 00 55 00 66 00 77 00 88 00 99 12 FF]"
        // Example F.03> "015E 0000 0006 01 03 [00 04 00 06] "
        //-----------------------------------------------------------------------------------------------------------------//
        if(rcv.size() > 7){
            int addr = (int)rcv[6];
            if(addr == 0){
                cmd.broad = true;
                return true;
            }

            else{
                // We don't check address.
                return true;
            }
        }
    }

    return false;
}

bool PcLinkDrv::commandCheck(PcLinkCmd& cmd, const QByteArray& rcv)
{
    if(m_prs == PRS_PCLINK || m_prs == PRS_PCLINK_SUM || m_prs == PRS_TCP_SUM){
        //------------------------------------------------------------//
        // [STX][01][RSD],[03],[0005][CR]+[LF]
        //------------------------------------------------------------//
        QByteArray str = rcv.mid(3, 3);

        if(str == "AMI")          { cmd.type = AMI_CMD;    return true; }
        else if(str == "RSD")     { cmd.type = RSD_CMD;    return true; }
        else if(str == "RRD")     { cmd.type = RRD_CMD;    return true; }
        else if(str == "WSD")     { cmd.type = WSD_CMD;    return true; }
        else if(str == "WRD")     { cmd.type = WRD_CMD;    return true; }
        else if(str == "TCM")     { cmd.type = TCM_CMD;    return true; }
        else if(str == "TNO")     { cmd.type = TNO_CMD;    return true; }
        else if(str == "SET")     { cmd.type = SET_CMD;    return true; }
        else if(str == "LOF")     { cmd.type = LOF_CMD;    return true; }

        else return false;
    }

    else if(m_prs == PRS_MOD_ASC){
        //------------------------------------------------------------//
        // STRING : [:][01][03][0001][0002][LRC][CR][LF]
        //------------------------------------------------------------//
        QByteArray str = rcv.mid(3, 2);

        if(str == "03")           { cmd.type = MODA03_CMD;     return true; }
        else if(str == "06")      { cmd.type = MODA06_CMD;     return true; }
        else if(str == "08")      { cmd.type = MODA08_CMD;     return true; }
        else if(str == "10")      { cmd.type = MODA16_CMD;     return true; }
        else return false;
    }

    else if(m_prs == PRS_MOD_RTU){
        //------------------------------------------------------------//
        // BINARY : [01][03][0001][0002][CRC]
        //------------------------------------------------------------//
        if(rcv[1] == 0x03)         { cmd.type = MODR03_CMD;     return true; }
        else if(rcv[1] == 0x06)    { cmd.type = MODR06_CMD;     return true; }
        else if(rcv[1] == 0x08)    { cmd.type = MODR08_CMD;     return true; }
        else if(rcv[1] == 0x10)    { cmd.type = MODR16_CMD;     return true; }
        else return false;
    }

    else if(m_prs == PRS_MOD_TCP){
        //------------------------------------------------------------//
        // BINARY : "015E 0000 0006 01 03 [00 04 00 06] "
        //------------------------------------------------------------//
        if(rcv[7] == 0x03)         { cmd.type = MODT03_CMD;     return true; }
        else if(rcv[7] == 0x06)    { cmd.type = MODT06_CMD;     return true; }
        else if(rcv[7] == 0x08)    { cmd.type = MODT08_CMD;     return true; }
        else if(rcv[7] == 0x10)    { cmd.type = MODT16_CMD;     return true; }
        else return false;
    }
    return false;
}

eCOMM_ERROR PcLinkDrv::dataCheck(PcLinkCmd& cmd, const QByteArray& rcv)
{
    bool ok=true;

    if(cmd.type == AMI_CMD){
        cmd.cnt = 0;
        return ECODE_NOERROR;
    }

    else if(cmd.type == RSD_CMD){
        //------------------------------------------------------------//
        // [STX][01][RSD],[03],[0005][CR]+[LF] : minimum length : 16
        //------------------------------------------------------------//
        cmd.cnt = rcv.mid( 7, 2).toInt(&ok, 10);  if(ok==false) return ECODE_FORMAT;
        int adr = rcv.mid(10, 4).toInt(&ok, 10);  if(ok==false) return ECODE_FORMAT;

        for(int i=0; i<cmd.cnt; i++){
            if(adr+i > DREG_MAX_NO){
                return ECODE_REG;
            }
            cmd.adr.push_back(adr+i);
        }
    }

    else if(cmd.type == RRD_CMD){
        //------------------------------------------------------------//
        // [STX][01][RRD],[01],[0001][CR]+[LF] : minimum length : 16
        //------------------------------------------------------------//
        cmd.cnt = rcv.mid(7, 2).toInt();

        if(rcv.length() != cmd.cnt*5+11+m_sn){
            return ECODE_FORMAT;
        }

        for(int i=0; i<cmd.cnt; i++){
            int adr = rcv.mid(10+(i*5), 4).toInt(&ok, 10);

            if(ok==false){
                return ECODE_FORMAT;
            }

            if(adr > DREG_MAX_NO){
                return ECODE_REG;
            }
            cmd.adr.push_back(adr);
        }
    }

    else if(cmd.type == WSD_CMD){
        //------------------------------------------------------------//
        // [STX][01][WSD],[03],[0001],[000A,000B,000C][CR]+[LF]
        //------------------------------------------------------------//
        cmd.cnt = rcv.mid(7, 2).toInt();
        int adr = rcv.mid(10, 4).toInt(); // [0001]

        if(rcv.length() != cmd.cnt*5+16+m_sn){
            return ECODE_FORMAT;
        }

        for(int i=0; i<cmd.cnt; i++){
            bool ok=true;
            qint16  val = rcv.mid(15+(i*5), 4).toInt(&ok, 16);

            if(ok==false){
                return ECODE_FORMAT;
            }

            if(adr+i > DREG_MAX_NO){
                return ECODE_REG;
            }

            cmd.adr.push_back(adr+i);
            cmd.val.push_back(val);
        }
    }

    else if(cmd.type == WRD_CMD){
        //------------------------------------------------------------//
        // [STX][01][WRD],[02],[0001],[000A],[0002],[000C][CR]+[LF]
        //------------------------------------------------------------//
        cmd.cnt = rcv.mid(7, 2).toInt();

        if(rcv.length() != cmd.cnt*5*2+11+m_sn){
            return ECODE_FORMAT;
        }

        for(int i=0; i<cmd.cnt*2; i+=2){
            bool ok=true;
            quint16 adr = rcv.mid(    10+(i*5), 4).toInt(&ok, 10); if(ok==false) return ECODE_FORMAT;
            qint16  val = rcv.mid(10+((i+1)*5), 4).toInt(&ok, 16); if(ok==false) return ECODE_FORMAT;

            if(adr > DREG_MAX_NO){
                return ECODE_REG;
            }

            cmd.adr.push_back(adr);
            cmd.val.push_back(val);
        }
    }

    else if(cmd.type == MODA03_CMD){
        //------------------------------------------------------------//
        //[:][01][03][0000][0003][LRC][CR][LF]
        //------------------------------------------------------------//
        bool ok=true;
        cmd.cnt = rcv.mid(9, 4).toInt(&ok, 16); if(ok==false) return ECODE_FORMAT;
        int adr = rcv.mid(5, 4).toInt(&ok, 16); if(ok==false) return ECODE_FORMAT;

        for(int i=0; i<cmd.cnt; i++){
            if((adr+1)+i > DREG_MAX_NO){
                return ECODE_REG;
            }

            cmd.adr.push_back((adr+1)+i);
        }
    }

    else if(cmd.type == MODA06_CMD){
        //------------------------------------------------------------//
        //[:][01][06][0001][000A][LRC][CR][LF]
        //------------------------------------------------------------//
        bool ok=true;
        quint16 adr = rcv.mid(5, 4).toInt(&ok, 16); if(ok==false) return ECODE_FORMAT;
        qint16  val = rcv.mid(9, 4).toInt(&ok, 16); if(ok==false) return ECODE_FORMAT;

        if((adr+1) > DREG_MAX_NO){
            return ECODE_REG;
        }

        cmd.cnt = 1;
        cmd.adr.push_back(adr+1);
        cmd.val.push_back(val);
    }

    else if(cmd.type == MODA08_CMD){
        return ECODE_NOERROR;
    }

    else if(cmd.type == MODA16_CMD){
        //------------------------------------------------------------//
        //[:][01][10][0000][0001][02] [00FF][LRC][CR][LF]
        //------------------------------------------------------------//
        bool ok=true;
        quint16 adr = rcv.mid( 5, 4).toInt(&ok, 16); if(ok==false) return ECODE_FORMAT;
        cmd.cnt     = rcv.mid( 9, 4).toInt(&ok, 16); if(ok==false) return ECODE_FORMAT;
        int bytes   = rcv.mid(13, 2).toInt(&ok, 16); if(ok==false) return ECODE_FORMAT;

        if(rcv.length() != cmd.cnt*4+19){
            return ECODE_FORMAT;
        }

        for(int i=0; i<cmd.cnt; i++){
            qint16 val = rcv.mid(15+(i*4), 4).toInt(&ok, 16);
            if(ok==false){
                return ECODE_FORMAT;
            }

            if((adr+1)+i > DREG_MAX_NO){
                return ECODE_REG;
            }

            cmd.adr.push_back((adr+1)+i);
            cmd.val.push_back(val);
        }
    }

    else if(cmd.type == MODR03_CMD){
        //------------------------------------------------------------//
        //[01][03][0000][0003][CRC]
        //------------------------------------------------------------//
        bool ok=true;
        cmd.cnt = rcv.mid(4, 2).toHex().toInt(&ok, 16); if(ok==false) return ECODE_FORMAT;
        int adr = rcv.mid(2, 2).toHex().toInt(&ok, 16); if(ok==false) return ECODE_FORMAT;

        for(int i=0; i<cmd.cnt; i++){
            if((adr+1)+i > DREG_MAX_NO){
                return ECODE_REG;
            }

            cmd.adr.push_back((adr+1)+i);
        }
    }

    else if(cmd.type == MODR06_CMD){
        //------------------------------------------------------------//
        //[01][06][0001][000A][CRC]
        //------------------------------------------------------------//
        bool ok=true;
        quint16 adr = rcv.mid(2, 2).toHex().toInt(&ok, 16); if(ok==false) return ECODE_FORMAT;
        qint16  val = rcv.mid(4, 2).toHex().toInt(&ok, 16); if(ok==false) return ECODE_FORMAT;

        if((adr+1) > DREG_MAX_NO){
            return ECODE_REG;
        }

        cmd.cnt = 1;
        cmd.adr.push_back(adr+1);
        cmd.val.push_back(val);
    }

    else if(cmd.type == MODR08_CMD){
        return ECODE_NOERROR;
    }

    else if(cmd.type == MODR16_CMD){
        //------------------------------------------------------------//
        //[01][10][0000][0001][02] [00FF][CRC]
        //------------------------------------------------------------//
        bool ok=true;
        quint16 adr = rcv.mid( 2, 2).toHex().toInt(&ok, 16); if(ok==false) return ECODE_FORMAT;
        cmd.cnt     = rcv.mid( 4, 2).toHex().toInt(&ok, 16); if(ok==false) return ECODE_FORMAT;
        int bytes   = rcv.mid( 6, 1).toHex().toInt(&ok, 16); if(ok==false) return ECODE_FORMAT;

        if(rcv.length() != cmd.cnt*2+9){
            return ECODE_FORMAT;
        }

        for(int i=0; i<cmd.cnt; i++){
            qint16 val = rcv.mid(7+(i*2), 2).toHex().toInt(&ok, 16);
            if(ok==false){
                return ECODE_FORMAT;
            }

            if((adr+1)+i > DREG_MAX_NO){
                return ECODE_REG;
            }

            cmd.adr.push_back((adr+1)+i);
            cmd.val.push_back(val);
        }
    }

    else if(cmd.type == MODT03_CMD){
        //------------------------------------------------------------//
        //[015E 0000 0006][01][03][0004][0006]
        //------------------------------------------------------------//
        bool ok=true;
        cmd.cnt = rcv.mid(10, 2).toHex().toInt(&ok, 16); if(ok==false) return ECODE_FORMAT;
        int adr = rcv.mid( 8, 2).toHex().toInt(&ok, 16); if(ok==false) return ECODE_FORMAT;

        for(int i=0; i<cmd.cnt; i++){
            if((adr+1)+i > DREG_MAX_NO){
                return ECODE_REG;
            }

            cmd.adr.push_back((adr+1)+i);
        }
    }

    else if(cmd.type == MODT06_CMD){
        //------------------------------------------------------------//
        //[015E 0000 0006][01][06][0001][0002]
        //------------------------------------------------------------//
        bool ok=true;
        quint16 adr = rcv.mid( 8, 2).toHex().toInt(&ok, 16); if(ok==false) return ECODE_FORMAT;
        qint16  val = rcv.mid(10, 2).toHex().toInt(&ok, 16); if(ok==false) return ECODE_FORMAT;

        if((adr+1) > DREG_MAX_NO){
            return ECODE_REG;
        }

        cmd.cnt = 1;
        cmd.adr.push_back(adr+1);
        cmd.val.push_back(val);
    }

    else if(cmd.type == MODT08_CMD){
        return ECODE_NOERROR;
    }

    else if(cmd.type == MODT16_CMD){
        //------------------------------------------------------------//
        //[015E 0000 0006][01][10][0000][0001][02][00FF]
        //------------------------------------------------------------//
        bool ok=true;
        quint16 adr = rcv.mid( 8, 2).toHex().toInt(&ok, 16); if(ok==false) return ECODE_FORMAT;
        cmd.cnt     = rcv.mid(10, 2).toHex().toInt(&ok, 16); if(ok==false) return ECODE_FORMAT;
        int bytes   = rcv.mid(12, 1).toHex().toInt(&ok, 16); if(ok==false) return ECODE_FORMAT;

        if(rcv.length() != cmd.cnt*2+13){
            return ECODE_FORMAT;
        }

        for(int i=0; i<cmd.cnt; i++){
            qint16 val = rcv.mid(13+(i*2), 2).toHex().toInt(&ok, 16);
            if(ok==false){
                return ECODE_FORMAT;
            }

            if((adr+1)+i > DREG_MAX_NO){
                return ECODE_REG;
            }

            cmd.adr.push_back((adr+1)+i);
            cmd.val.push_back(val);
        }
    }

    return ECODE_NOERROR;
}

void PcLinkDrv::doResponse(PcLinkCmd& cmd, const QByteArray& rcv)
{
    QByteArray res="";

    //----------------------------------------------------------------//
    // Response AMI
    //----------------------------------------------------------------//
    if(cmd.type == AMI_CMD){
        res = QString("%1AMI,OK,%2 V%3 R%4")
              .arg(MEMLC.DBP_ADR, 2, 10, QChar('0'))
              .arg(MODEL_CODE_STR)
              .arg(MODEL_VER, 2, 10, QChar('0'))
              .arg(MODEL_REV, 2, 10, QChar('0'))
              .toLocal8Bit();

        if(m_prs == PRS_PCLINK_SUM || m_prs == PRS_TCP_SUM){
            makePcLinkCheckSum(res);
        }
        write(STX+res+CR+LF);
    }

    //----------------------------------------------------------------//
    // Response LOF
    //----------------------------------------------------------------//
    else if(cmd.type == LOF_CMD){
        res = QString("%1LOF,OK")
              .arg(MEMLC.DBP_ADR, 2, 10, QChar('0'))
              .toLocal8Bit();

        if(m_prs == PRS_PCLINK_SUM || m_prs == PRS_TCP_SUM){
            makePcLinkCheckSum(res);
        }
        write(STX+res+CR+LF);
    }

    //----------------------------------------------------------------//
    // Response MODBUS Loop back
    //----------------------------------------------------------------//
    else if(cmd.type == MODA08_CMD ||
            cmd.type == MODR08_CMD ||
            cmd.type == MODT08_CMD){
        write(rcv);
    }

    //----------------------------------------------------------------//
    // Response SW READ
    //----------------------------------------------------------------//
    else if(cmd.type == RSD_CMD || cmd.type == RRD_CMD){
        m_pdc->pcLinkProcess(cmd);

        if(cmd.type == RSD_CMD) res = QString("%1RSD,OK").arg(MEMLC.DBP_ADR, 2, 10, QChar('0')).toLocal8Bit();
        if(cmd.type == RRD_CMD) res = QString("%1RRD,OK").arg(MEMLC.DBP_ADR, 2, 10, QChar('0')).toLocal8Bit();

        for(int i=0; i<cmd.cnt; i++){
            QString tmp;
            tmp.sprintf(",%04X", (quint16)cmd.val[i]);
            res+= tmp.toLocal8Bit();
        }

        if(m_prs == PRS_PCLINK_SUM || m_prs == PRS_TCP_SUM){
            makePcLinkCheckSum(res);
        }
        write(STX+res+CR+LF);
    }

    //----------------------------------------------------------------//
    // Response SW WRITE
    //----------------------------------------------------------------//
    else if(cmd.type == WSD_CMD || cmd.type == WRD_CMD){
        //------------------------------------------------------------//
        // [STX][01][WRD],[02],[0001],[000A],[0002],[000C][CR]+[LF]
        //------------------------------------------------------------//
        m_pdc->pcLinkProcess(cmd);

        if(cmd.type == WSD_CMD) res = QString("%1WSD,OK").arg(MEMLC.DBP_ADR, 2, 10, QChar('0')).toLocal8Bit();
        if(cmd.type == WRD_CMD) res = QString("%1WRD,OK").arg(MEMLC.DBP_ADR, 2, 10, QChar('0')).toLocal8Bit();

        if(m_prs == PRS_PCLINK_SUM || m_prs == PRS_TCP_SUM){
            makePcLinkCheckSum(res);
        }
        write(STX+res+CR+LF);
    }

    //----------------------------------------------------------------//
    // Response MODBUS ASCII
    //----------------------------------------------------------------//
    else if(cmd.type == MODA03_CMD){
        //------------------------------------------------------------//
        //[:][01][03][0000][0003][LRC][CR][LF]
        //[:][01][03][byte cnt(1)][data1(2)]~[dataN(2)][LRC][CR][LF]
        //------------------------------------------------------------//
        m_pdc->pcLinkProcess(cmd);

        res = rcv.mid(1, 4);
        res.append(QString("%1").arg((cmd.cnt*2), 2, 16, QChar('0')).toUpper());

        for(int i=0; i<cmd.cnt; i++){
            res.append(QString("%1").arg(quint16(cmd.val[i]), 4, 16, QChar('0')).toUpper());
        }
        lrcSet(res);
        write(":"+res+CR+LF);
    }

    else if(cmd.type == MODA06_CMD){
        //------------------------------------------------------------//
        //[:][01][06][0000][0003][LRC][CR][LF]
        //[:][01][06][addr(2)][val(2)][LRC][CR][LF]
        //------------------------------------------------------------//
        m_pdc->pcLinkProcess(cmd);

        res = rcv.mid(1, 4);
        res.append(QString("%1").arg((cmd.adr[0]-1), 4, 16, QChar('0')).toUpper());
        res.append(QString("%1").arg(quint16(cmd.val[0]), 4, 16, QChar('0')).toUpper());

        lrcSet(res);
        write(":"+res+CR+LF);
    }

    else if(cmd.type == MODA16_CMD){
        //------------------------------------------------------------//
        //[:][01][16][addr(2)][count(2)][byte(1)][val1(2)]~[valN(2)][LRC][CR][LF]
        //[:][01][16][addr(2)][count(2)][LRC][CR][LF]
        //[:][01][16][0001][000A][LRC][CR][LF]
        //------------------------------------------------------------//
        m_pdc->pcLinkProcess(cmd);

        res = rcv.mid(1, 4);
        res.append(QString("%1").arg((cmd.adr[0]-1), 4, 16, QChar('0')).toUpper());
        res.append(QString("%1").arg(       cmd.cnt, 4, 16, QChar('0')).toUpper());

        lrcSet(res);
        write(":"+res+CR+LF);
    }

    //----------------------------------------------------------------//
    // Response MODBUS RTU
    //----------------------------------------------------------------//
    else if(cmd.type == MODR03_CMD){
        //------------------------------------------------------------//
        //[01][03][0000][0003][LRC][CR][LF]
        //[01][03][byte cnt(1)][data1(2)]~[dataN(2)][LRC][CR][LF]
        //------------------------------------------------------------//
        m_pdc->pcLinkProcess(cmd);

        res.append(rcv[0]);
        res.append(rcv[1]);
        res.append((char)cmd.cnt*2);

        for(int i=0; i<cmd.cnt; i++){
            res.append((char)(cmd.val[i]>>8));
            res.append((char)(cmd.val[i]   ));
        }
        crcSet(res);
        write(res);
    }

    else if(cmd.type == MODR06_CMD){
        //------------------------------------------------------------//
        //[01][06][0000][0003][LRC][CR][LF]
        //[01][06][addr(2)][val(2)][LRC][CR][LF]
        //------------------------------------------------------------//
        m_pdc->pcLinkProcess(cmd);

        res.append(rcv[0]);
        res.append(rcv[1]);

        res.append((char)((cmd.adr[0]-1)>>8));
        res.append((char)((cmd.adr[0]-1)   ));
        res.append((char)(cmd.val[0]>>8));
        res.append((char)(cmd.val[0]   ));

        crcSet(res);
        write(res);
    }

    else if(cmd.type == MODR16_CMD){
        //------------------------------------------------------------//
        //[:][01][16][addr(2)][count(2)][byte(1)][val1(2)]~[valN(2)][LRC][CR][LF]
        //[:][01][16][addr(2)][count(2)][LRC][CR][LF]
        //[:][01][16][0001][000A][LRC][CR][LF]
        //------------------------------------------------------------//
        m_pdc->pcLinkProcess(cmd);

        res.append(rcv[0]);
        res.append(rcv[1]);

        res.append((char)((cmd.adr[0]-1)>>8));
        res.append((char)((cmd.adr[0]-1)   ));
        res.append((char)(cmd.cnt>>8));
        res.append((char)(cmd.cnt   ));

        crcSet(res);
        write(res);
    }

    //----------------------------------------------------------------//
    // Response MODBUS TCP
    //----------------------------------------------------------------//
    else if(cmd.type == MODT03_CMD){
        //------------------------------------------------------------//
        //[015B 0000 001B][01][03][0000][0003][LRC][CR][LF]
        //[015B 0000 001B][01][03][byte cnt(1)][data1(2)]~[dataN(2)][LRC][CR][LF]
        //------------------------------------------------------------//
        m_pdc->pcLinkProcess(cmd);
        qint16 dl = cmd.cnt*2+3;

        res.append(rcv[0]);          // TRANSACTION ID [0]
        res.append(rcv[1]);          // TRANSACTION ID [1]
        res.append(rcv[2]);          // PROTOCOL IDENTIFIER [0]
        res.append(rcv[3]);          // PROTOCOL IDENTIFIER [1]
        res.append((char)dl>>8);     // DATA LENGTH [0]
        res.append((char)dl);        // DATA LENGTH [1]
        res.append(rcv[6]);          // [01] : ADDR
        res.append(rcv[7]);          // [03] : FUNC CODE
        res.append((char)cmd.cnt*2); // BYTE OF DATA SIZE

        for(int i=0; i<cmd.cnt; i++){
            res.append((char)(cmd.val[i]>>8));
            res.append((char)(cmd.val[i]   ));
        }
        write(res);
    }

    else if(cmd.type == MODT06_CMD){
        //------------------------------------------------------------//
        //[015B 0000 001B][01][06][0000][0003][LRC][CR][LF]
        //[015B 0000 001B][01][06][addr(2)][val(2)][LRC][CR][LF]
        //------------------------------------------------------------//
        m_pdc->pcLinkProcess(cmd);
        qint16 dl = 6;               // ADDR[1]+FUNC CODE[1]+REG ADDR[2]+REG VALUE[2]

        res.append(rcv[0]);          // TRANSACTION ID [0]
        res.append(rcv[1]);          // TRANSACTION ID [1]
        res.append(rcv[2]);          // PROTOCOL IDENTIFIER [0]
        res.append(rcv[3]);          // PROTOCOL IDENTIFIER [1]
        res.append((char)dl>>8);     // DATA LENGTH [0]
        res.append((char)dl);        // DATA LENGTH [1]
        res.append(rcv[6]);          // [01] : ADDR
        res.append(rcv[7]);          // [03] : FUNC CODE

        res.append((char)((cmd.adr[0]-1)>>8));
        res.append((char)((cmd.adr[0]-1)   ));
        res.append((char)(cmd.val[0]>>8));
        res.append((char)(cmd.val[0]   ));
        write(res);
    }

    else if(cmd.type == MODT16_CMD){
        //------------------------------------------------------------//
        //[015B 0000 001B][01][16][addr(2)][count(2)][byte(1)][val1(2)]~[valN(2)][LRC][CR][LF]
        //[015B 0000 001B][01][16][addr(2)][count(2)][LRC][CR][LF]
        //[015B 0000 001B][01][16][0001][000A][LRC][CR][LF]
        //------------------------------------------------------------//
        m_pdc->pcLinkProcess(cmd);
        qint16 dl = 6;               // ADDR[1]+FUNC CODE[1]+START REG ADDR[2]+REG COUNT[2]

        res.append(rcv[0]);          // TRANSACTION ID [0]
        res.append(rcv[1]);          // TRANSACTION ID [1]
        res.append(rcv[2]);          // PROTOCOL IDENTIFIER [0]
        res.append(rcv[3]);          // PROTOCOL IDENTIFIER [1]
        res.append((char)dl>>8);     // DATA LENGTH [0]
        res.append((char)dl);        // DATA LENGTH [1]
        res.append(rcv[6]);          // [01] : ADDR
        res.append(rcv[7]);          // [03] : FUNC CODE

        res.append((char)((cmd.adr[0]-1)>>8));
        res.append((char)((cmd.adr[0]-1)   ));
        res.append((char)(cmd.cnt>>8));
        res.append((char)(cmd.cnt   ));
        write(res);
    }
}
