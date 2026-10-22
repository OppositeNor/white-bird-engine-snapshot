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
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include <string_view>

namespace WhiteBirdEngine {

namespace {

std::string read_declarations(std::string_view p_file_name) {
    const std::string path = std::string(WBE_TEAL_DECLARATIONS_DIR) + "/" + std::string(p_file_name);
    std::ifstream file(path);
    EXPECT_TRUE(file.is_open()) << "Missing generated Teal declarations: " << path;
    std::stringstream stream;
    stream << file.rdbuf();
    return stream.str();
}

} // namespace

TEST(WBETealDeclarationsTest, DeclaresOnlyScriptTypesOfTheEngine) {
    const std::string declarations = read_declarations("wbe.d.tl");
    EXPECT_NE(declarations.find("local record wbe\n"), std::string::npos);
    EXPECT_NE(declarations.find("    enum MeshPartVK\n        \"OPAQUE\"\n        \"opaque\"\n"), std::string::npos);
    // Every script class is an interface; enum fields also accept the integer value.
    EXPECT_NE(declarations.find("    interface RTParamsGraphicsDrawMeshVK\n"), std::string::npos);
    EXPECT_NE(declarations.find("        parts: {MeshPartVK | integer}\n"), std::string::npos);
    EXPECT_NE(declarations.find("        [\"type\"]: string\n"), std::string::npos);
    EXPECT_NE(declarations.find("    interface RendRecipeContextVK\n"), std::string::npos);
    // Other reflected types that are not WBE_REFLECT_SCRIPT stay out.
    EXPECT_EQ(declarations.find("RUSchemaAudio"), std::string::npos);
    EXPECT_EQ(declarations.find("EncodeType"), std::string::npos);
    EXPECT_EQ(declarations.find("TestEnumSigned"), std::string::npos);
    EXPECT_NE(declarations.find("\nreturn wbe"), std::string::npos);
}

TEST(WBETealDeclarationsTest, DeclaresTheScriptTypesOfTheTestExecutablesInTestTargets) {
    // The unit tests build with the tests, whose headers reflection scans only then.
    const std::string declarations = read_declarations("wbe.d.tl");
    // A derived script class is an interface that is its bases.
    EXPECT_NE(declarations.find("    interface DeferredRecipeContext is RendRecipeContextVK\n"), std::string::npos);
    EXPECT_NE(declarations.find("    interface SVORecipeParams is RecipeFeatureParams\n"), std::string::npos);
    EXPECT_NE(declarations.find("        static_build: RendRecipeTriggerVK\n"), std::string::npos);
    EXPECT_NE(declarations.find("        variant: SVORecipeVariant | integer\n"), std::string::npos);
}

TEST(WBETealDeclarationsTest, GeneratesTheGraphConstructorsOfTheNamedRenderTasks) {
    const std::string declarations = read_declarations("render_graph_utils/render_graph.tl");
    const std::string constructors = read_declarations("render_graph_utils/render_graph_tasks.tl");
    // The shared API owns concrete graph and authoring types; the installer depends only on those declarations.
    EXPECT_NE(declarations.find("    record Resource\n"), std::string::npos);
    EXPECT_NE(declarations.find("    record Task\n"), std::string::npos);
    EXPECT_NE(declarations.find("    record Graph\n"), std::string::npos);
    EXPECT_EQ(declarations.find("require(\"render_graph_utils.render_graph_utils\")"), std::string::npos);
    EXPECT_NE(constructors.find("local type render_graph = require(\"render_graph_utils.render_graph\")"), std::string::npos);
    EXPECT_EQ(constructors.find("require(\"render_graph_utils.render_graph_utils\")"), std::string::npos);
    // WBE_RENDER_TASK(<method>) names a constructor, which creates the task type of the class's first serialization alias.
    EXPECT_NE(declarations.find("        compute: function(self: Graph, p_id: string, p_params: RTParamsRunComputePipelineVK,\n"),
        std::string::npos);
    EXPECT_NE(constructors.find("p_make_task(p_id, \"run_compute_pipeline\", p_params, p_options, compute_fields)"), std::string::npos);
    // Resource-bearing params are generated directly in the shared API without per-task aliases or generic bindings.
    EXPECT_NE(declarations.find("p_params: RTParamsGraphicsDrawMeshVK,\n"), std::string::npos);
    EXPECT_NE(declarations.find("    interface RTParamsGraphicsDrawMeshVK\n"), std::string::npos);
    EXPECT_NE(declarations.find("        mesh: Resource\n"), std::string::npos);
    EXPECT_NE(declarations.find("        pipeline: Resource\n"), std::string::npos);
    EXPECT_NE(declarations.find("        transform: Resource | string\n"), std::string::npos);
    EXPECT_NE(declarations.find("        parts: {wbe.MeshPartVK | integer}\n"), std::string::npos);
    EXPECT_NE(declarations.find("        dispatches: {RTParamsComputeDispatchVK}\n"), std::string::npos);
    EXPECT_NE(declarations.find("        aliases: {string:Resource | string}\n"), std::string::npos);
    EXPECT_NE(constructors.find("{ name = \"mesh\", kind = \"resource\", scope = \"database\", expected_type = \"WhiteBirdEngine::MeshResourcesVK\" }"),
        std::string::npos);
    EXPECT_NE(constructors.find("{ name = \"pipeline\", kind = \"resource\", scope = \"database\", expected_type = \"WhiteBirdEngine::ROGraphicsPipelineVK\" }"),
        std::string::npos);
    EXPECT_NE(constructors.find("{ name = \"pipeline\", kind = \"resource\", scope = \"database\", expected_type = \"WhiteBirdEngine::ROComputePipelineVK\" }"),
        std::string::npos);
    EXPECT_NE(constructors.find("{ name = \"indirect_buffer\", kind = \"resource\", scope = \"task_local\" }"), std::string::npos);
    // A bare WBE_RENDER_TASK registers its task type without a constructor.
    EXPECT_EQ(declarations.find("max_downsample"), std::string::npos);
    EXPECT_EQ(declarations.find("registry_probe"), std::string::npos);
    EXPECT_EQ(constructors.find("max_downsample"), std::string::npos);
    EXPECT_EQ(constructors.find("registry_probe"), std::string::npos);
}

} // namespace WhiteBirdEngine
