#include <priv/DownloadManager.h>
using namespace DM;

#include <Protos/queue.pb.h>

#include <Common/Settings.h>
#include <Common/PersistantData.h>
#include <Common/Constants.h>
#include <Common/ProtoHelper.h>

#include <priv/Log.h>
#include <priv/FileDownload.h>
#include <priv/DirDownload.h>
#include <priv/Constants.h>

DownloadManager::DownloadManager(QSharedPointer<FM::IFileManager> fileManager, QSharedPointer<PM::IPeerManager> peerManager)
   : fileManager(fileManager), peerManager(peerManager), numberOfDownload(0)
{
   connect(&this->occupiedPeersAskingForHashes, SIGNAL(newFreePeer(PM::IPeer*)), this, SLOT(peerNoLongerAskingForHashes(PM::IPeer*)));
   connect(&this->occupiedPeersDownloadingChunk, SIGNAL(newFreePeer(PM::IPeer*)), this, SLOT(peerNoLongerDownloadingChunk(PM::IPeer*)));

   connect(this->fileManager.data(), SIGNAL(fileCacheLoaded()), this, SLOT(fileCacheLoaded()));

   this->timer.setInterval(RESCAN_QUEUE_PERIOD_IF_ERROR);
   this->timer.setSingleShot(true);
   connect(&this->timer, SIGNAL(timeout()), this, SLOT(scanTheQueue()));
}

DownloadManager::~DownloadManager()
{
   this->saveQueueToFile();
}

/**
  * Insert a new download at the end.
  *
  */
void DownloadManager::addDownload(const Protos::Common::Entry& entry, Common::Hash peerSource)
{
   QMutableLinkedListIterator<Download*> i(this->downloads);
   i.toBack();
   this->addDownload(entry, peerSource, false, i);
}

void DownloadManager::addDownload(const Protos::Common::Entry& entry, Common::Hash peerSource, bool complete)
{
   QMutableLinkedListIterator<Download*> i(this->downloads);
   i.toBack();
   this->addDownload(entry, peerSource, complete, i);
}

/**
  * Insert a new download at the given position.
  */
void DownloadManager::addDownload(const Protos::Common::Entry& entry, Common::Hash peerSource, bool complete, QMutableLinkedListIterator<Download*> iterator)
{
   if (this->isEntryAlreadyQueued(entry))
   {
      L_WARN(QString("Entry already exists, it will no be added to the queue : %1").arg(Common::ProtoHelper::getStr(entry, &Protos::Common::Entry::name)));
      return;
   }

   switch (entry.type())
   {
   case Protos::Common::Entry_Type_DIR :
      {
         DirDownload* dirDownload = new DirDownload(this->fileManager, this->peerManager, peerSource, entry);
         connect(dirDownload, SIGNAL(newEntries(Protos::Core::GetEntriesResult)), this, SLOT(newEntries(Protos::Core::GetEntriesResult)));
         iterator.insert(dirDownload);
         dirDownload->retrieveEntries();
      }
      break;

   case Protos::Common::Entry_Type_FILE :
      {
         FileDownload* fileDownload = new FileDownload(this->fileManager, this->peerManager, this->occupiedPeersAskingForHashes, this->occupiedPeersDownloadingChunk, peerSource, entry, complete);
         iterator.insert(fileDownload);
      }
      break;
   }
}

QList<IDownload*> DownloadManager::getDownloads()
{
   QList<IDownload*> listDownloads;

   // TODO : very heavy!
   for (QLinkedListIterator<Download*> i(this->downloads); i.hasNext();)
      listDownloads << i.next();

   return listDownloads;
}

QList< QSharedPointer<IChunkDownload> > DownloadManager::getUnfinishedChunks(int n)
{
   // TODO
   return QList< QSharedPointer<IChunkDownload> >();
}

int DownloadManager::getDownloadRate() const
{
   int downloadRate = 0;
   for (QLinkedListIterator<Download*> i(this->downloads); i.hasNext();)
   {
      FileDownload* fileDownload = dynamic_cast<FileDownload*>(i.next());

      if (fileDownload && fileDownload->getStatus() == DOWNLOADING)
         downloadRate += fileDownload->getDownloadRate();
   }
   return downloadRate;
}

void DownloadManager::fileCacheLoaded()
{
   this->loadQueueFromFile();
}

