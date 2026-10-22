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
#include "resource/engine_resource.hh"
#include "core/engine_config/engine_config.hh"
#include "core/engine_core.hh"
#include "core/memory/unique.hh"
#include "resource/resource_chunk/i_resource_chunk.hh"
#include "resource/resource_manager/resource_manager.hh"

namespace WhiteBirdEngine {

EngineResource::EngineResource() {
    auto engine_config = EngineConfig::get_singleton()->get_config_options();
    resource_manager = make_unique<ResourceManager>(EngineCore::get_singleton()->pool_allocator, engine_config.resource_mem_pool_size);
}

EngineResource::~EngineResource() {
    resource_manager.reset();
}

} // namespace WhiteBirdEngine
