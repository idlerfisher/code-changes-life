/***************************************************************************
** Name         : coadialog.cpp
** Author       : snowcicada
** Date         : 20170331
** Description  : Getting jira tasks by year and quarter.
** GitHub       : https://github.com/snowcicada
** E-mail       : snowcicadas#gmail.com (# -> @)
** This file may be redistributed under the terms of the GNU Public License.
***************************************************************************/

#include "coadialog.h"
#include "ui_coadialog.h"
#include <QtGui>

#define UNKNOWN_STATE "unknown"
#define APP_VERSION "Jira���� v1.2"

COaDialog::COaDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::COaDialog)
{
    ui->setupUi(this);
    setWindowFlags(Qt::WindowCloseButtonHint);
    setFixedSize(this->width(), this->height());
    setWindowTitle(tr(APP_VERSION));

    //init
    CCurl::GlobalInit();

    initUI();

    //����
    readSettings();
}

COaDialog::~COaDialog()
{
    delete ui;
    CCurl::GlobalCleanup();
}

void COaDialog::on_pushButton_clicked()
{
    m_mapTask.clear();
    m_listTask.clear();

    if (ui->lineUser->text().isEmpty()) {
        QMessageBox::warning(this, tr("��ʾ"), tr("�������û���"));
        return;
    }

    if (ui->linePwd->text().isEmpty()) {
        QMessageBox::warning(this, tr("��ʾ"), tr("����������"));
        return;
    }

    if (ui->comboDepartment->currentIndex() == 0) {
        QMessageBox::warning(this, tr("��ʾ"), tr("��ѡ����"));
        return;
    }

    if (ui->chkQa->isChecked() && ui->comboQaDepartment->currentIndex() == 0) {
        QMessageBox::warning(this, tr("��ʾ"), tr("QA��ѡ����"));
        return;
    }

    //��������
    writeSettings();

    enableCtrl(false);

    if (!login())
    {
        QMessageBox::warning(this, tr("��ʾ"), tr("��¼ʧ��"));
        return;
    }

    if (!ui->chkQa->isChecked())
    {
        if (!spider())
        {
            return;
        }
    }
    else
    {
        if (!spiderQa())
        {
            return;
        }
    }

    QString strDepartment = ui->comboDepartment->currentText();
    QString strHead = m_mapDepartment[strDepartment];

    filterData(strHead, m_listTask);
    if (m_listTask.empty())
    {
        QMessageBox::warning(this, tr("��ʾ"), tr("û������"));
        enableCtrl(true);
        return;
    }

    getTaskInfo(m_listTask);

    outputResult(m_listTask);

    enableCtrl(true);
    ui->progressBar->setValue(0);
}

bool COaDialog::login()
{
    QString strUser = ui->lineUser->text();
    QString strPwd = ui->linePwd->text();
    QString strField = tr("os_username=%1&os_password=%2&os_captcha=").arg(strUser).arg(strPwd);
    QString strHtml;
    bool bRet = m_curl.Post("http://jira.woobest.com/rest/gadget/1.0/login", strField, strHtml);
    if (!bRet)
    {
        return false;
    }

    bRet = m_curl.Get("http://jira.woobest.com/secure/IssueNavigator.jspa?mode=hide&requestId=11559", strHtml);
    if (!bRet || strHtml.indexOf("results-count-total") == -1)
    {
        return false;
    }
    return true;
}

