
#include "common.h"
#include "globvars.h"
#include <unordered_map>

namespace inasm64
{
    namespace globvars
    {
        namespace detail
        {
            struct hash_32_fnv1a
            {
                static constexpr uint32_t val_32_const = 0x811c9dc5;
                static constexpr uint32_t prime_32_const = 0x1000193;
                uint32_t hash_32_fnv1a_const(const char* const str, const uint32_t value = val_32_const) const
                {
                    return (str[0] == 0) ? value : hash_32_fnv1a_const(str + 1, uint32_t(1ull * (value ^ uint32_t(str[0])) * prime_32_const));
                }
                int operator()(const char* str) const
                {
                    return hash_32_fnv1a_const(str);
                }
            };

            struct striequal
            {
                bool operator()(const char* __x, const char* __y) const
                {
                    return _stricmp(__x, __y) == 0;
                }
            };

            // pointers to data in the backup string store
            using glob_map_t = std::unordered_map<const char*, uintptr_t, hash_32_fnv1a, striequal>;
            glob_map_t _glob_map;

        }  // namespace detail

        void ClearAll()
        {
            for(auto& kv : detail::_glob_map)
            {
                delete[] kv.first;
            }
            detail::_glob_map.clear();
        }

        bool Set(const char* name, const uintptr_t value)
        {
            const auto iter = detail::_glob_map.find(name);
            const auto name_len = strlen(name) + 1;
            const auto name_c = new char[name_len];
            strcpy_s(name_c, name_len, name);
            detail::_glob_map[name_c] = value;
            return iter != detail::_glob_map.end();
        }

        bool Get(const char* name, uintptr_t& value)
        {
            const auto iter = detail::_glob_map.find(name);
            if(iter != detail::_glob_map.end())
            {
                value = iter->second;
                return true;
            }
            return false;
        }
    }  // namespace globvars
}  // namespace inasm64