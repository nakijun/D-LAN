/**
  * D-LAN - A decentralized LAN file sharing software.
  * Copyright (C) 2010-2012 Greg Burri <greg.burri@gmail.com>
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
  
#ifndef COMMON_INDEXEDARRAY_H
#define COMMON_INDEXEDARRAY_H

#include <QMap>
#include <QList>

namespace Common
{
   template<typename K, typename T>
   class IndexedArray
   {
   public:
      int size() const;
      int insert(K key, T value);

      const T& operator[](int index) const;
      T& operator[](int index);

      void remove(int index);

      int indexOf(const K& key);

      //void sort

   private:
      struct Value {
         K key;
         T value;
      };

      QList<Value> values;
      QMap<K, int> index;
   };
}

template<typename K, typename T>
int Common::IndexedArray<K, T>::size() const
{
   return this->values.size();
}

template<typename K, typename T>
int Common::IndexedArray<K, T>::insert(K key, T value)
{
   this->values << Value { key, value };
   this->index.insert(key, this->values.size() - 1);
   return this->values.size() - 1;
}

template<typename K, typename T>
const T& Common::IndexedArray<K, T>::operator[](int index) const
{
   return this->values[index].value;
}

template<typename K, typename T>
T& Common::IndexedArray<K, T>::operator[](int index)
{
   return this->values[index].value;
}

template<typename K, typename T>
void Common::IndexedArray<K, T>::remove(int index)
{
   this->index.remove(this->values[index].key);
   this->values.removeAt(index);
   for (int i = index; i < this->values.size(); i++)
      this->index[this->values[i].key]--;
}

template<typename K, typename T>
int Common::IndexedArray<K, T>::indexOf(const K& key)
{
   if (!this->index.contains(key))
      return -1;
   return this->index[key];
}

#endif