bool COaDialog::spider()
{
    bool bRet = false;
    QString strHtml;

    //first visit my jira
    bRet = m_curl.Get("http://jira.woobest.com/secure/IssueNavigator.jspa?mode=hide&requestId=11559", strHtml);
    if (!bRet)
    {
        return false;
    }

    int nTaskNum = getTaskNum(strHtml);
    if (nTaskNum < 0)
    {
        QMessageBox::warning(this, tr("��ʾ"), tr("û������"));
        return false;
    }

    QString strUrl;
    int nPages = (int)(nTaskNum / 50) + 1;
    ui->progressBar->setMaximum(nPages);
    for (int i = 0; i < nPages; i++)
    {
        strUrl = tr("http://jira.woobest.com/secure/IssueNavigator.jspa?pager/start=%1").arg(i * 50);
        bRet = m_curl.Get(strUrl, strHtml);
        if (!bRet)
        {
            return false;
        }

        parseHtml(strHtml);
        ui->progressBar->setValue(i+1);
        QCoreApplication::processEvents();
    }

    return true;
}

bool COaDialog::spiderQa()
{
    bool bRet = false;
    QString strHtml, strFieldInfo;

    //first visit my jira
    bRet = m_curl.Get("http://jira.woobest.com/secure/IssueNavigator!executeAdvanced.jspa", strHtml);
    if (!bRet)
    {
        return false;
    }

    QString strQa = m_mapQaDepartment[ui->comboQaDepartment->currentText()];
    strFieldInfo = tr("jqlQuery=%1currentUser%28%29&runQuery=true&autocomplete=on").arg(strQa);
    bRet = m_curl.Post("http://jira.woobest.com/secure/IssueNavigator!executeAdvanced.jspa", strFieldInfo, strHtml);
    if (!bRet)
    {
        return false;
    }


    int nTaskNum = getTaskNum(strHtml);
    if (nTaskNum < 0)
    {
        QMessageBox::warning(this, tr("��ʾ"), tr("û������"));
        return false;
    }

    QString strUrl;
    int nPages = (int)(nTaskNum / 50) + 1;
    ui->progressBar->setMaximum(nPages);
    for (int i = 0; i < nPages; i++)
    {
        strUrl = tr("http://jira.woobest.com/secure/IssueNavigator.jspa?pager/start=%1").arg(i * 50);
        bRet = m_curl.Get(strUrl, strHtml);
        if (!bRet)
        {
            return false;
        }

        parseHtml(strHtml);
        ui->progressBar->setValue(i+1);
        QCoreApplication::processEvents();
    }

    return true;
}

bool COaDialog::parseHtml(const QString& strHtml)
{
    std::vector<QString> vecTaskId;
    std::vector<uint> vecDate;
    parseTaskId(strHtml, vecTaskId);
    parseDate(strHtml, vecDate);
    if (vecTaskId.size() != vecDate.size())
    {
        QMessageBox::warning(this, tr("��ʾ"), tr("������ƥ��"));
        return false;
    }

    int len = vecTaskId.size();
    for (int i = 0; i < len; i++)
    {
        m_mapTask[vecTaskId[i]] = vecDate[i];
//        qDebug() << vecTaskId[i] << QDateTime::fromTime_t(vecDate[i]).toString("yyyy-MM-dd");
    }

//    std::vector<PAIR> vecSort(m_mapTask.begin(), m_mapTask.end());
//    std::sort(vecSort.begin(), vecSort.end(), compareMapValueDesc);
//    for (std::vector<PAIR>::iterator it = vecSort.begin(); it != vecSort.end(); ++it)
//    {
//        qDebug() << (*it).first << (*it).second << QDateTime::fromTime_t((*it).second).toString("yyyy-MM-dd");
//    }

    return true;
}

void COaDialog::parseTaskId(const QString& strHtml, std::vector<QString>& vec)
{
    vec.clear();
    QString strPattern = "data-issuekey=\"(.*)\"";
    QRegExp regExp;
    regExp.setMinimal(true);//��̰��
    regExp.setPattern(strPattern);
    int pos = 0;
    while ((pos = regExp.indexIn(strHtml, pos)) != -1)
    {
        if (regExp.captureCount() > 0)
        {
            vec.push_back(regExp.cap(1));
        }
        pos += regExp.matchedLength();
    }
}

