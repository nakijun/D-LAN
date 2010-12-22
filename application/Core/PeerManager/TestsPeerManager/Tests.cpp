/**
  * Aybabtu - A decentralized LAN file sharing software.
  * Copyright (C) 2010-2011 Greg Burri <greg.burri@gmail.com>
  *
  * This program is free software: you can redistribute it and/or modify
  * it under the terms of the GNU General Public License as published by
  * the Free Software Foundation, either version 3 of the License, or
  * (at your option) any later version.
  *
  * This program is distributed in the hope that it will be useful,
  * but WITHOUT ANY WARRANTY; without even the implied warranty of
  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  * GNU General Public License for more details.
  *
  * You should have received a copy of the GNU General Public License
  * along with this program.  If not, see <http://www.gnu.org/licenses/>.
  */
  
#include <Tests.h>
using namespace PM;

#include <QtDebug>
#include <QStringList>

#include <Protos/core_protocol.pb.h>
#include <Protos/core_settings.pb.h>
#include <Protos/common.pb.h>

#include <Common/LogManager/Builder.h>
#include <Common/PersistentData.h>
#include <Common/Constants.h>
#include <Common/Global.h>
#include <Common/Network.h>
#include <Common/ZeroCopyStreamQIODevice.h>
#include <Common/Settings.h>

#include <ResultListener.h>
#include <IGetEntriesResult.h>
#include <IGetHashesResult.h>

const int Tests::PORT = 59487;

Tests::Tests()
{
}

void Tests::initTestCase()
{
   LM::Builder::initMsgHandler();
   qDebug() << "===== initTestCase() =====";
   try
   {
      QString tempFolder = Common::Global::setCurrentDirToTemp("PeerManagerTests");
      qDebug() << "Application folder path (where is put the persistent data) : " << APPLICATION_FOLDER_PATH;
      qDebug() << "The file created during this test are put in : " << tempFolder;
   }
   catch(Common::Global::UnableToSetTempDirException& e)
   {
      QFAIL(e.what());
   }

   Common::PersistentData::rmValue(Common::FILE_CACHE); // Reset the stored cache.

   SETTINGS.setFilename("core_settings_peer_manager_tests.txt");
   SETTINGS.setSettingsMessage(new Protos::Core::Settings());

   this->createInitialFiles();

   this->fileManagers << FM::Builder::newFileManager() << FM::Builder::newFileManager();

   this->peerIDs << Hash::fromStr("11111111111111111111111111111111111111111111111111111111") << Hash::fromStr("22222222222222222222222222222222222222222222222222222222");
   this->peerSharedDirs << "/sharedDirs/peer1" << "/sharedDirs/peer2";

   // 1) Create eache peer manager.
   for (int i = 0; i < this->peerIDs.size(); i++)
   {
      SETTINGS.set("peer_id", this->peerIDs[i]);
      this->peerManagers << Builder::newPeerManager(this->fileManagers[i]);
   }

   // 2) Set the shared directories.
   for (int i = 0; i < this->peerIDs.size(); i++)
   {
      this->fileManagers[i]->setSharedDirsReadOnly(QStringList() << QDir::currentPath().append(this->peerSharedDirs[i]));
   }

   // 3) Create the peer update (simulate the periodic update).
   this->peerUpdater = new PeerUpdater(this->fileManagers, this->peerManagers, PORT);

   // 4) Create the servers to listen new TCP connections and forward them to the right peer manager.
   for (int i = 0; i < this->peerIDs.size(); i++)
   {
      this->servers << new TestServer(this->peerManagers[i], PORT + i);
   }
}

