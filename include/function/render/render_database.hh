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
#ifndef WBE_FILE_RENDER_DATABASE_HH
#define WBE_FILE_RENDER_DATABASE_HH

#include "core/allocator/stl_allocator.hh"
#include "core/memory/reference_strong.hh"
#include "core/memory/reference_weak.hh"
#include "function/render/i_render_object.hh"
#include "render_defs.hh"
#include "utils/interface/i_singleton.hh"
#include "utils/utils.hh"
#include <string>
#include <string_view>

namespace WhiteBirdEngine {

/**
 * @class RenderDatabase
 * @brief Renderer render database.
 *
 * @todo Test
 */
class RenderDatabase final : public ISingleton<RenderDatabase> {
private:
    using AllocType = RenderAllocator;
    template <typename TKey, typename TVal>
    using Map = ::WhiteBirdEngine::Map<TKey, TVal, AllocType>;

public:
    /**
     * @class CompiledRenderResourceKey
     * @brief Key for a backend-neutral compiled render resource registry entry.
     *
     * A resource key alone is not enough because one resource can compile into
     * multiple backend objects, such as shaders, image views, and pipelines.
     */
    struct CompiledRenderResourceKey {
        /**
         * @brief Manifest resource key or compiler-generated subresource key.
         */
        std::string resource_key;
        /**
         * @brief Runtime type id of the compiled render object.
         */
        HashCode render_object_type_id = 0;

        bool operator<(const CompiledRenderResourceKey& p_other) const {
            if (resource_key != p_other.resource_key) {
                return resource_key < p_other.resource_key;
            }
            return render_object_type_id < p_other.render_object_type_id;
        }
    };

    virtual ~RenderDatabase() override {
        singleton = nullptr;
    }
    RenderDatabase(const RenderDatabase&) = delete;
    RenderDatabase(RenderDatabase&&) noexcept = delete;
    RenderDatabase& operator=(const RenderDatabase&) = delete;
    RenderDatabase& operator=(RenderDatabase&&) noexcept = delete;

    /**
     * @brief Constructor.
     */
    RenderDatabase(AllocType* p_allocator);

    /**
     * @brief Register or replace a compiled render object for a resource key.
     *
     * The object is named after the key through IRenderObject::set_debug_name(). Call on the rendering thread.
     *
     * @param p_resource_key The source resource key or a derived compiler key.
     * @param p_render_object The compiled render object whose concrete type identifies the registry entry.
     */
    void register_compiled_render_resource(std::string_view p_resource_key, Ref<IRenderObject> p_render_object);

    /**
     * @brief Get a compiled render object by resource key and object type.
     *
     * @return A reference, or null if no entry exists.
     */
    Ref<IRenderObject> get_compiled_render_resource(std::string_view p_resource_key, HashCode p_render_object_type_id);

    /**
     * @brief Check whether a live compiled render object is registered.
     */
    bool contains_compiled_render_resource(std::string_view p_resource_key, HashCode p_render_object_type_id) const;

    /**
     * @brief Clear every compiled render object registry entry.
     */
    void clear_compiled_render_resources();

    static RenderDatabase* get_singleton() {
        return singleton;
    }

private:
    inline static RenderDatabase* singleton = nullptr;

    AllocType* allocator = nullptr;

    Map<CompiledRenderResourceKey, RefWeak<IRenderObject>> compiled_render_resources;

};

} // namespace WhiteBirdEngine

#endif
