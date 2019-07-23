
#include "common.h"
#include "globvars.h"
#include <unordered_map>

namespace inasm64
{
    namespace detail
    {
        // pointers to data in the backup string store
        char_string_map_t _glob_map;
    }  // namespace detail

    namespace globvars
    {
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