void Tests::updatePeers()
{
   qDebug() << "===== updatePeers() =====";

   this->peerUpdater->start();

   // This test shouldn't take less than ~3 s.
   QElapsedTimer timer;
   timer.start();

   // Check if each peer know the other.
   for (int i = 0; i < this->peerIDs.size(); i++)
   {
      QList<IPeer*> peers = this->peerManagers[i]->getPeers();

      // Wait peer i knows other peers.
      if (peers.size() != this->peerIDs.size() - 1)
      {
         i--;
         QTest::qWait(100);
         if (timer.elapsed() > 3000)
            QFAIL("Update peers failed..");
         continue;
      }

      for (int j = 0; j < this->peerIDs.size(); j++)
      {
         if (j == i)
            continue;

         bool found = false;
         for (int k = 0; k < peers.size(); k++)
            if (peers[k]->getID() == this->peerManagers[j]->getID())
            {
               found = true;
               QCOMPARE(peers[k]->getNick(), this->peerManagers[j]->getNick());

               // Wait peer j knows peer k amount (amount increase concurrently during the scanning process).
               if (peers[k]->getSharingAmount() != this->fileManagers[j]->getAmount())
               {
                  k--;
                  QTest::qWait(100);
                  if (timer.elapsed() > 3000)
                     QFAIL("Sharing amount not equals..");
                  continue;
               }
               break;
            }

         QVERIFY(found);
      }
   }
}

void Tests::getPeerFromID()
{
   qDebug() << "===== getPeerFromID() =====";

   for (int i = 0; i < this->peerIDs.size(); i++)
   {
      for (int j = 0; j < this->peerIDs.size(); j++)
      {
         if (j == i)
            continue;
         QCOMPARE(this->peerManagers[i]->getID(), this->peerManagers[j]->getPeer(this->peerManagers[i]->getID())->getID());
      }

      QVERIFY(this->peerManagers[i]->getPeer(Common::Hash::rand()) == 0);
   }
}

void Tests::askForRootEntries()
{
   qDebug() << "===== askForRootEntries() =====";

   Protos::Core::GetEntries getEntriesMessage;
   QSharedPointer<IGetEntriesResult> result = this->peerManagers[0]->getPeers()[0]->getEntries(getEntriesMessage);
   connect(result.data(), SIGNAL(result(Protos::Core::GetEntriesResult)), &this->resultListener, SLOT(entriesResult(Protos::Core::GetEntriesResult)));
   result->start();
   QTest::qWait(1500);
   QCOMPARE(this->resultListener.getNbEntriesResultReceived(0), 1);
}

/**
  * Use the same socket as the previous request.
  */
void Tests::askForSomeEntries()
{
   qDebug() << "===== askForSomeEntries() =====";

   QVERIFY(!this->resultListener.getEntriesResultList().isEmpty());

   Protos::Core::GetEntries getEntriesMessage1;
   getEntriesMessage1.mutable_dirs()->add_entry()->CopyFrom(this->resultListener.getEntriesResultList().last().entries(0).entry(0));
   QSharedPointer<IGetEntriesResult> result1 = this->peerManagers[0]->getPeers()[0]->getEntries(getEntriesMessage1);
   connect(result1.data(), SIGNAL(result(Protos::Core::GetEntriesResult)), &this->resultListener, SLOT(entriesResult(Protos::Core::GetEntriesResult)));
   result1->start();
   QTest::qWait(1000);
   QCOMPARE(this->resultListener.getNbEntriesResultReceived(0), 4);

   Protos::Core::GetEntries getEntriesMessage2;
   Protos::Common::Entry* entry = getEntriesMessage2.mutable_dirs()->add_entry();
   entry->CopyFrom(this->resultListener.getEntriesResultList().last().entries(0).entry(0));
   entry->mutable_shared_dir()->CopyFrom(getEntriesMessage1.dirs().entry(0).shared_dir());
   QSharedPointer<IGetEntriesResult> result2 = this->peerManagers[0]->getPeers()[0]->getEntries(getEntriesMessage2);
   connect(result2.data(), SIGNAL(result(Protos::Core::GetEntriesResult)), &this->resultListener, SLOT(entriesResult(Protos::Core::GetEntriesResult)));
   result2->start();
   QTest::qWait(1000);
   QCOMPARE(this->resultListener.getNbEntriesResultReceived(0), 3);
}

