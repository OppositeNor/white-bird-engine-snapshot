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
#include "resource/ktx_texture.hh"
#include <format>
#include <ktx.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace WhiteBirdEngine {

KTXTexture2::KTXTexture2(std::string_view p_path) {
    std::string path(p_path);
    KTX_error_code result = ktxTexture2_CreateFromNamedFile(path.c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &texture);
    if (result != KTX_SUCCESS) {
        throw std::runtime_error(std::format("Failed to load KTX 2 texture '{}': {}.", path, ktxErrorString(result)));
    }
}

KTXTexture2::~KTXTexture2() {
    if (texture != nullptr) {
        ktxTexture2_Destroy(texture);
    }
}

KTXTexture2::KTXTexture2(KTXTexture2&& p_other) noexcept : texture(std::exchange(p_other.texture, nullptr)) {
}

KTXTexture2& KTXTexture2::operator=(KTXTexture2&& p_other) noexcept {
    if (this == &p_other) {
        return *this;
    }
    if (texture != nullptr) {
        ktxTexture2_Destroy(texture);
    }
    texture = std::exchange(p_other.texture, nullptr);
    return *this;
}

ktxTexture2* KTXTexture2::get() const {
    return texture;
}

} // namespace WhiteBirdEngine
