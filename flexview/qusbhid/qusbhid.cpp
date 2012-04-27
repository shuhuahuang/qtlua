#include "qusbhid.h"
#include <QDebug>
QUsbHid::QUsbHid(QObject *parent,QueryMode mode) :
    QIODevice(parent),
    m_queryMode(mode)
{
    initial();
}

QUsbHid::QUsbHid(const QString& path, QObject *parent,QueryMode mode) :
    QIODevice(parent),
     m_queryMode(mode),
    m_path(path)
{
    initial();
}

QUsbHid::~QUsbHid()
{
    deinitial();
}

void QUsbHid::initial()
{
    mutex = new QMutex(QMutex::Recursive);

    Win_Handle=INVALID_HANDLE_VALUE;
    ZeroMemory(&overlap, sizeof(OVERLAPPED));
    overlap.hEvent = CreateEvent(NULL, true, false, NULL);
    winEventNotifier = 0;
    bytesToWriteLock = new QReadWriteLock;
    _bytesToWrite = 0;
    readBufferLock = new QReadWriteLock;
    memset(&hidCaps, 0, sizeof(hidCaps));
    enumerator = new QUsbHidEnumerator(this);
    connect(enumerator, SIGNAL(deviceDiscovered(QUsbHidInfo)), this, SIGNAL(connected(QUsbHidInfo)));
    connect(enumerator, SIGNAL(deviceRemoved(QUsbHidInfo)), this, SIGNAL(disconnected(QUsbHidInfo)));
    m_timeout = 30000;
    lastErr = 0;
}

void QUsbHid::deinitial()
{
    CloseHandle(overlap.hEvent);
    delete bytesToWriteLock;
    delete readBufferLock;
    if (isOpen()) {
        close();
    }
    delete mutex;
}

bool QUsbHid::open(OpenMode mode)
{
    DWORD dwFlagsAndAttributes = 0;
    if (queryMode() == QUsbHid::EventDriven)
        dwFlagsAndAttributes += FILE_FLAG_OVERLAPPED;
    QMutexLocker lock(mutex);
    if (mode == QIODevice::NotOpen)
        return isOpen();
    Win_Handle=CreateFileA(m_path.toAscii(), GENERIC_READ|GENERIC_WRITE,
                          0, NULL, OPEN_EXISTING, dwFlagsAndAttributes, NULL);

    if (Win_Handle!=INVALID_HANDLE_VALUE) {
        QIODevice::open(mode);
        HidD_GetPreparsedData (Win_Handle, &hidPrepParsedData);
        HidP_GetCaps (hidPrepParsedData, &hidCaps);
        //init event driven approach
        if (queryMode() == QUsbHid::EventDriven) {
            winEventNotifier = new QWinEventNotifier(overlap.hEvent, this);
            connect(winEventNotifier, SIGNAL(activated(HANDLE)), this, SLOT(onWinEvent(HANDLE)));
            rawData[0] = 0;
            ::ReadFile(Win_Handle, rawData, hidCaps.InputReportByteLength, 0, &overlap);
        }

    }else{
        return false;
    }
    lastErr = GetLastError();
    return isOpen();
}

void QUsbHid::close()
{
    QMutexLocker lock(mutex);
    if (isOpen()) {
        flush();
        QIODevice::close(); // mark ourselves as closed
        CancelIo(Win_Handle);
        if (CloseHandle(Win_Handle))
            Win_Handle = INVALID_HANDLE_VALUE;
        if (winEventNotifier)
            winEventNotifier->deleteLater();

        _bytesToWrite = 0;

        foreach(OVERLAPPED* o, pendingWrites) {
            CloseHandle(o->hEvent);
            delete o;
        }
        pendingWrites.clear();
        memset(&hidCaps, 0, sizeof(hidCaps));
    }
    lastErr = GetLastError();
}

void QUsbHid::flush()
{
    QMutexLocker lock(mutex);
    if (isOpen()) {
        FlushFileBuffers(Win_Handle);
        lastErr = GetLastError();
    }
}

qint64 QUsbHid::bytesAvailable() const
{
    QReadLocker rl(readBufferLock);
    return readBuffer.length();
}

qint64 QUsbHid::bytesToWrite() const
{
    QReadLocker rl(bytesToWriteLock);
    return _bytesToWrite;
}

QByteArray QUsbHid::readAll()
{
    QByteArray res = readBuffer;
    QWriteLocker wr(readBufferLock);
    readBuffer.clear();
    return res;
}

QByteArray QUsbHid::readData(qint64 maxSize)
{
    return readData(0,maxSize);
}

