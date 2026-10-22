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
#ifndef WBE_FILE_KTX_TEXTURE_HH
#define WBE_FILE_KTX_TEXTURE_HH

#include <string_view>

struct ktxTexture2;

namespace WhiteBirdEngine {

/**
 * @class KTXTexture2
 * @brief Move-only owner of a loaded KTX 2 texture.
 *
 * The texture data is loaded eagerly and remains owned by this object until
 * destruction or ownership transfer.
 */
class KTXTexture2 {
public:
    KTXTexture2() = delete;

    /**
     * @brief Loads a KTX 2 texture and its image data from a file.
     * @param p_path Path to the KTX 2 file.
     * @throws std::runtime_error When the file cannot be loaded as KTX 2.
     */
    explicit KTXTexture2(std::string_view p_path);

    ~KTXTexture2();
    KTXTexture2(const KTXTexture2&) = delete;
    KTXTexture2(KTXTexture2&& p_other) noexcept;
    KTXTexture2& operator=(const KTXTexture2&) = delete;
    KTXTexture2& operator=(KTXTexture2&& p_other) noexcept;

    /**
     * @brief Gets the non-owning libktx texture handle.
     * @return The loaded texture handle, or nullptr for a moved-from object.
     */
    ktxTexture2* get() const;

private:
    ktxTexture2* texture = nullptr;
};

} // namespace WhiteBirdEngine

#endif