void Tests::askForHashes()
{
   qDebug() << "===== askForHashes() =====";

   // 1) Create a big file.
   {
      QFile file("sharedDirs/peer2/big.bin");
      file.open(QIODevice::WriteOnly);

      // To have four different hashes.
      for (int i = 0; i < 4; i++)
      {
         QByteArray randomData(32 * 1024 * 1024, i);
         file.write(randomData);
      }
   }
   QTest::qWait(200);

   Protos::Common::Entry fileEntry;
   fileEntry.set_type(Protos::Common::Entry_Type_FILE);
   fileEntry.set_path("/");
   fileEntry.set_name("big.bin");
   fileEntry.set_size(0);
   fileEntry.mutable_shared_dir()->CopyFrom(this->resultListener.getEntriesResultList().first().entries(0).entry(0).shared_dir());

   QSharedPointer<IGetHashesResult> result = this->peerManagers[0]->getPeers()[0]->getHashes(fileEntry);
   connect(result.data(), SIGNAL(result(const Protos::Core::GetHashesResult&)), &this->resultListener, SLOT(result(const Protos::Core::GetHashesResult&)));
   connect(result.data(), SIGNAL(nextHash(const Common::Hash&)), &this->resultListener, SLOT(nextHash(const Common::Hash&)));
   result->start();
   QTest::qWait(5000);
}

void Tests::askForAChunk()
{
   qDebug() << "===== askForAChunk() =====";

   connect(this->peerManagers[1].data(), SIGNAL(getChunk(Common::Hash, int, QSharedPointer<PM::ISocket>)), &this->resultListener, SLOT(getChunk(Common::Hash, int, QSharedPointer<PM::ISocket>)));

   Protos::Core::GetChunk getChunkMessage;
   getChunkMessage.mutable_chunk()->set_hash(Common::Hash::rand().getData(), Common::Hash::HASH_SIZE);
   getChunkMessage.set_offset(0);
   QSharedPointer<IGetChunkResult> result = this->peerManagers[0]->getPeers()[0]->getChunk(getChunkMessage);
   connect(result.data(), SIGNAL(result(const Protos::Core::GetChunkResult&)), &this->resultListener, SLOT(result(const Protos::Core::GetChunkResult&)));
   connect(result.data(), SIGNAL(stream(QSharedPointer<PM::ISocket>)), &this->resultListener, SLOT(stream(QSharedPointer<PM::ISocket>)));
   result->start();
   QTest::qWait(1000);
}

void Tests::cleanupTestCase()
{
   qDebug() << "===== cleanupTestCase() =====";

   for (QListIterator<TestServer*> i(this->servers); i.hasNext();)
      delete i.next();

   delete this->peerUpdater;
}

void Tests::createInitialFiles()
{
   this->deleteAllFiles();

   Common::Global::createFile("sharedDirs/peer1/subdir/a.txt");
   Common::Global::createFile("sharedDirs/peer1/subdir/b.txt");
   Common::Global::createFile("sharedDirs/peer1/subdir/c.txt");
   Common::Global::createFile("sharedDirs/peer1/d.txt");
   Common::Global::createFile("sharedDirs/peer1/e.txt");

   Common::Global::createFile("sharedDirs/peer2/subdir/f.txt");
   Common::Global::createFile("sharedDirs/peer2/subdir/g.txt");
   Common::Global::createFile("sharedDirs/peer2/subdir/h.txt");
   Common::Global::createFile("sharedDirs/peer2/i.txt");
   Common::Global::createFile("sharedDirs/peer2/j.txt");
   Common::Global::createFile("sharedDirs/peer2/k.txt");
}

void Tests::deleteAllFiles()
{
   Common::Global::recursiveDeleteDirectory("sharedDirs");
}

