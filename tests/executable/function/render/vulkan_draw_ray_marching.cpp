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

#include "core/engine_core.hh"
#include "function/application/application_game.hh"
#include "input_manager_mocked.hh"
namespace WBE = WhiteBirdEngine;
class ApplicationVulkanDrawFur final : public WBE::ApplicationGame {
public:
    ApplicationVulkanDrawFur()
        : WBE::ApplicationGame({}, WBE::make_ref<WBE::InputManagerMocked>(WBE::global_allocator(), WBE::global_allocator())) {

    }

    virtual void render_update(double p_delta) override {
        // TODO
    };

    virtual void logic_update(double p_delta) override {
        // TODO
    };

    void setup_graphics() {

    }
};
