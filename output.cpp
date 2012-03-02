#include "output.h"

#include <QDateTime>
#include <QFileInfo>
#include <QDir>

namespace Langmuir
{

OutputInfo::OutputInfo(const QString &name, const SimulationParameters *par, Options options)
{
    // make substitutions in the passed name
    QString result = name;
    QString sep = "-";
    QString stub("");
    QString step("");
    QString  sim("");
    QString path("");

    // if SimulationParameters were passed, convert the needed ones to strings
    if (par != 0)
    {
        stub = QString("%1").arg(par->outputStub);
        step = QString("%1").arg(par->currentStep);
        sim  = QString("%1").arg(par->currentSimulation);
        path = QString("%1").arg(par->outputPath)+QDir::separator();
    }
    result.replace(QRegExp("%stub", Qt::CaseInsensitive), stub);  // replace %stub
    result.replace(QRegExp("%sim" , Qt::CaseInsensitive), sim );  // replace %sim
    result.replace(QRegExp("%step", Qt::CaseInsensitive), step);  // replace %step

    // replace %path, making sure it only occured once, and was at the begining of name
    {
        QRegExp regex("%path", Qt::CaseInsensitive);
        int count = result.count(regex);
        if (count > 1)
        {
            qFatal("invalid file name %s",qPrintable(name));
        }
        if (count == 1)
        {
            regex = QRegExp("\\s*^%path", Qt::CaseInsensitive);
            count = result.count(regex);
            if (count != 1)
            {
                qFatal("invalid file name %s",qPrintable(name));
            }
            result.replace(regex,path);
            result = QDir::cleanPath(result);
        }
    }

    // reset the fileInfo (this class is derived from QFileInfo)
    setFile(result);

    // get rid of extra "seperators" in the generated file name
    result = baseName();
    result.replace(QRegExp(QString("^%1*").arg(sep)), ""); // at the begining
    result.replace(QRegExp(QString("%1*$").arg(sep)), ""); // at the end
    result.replace(QRegExp(QString("%1+" ).arg(sep)),sep); // remove duplicates

    // add back in the file name extension if there was one
    if (!completeSuffix().isEmpty())
    {
        result = result + "." + completeSuffix();
    }

    // reset the fileInfo (this class is derived from QFileInfo)
    setFile(absoluteDir(),result);

    // make sure we generated a fileName and not a directory
    if (isDir())
    {
        qFatal("can not generate file name:\n\t"
               "the file name is a directory:\n\t%s",
               qPrintable(absoluteFilePath()));
    }

    // make sure we didn't generate an empty file name
    if ( completeBaseName().isEmpty() )
    {
        qFatal("can not generate file name:\n\t"
               "the file name is empty:\n\t%s",
               qPrintable(absoluteFilePath()));
    }

    // if the directory passed doesn't exist, try to make it
    if (!dir().exists())
    {
        // if we can't make the directory, then theres an error
        if ( ! dir().mkpath(dir().absolutePath()) )
        {
            qFatal("can not generate file:\n\t"
                   "directory does not exist and "
                   "I can not create it:\n\t%s",
                   qPrintable(absoluteFilePath()));
        }
    }

    // if we didn't want to open in append mode...
    if (!options.testFlag(AppendMode))
    {
        // if the file exists...
        if (exists())
        {
            // if we were given parameters and don't want to overwrite files or...
            // if we were not givent parameters...
            if ((par != 0 && ! par->outputOverwrite) || par == 0)
            {
                // backup the file
                backupFile(absoluteFilePath());
            }
        }
    }
}

OutputStream::OutputStream(const QString &name,
           const SimulationParameters *par,
           OutputInfo::Options options,
           QObject *parent)
    : QObject(parent), m_info(name,par,options)
{
    // open as Text and as WriteOnly, it's an OutputStream
    QIODevice::OpenMode mode = QIODevice::Text|QIODevice::WriteOnly;

    // open as Append if the option was set
    if (options.testFlag(OutputInfo::AppendMode)) mode |= QIODevice::Append;

    // set the parent of our file
    m_file.setParent(this);

    // set the file name
    m_file.setFileName(m_info.absoluteFilePath());

    // give an error if we can't open the file
    if (!m_file.open(mode))
    {
        qFatal("can not open file:\n\t%s",
               qPrintable(m_info.absoluteFilePath()));
    }

    // set the file as our device (this class is derived from QTextStream)
    setDevice(&m_file);
}

OutputStream::~OutputStream()
{
    // make sure the stream is flushed and the the file is closed
    // this probabily happens already, just making sure
    flush();
    m_file.close();
}

const OutputInfo& OutputStream::info()
{
    return m_info;
}

const QFile& OutputStream::file()
{
    return m_file;
}

void backupFile(const QString& name)
{
    // obtain file info
    QFileInfo info(name);

    // do nothing if this file does not exist
    if ( ! info.exists() ) return;

    // give an error if we were passed a directory
    if (info.isDir())
    {
        qFatal("backup failure: name is not a file\n\t%s",
                 qPrintable(info.absoluteFilePath()));
    }

    // get the current time
    QString now = QDateTime::currentDateTime().toString("yyyyMMdd");

    // search the directory for other files of the form path/name.date.*
    QFileInfoList others = info.dir().entryInfoList(
                QStringList() << QString("%1.%2*")
                    .arg(info.fileName())
                    .arg(now)
                );

    // examing the names found to see if they are of the form path/name.date.number
    int revision = 1;
    QRegExp regex(QString("%1\\.%2\\.(\\d+)$")
                    .arg(info.fileName())
                    .arg(now));
    foreach( QFileInfo other, others )
    {
        // make the revision number greater than those already found
        if (regex.exactMatch(other.fileName()))
        {
            int n = regex.cap(1).toInt();
            if (n >= revision)
            {
                revision = n + 1;
            }
        }
    }

    // make the new file name path/name.date.revision
    QString newName = QString("%1.%2.%3")
            .arg(info.absoluteFilePath())
            .arg(now)
            .arg(revision);

    // rename (move) the old file to the new name
    // give an error if we fail at moving the file
    if (!QFile::rename(info.absoluteFilePath(),newName))
    {
        qFatal("backup failure:\n\t%s\n\t%s",
                 qPrintable(info.absoluteFilePath()),
                 qPrintable(newName));
    }
}

}

QTextStream & newline(QTextStream& s)
{
    // store the old width
    int width = s.fieldWidth();
    s << qSetFieldWidth(0) << '\n' << qSetFieldWidth(width);
    return s;
}

QTextStream & space(QTextStream& s)
{
    // store the old width
    int width = s.fieldWidth();
    s << qSetFieldWidth(0) << ' ' << qSetFieldWidth(width);
    return s;
}