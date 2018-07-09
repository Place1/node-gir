#pragma once

#include <map>

namespace gir {

template<class KeyType, class PersistentType>
class PersistentObjectStore {
public:
    PersistentObjectStore() = default;

    ~PersistentObjectStore() {
        for (auto &keyValue : persistentObjects)
            keyValue.second.Empty(); // Disposes of the persistent's handle
    };

    PersistentType &at(const KeyType &key) {
        return persistentObjects.at(key);
    }

    void insert(const std::pair<const KeyType, PersistentType> pair) {
        persistentObjects.insert(pair);
    }

    bool exists(const KeyType &key) {
        return persistentObjects.count(key) > 0;
    }

private:
    std::map<KeyType, PersistentType> persistentObjects;
};

} // namespace gir
