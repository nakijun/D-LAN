#include <QtCore/QDebug>

#include <QMutexLocker>

#if defined(Q_OS_WIN32)
#include <priv/FileUpdater/DirWatcherWin.h>
using namespace FM;

#include <priv/Exceptions.h>
#include <priv/FileUpdater/WaitConditionWin.h>

DirWatcherWin::DirWatcherWin()
   : mutex(QMutex::Recursive)
{
}

DirWatcherWin::~DirWatcherWin()
{
   QMutexLocker lock(&this->mutex);

   foreach (Dir* d, this->dirs)
      delete d;
}

bool DirWatcherWin::addDir(const QString& path)
{
   QMutexLocker lock(&this->mutex);

   if (this->dirs.size() > MAXIMUM_WAIT_OBJECTS - MAX_WAIT_CONDITION)
      return false;

   TCHAR pathTCHAR[path.size() + 1];
   path.toWCharArray(pathTCHAR);
   pathTCHAR[path.size()] = 0;

   HANDLE fileHandle = CreateFile(pathTCHAR, // Pointer to the file name.
      FILE_LIST_DIRECTORY, // Access (read/write) mode.
      FILE_SHARE_READ | FILE_SHARE_WRITE, // Share mode.
      NULL, // security descriptor
      OPEN_EXISTING, // how to create
      FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, // file attributes
      NULL // file with attributes to copy
   );

   if (fileHandle == INVALID_HANDLE_VALUE)
      throw DirNotFoundException(path);

   HANDLE eventHandle = CreateEvent(NULL, FALSE, FALSE, NULL);
   if (eventHandle == NULL)
      throw DirNotFoundException(path);

   Dir* dir = new Dir(fileHandle, eventHandle, path);

   if (!this->watch(dir))
   {
      delete dir;
      return false;
   }

   this->dirs << dir;
   return true;
}

void DirWatcherWin::rmDir(const QString& path)
{
   QMutexLocker lock(&this->mutex);

   for (QListIterator<Dir*> i(this->dirs); i.hasNext();)
   {
      Dir* dir = i.next();
      if (dir->fullPath == path)
      {
         this->dirsToDelete << dir;
         break;
      }
   }
}

int DirWatcherWin::nbWatchedDir()
{
   QMutexLocker lock(&this->mutex);
   return this->dirs.size();
}

const QList<WatcherEvent> DirWatcherWin::waitEvent(QList<WaitCondition*> ws)
{
   return this->waitEvent(INFINITE, ws);
}

