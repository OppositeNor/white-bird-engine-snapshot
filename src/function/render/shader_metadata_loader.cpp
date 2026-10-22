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
#include "function/render/shader_metadata_loader.hh"
#include "core/parser/parser_json.hh"
#include "function/render/render_resource_unit.hh"
#include "function/render/shader_metadata.hh"
#include "resource/resource_unit/resource_unit.hh"
#include "resource/resource_utils.hh"
#include <stdexcept>

namespace WhiteBirdEngine {

ShaderMetadata ShaderMetadataLoader::load(RefRes<RUShader> p_shader) const {
    if (p_shader == nullptr || p_shader->metadata_path == nullptr) {
        throw std::runtime_error("Failed to load shader metadata: metadata path is missing.");
    }
    ParserJSON metadata_parser;
    metadata_parser.parse(get_resource_path(p_shader->metadata_path.get()));
    ShaderMetadata metadata;
    metadata.deserialize(metadata_parser.get_data());
    return metadata;
}

} // namespace WhiteBirdEngine
