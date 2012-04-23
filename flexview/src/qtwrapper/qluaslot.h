#ifndef QLUASLOT_H
#define QLUASLOT_H

#include <QtGui>
#include <QtNetwork>
#include "lua.hpp"
#include <luabind/luabind.hpp>
#include <boost/smart_ptr.hpp>
using namespace luabind;
struct QextPortInfo;
class QLuaSlot : public QObject
{
Q_OBJECT
public:
    explicit QLuaSlot(const QString& signature);
    QLuaSlot(const object& obj, const QString& signature);
    void emit_gen_signal();
    void emit_gen_signal(QString);
    void emit_gen_signal(int);
    ~QLuaSlot();
    QByteArray slot() const;
    QByteArray signal() const;
    object get_object() const{ return m_obj; }
    void set_object(const object& obj){ m_obj = obj; }
signals:
    void general_signal();
    void general_signal(char);
    void general_signal(short);
    void general_signal(int);
    void general_signal(bool);
    void general_signal(const QString&);
public slots:
    void general_slot();
    void general_slot(char);
    void general_slot(short);
    void general_slot(int);
    void general_slot(double);
    void general_slot(bool);
    void general_slot(const QString&);
    void general_slot(QListWidgetItem* item);
    void general_slot(QListWidgetItem* item1,QListWidgetItem* item2);
    void general_slot(QTreeWidgetItem* item);
    void general_slot(QTreeWidgetItem* item1,QTreeWidgetItem* item2);
    void general_slot(QTreeWidgetItem* item1, int column);
    void general_slot(int,int);
    void general_slot(int,int,int,int);
    void general_slot(QTableWidgetItem* item1);
    void general_slot(QTableWidgetItem* item1, QTableWidgetItem* column);

    void general_slot(qint64);
    void general_slot(const QextPortInfo&);
    void general_slot(QMdiSubWindow*);
    void general_slot(QSystemTrayIcon::ActivationReason);
    void general_slot(const QDate&);
    void general_slot(const QTime&);
    void general_slot(const QDateTime&);
    void general_slot(QAction*);
    void general_slot(QProcess::ProcessError);
    void general_slot(QProcess::ExitStatus);
    void general_slot(QProcess::ProcessState);
    void general_slot(QWidget*,QWidget*);
    void general_slot(QWidget*);
    void general_slot(QAbstractSocket::SocketError);
    void general_slot(QAbstractSocket::SocketState);
    void general_slot(QClipboard::Mode);
    void general_slot(const QHostInfo&);
private:
    object m_obj;
    QString m_method;
    QString m_signature;
};
//typedef boost::shared_ptr<QLuaSlot> auto_slot;


bool sigfunc_connect(QObject* sender, const char* signal, object func);
QLuaSlot* get_slot(const QObject* obj, const char* member);

#define SIGNAL_PROPERYT(prefix,name, type, sig)\
void prefix##_set_##name(type* btn, const object& obj)\
{\
    QLuaSlot* slot = get_slot(btn,#name sig);\
    /*qDebug()<<type(obj);*/\
    if(slot){\
        slot->set_object(obj);\
    }else{\
        sigfunc_connect(btn,#name sig,obj);\
    }\
}\
\
object prefix##_get_##name(const type* btn)\
{\
    QLuaSlot* slot = get_slot(btn,#name sig);\
    if(slot){\
        return slot->get_object();\
    }\
    return object();\
}

#endif // QLUASLOT_H
