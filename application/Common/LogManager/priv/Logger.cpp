#include <priv/Logger.h>
using namespace LM;

#include <QtDebug>
#include <QThread>
#include <QSharedPointer>

#include <Constants.h>
#include <Global.h>

#include <priv/Entry.h>

QTextStream* Logger::out(0);
QMutex Logger::mutex(QMutex::Recursive);
int Logger::nbLogger(0);
QString Logger::logDirName;

QList< QSharedPointer<LoggerHook> > Logger::loggerHooks;

void Logger::setLogDirName(const QString& logDirName)
{
   Logger::logDirName = logDirName;
}

void Logger::addALoggerHook(QSharedPointer<LoggerHook> loggerHook)
{
   QMutexLocker lock(&Logger::mutex);
   Logger::loggerHooks << loggerHook;
}

Logger::Logger(const QString& name)
   : name(name)
{
   Logger::nbLogger += 1;
}

Logger::~Logger()
{
   QMutexLocker lock(&Logger::mutex);
   Logger::nbLogger -= 1;

   if (Logger::nbLogger == 0 && Logger::out)
   {
      delete Logger::out->device(); // Is this necessary?
      delete Logger::out;
   }
}

void Logger::log(const QString& message, Severity severity, const char* filename, int line) const
{
   QMutexLocker lock(&Logger::mutex);

   QString threadName = QThread::currentThread()->objectName();
   threadName = threadName.isEmpty() ? QString::number((quint32)QThread::currentThreadId()) : threadName;

   QString filenameLine;
   if (filename && line)
      filenameLine = QString("%1:%2").arg(filename, QString::number(line));

   QSharedPointer<Entry> entry(new Entry(QDateTime::currentDateTime(), severity, this->name, threadName, filenameLine, message));

   // Say to all hooks there is a new message.
   foreach (QSharedPointer<LoggerHook> loggerHook, Logger::loggerHooks)
      loggerHook->newMessage(entry);

   Logger::createFileLog();
   if (Logger::out)
      (*Logger::out) << entry->toStrLine() << endl;
}

void Logger::log(const ILoggable& object, Severity severity, const char* filename, int line) const
{
    this->log(object.toStringLog(), severity, filename, line);
}

/**
  * To sort a 'QList<QFileInfo>' by its last modified date.
  * See 'Logger::deleteOldestLog(..)'.
  */
bool fileInfoLessThan(const QFileInfo& f1, const QFileInfo& f2)
{
   return f1.lastModified() < f2.lastModified();
}

/**
  * It will create the file log and open it for writing if it doesn't already exist.
  * Must be called in a mutex.
  */
void Logger::createFileLog()
{
   if (!Logger::out)
   {
      if (logDirName.isEmpty())
         logDirName = Common::LOG_FOLDER_NAME;

      QTextStream out(stderr);

      if (!Common::Global::createApplicationFolder())
      {
         out << "Error, cannot create application directory : " << Common::APPLICATION_FOLDER_PATH << endl;
      }
      else
      {
         QDir appDir(Common::APPLICATION_FOLDER_PATH);
         if (!appDir.exists(logDirName) && !appDir.mkdir(logDirName))
         {
            out << "Error, cannot create log directory : " << Common::APPLICATION_FOLDER_PATH << "/" << logDirName << endl;
         }
         else
         {
            QDir logDir(Common::APPLICATION_FOLDER_PATH + '/' + logDirName);

            QString filename = QDateTime::currentDateTime().toString("yyyy_MM_dd-hh_mm_ss") + ".log";

            QFile* file = new QFile(logDir.absoluteFilePath(filename));
            if (!file->open(QIODevice::WriteOnly))
            {
               out << "Error, cannot create log file : " << logDir.absoluteFilePath(filename) << endl;
               delete file;
            }
            else
            {
               Logger::deleteOldestLog(logDir);
               Logger::out = new QTextStream(file);
               Logger::out->setCodec("UTF-8");
            }
         }
      }
   }
}

void Logger::deleteOldestLog(const QDir& logDir)
{
   QList<QFileInfo> entries;
   foreach (QFileInfo entry, logDir.entryInfoList())
   {
      if (entry.fileName() == "." || entry.fileName() == ".." || !entry.fileName().endsWith(".log"))
         continue;
      if (entry.isFile())
         entries.append(entry);
   }
   qSort(entries.begin(), entries.end(), fileInfoLessThan);

   while (entries.size() > NB_LOGFILE)
      QFile::remove(entries.takeFirst().absoluteFilePath());
}