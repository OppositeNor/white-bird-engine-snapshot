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
#ifndef WBE_FILE_SYSTEMS_HH
#define WBE_FILE_SYSTEMS_HH

#include "function/scene/world.hh"
#include <entt/entt.hpp>

namespace WhiteBirdEngine {

/**
 * @brief Update transform matrices.
 *
 * @param p_world The world to update transform.
 * @param p_version A value that keeps the version of the current update, to
 * ensure every data that might be dirty are up to date.
 */
void update_transform_mats(World& p_world, int64_t p_version);

/**
 * @brief Update camera view matrices from entity transform matrices.
 *
 * For every entity containing both `ComponentCamera` and
 * `ComponentTransformMats`, the camera view matrix becomes the inverse of the
 * entity global transform combined with the camera-local transform.
 *
 * @param p_world The world containing the cameras and transforms to update.
 */
void update_camera_views(World& p_world);

/**
 * @brief Run setup callbacks for every driver in a world.
 *
 * @param p_world The world containing the drivers to set up.
 */
void system_driver_setup(World& p_world);

/**
 * @brief Run ready callbacks for every driver in a world.
 *
 * @param p_world The world containing the drivers to mark ready.
 */
void system_driver_ready(World& p_world);

/**
 * @brief Run update callbacks for every driver in a world.
 *
 * @param p_world The world containing the drivers to update.
 * @param p_delta The elapsed time since the previous update, in seconds.
 */
void system_driver_update(World& p_world, double p_delta);

/**
 * @brief Run teardown callbacks for every driver in a world.
 *
 * @param p_world The world containing the drivers to tear down.
 */
void system_driver_teardown(World& p_world);

} // namespace WhiteBirdEngine

#endif
