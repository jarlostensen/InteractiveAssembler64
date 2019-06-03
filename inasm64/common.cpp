#include "common.h"

namespace inasm64
{
    namespace detail
    {
        Error _error = Error::NoError;

        void SetError(Error error)
        {
            _error = error;
        }

    }  // namespace detail

    Error GetError()
    {
        return detail::_error;
    }

}  // namespace inasm64