void COaDialog::parseDate(const QString& strHtml, std::vector<uint>& vec)
{
    vec.clear();
    QString strPattern = "<td.*updated\">.*datetime.*>(.*)</time>";
    QRegExp regExp;
    regExp.setMinimal(true);//��̰��
    regExp.setPattern(strPattern);
    int pos = 0;
    while ((pos = regExp.indexIn(strHtml, pos)) != -1)
    {
        if (regExp.captureCount() > 0)
        {
            QDateTime dt = QDateTime::fromString(regExp.cap(1), "yyyy/MM/dd");
            vec.push_back(dt.toTime_t());
        }
        pos += regExp.matchedLength();
    }
}

int COaDialog::getTaskNum(const QString& strHtml)
{
    QRegExp regExp;
    regExp.setMinimal(true);//��̰��
    regExp.setPattern("<strong class=\"results-count-total\">(.*)</strong>");
    regExp.indexIn(strHtml);
    if (regExp.captureCount() > 0)
    {
        QString str = regExp.cap(1);
        return str.toInt();
    }
    return 0;
}

QString COaDialog::getTaskState(const QString& strHtml)
{
    QRegExp regExp;
    regExp.setMinimal(true);//��̰��
    regExp.setPattern("value resolved.*>(.*)</span>");
    regExp.indexIn(strHtml);
    if (regExp.captureCount() > 0)
    {
        QString str = regExp.cap(1).trimmed();
        return str;
    }
    return QString(UNKNOWN_STATE);
}

QString COaDialog::getTaskTitle(const QString& strHtml)
{
    QRegExp regExp;
    regExp.setMinimal(true);//��̰��
    regExp.setPattern("summary-val\">(.*)</h1>");
    regExp.indexIn(strHtml);
    if (regExp.captureCount() > 0)
    {
        QString str = regExp.cap(1).trimmed();
//        qDebug() << str;
        return str;
    }
    return "";
}

QString COaDialog::ToUtf8(const QString &str)
{
    QTextCodec *codec = QTextCodec::codecForName("UTF-8");
    return codec->toUnicode(str.toAscii());
}

void COaDialog::WriteToFile(const QString& strFile, const QString& strText)
{
    QFile file(strFile);
    if (file.open(QFile::ReadWrite | QFile::Truncate))
    {
        file.write(strText.toAscii().data(), strText.size());
        file.close();
    }
}

bool COaDialog::compareMapValueDesc(const PAIR& a, const PAIR& b)
{
    return a.second > b.second;
}

bool COaDialog::compareTaskDesc(const stTaskId& a, const stTaskId& b)
{
    return a.date > b.date;
}

void COaDialog::filterData(const QString &strHead, std::list<stTaskId>& listTask)
{
    uint begin = 0, end = 0;
    getBeginEndTime(begin, end);

    listTask.clear();
    for (std::map<QString, uint>::iterator it = m_mapTask.begin(); it != m_mapTask.end(); ++it)
    {
        if (it->second >= begin && it->second <= end && it->first.startsWith(strHead))
        {
            listTask.push_back(stTaskId(it->first, it->second));
        }
    }
}

void COaDialog::getTaskInfo(std::list<stTaskId>& listTask)
{
    QString strUrl, strHtml;
    ui->progressBar->setMaximum(listTask.size());
    int count = 0;
    for (std::list<stTaskId>::iterator it = listTask.begin(); it != listTask.end(); ++it)
    {
        ui->progressBar->setValue(++count);
        stTaskId& task = *it;
        strUrl = tr("http://jira.woobest.com/browse/%1").arg(task.id);
        bool bRet = m_curl.Get(strUrl, strHtml);
//        WriteToFile(task.id, strHtml);
        if (bRet)
        {
            task.state = getTaskState(strHtml);
            task.title = getTaskTitle(strHtml);
        }
        else
        {
            task.state = UNKNOWN_STATE;
            continue;
        }
        QCoreApplication::processEvents();
    }
}

