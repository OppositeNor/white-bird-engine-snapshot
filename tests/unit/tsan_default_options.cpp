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

#if defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define WBE_UNIT_TEST_TSAN_ENABLED
#endif
#endif
#if defined(__SANITIZE_THREAD__)
#define WBE_UNIT_TEST_TSAN_ENABLED
#endif

#ifdef WBE_UNIT_TEST_TSAN_ENABLED
// ThreadSanitizer reads its default options from this hook. The allocator tests request impossible sizes on purpose and
// expect the allocator to report the failure, so let TSan's allocator return null like libc instead of aborting.
extern "C" const char* __tsan_default_options() { // NOLINT(readability-identifier-naming)
    return "allocator_may_return_null=1";
}
#endif