QByteArray QUsbHid::readData(int reportID, qint64 maxSize)
{
    QByteArray res;
    res.resize(maxSize);
    res[0] = (char)reportID;
    DWORD retVal;
    QMutexLocker lock(mutex);
    retVal = 0;
    if (queryMode() == QUsbHid::EventDriven) {
        OVERLAPPED overlapRead;
        ZeroMemory(&overlapRead, sizeof(OVERLAPPED));
        if (!ReadFile(Win_Handle, (char*)res.data(), (DWORD)maxSize, & retVal, & overlapRead)) {
            if (GetLastError() == ERROR_IO_PENDING){
                GetOverlappedResult(Win_Handle, & overlapRead, & retVal, true);
            } else {
                lastErr = GetLastError();
                retVal = (DWORD)-1;
            }
        }
    } else if (!ReadFile(Win_Handle, (char*)res.data(), (DWORD)maxSize, & retVal, NULL)) {
        lastErr = GetLastError();
        retVal = (DWORD)-1;
    }
    if(retVal == (DWORD)-1){
        res.clear();
    }else{
        res.resize(retVal);
    }
    return res;
}

qint64 QUsbHid::writeData(int reportID, const QByteArray& data)
{
    QByteArray arr;
    arr.append(char(reportID));
    arr.append(data);
    return writeData(data);
}

qint64 QUsbHid::writeData(const QByteArray& data)
{
    QMutexLocker lock( mutex );
    DWORD retVal = 0;
    if (queryMode() == QUsbHid::EventDriven) {
        OVERLAPPED newOverlapWrite;
        ZeroMemory(&newOverlapWrite, sizeof(OVERLAPPED));
        newOverlapWrite.hEvent = CreateEvent(NULL, true, false, NULL);
        if (WriteFile(Win_Handle, (char*)data.data(), data.length(), & retVal, &newOverlapWrite)) {
            CloseHandle(newOverlapWrite.hEvent);
            emit bytesWritten((qint64)retVal);
        }
        else if (GetLastError() == ERROR_IO_PENDING) {
            // writing asynchronously...not an error
            if (::WaitForSingleObject(newOverlapWrite.hEvent,m_timeout) != WAIT_OBJECT_0){
                retVal = (DWORD)-1;
            }else{
                retVal = newOverlapWrite.InternalHigh;
                emit bytesWritten((qint64)retVal);
            }
            lastErr = GetLastError();
//            if (GetOverlappedResult(Win_Handle, &newOverlapWrite, & retVal, true)) {
//            }else{
//                retVal = (DWORD)-1;
//            }
            CloseHandle(newOverlapWrite.hEvent);
        }
        else {
            lastErr = GetLastError();
            qDebug() << "USB write error:" << lastErr;
            retVal = (DWORD)-1;
            if(!CancelIo(newOverlapWrite.hEvent))
                qDebug() << "QUsbHid: couldn't cancel IO";
            if(!CloseHandle(newOverlapWrite.hEvent))
                qDebug() << "QUsbHid: couldn't close OVERLAPPED handle";
        }
    } else if (!WriteFile(Win_Handle, (char*)data.data(), (DWORD)data.length(), & retVal, NULL)) {
        lastErr = GetLastError();
        retVal = (DWORD)-1;
    }
    return (qint64)retVal;
}


qint64 QUsbHid::readData(char *data, qint64 maxlen)
{
    QByteArray res = readData(maxlen);
    memcpy(data, res.data(), res.length());
    return res.length();
}

qint64 QUsbHid::writeData(const char *data, qint64 len)
{
    return writeData(QByteArray::fromRawData(data,len));
}

void QUsbHid::onWinEvent(HANDLE h)
{
    QMutexLocker lock(mutex);
    if(h == overlap.hEvent) {
        // got data
        {
            QWriteLocker wl(readBufferLock);
            DWORD retVal = 0;
            GetOverlappedResult(Win_Handle, & overlap, & retVal, false);
            readBuffer.append(rawData,retVal);
        }
        if (sender() != this && bytesAvailable() > 0)
            emit readyRead();
        rawData[0] = 0;
        ::ReadFile(Win_Handle, rawData, hidCaps.InputReportByteLength, 0, &overlap);
        lastErr = GetLastError();
    }
}

QString QUsbHid::errorString()const
{
    QString res;
#ifdef Q_OS_WIN
    LPTSTR lpMsgBuf = 0;
    DWORD ret = FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,
                  0,
                  lastErr,
                  MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                  (LPTSTR) &lpMsgBuf, 0, 0);
#ifdef UNICODE
    res = QString::fromWCharArray( (LPTSTR)lpMsgBuf);