void COaDialog::outputResult(std::list<stTaskId>& listTask)
{
    if (listTask.empty())
    {
        return;
    }
    //std::sort(listTask.begin(), listTask.end(), compareTaskDesc);
    listTask.sort();

    QFile file("task.txt");
    if (!file.open(QFile::Truncate | QFile::WriteOnly))
    {
        return;
    }

    QString strLine;
    for (std::list<stTaskId>::iterator it = listTask.begin(); it != listTask.end(); ++it)
    {
        stTaskId& task = *it;
        if (task.state == tr("���޸�"))
        {
            strLine.append(tr("%1 %2 %3 \r\n").arg(QDateTime::fromTime_t(task.date).toString("yyyy-MM-dd")).arg(task.id).arg(task.title));
        }
    }
    strLine.append("--------------------------------------------------------\r\n");
    QByteArray ba = strLine.toLocal8Bit();
    file.write(ba, ba.size());
    file.close();

    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo("task.txt").absoluteFilePath()));
}

void COaDialog::getBeginEndTime(uint& begin, uint& end)
{
    static const QString s_begin[] = {"/01/01", "/04/01", "/07/01", "/10/01"};
    static const QString s_end[] = {"/03/31", "/06/30", "/09/30", "/12/31"};
    int year = ui->comboYear->currentText().toInt();
    int index = ui->comboQuarter->currentIndex();
    QString strBegin = tr("%1%2").arg(year).arg(s_begin[index]);
    QString strEnd = tr("%1%2").arg(year).arg(s_end[index]);
    QDateTime dtBegin = QDateTime::fromString(strBegin, "yyyy/MM/dd");
    QDateTime dtEnd = QDateTime::fromString(strEnd, "yyyy/MM/dd");
    begin = dtBegin.toTime_t();
    end = dtEnd.toTime_t();
}

void COaDialog::initUI()
{
    //ui init
    for (int i = 0; i < 90; i++)
    {
        ui->comboYear->addItem(QString(tr("%1").arg(i + BEGIN_YEAR)));
    }
    int year = QDate::currentDate().year();
    int index = year - BEGIN_YEAR;
    ui->comboYear->setCurrentIndex(index);

    updateUI();

    //��������
    m_mapDepartment[tr("P18")] = "NEX";
    m_mapDepartment[tr("P18��Ӫ��")] = "MXOPERATIONS";
    m_mapDepartment[tr("P20")] = "DF";
    m_mapDepartment[tr("P20��Ӫ��")] = "DFOPERATINS";
    m_mapDepartment[tr("P22��Ӫ��")] = "DY";
    m_mapDepartment[tr("P22��������������")] = "PRO";
    m_mapDepartment[tr("P23")] = "MOW";
    m_mapDepartment[tr("P23��Ӫ��")] = "MWO";
    m_mapDepartment[tr("P25��Ӫ��")] = "JJ";
    m_mapDepartment[tr("P26")] = "JQB";
    m_mapDepartment[tr("P26��Ӫ��")] = "PDJQB";
    m_mapDepartment[tr("P27")] = "NSY";
    m_mapDepartment[tr("P28��Ӫ��")] = "JZMY";
    m_mapDepartment[tr("T08")] = "BBC";
    m_mapDepartment[tr("��Ʒ��")] = "PRODUCT";
    m_mapDepartment[tr("������Դ��")] = "HR";
    m_mapDepartment[tr("ԭζ����")] = "YWSG";
    m_mapDepartment[tr("����")] = "BUSINESS";
    m_mapDepartment[tr("�ͷ���")] = "SERVICE";
    m_mapDepartment[tr("�г���")] = "MARKETING";
    m_mapDepartment[tr("ƽ̨������")] = "PS";
    m_mapDepartment[tr("������")] = "TECH";
    m_mapDepartment[tr("������")] = "ART";
    m_mapDepartment[tr("������")] = "ADMINISTRATION";
    m_mapDepartment[tr("����")] = "FINANCIAL";

    ui->comboDepartment->addItem(tr("��"));
    for (auto it : m_mapDepartment) {
        ui->comboDepartment->addItem(it.first);
    }
    ui->comboDepartment->setCurrentIndex(0);

    //QA��������,URL���룬Ϊ�˷��㣬�����ֶ�ת����
    m_mapQaDepartment[tr("P18")] = "%E9%AA%8C%E6%94%B6%E8%80%85+%3D+";//������
    m_mapQaDepartment[tr("P22")] = "%e9%aa%8c%e6%94%b6%e4%ba%ba+%3d+";//������
    m_mapQaDepartment[tr("P23")] = "%E9%AA%8C%E6%94%B6%E8%80%85+%3D+";//������
    m_mapQaDepartment[tr("P26")] = "%E9%AA%8C%E6%94%B6%E8%80%85+%3D+";//������
    m_mapQaDepartment[tr("P27")] = "%e9%aa%8c%e6%94%b6%e5%91%98+%3d+";//����Ա
    m_mapQaDepartment[tr("ԭζ����")] = "%E9%AA%8C%E6%94%B6%E8%80%85+%3D+";//������

    ui->comboQaDepartment->addItem(tr("��"));
    for (auto it : m_mapQaDepartment) {
        ui->comboQaDepartment->addItem(it.first);
    }
    ui->comboQaDepartment->setCurrentIndex(0);
}

