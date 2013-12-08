#ifndef FILEMANAGER_SIZE_INDEX_H
#define FILEMANAGER_SIZE_INDEX_H

#include <QMutex>

#include <Common/Containers/SortedArray.h>

#include <priv/Cache/Entry.h>

namespace FM
{
   class SizeIndexEntries
   {
   public:
      SizeIndexEntries();

      void addItem(Entry* item);
      void rmItem(Entry* item);

      QList<Entry*> search(qint64 sizeMin, qint64 sizeMax) const;

   private:
      Common::SortedArray<Entry*> index;
      mutable QMutex mutex;
   };
}

#endif