const QList<WatcherEvent> DirWatcherWin::waitEvent(int timeout, QList<WaitCondition*> ws)
{
   this->mutex.lock();

   if (ws.size() > MAX_WAIT_CONDITION)
   {
      L_ERRO(QString("DirWatcherWin::waitEvent : No more than %1 condition(s), some directory will not be watched any more.").arg(MAX_WAIT_CONDITION));
      int n = this->dirs.size() + ws.size() - MAXIMUM_WAIT_OBJECTS;
      while (n --> 0) // The better C++ operator!
         this->dirsToDelete << this->dirs.takeLast();
   }

   if (!this->dirsToDelete.isEmpty())
   {
      for (QMutableListIterator<Dir*> i(this->dirs); i.hasNext();)
      {
         Dir* dir = i.next();
         if (this->dirsToDelete.contains(dir))
         {
            i.remove();
            delete dir;
         }
      }
      this->dirsToDelete.clear();
   }

   QList<Dir*> dirsCopy(this->dirs);

   int n = dirsCopy.size();
   int m = n + ws.size();

   HANDLE eventsArray[m];
   for(int i = 0; i < n; i++)
      eventsArray[i] = dirsCopy[i]->overlapped.hEvent;

   for (int i = 0; i < ws.size(); i++)
   {
      HANDLE hdl = ws[i]->getHandle();
      eventsArray[i+n] = hdl;
   }

   this->mutex.unlock();

   DWORD waitStatus = WaitForMultipleObjects(m, eventsArray, FALSE, timeout);

   QMutexLocker lock(&this->mutex);

   if (!dirsCopy.empty() && waitStatus >= WAIT_OBJECT_0 && waitStatus <= WAIT_OBJECT_0 + (DWORD)n - 1)
   {
      int n = waitStatus - WAIT_OBJECT_0;

      QList<WatcherEvent> events;

      FILE_NOTIFY_INFORMATION* notifyInformation = (FILE_NOTIFY_INFORMATION*)this->notifyBuffer;

      QString previousPath; // Used for FILE_ACTION_RENAMED_OLD_NAME

      forever
      {
         // We need to add a null character termination because 'QString::fromStdWString' need one.
         int nbChar = notifyInformation->FileNameLength / sizeof(TCHAR);
         TCHAR filenameTCHAR[nbChar + 1];
         wcsncpy(filenameTCHAR, notifyInformation->FileName, nbChar);
         filenameTCHAR[nbChar] = 0;
         QString filename = QString::fromStdWString(filenameTCHAR);

//         L_WARN("---------");
//         L_WARN(QString("Action = %1").arg(notifyInformation->Action));
//         L_WARN(QString("filename = %1").arg(filename));
//         L_WARN(QString("offset = %1").arg(notifyInformation->NextEntryOffset));
//         L_WARN("---------");

         QString path = dirsCopy[n]->fullPath;
         path.append('/').append(filename);

         switch (notifyInformation->Action)
         {
         case FILE_ACTION_ADDED:
            events << WatcherEvent(WatcherEvent::NEW, path);
            break;
         case FILE_ACTION_REMOVED:
            events << WatcherEvent(WatcherEvent::DELETED, path);
            break;
         case FILE_ACTION_MODIFIED:
            events << WatcherEvent(WatcherEvent::CONTENT_CHANGED, path);
            break;
         case FILE_ACTION_RENAMED_OLD_NAME:
            previousPath = path;
            break;
         case FILE_ACTION_RENAMED_NEW_NAME:
            events << WatcherEvent(WatcherEvent::RENAME, previousPath, path);
            break;
         default:
            L_WARN(QString("File event action unkown : %1").arg(notifyInformation->Action));
         }

         if (!notifyInformation->NextEntryOffset)
            break;

         notifyInformation = (FILE_NOTIFY_INFORMATION*)((LPBYTE)notifyInformation + notifyInformation->NextEntryOffset);
      }

      this->watch(dirsCopy[n]);

      return events;
   }
   else if (!ws.isEmpty() && waitStatus >= WAIT_OBJECT_0 + (DWORD)n && waitStatus <= WAIT_OBJECT_0 + (DWORD)m - 1)
   {
      return QList<WatcherEvent>();
   }
   else if (waitStatus == WAIT_TIMEOUT)
   {
      QList<WatcherEvent> events;
      events.append(WatcherEvent(WatcherEvent::TIMEOUT));
      return events;
   }
   else if (waitStatus == WAIT_FAILED)
   {
      L_ERRO(QString("WaitForMultipleObjects(..) failed, error code : %1").arg(GetLastError()));
   }
   else
   {
      L_ERRO(QString("WaitForMultipleObjects(..), status : %1").arg(waitStatus));
   }

   QList<WatcherEvent> events;
   events << WatcherEvent(WatcherEvent::UNKNOWN);
   return QList<WatcherEvent>();
}

bool DirWatcherWin::watch(Dir* dir)
{
   return ReadDirectoryChangesW(
      dir->file, // The file handle;
      &this->notifyBuffer, // The buffer where the information is put when an event occur.
      NOTIFY_BUFFER_SIZE,
      TRUE, // Watch subtree.
      FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION,
      &this->nbBytesNotifyBuffer, // Not used in asynchronous mode.
      &dir->overlapped,
      NULL
   );
}

#endif