void COaDialog::updateUI()
{
    ui->comboQaDepartment->setVisible(ui->chkQa->isChecked());
}

void COaDialog::enableCtrl(bool bEnabled)
{
    ui->comboQuarter->setEnabled(bEnabled);
    ui->comboYear->setEnabled(bEnabled);
    ui->comboDepartment->setEnabled(bEnabled);
    ui->comboQaDepartment->setEnabled(bEnabled);
    ui->chkQa->setEnabled(bEnabled);
    ui->pushButton->setEnabled(bEnabled);
}

void COaDialog::readSettings()
{
    QString strIniFileName = qApp->applicationDirPath() + "/jiratools.ini";
    QSettings settings(strIniFileName, QSettings::IniFormat);
    ui->lineUser->setText(settings.value("jiratools/user").toString());
    QString strPwdEncode = settings.value("jiratools/pwd").toString();
    QString strPwdUncode = QByteArray::fromHex(QByteArray::fromBase64(strPwdEncode.toLatin1()));
    ui->linePwd->setText(strPwdUncode);
    qDebug() << strPwdEncode;
    qDebug() << strPwdUncode;
    if (settings.contains("jiratools/department")) {
        ui->comboDepartment->setCurrentIndex(settings.value("jiratools/department").toInt());
    }
    if (settings.contains("jiratools/qa")) {
        ui->chkQa->setChecked(settings.value("jiratools/qa").toBool());
    }
    if (settings.contains("jiratools/qadepartment")) {
        ui->comboQaDepartment->setCurrentIndex(settings.value("jiratools/qadepartment").toInt());
    }
}

void COaDialog::writeSettings()
{
    QString strIniFileName = qApp->applicationDirPath() + "/jiratools.ini";
    QString strPwd = ui->linePwd->text().toLatin1().toHex().toBase64().data();
    QSettings settings(strIniFileName, QSettings::IniFormat);
    settings.setValue("jiratools/user", ui->lineUser->text());
    settings.setValue("jiratools/pwd", strPwd);
    settings.setValue("jiratools/department", ui->comboDepartment->currentIndex());
    settings.setValue("jiratools/qa", ui->chkQa->isChecked());
    settings.setValue("jiratools/qadepartment", ui->comboQaDepartment->currentIndex());
}

void COaDialog::on_chkQa_toggled(bool checked)
{
    updateUI();
}
