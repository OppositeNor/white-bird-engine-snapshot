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

/** @brief Identifies the role of a reflected shader entry point. */
[__AttributeUsage(_AttributeTargets.Function)]
struct EntryContractAttribute
{
    /** @brief Entry-point role name. */
    string name;
};

/** @brief Identifies the role of a reflected shader variable. */
[__AttributeUsage(_AttributeTargets.Var)]
struct VariableContractAttribute
{
    /** @brief Variable role name. */
    string name;
};

/** @brief Associates a binding contract value with a reflected shader variable. */
[__AttributeUsage(_AttributeTargets.Var)]
struct BindingContractAttribute
{
    /** @brief Binding contract value. */
    int value;
};

/** @brief Marks a reflected shader variable without carrying arguments. */
[__AttributeUsage(_AttributeTargets.Var)]
struct MarkerContractAttribute
{
};

/** @brief Associates an inclusive integer range with a reflected shader variable. */
[__AttributeUsage(_AttributeTargets.Var)]
struct RangeContractAttribute
{
    /** @brief Minimum range value. */
    int minimum;
    /** @brief Maximum range value. */
    int maximum;
};

/** @brief Associates a floating-point weight with a reflected shader variable. */
[__AttributeUsage(_AttributeTargets.Var)]
struct WeightContractAttribute
{
    /** @brief Weight contract value. */
    float value;
};

/** @brief Associates an optional alias with a reflected shader variable. */
[__AttributeUsage(_AttributeTargets.Var)]
struct AliasContractAttribute
{
    /** @brief Optional alias value. */
    string value;
};

/** @brief Marks a reflected field as a material texture descriptor index. */
[__AttributeUsage(_AttributeTargets.Var)]
struct WBE_TEXTUREAttribute
{
    /** @brief Texture role; an empty value selects the annotated variable name. */
    string texture_role = "";
};

/** @brief Associates an integer contract value with an entry-point parameter. */
[__AttributeUsage(_AttributeTargets.Param)]
struct ParameterContractAttribute
{
    /** @brief Contract value. */
    int value;
};

/** @brief Associates a floating-point contract value with a structure. */
[__AttributeUsage(_AttributeTargets.Struct)]
struct StructContractAttribute
{
    /** @brief Contract value. */
    float value;
};

/** @brief Payload used to verify reflected structure and field attributes. */
[StructContract(0.5)]
struct AttributePayload
{
    /** @brief Color supplied by the reflected constant buffer. */
    [VariableContract("field")]
    float4 color;
    /** @brief First explicit texture-role index. */
    [WBE_TEXTURE("role_1")]
    int texture_index_1;
    /** @brief Second explicit texture-role index. */
    [WBE_TEXTURE("role_2")]
    int texture_index_2;
    /** @brief Third explicit texture-role index. */
    [WBE_TEXTURE("role_3")]
    int texture_index_3;
    /** @brief Fourth explicit texture-role index. */
    [WBE_TEXTURE("role_4")]
    int texture_index_4;
    /** @brief Fifth explicit texture-role index. */
    [WBE_TEXTURE("role_5")]
    int texture_index_5;
    /** @brief Texture-role index that exercises variable-name fallback. */
    [WBE_TEXTURE]
    int texture_index_6;
};

/** @brief Attributed global parameter used by the fragment entry point. */
[VariableContract("global")]
[BindingContract(3)]
[MarkerContract()]
[RangeContract(-4, 9)]
[WeightContract(-0.25)]
[AliasContract("")]
ConstantBuffer<AttributePayload> attributed_global;

/**
 * @brief Combine an attributed stage input with the attributed global color.
 * @param p_input Attributed fragment-stage input color.
 * @return Combined output color.
 */
[EntryContract("entry")]
[shader("fragment")]
float4 fragment_main([ParameterContract(7)] float4 p_input : COLOR) : SV_Target
{
    return p_input + attributed_global.color;
}
