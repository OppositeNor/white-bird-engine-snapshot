/* Copyright 2025 OppositeNor

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/
#ifndef WBE_FILE_RENDER_SOFTWARE_DEFS
#define WBE_FILE_RENDER_SOFTWARE_DEFS

#include "core/allocator/stl_allocator.hh"
#include "function/render/render_defs.hh"
namespace WhiteBirdEngine {

struct BaseRenderDataSoftware {
    BaseRenderDataSoftware(RenderAllocator* p_allocator) : main_allocator(p_allocator), per_thread_allocators(p_allocator) {
    }
    RenderAllocator* main_allocator = nullptr;
    Vector<RenderAllocator*, RenderAllocator> per_thread_allocators;
};

} // namespace WhiteBirdEngine
#endif
