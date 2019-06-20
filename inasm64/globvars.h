#pragma once

namespace inasm64
{
    namespace globvars
    {
		///<summary>
		/// Set a named global variable, returns true if already exists
		///</summary>
		bool Set(const char* name, const uintptr_t value);
        ///<summary>
        /// Get a named global variable, returns true if exists
        ///</summary>
		bool Get(const char* name, uintptr_t value);
        
    }  // namespace globvars
}  // namespace inasm64