#else
    res =  QString::fromLocal8Bit((LPTSTR) lpMsgBuf);
#endif
    res.remove(QChar('\n'));
    LocalFree(lpMsgBuf);
    (void)ret;
#endif
    return res;
}

QList<QUsbHidInfo> QUsbHid::enumDevices(int vid, int pid)
{
    return QUsbHidEnumerator::getPorts((WORD)vid,(WORD)pid);
}


QHidAttr QUsbHid::GetAttributes(bool* r)
{
    HIDD_ATTRIBUTES attr = {0};
    bool res = HidD_GetAttributes(Win_Handle, &attr);
    if(r) *r = res;
    lastErr = GetLastError();
    return QHidAttr(attr);
}
QByteArray QUsbHid::getFeature(int maxLen, bool* r)
{
    return getFeature(0,maxLen,r);
}

QByteArray QUsbHid::getFeature(int reportID, int maxLen,bool* r)
{
    QByteArray ret;
    ret.resize(maxLen);
    ret[0] = reportID;
    bool res = HidD_GetFeature(Win_Handle, (char*)ret.data(), ret.length());
    if(r) *r = res;
    lastErr = GetLastError();
    return ret;
}

QByteArray QUsbHid::getInputReport(int maxLen,bool* r)
{
    return getInputReport(0,maxLen,r);
}

QByteArray QUsbHid::getInputReport(int reportID, int maxLen,bool* r)
{
    QByteArray ret;
    ret.resize(maxLen);
    ret[0] = reportID;
    bool res = HidD_GetInputReport(Win_Handle, (char*)ret.data(), ret.length());
    if(r) *r = res;
    lastErr = GetLastError();
    return ret;
}

int QUsbHid::getNumInputBuffers(bool* r)
{
    ULONG num = 0;
    bool res = HidD_GetNumInputBuffers(Win_Handle, &num);
    if(r) *r = res;
    lastErr = GetLastError();
    return num;
}

QByteArray QUsbHid::getPhysicalDescriptor(int maxLen, bool* r)
{
    QByteArray ret;
    ret.resize(maxLen);
    bool res = HidD_GetPhysicalDescriptor(Win_Handle, (char*)ret.data(), ret.length());
    if(r) *r = res;
    lastErr = GetLastError();
    return ret;
}

QString QUsbHid::getIndexedString(int index,bool* r)
{
    wchar_t ret[128];
    bool res = HidD_GetIndexedString(Win_Handle, index, ret, sizeof(ret));
    if(r) *r = res;
    lastErr = GetLastError();
    return QString::fromUtf16((ushort*)ret);
}

QString QUsbHid::getManufacturerString(bool* r)
{
    wchar_t ret[128];
    bool res = HidD_GetManufacturerString(Win_Handle, ret, sizeof(ret));
    if(r) *r = res;
    lastErr = GetLastError();
    return QString::fromUtf16((ushort*)ret);
}

QString QUsbHid::getProductString(bool* r)
{
    wchar_t ret[128];
    bool res = HidD_GetProductString(Win_Handle, ret, sizeof(ret));
    if(r) *r = res;
    lastErr = GetLastError();
    return QString::fromUtf16((ushort*)ret);
}

QString QUsbHid::getSerialNumberString(bool* r)
{
    wchar_t ret[128];
    bool res = HidD_GetSerialNumberString(Win_Handle, ret, sizeof(ret));
    if(r) *r = res;
    lastErr = GetLastError();
    return QString::fromUtf16((ushort*)ret);
}

bool QUsbHid::setFeature(const QByteArray& arr)
{
    QByteArray s(arr);
    bool res =  HidD_SetFeature(Win_Handle,s.data(), arr.length());
    lastErr = GetLastError();
    return res;
}

bool QUsbHid::setFeature(int reportID, const QByteArray& arr)
{
    QByteArray x;
    x.append(char(reportID));
    x.append(arr);
    return setFeature(x);
}

bool QUsbHid::setOutputReport(const QByteArray& arr)
{
    QByteArray s(arr);
    bool res =  HidD_SetOutputReport(Win_Handle,s.data(), arr.length());
    lastErr = GetLastError();
    return res;
    //return false;
}

bool QUsbHid::setOutputReport(int reportID, const QByteArray& arr)
{
    QByteArray x;
    x.append(char(reportID));
    x.append(arr);
    return setOutputReport(x);
}

bool QUsbHid::setNumInputBuffers(int num)
{
    bool res = HidD_SetNumInputBuffers(Win_Handle,(ULONG)num);
    lastErr = GetLastError();
    return res;
}