void DownloadManager::newEntries(const Protos::Core::GetEntriesResult& entries)
{
   DirDownload* dirDownload = dynamic_cast<DirDownload*>(this->sender());
   QMutableLinkedListIterator<Download*> i(this->downloads);
   if (!i.findNext(dirDownload))
      return;
   i.remove();

   for (int n = 0; n < entries.entry_size(); n++)
      this->addDownload(entries.entry(n), dirDownload->getPeerSourceID(), false, i);

   delete dirDownload;
}

/**
  * Search for a new file to asking hashes.
  */
void DownloadManager::peerNoLongerAskingForHashes(PM::IPeer* peer)
{
   for (QLinkedListIterator<Download*> i(this->downloads); i.hasNext();)
   {
      FileDownload* fileDownload = dynamic_cast<FileDownload*>(i.next());
      if (fileDownload && fileDownload->retreiveHashes())
         break;
   }
}

/**
  * Search a chunk to download.
  */
void DownloadManager::peerNoLongerDownloadingChunk(PM::IPeer* peer)
{
   L_DEBU(QString("A peer is free : %1, number of downloading thread : %2").arg(peer->getID().toStr()).arg(this->numberOfDownload));
   this->scanTheQueue();
}

void DownloadManager::scanTheQueue()
{
   L_DEBU("Scanning the queue..");
   for (QLinkedListIterator<Download*> i(this->downloads); i.hasNext() && this->numberOfDownload < static_cast<int>(SETTINGS.get<quint32>("number_of_downloader"));)
   {
      FileDownload* fileDownload = dynamic_cast<FileDownload*>(i.next());
      if (!fileDownload)
         continue;

      QSharedPointer<ChunkDownload> chunkDownload = fileDownload->getAChunkToDownload();

      if (fileDownload->getStatus() >= 0x20)
         this->timer.start();

      if (chunkDownload.isNull())
         continue;

      connect(chunkDownload.data(), SIGNAL(downloadFinished()), this, SLOT(downloadFinished()), Qt::DirectConnection);
      if (chunkDownload->startDownloading())
         this->numberOfDownload++;
   }
}

/**
  * Called from a download thread.
  */
void DownloadManager::downloadFinished()
{
   this->sender()->disconnect(this, SLOT(downloadFinished()));
   this->numberOfDownload--;
}

void DownloadManager::loadQueueFromFile()
{
   Protos::Queue::Queue savedQueue;

   try
   {
      Common::PersistantData::getValue(Common::FILE_QUEUE, savedQueue);
      if (static_cast<int>(savedQueue.version()) != FILE_QUEUE_VERSION)
      {
         L_ERRO(QString("The version (%1) of the queue file \"%2\" doesn't match the current version (%3)").arg(savedQueue.version()).arg(Common::FILE_QUEUE).arg(FILE_QUEUE_VERSION));
         Common::PersistantData::rmValue(Common::FILE_QUEUE);
         return;
      }

      for (int i = 0; i < savedQueue.entry_size(); i++)
      {
         const Protos::Queue::Queue_Entry& entry = savedQueue.entry(i);
         this->addDownload(entry.entry(), Common::Hash(entry.peer_id().hash().data()), entry.complete());
      }
   }
   catch (Common::UnknownValueException& e)
   {
      L_WARN(QString("The download queue file cache cannot be retrived (the file doesn't exist) : %1").arg(Common::FILE_QUEUE));
   }
   catch (...)
   {
      L_WARN(QString("The download queue file cache cannot be retrived (Unkown exception) : %1").arg(Common::FILE_QUEUE));
   }
}

void DownloadManager::saveQueueToFile()
{
   Protos::Queue::Queue savedQueue;
   savedQueue.set_version(FILE_QUEUE_VERSION);

   for (QLinkedListIterator<Download*> i(this->downloads); i.hasNext();)
   {
      i.next()->populateEntry(savedQueue.add_entry());
   }

   Common::PersistantData::setValue(Common::FILE_QUEUE, savedQueue);
}

bool DownloadManager::isEntryAlreadyQueued(const Protos::Common::Entry& entry)
{
   for (QLinkedListIterator<Download*> i(this->downloads); i.hasNext();)
   {
      Download* download = i.next();
      // TODO : Do we should check peer_id also?
      if (
         download->getEntry().type() == entry.type() &&
         download->getEntry().path() == entry.path() &&
         download->getEntry().name() == entry.name() &&
         download->getEntry().size() == entry.size()
      )
         return true;
   }
   return false;
}
