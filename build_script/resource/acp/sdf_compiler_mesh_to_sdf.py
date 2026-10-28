# Copyright 2025 OppositeNor
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
from __future__ import annotations

import os

import numpy as np

from build_script.resource.acp.acp_compiler_texture import TextureCompilerPyKTX
from wbe_utils_model_compiler import SDFCompileRequest, SDFCompiler


class SDFCompilerMeshToSDF(SDFCompiler):
    """
    Sample signed distance fields with mesh-to-sdf, which finds the surface by rendering depth scans of the mesh, and write
    them as KTX2 textures through the texture compiler's libktx.
    """

    def __init__(self, p_texture_compiler: TextureCompilerPyKTX) -> None:
        self._texture_compiler = p_texture_compiler

    def compile_sdf(self, p_request: SDFCompileRequest) -> None:
        if p_request.data_structure != "texture_3d":
            raise RuntimeError(f"SDFCompilerMeshToSDF does not support SDF data structure '{p_request.data_structure}'.")
        if p_request.destination_path.suffix.lower() != ".ktx2":
            raise RuntimeError("SDF texture destination must use the .ktx2 extension.")
        # pyrender renders the scans through EGL, which needs no display; under Wayland, PyOpenGL and pyrender's default
        # pyglet context disagree on the platform. mesh_to_sdf must be imported before pyrender and OpenGL, so import it lazily
        # after selecting the platform.
        os.environ.setdefault("PYOPENGL_PLATFORM", "egl")
        import mesh_to_sdf
        import trimesh

        extent = np.array(p_request.extent, dtype=np.float64)
        sample_origin = np.array(p_request.sample_origin, dtype=np.float64)
        # Scans orbit the origin, so center the mesh on it.
        center = sample_origin + ((extent - 1.0) * p_request.sample_spacing * 0.5)
        mesh = trimesh.Trimesh(
            vertices=p_request.vertex_positions.astype(np.float64) - center,
            faces=p_request.triangle_indices.astype(np.int64),
            process=False,
        )
        axes = [
            sample_origin[axis] - center[axis] + (np.arange(p_request.extent[axis]) * p_request.sample_spacing)
            for axis in range(3)
        ]
        # Index z, y, x so the flattened samples vary fastest along x.
        grid_z, grid_y, grid_x = np.meshgrid(axes[2], axes[1], axes[0], indexing="ij")
        query_points = np.stack([grid_x.reshape(-1), grid_y.reshape(-1), grid_z.reshape(-1)], axis=1)
        distances = np.asarray(
            mesh_to_sdf.mesh_to_sdf(mesh, query_points, surface_point_method="scan", sign_method="normal"), dtype=np.float32)
        self._write_texture(p_request, self._to_half_toward_zero(distances))

    @staticmethod
    def _to_half_toward_zero(p_distances: np.ndarray) -> np.ndarray:
        # Rounding to nearest may enlarge a distance, which lets a ray march past the surface; step those magnitudes one half
        # float toward zero instead. Distances beyond the half range overflow to infinity and step back to its maximum.
        with np.errstate(over="ignore"):
            result = p_distances.astype(np.float16)
        enlarged = np.abs(result.astype(np.float32)) > np.abs(p_distances)
        result[enlarged] = np.nextafter(result[enlarged], np.float16(0.0))
        return result

    def _write_texture(self, p_request: SDFCompileRequest, p_samples: np.ndarray) -> None:
        pyktx = self._texture_compiler.load_pyktx()
        width, height, depth = p_request.extent
        create_info = pyktx.KtxTextureCreateInfo(
            gl_internal_format=None,
            base_width=width,
            base_height=height,
            base_depth=depth,
            vk_format=pyktx.VkFormat.VK_FORMAT_R16_SFLOAT,
            num_dimensions=3,
            num_levels=1,
            num_layers=1,
            num_faces=1,
            is_array=False,
            generate_mipmaps=False,
        )
        texture = pyktx.KtxTexture2.create(create_info, pyktx.KtxTextureCreateStorage.ALLOC)
        try:
            # Each 3D image libktx sets is one depth slice.
            slices = p_samples.astype("<f2").reshape(depth, height, width)
            for z in range(depth):
                texture.set_image_from_memory(0, 0, z, slices[z].tobytes())
            temporary_path = p_request.destination_path.with_suffix(".tmp.ktx2")
            texture.write_to_named_file(temporary_path.as_posix())
            os.replace(temporary_path, p_request.destination_path)
        finally:
            self._texture_compiler.destroy_texture(pyktx, texture)
