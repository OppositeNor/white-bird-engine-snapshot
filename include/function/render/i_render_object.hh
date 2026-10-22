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
#ifndef WBE_FILE_I_RENDER_OBJECT_HH
#define WBE_FILE_I_RENDER_OBJECT_HH

#include "utils/utils.hh"

#include <string>
#include <string_view>

namespace WhiteBirdEngine {

/**
 * @class IRenderObject
 * @brief Interface for all the render objects, such as pipelines, shaders, etc.
 *
 */
class IRenderObject {
public:
    IRenderObject() = default;
    virtual ~IRenderObject() = default;
    IRenderObject(const IRenderObject&) = delete;
    IRenderObject(IRenderObject&&) noexcept = delete;
    IRenderObject& operator=(const IRenderObject&) = delete;
    IRenderObject& operator=(IRenderObject&&) noexcept = delete;

    /**
     * @brief Get the concrete render-object type UUID.
     * @return The generated UUID of the concrete render-object type.
     */
    virtual TypeID get_type_uuid() const = 0;

    /**
     * @brief Name the backend objects the render object owns, for debuggers and validation messages.
     *
     * RenderDatabase calls this with the key an object is registered under, so registered objects are named after their
     * database key; importing an unnamed object into a render graph names it after its import id. Backends without object
     * naming, and render objects that own no backend objects, ignore the name but still record it. Composites register
     * their parts as `<p_name>.<part>`, which names them too. Call on the rendering thread.
     *
     * @param p_name The name, normally the resource key the object is registered under.
     */
    void set_debug_name(std::string_view p_name) {
        debug_name = p_name;
        apply_debug_name(debug_name);
    }

    /**
     * @brief Get the name last given through set_debug_name().
     * @return The name, or an empty string if the object has not been named.
     */
    const std::string& get_debug_name() const {
        return debug_name;
    }

protected:
    /**
     * @brief Apply a name given through set_debug_name() to the backend objects the render object owns.
     *
     * The default implementation ignores the name.
     *
     * @param p_name The name.
     */
    virtual void apply_debug_name(std::string_view p_name) {
        static_cast<void>(p_name);
    }

private:
    std::string debug_name;
};

} // namespace WhiteBirdEngine

#endif
