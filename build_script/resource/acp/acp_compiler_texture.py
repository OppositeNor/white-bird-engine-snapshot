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

import ctypes
import math
import os
from pathlib import Path
import sys
from threading import Lock
from typing import Any, override

from build_script.resource.acp.acp_compiler import (
    ManifestResource,
    SOURCE_RESOURCE_FILE_KEY,
    SOURCE_RESOURCE_TYPE_KEY,
    WBEACPCompiler,
)

from wbe_utils_model_compiler import TextureCompileRequest, TextureCompiler


_FORMATS = {"rgb", "srgb", "bc5", "bc7", "sbc7"}


class TextureCompilerPyKTX(TextureCompiler):
    """Compile raster images to upload-ready KTX2 files using pyktx/libktx."""

    _encoder_initialization_lock = Lock()
    _encoder_initialized = False

    def __init__(self, p_native_library_dir: Path, p_python_package_dir: Path) -> None:
        self._native_library_dir = p_native_library_dir
        self._python_package_dir = p_python_package_dir
        self._native_library: Any = None
        self._dynamic_library_directory: Any = None
        self._initialization_lock = Lock()
        self._pyktx: Any = None

    def _load_native_library(self) -> None:
        if self._native_library is not None:
            return
        if sys.platform == "win32":
            library_name = "ktx.dll"
            self._dynamic_library_directory = os.add_dll_directory(self._native_library_dir)
        elif sys.platform == "darwin":
            library_name = "libktx.dylib"
        else:
            library_name = "libktx.so"
        library_path = self._native_library_dir / library_name
        if not library_path.is_file():
            raise RuntimeError(f"The libktx library has not been built: {library_path}.")
        self._native_library = ctypes.CDLL(library_path.as_posix(), mode=getattr(ctypes, "RTLD_GLOBAL", 0))
        self._native_library.ktxTexture2_Destroy.argtypes = [ctypes.c_void_p]
        self._native_library.ktxTexture2_Destroy.restype = None

    def load_pyktx(self) -> Any:
        """Load libktx and the pyktx module once, and return the module. Thread-safe."""
        with self._initialization_lock:
            if self._pyktx is not None:
                return self._pyktx
            self._load_native_library()
            python_package_path = self._python_package_dir.resolve().as_posix()
            if python_package_path not in sys.path:
                sys.path.insert(0, python_package_path)
            try:
                import pyktx
            except ImportError as error:
                raise RuntimeError("Texture compilation requires the pyktx package.") from error
            self._pyktx = pyktx
            return pyktx

    @classmethod
    def _compress_basis(cls, p_texture: Any, p_parameters: Any) -> None:
        # libktx's first compression initializes shared encoder state without its own guard.
        if not cls._encoder_initialized:
            with cls._encoder_initialization_lock:
                if not cls._encoder_initialized:
                    p_texture.compress_basis(p_parameters)
                    cls._encoder_initialized = True
                    return
        p_texture.compress_basis(p_parameters)

    def destroy_texture(self, p_pyktx: Any, p_texture: Any) -> None:
        """Destroy a pyktx texture through the loaded libktx and leave it empty."""
        texture_pointer = p_texture._ptr
        if texture_pointer == p_pyktx.native.ffi.NULL:
            return
        pointer_value = int(p_pyktx.native.ffi.cast("uintptr_t", texture_pointer))
        self._native_library.ktxTexture2_Destroy(ctypes.c_void_p(pointer_value))
        p_texture._ptr = p_pyktx.native.ffi.NULL

    @staticmethod
    def _decode_transfer(p_pixels: Any, p_source_format: str, p_numpy: Any) -> Any:
        result = p_pixels.astype(p_numpy.float32) / 255.0
        if p_source_format in {"srgb", "sbc7"}:
            rgb = result[..., :3]
            result[..., :3] = p_numpy.where(rgb <= 0.04045, rgb / 12.92, ((rgb + 0.055) / 1.055) ** 2.4)
        return result

    @staticmethod
    def _encode_transfer(p_pixels: Any, p_target_format: str, p_numpy: Any) -> bytes:
        result = p_pixels.copy()
        if p_target_format in {"srgb", "sbc7"}:
            rgb = result[..., :3]
            result[..., :3] = p_numpy.where(
                rgb <= 0.0031308, rgb * 12.92, 1.055 * p_numpy.power(rgb, 1.0 / 2.4) - 0.055)
        result = p_numpy.clip(p_numpy.rint(result * 255.0), 0.0, 255.0).astype(p_numpy.uint8)
        return result.tobytes()

    @staticmethod
    def _downsample(p_pixels: Any, p_numpy: Any) -> Any:
        height, width, _ = p_pixels.shape
        if width == 1 and height == 1:
            return p_pixels
        next_height = max(1, height // 2)
        next_width = max(1, width // 2)
        first_rows = p_numpy.arange(next_height) * 2
        second_rows = p_numpy.minimum(first_rows + 1, height - 1)
        first_columns = p_numpy.arange(next_width) * 2
        second_columns = p_numpy.minimum(first_columns + 1, width - 1)
        return (p_pixels[first_rows[:, None], first_columns]
            + p_pixels[second_rows[:, None], first_columns]
            + p_pixels[first_rows[:, None], second_columns]
            + p_pixels[second_rows[:, None], second_columns]) * 0.25

    def compile_texture(self, p_request: TextureCompileRequest) -> None:
        if p_request.source_format not in _FORMATS:
            raise ValueError("Texture source_format must be one of rgb, srgb, bc5, bc7, or sbc7.")
        if p_request.target_format not in _FORMATS:
            raise ValueError("Texture target_format must be one of rgb, srgb, bc5, bc7, or sbc7.")
        if p_request.source_path.suffix.lower() in {".ktx", ".ktx2"}:
            raise ValueError("Raster texture compilation does not accept KTX input.")
        if p_request.destination_path.suffix.lower() != ".ktx2":
            raise ValueError("Compiled texture destination must use the .ktx2 extension.")

        thread_count = p_request.thread_count
        if thread_count is None:
            thread_count = min(16, os.cpu_count() or 1)
        if not isinstance(thread_count, int) or thread_count < 1:
            raise ValueError("Texture thread_count must be a positive integer.")
        pyktx = self.load_pyktx()
        try:
            import numpy
            from PIL import Image
        except ImportError as error:
            raise RuntimeError("Texture compilation requires Pillow and NumPy.") from error

        with Image.open(p_request.source_path) as image:
            width, height = image.size
        mip_count = int(math.floor(math.log2(max(width, height)))) + 1 if p_request.generate_mipmap else 1
        expected_format = {
            "rgb": pyktx.VkFormat.VK_FORMAT_R8G8B8A8_UNORM,
            "srgb": pyktx.VkFormat.VK_FORMAT_R8G8B8A8_SRGB,
            "bc5": pyktx.VkFormat.VK_FORMAT_BC5_UNORM_BLOCK,
            "bc7": pyktx.VkFormat.VK_FORMAT_BC7_UNORM_BLOCK,
            "sbc7": pyktx.VkFormat.VK_FORMAT_BC7_SRGB_BLOCK,
        }[p_request.target_format]
        with Image.open(p_request.source_path) as image:
            rgba_pixels = numpy.asarray(image.convert("RGBA"), dtype=numpy.uint8)
        if p_request.flip_h:
            rgba_pixels = numpy.flip(rgba_pixels, axis=1)
        if p_request.flip_v:
            rgba_pixels = numpy.flip(rgba_pixels, axis=0)
        linear_pixels = self._decode_transfer(rgba_pixels, p_request.source_format, numpy)
        base_format = "srgb" if p_request.target_format in {"srgb", "sbc7"} else "rgb"
        if p_request.target_format == "bc5":
            vk_format = pyktx.VkFormat.VK_FORMAT_R8G8_UNORM
            linear_pixels = linear_pixels[..., :2]
        else:
            vk_format = (
                pyktx.VkFormat.VK_FORMAT_R8G8B8A8_SRGB
                if base_format == "srgb"
                else pyktx.VkFormat.VK_FORMAT_R8G8B8A8_UNORM
            )
        create_info = pyktx.KtxTextureCreateInfo(
            gl_internal_format=None,
            base_width=width,
            base_height=height,
            base_depth=1,
            vk_format=vk_format,
            num_dimensions=2,
            num_levels=mip_count,
            num_layers=1,
            num_faces=1,
            is_array=False,
            generate_mipmaps=False,
        )
        texture = pyktx.KtxTexture2.create(create_info, pyktx.KtxTextureCreateStorage.ALLOC)
        try:
            mip_pixels = linear_pixels
            for level in range(mip_count):
                texture.set_image_from_memory(level, 0, 0, self._encode_transfer(mip_pixels, base_format, numpy))
                mip_pixels = self._downsample(mip_pixels, numpy)

            if p_request.target_format in {"bc5", "bc7", "sbc7"}:
                uastc_flags = int(pyktx.KtxPackUastcFlagBits.FASTEST)
                if p_request.target_format in {"bc7", "sbc7"}:
                    uastc_flags |= int(pyktx.KtxPackUastcFlagBits.FAVOR_BC7_ERROR)
                basis_params = pyktx.KtxBasisParams(
                    codec=pyktx.KtxBasisCodec.UASTC_LDR_4x4,
                    thread_count=thread_count,
                    uastc_flags=uastc_flags,
                    uastc_rdo=False,
                    # This will map R from source to R in basis, and G from source to A in basis. the BC5 transcoder then 
                    # is going to map R -> R and A -> G, so we're preserving the R and G channels.
                    normal_map=p_request.target_format == "bc5",
                )
                self._compress_basis(texture, basis_params)
                transcode_format = (
                    pyktx.KtxTranscodeFmt.BC5_RG
                    if p_request.target_format == "bc5"
                    else pyktx.KtxTranscodeFmt.BC7_RGBA
                )
                texture.transcode_basis(transcode_format)

            if texture.vk_format != expected_format or texture.num_levels != mip_count:
                raise RuntimeError("libktx produced texture metadata that does not match the compile request.")
            if texture.base_width != width or texture.base_height != height or texture.needs_transcoding:
                raise RuntimeError("libktx produced an invalid or non-upload-ready KTX2 texture.")

            p_request.destination_path.parent.mkdir(parents=True, exist_ok=True)
            temporary_path = p_request.destination_path.with_suffix(".tmp.ktx2")
            texture.write_to_named_file(temporary_path.as_posix())
            os.replace(temporary_path, p_request.destination_path)
        finally:
            self.destroy_texture(pyktx, texture)


class WBEACPCompilerTexture(WBEACPCompiler):
    """Adapt texture manifest entries to the shared texture compiler API."""

    def __init__(self, p_texture_compiler: TextureCompiler) -> None:
        self._texture_compiler = p_texture_compiler

    @override
    def get_supported_resource_types(self) -> list[str]:
        return ["image_texture"]

    @staticmethod
    def _resolve_output_path(p_path: str, p_manifest_path: Path, p_res_output_dir: Path) -> tuple[Path, Path]:
        relative_path = Path(p_path)
        if relative_path.is_absolute() or relative_path.as_posix() in {"", "."}:
            raise RuntimeError(f"Texture destination must be resource-root-relative in {p_manifest_path}: {p_path}.")
        output_path = (p_res_output_dir / relative_path).resolve()
        try:
            output_path.relative_to(p_res_output_dir.resolve())
        except ValueError as error:
            raise RuntimeError(f"Texture destination escapes the resource output root in {p_manifest_path}: {p_path}.") from error
        return output_path, relative_path

    @override
    def compile(self, resource: ManifestResource, manifest_path: Path,
            res_dir: Path, res_output_dir: Path) -> list[ManifestResource]:
        source_type = self._get_source_resource_type(resource)
        if source_type == "image_texture":
            source_value = resource.get("src")
            destination_value = resource.get("dst")
            source_format = resource.get("source_format")
            target_format = resource.get("target_format")
            generate_mipmap = resource.get("generate_mipmap")
            flip_h = resource.get("flip_h", False)
            flip_v = resource.get("flip_v", False)
            if not isinstance(source_value, str) or not source_value:
                raise RuntimeError(f"image_texture requires a non-empty src in {manifest_path}.")
            if not isinstance(destination_value, str) or not destination_value:
                raise RuntimeError(f"image_texture requires a non-empty dst in {manifest_path}.")
            if source_format not in _FORMATS:
                raise RuntimeError(f"image_texture source_format must be rgb, srgb, bc5, bc7, or sbc7 in {manifest_path}.")
            if target_format not in _FORMATS:
                raise RuntimeError(f"image_texture target_format must be rgb, srgb, bc5, bc7, or sbc7 in {manifest_path}.")
            if not isinstance(generate_mipmap, bool):
                raise RuntimeError(f"image_texture generate_mipmap must be a boolean in {manifest_path}.")
            if not isinstance(flip_h, bool) or not isinstance(flip_v, bool):
                raise RuntimeError(f"image_texture flip_h and flip_v must be booleans in {manifest_path}.")
            source_path, _ = self._resolve_resource_file_path(source_value, manifest_path, res_dir)
            if source_path.suffix.lower() in {".ktx", ".ktx2"}:
                raise RuntimeError(f"image_texture does not accept KTX input in {manifest_path}.")
            output_path, relative_output = self._resolve_output_path(destination_value, manifest_path, res_output_dir)
            self._texture_compiler.compile_texture(TextureCompileRequest(
                source_path=source_path,
                destination_path=output_path,
                source_format=source_format,
                target_format=target_format,
                generate_mipmap=generate_mipmap,
                flip_h=flip_h,
                flip_v=flip_v,
            ))
        else:
            raise RuntimeError(f"Unsupported texture source type in {manifest_path}: {source_type}.")

        result = dict(resource)
        result["type"] = "texture"
        result["path"] = relative_output.as_posix()
        for key in (
            SOURCE_RESOURCE_TYPE_KEY,
            SOURCE_RESOURCE_FILE_KEY,
            "src",
            "dst",
            "source_format",
            "target_format",
            "generate_mipmap",
        ):
            result.pop(key, None)
        return [result]
