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

import os
import shutil
from build_setup import root_dir, build_root_dir, dependencies_dir
from build_script.utils import list_files

print("WBEClean: Removing build directory...")
shutil.rmtree(build_root_dir)
print(f'WBEClean: Directory "{build_root_dir}" removed.')

print("WBEClean: Removing generated files...")
# list_files matches ignored entries against single path components, so pass directory names.
files = list_files(root_dir, [os.path.basename(dependencies_dir), os.path.basename(build_root_dir), ".cache", ".git"])

generated_files = [file for file in files if ".gen." in file]

for generated_file in generated_files:
    os.remove(generated_file)
    print(f'WBEClean: {generated_file} removed.')

